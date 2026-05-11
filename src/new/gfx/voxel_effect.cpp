/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Voxel point-list effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_lut.h"
#include "texture2d.h"
#include "tspp.h"
#include "vector3.h"


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Slots are 256 entries each; vanilla's per-table entry counts are
         *  16 / 36 / 64 / 244 (low → high detail). We pad each slot to 256
         *  so the structured-buffer index math is `(normal_type-1) * 256 +
         *  normal_idx` — trivial in the shader.
         */
        constexpr int kEntriesPerTable = 256;
        constexpr int kTables          = 4;
        constexpr int kNormalsBufEntries = kTables * kEntriesPerTable;


        const D3D11_INPUT_ELEMENT_DESC VoxelIL[] = {
            /* (X, Y, Z, ColorIdx) packed as 4×uint8 in the first 4 bytes,
               (NormalIdx, _Pad, _Pad, _Pad) in the next 4 bytes. */
            { "POSITION",   0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMALIDX",  0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 4, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };


        const char VoxelShaderHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float4 T0;\n"
            "    float4 T1;\n"
            "    float4 T2;\n"
            "    float4 T3;\n"
            "    float4 LightDir;     // xyz = light, w = normal-table base offset\n"
            "    float4 Tint;\n"
            "    float4 Misc;         // x = z_adjust (pixels, object) or 0 (shadow);\n"
            "                         // y = depth scale (1/16000);\n"
            "                         // z = per-section kObjectEps (object) or shadow alpha;\n"
            "                         // w = flags (VEF_SHADOW = 1)\n"
            "};\n"
            "\n"
            "Texture2D<float4>            Palette      : register(t0);\n"
            "Texture2D<uint>              LightRemapTex : register(t1);\n"
            "StructuredBuffer<float3>     Normals      : register(t2);\n"
            "\n"
            "struct VSIn  {\n"
            "    uint4 pos       : POSITION;    // x, y, z, color_idx\n"
            "    uint4 normal_w  : NORMALIDX;   // normal_idx, _, _, _\n"
            "};\n"
            "struct VSOut {\n"
            "    float4 pos        : SV_Position;\n"
            "    nointerpolation uint  color_idx  : COLOR0;\n"
            "    nointerpolation uint  normal_idx : COLOR1;\n"
            "    nointerpolation float voxel_z    : VOXELZ;\n"
            "    nointerpolation float unit_y     : UNITY;\n"
            "};\n"
            "\n"
            "VSOut VSMain(VSIn i) {\n"
            "    float vx = (float)i.pos.x;\n"
            "    float vy = (float)i.pos.y;\n"
            "    float vz = (float)i.pos.z;\n"
            "\n"
            "    // Per-section transform: screen = T0 + x*T1 + y*T2 + z*T3.\n"
            "    float screen_x = T0.x + vx * T1.x + vy * T2.x + vz * T3.x;\n"
            "    float screen_y = T0.y + vx * T1.y + vy * T2.y + vz * T3.y;\n"
            "    float voxel_z  = T0.z + vx * T1.z + vy * T2.z + vz * T3.z;\n"
            "\n"
            "    // Object voxels: depth = base - kObjectEps - voxel_z*kVZS +\n"
            "    // z_adjust/16000. The z_adjust term carries vanilla's\n"
            "    // Get_Z_Adjustment() (negative for elevated objects) so a\n"
            "    // flying unit sorts at the ground cell beneath rather than\n"
            "    // where its raised screen_y happens to land. A final min()\n"
            "    // clamp guarantees every voxel sits ahead of terrain by\n"
            "    // at least `kBackClamp` regardless of |voxel_z| magnitude.\n"
            "    //\n"
            "    // Shadow voxels: VS leaves SV_Position.z as the per-vertex\n"
            "    // computed value but the PIXEL SHADER overrides depth via\n"
            "    // SV_Depth, computing it from the fragment's pixel-center\n"
            "    // Y. This makes shadow depth a function of which pixel was\n"
            "    // hit, not which vertex hit it — so every shadow voxel\n"
            "    // rasterized to the same pixel (across body/turret/barrel\n"
            "    // sections and across iso-squashed adjacent voxels) gets\n"
            "    // the SAME depth value. WriteLess then keeps only the\n"
            "    // first write and DestMultiplyHalf can't compound-darken.\n"
            "    // Depth is computed in the PIXEL shader (see PSMain). The\n"
            "    // VS just produces SV_Position with a placeholder z; the\n"
            "    // PS overrides via SV_Depth using SV_Position.y (the\n"
            "    // fragment's pixel-center Y) so all voxels rasterizing\n"
            "    // to the same pixel — across body/turret/barrel sections\n"
            "    // and overlapping iso-projected voxels — get identical\n"
            "    // base. voxel_z then alone decides front-to-back order.\n"
            "    float4 clip = mul(ProjMtx, float4(screen_x, screen_y, 0.0, 1.0));\n"
            "\n"
            "    VSOut o;\n"
            "    o.pos        = float4(clip.x, clip.y, 0.5, 1.0);\n"
            "    o.color_idx  = i.pos.w;\n"
            "    o.normal_idx = i.normal_w.x;\n"
            "    o.voxel_z    = voxel_z;\n"
            "    o.unit_y     = T0.w;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "struct PSOut {\n"
            "    float4 color : SV_Target;\n"
            "    float  depth : SV_Depth;\n"
            "};\n"
            "\n"
            "PSOut PSMain(VSOut v) {\n"
            "    uint flags = (uint)Misc.w;\n"
            "    if (flags & 1u) {\n"
            "        // Shadow path: emit dark gray. Caller uses DestMultiplyHalf\n"
            "        // blend so the scene RT darkens to ~50% under the shadow.\n"
            "        // Depth from pixel-center Y (SV_Position.y in PS) so all\n"
            "        // shadow voxels landing in this pixel — regardless of\n"
            "        // which vertex/section produced them — share one depth\n"
            "        // value; WriteLess+dedup then prevents compound darken.\n"
            "        const float kShadowEps = 5e-5;\n"
            "        PSOut so;\n"
            "        so.color = float4(0.5, 0.5, 0.5, Misc.z);\n"
            "        so.depth = 1.0 - v.pos.y * Misc.y - kShadowEps;\n"
            "        so.depth = clamp(so.depth, 0.0001, 0.9999);\n"
            "        return so;\n"
            "    }\n"
            "\n"
            "    // Lambertian shade. Vanilla maps diffuse to the VPL ramp via\n"
            "    // VOXEL_PALETTE_LOOKUP_NEUTRAL (=16), and the VPL has 32 rows\n"
            "    // total: 0..15 = darkened, 16 = neutral, 17..31 = overbright.\n"
            "    // Cell brightness modulates the SHADE INDEX (not the output\n"
            "    // color) — dark cells pull shade down, lit cells pull it up.\n"
            "    // Tint.r carries brightness/1000 from the CPU side.\n"
            "    const float kNeutralShade = 16.0;\n"
            "    int   table_base = (int)LightDir.w;\n"
            "    float3 n = Normals.Load(table_base + (int)v.normal_idx);\n"
            "    float  diffuse = saturate(dot(n, LightDir.xyz));\n"
            "    float  shade_f = diffuse * kNeutralShade * Tint.r;\n"
            "    int    shade = (int)shade_f;\n"
            "    if (shade > 31) shade = 31;\n"
            "    if (shade < 0)  shade = 0;\n"
            "\n"
            "    // VPL shade ramp: (shade, voxel color) -> theatre-palette index.\n"
            "    uint shaded_idx = LightRemapTex.Load(int3((int)v.color_idx, shade, 0));\n"
            "    if (shaded_idx == 0) discard;  // transparent palette entry\n"
            "\n"
            "    // House-aware palette LUT (ColorScheme converter baked the\n"
            "    // remap-range slots into the unit's faction colors already).\n"
            "    // Brightness has already been applied via the shade index, so\n"
            "    // we don't multiply rgba.rgb by Tint here — Tint.a still\n"
            "    // carries visual-character translucency (VISUAL_DARKEN etc.).\n"
            "    float4 rgba = Palette.Load(int3((int)shaded_idx, 0, 0));\n"
            "    PSOut o;\n"
            "    o.color = float4(rgba.rgb, rgba.a * Tint.a);\n"
            "    // Per-UNIT depth (NOT per-pixel). v.unit_y carries the\n"
            "    // section's drawpoint Y from T0.w, identical across every\n"
            "    // voxel of this section. Using drawpoint Y instead of the\n"
            "    // fragment's screen_y means two units that overlap on the\n"
            "    // same pixel sort by their drawpoint difference (which is\n"
            "    // what we want: closer-to-camera drawpoint wins) rather\n"
            "    // than by which voxel happens to have larger voxel_z. The\n"
            "    // per-section kObjectEps is sized on the CPU to cover the\n"
            "    // worst pixel below drawpoint, so voxels still stay in\n"
            "    // front of terrain across the full sprite footprint.\n"
            "    const float kVoxelZScale = 1e-5;\n"
            "    float kObjectEps = Misc.z;\n"
            "    float base = 1.0 - v.unit_y * Misc.y;\n"
            "    o.depth = base - kObjectEps - v.voxel_z * kVoxelZScale + Misc.x * Misc.y;\n"
            "    o.depth = clamp(o.depth, 0.0001, 0.9999);\n"
            "    return o;\n"
            "}\n";


        /**
         *  Bindings to the four hardcoded normal tables in the vanilla
         *  binary. Each is an array of `float[3]` (Vector3) and the entry
         *  count comes from the parallel `VoxelNormalTableEntryCount`.
         */
        static float (&VoxelNormals1_g)[16][3]  = Make_Global<float[16][3]>(0x713A40);
        static float (&VoxelNormals2_g)[36][3]  = Make_Global<float[36][3]>(0x713B00);
        static float (&VoxelNormals3_g)[64][3]  = Make_Global<float[64][3]>(0x713CB0);
        static float (&VoxelNormals4_g)[245][3] = Make_Global<float[245][3]>(0x713FB0);
        // VoxelNormalTableEntryCount is indexed by VoxelNormalType enum which
        // includes NORMAL_NONE=0 as the first slot. So index 1 = VoxelNormals1's
        // count (16), index 2 = VoxelNormals2 (36), etc. We declare 5 entries to
        // safely read index 4.
        static int   (&VoxelNormalTableEntryCount_g)[5] = Make_Global<int[5]>(0x713A2C);
    }


    bool VoxelEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                VoxelShaderHLSL, sizeof(VoxelShaderHLSL) - 1,
                "voxel_palette",
                VoxelIL, _countof(VoxelIL),
                /* SpriteCB at b0 — float4x4 ProjMtx, 64 bytes */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(VoxelEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("VoxelEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }

        if (!Build_Normals_Buffer(device)) {
            DEBUG_ERROR("VoxelEffect: normals buffer build failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void VoxelEffect::Shutdown()
    {
        Release_Normals_Buffer();
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    bool VoxelEffect::Build_Normals_Buffer(GraphicsDevice& device)
    {
        /**
         *  Pack all four vanilla normal tables into a single 1024-entry
         *  StructuredBuffer<float3>. Unused slots in each 256-entry slice
         *  get (0, 0, 1) so the shader's `Load` of a stray index produces
         *  a flat lit pixel rather than NaN.
         */
        float (*src_tables[kTables])[3] = {
            VoxelNormals1_g, VoxelNormals2_g, VoxelNormals3_g, VoxelNormals4_g
        };
        float packed[kNormalsBufEntries][3];
        for (int t = 0; t < kTables; ++t) {
            // Entry count is indexed by `VoxelNormalType` which includes
            // NORMAL_NONE=0 as the first entry; the actual data tables are 1..4.
            // Our `t` is the slot index for VoxelNormals(t+1), so the count
            // lives at VoxelNormalTableEntryCount[t+1].
            const int count = VoxelNormalTableEntryCount_g[t + 1];
            const int safe_count = (count > 0 && count <= kEntriesPerTable) ? count : 0;
            for (int e = 0; e < kEntriesPerTable; ++e) {
                int base = t * kEntriesPerTable;
                if (e < safe_count) {
                    packed[base + e][0] = src_tables[t][e][0];
                    packed[base + e][1] = src_tables[t][e][1];
                    packed[base + e][2] = src_tables[t][e][2];
                } else {
                    packed[base + e][0] = 0.0f;
                    packed[base + e][1] = 0.0f;
                    packed[base + e][2] = 1.0f;
                }
            }
        }

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth           = sizeof(packed);
        bd.Usage               = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(float) * 3;
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = packed;
        if (FAILED(device.Get_Device()->CreateBuffer(&bd, &init, &NormalsBuf))) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format        = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements  = kNormalsBufEntries;
        if (FAILED(device.Get_Device()->CreateShaderResourceView(NormalsBuf, &sd, &NormalsSRV))) {
            return false;
        }
        return true;
    }


    void VoxelEffect::Release_Normals_Buffer()
    {
        Safe_Release(NormalsSRV);
        Safe_Release(NormalsBuf);
    }


    void VoxelEffect::Bind_Palette(GraphicsDevice& device, PaletteLUT& palette)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ID3D11ShaderResourceView* srv = palette.Get_Palette_Texture().Get_SRV();
        ctx->PSSetShaderResources(0, 1, &srv);
    }


    void VoxelEffect::Bind_Light_Remap(GraphicsDevice& device, Texture2D& light_remap_tex)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ID3D11ShaderResourceView* srv = light_remap_tex.Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
    }


    void VoxelEffect::Bind_Normals(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr || NormalsSRV == nullptr) return;
        ctx->PSSetShaderResources(2, 1, &NormalsSRV);
    }


    void VoxelEffect::Set_Params(GraphicsDevice& device, const VoxelEffectParams& params)
    {
        if (ParamsCB == nullptr) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(ParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        memcpy(mapped.pData, &params, sizeof(params));
        ctx->Unmap(ParamsCB, 0);
        ctx->VSSetConstantBuffers(1, 1, &ParamsCB);
        ctx->PSSetConstantBuffers(1, 1, &ParamsCB);
    }
}
