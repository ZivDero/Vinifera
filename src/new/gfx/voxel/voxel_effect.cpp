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
        if (!Effect::Initialize(device, "VOXEL",
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


    void VoxelEffect::Bind_Alpha(GraphicsDevice& device, ID3D11ShaderResourceView* alpha_srv)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ctx->PSSetShaderResources(3, 1, &alpha_srv);
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


    /**
     *  Predator variant. Same VS as VoxelEffect; PS adds a SceneCopy load
     *  at SV_Position + Predator.x horizontal offset, then lerps the shaded
     *  palette color toward the scene sample using Predator.y as the blend
     *  ratio. Vanilla `BlitTransLucent*ZReadWarp<ushort>` does this same
     *  lerp on the CPU: `dest[i] = blend(palette[shp], dest[i + warp])`.
     *  See `src/new/gfx/shaders/voxel_distortion.hlsl`.
     */
    bool VoxelDistortionEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device, "VOXEL_DISTORTION",
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
            DEBUG_ERROR("VoxelDistortionEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void VoxelDistortionEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void VoxelDistortionEffect::Bind_Palette(GraphicsDevice& device, PaletteLUT& palette)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ID3D11ShaderResourceView* srv = palette.Get_Palette_Texture().Get_SRV();
        ctx->PSSetShaderResources(0, 1, &srv);
    }


    void VoxelDistortionEffect::Bind_Light_Remap(GraphicsDevice& device, Texture2D& light_remap_tex)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ID3D11ShaderResourceView* srv = light_remap_tex.Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
    }


    void VoxelDistortionEffect::Bind_Normals(GraphicsDevice& device, ID3D11ShaderResourceView* normals_srv)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr || normals_srv == nullptr) return;
        ctx->PSSetShaderResources(2, 1, &normals_srv);
    }


    void VoxelDistortionEffect::Bind_Scene_Copy(GraphicsDevice& device, ID3D11ShaderResourceView* scene_copy_srv)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ctx->PSSetShaderResources(3, 1, &scene_copy_srv);
    }


    void VoxelDistortionEffect::Bind_Alpha(GraphicsDevice& device, ID3D11ShaderResourceView* alpha_srv)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ctx->PSSetShaderResources(4, 1, &alpha_srv);
    }


    void VoxelDistortionEffect::Set_Params(GraphicsDevice& device, const VoxelEffectParams& params)
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
