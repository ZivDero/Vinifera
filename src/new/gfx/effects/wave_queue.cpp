/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for `WaveClass` GPU rendering.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "wave_queue.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "gpu_surface_target.h"
#include "render_pass.h"
#include "scene_copy.h"
#include "states.h"

#include <cmath>
#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Wave shader. VS projects scene-pixel vertices through the standard
         *  pixel→NDC matrix, with depth derived from screen-Y so the polygon
         *  occludes correctly against terrain. PS branches on `Misc.x`:
         *
         *    Kind 0 (Sonic):
         *      - Compute the pixel's perpendicular distance from the beam's
         *        centre line (`radius`).
         *      - `amp = sin((SonicEC + radius) * 2*pi/500)` mirrors vanilla's
         *        500-entry SineTable phase.
         *      - Bucket amplitude into 0/1/2/3 cells of perpendicular offset
         *        (vanilla's `__colorints_188` does the same in discrete steps,
         *        but the continuous floor() here is visually equivalent).
         *      - Sample SceneCopy at the warp offset.
         *      - Boost G and B channels by `mult = (110 + amp*104)/256`
         *        (matches vanilla's IntensityTable[amp] = 110..214 range).
         *
         *    Kind 1 (Laser):
         *      - Sample SceneCopy at the current pixel (no warp).
         *      - Boost R channel by `Misc.z / 256` (precomputed from LaserEC).
         */
        const char WaveShaderHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer WaveCB : register(b1) {\n"
            "    float4 Misc;   // x=kind, y=SonicEC, z=LaserMult, w=SceneW\n"
            "    float4 Geom;   // x=SceneH, yzw=unused\n"
            "    float4 Start;  // xy=RadiusRef (sonic ripple origin), zw=unused\n"
            "    float4 Beam;   // xy=PerpDirPixels, zw=unused\n"
            "};\n"
            "\n"
            "struct VSIn  { float2 pos : POSITION; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 scene_xy : TEXCOORD0; };\n"
            "\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    // Depth from screen-Y, same convention as every other queue:\n"
            "    // closer to the bottom of the screen = closer to camera =\n"
            "    // smaller depth value. eps keeps the wave slightly in front\n"
            "    // of any terrain pixel at the same Y so the beam paints over\n"
            "    // the ground it crosses.\n"
            "    const float kPixelToDepth = 1.0 / 16000.0;\n"
            "    float z = clamp(1.0 - i.pos.y * kPixelToDepth - 1.0e-4, 1.0e-4, 0.9999);\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0.0, 1.0));\n"
            "    o.pos = float4(p.x, p.y, z, 1.0);\n"
            "    o.scene_xy = i.pos.xy;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<float4> SceneCopy : register(t0);\n"
            "\n"
            "float4 PSMain(VSOut v) : SV_Target {\n"
            "    int kind = (int)Misc.x;\n"
            "    int scene_w = (int)Misc.w;\n"
            "    int scene_h = (int)Geom.x;\n"
            "    int2 px = int2(v.scene_xy);\n"
            "\n"
            "    if (kind == 0) {\n"
            "        // Sonic: Euclidean distance from `RadiusRef` (the wave's\n"
            "        // source point in scene-RT pixels). Pixels at equal\n"
            "        // distance share the sine phase, producing concentric-\n"
            "        // ring ripples radiating from the source. Vanilla's\n"
            "        // RadiusTable does the same (sqrt(dx*dx + dy*dy)).\n"
            "        float radius = length(v.scene_xy - Start.xy);\n"
            "\n"
            "        // Vanilla's SineTable[i] = sin(i * 0.125) * 12 + 0.49, so the\n"
            "        // effective frequency is `i * 0.125` (period ~50 in i units).\n"
            "        // amp scaled into [0, 12] to match vanilla's integer index\n"
            "        // into the WaveIntensityTable.\n"
            "        float sine_v = sin((Misc.y + radius) * 0.125);\n"
            "        float amp = abs(sine_v) * 12.0;             // 0..12\n"
            "\n"
            "        // Vanilla's WaveIntensityTable bucketing: amp 0..1 -> 0 cells,\n"
            "        // 2..6 -> 1 cell, 7..9 -> 2 cells, 10..12 -> 3 cells. Mapped\n"
            "        // here with two `step()` comparisons (cheap branch-free).\n"
            "        int    amp_i = (int)amp;\n"
            "        float  magnitude = (amp_i <= 1) ? 0.0\n"
            "                         : (amp_i <= 6) ? 1.0\n"
            "                         : (amp_i <= 9) ? 2.0 : 3.0;\n"
            "\n"
            "        int2 offset = int2(Beam.xy * magnitude);\n"
            "        int2 sample_px = clamp(px + offset, int2(0, 0),\n"
            "                                int2(scene_w - 1, scene_h - 1));\n"
            "        float4 bg = SceneCopy.Load(int3(sample_px, 0));\n"
            "\n"
            "        // Vanilla's IntensityTable[i] = 110 + i*8; mult = / 256 to put\n"
            "        // the boost in float [0..0.805] range. G/B saturated, R unchanged.\n"
            "        float mult = (110.0 + amp * 8.0) / 256.0;\n"
            "        float3 c;\n"
            "        c.r = bg.r;\n"
            "        c.g = saturate(bg.g + bg.g * mult);\n"
            "        c.b = saturate(bg.b + bg.b * mult);\n"
            "        return float4(c, 1.0);\n"
            "    } else {\n"
            "        // Laser: pure red boost, no warp.\n"
            "        float4 bg = SceneCopy.Load(int3(px, 0));\n"
            "        float mult = Misc.z / 256.0;\n"
            "        float3 c;\n"
            "        c.r = saturate(bg.r + bg.r * mult);\n"
            "        c.g = bg.g;\n"
            "        c.b = bg.b;\n"
            "        return float4(c, 1.0);\n"
            "    }\n"
            "}\n";


        const D3D11_INPUT_ELEMENT_DESC WaveIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    bool WaveEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                WaveShaderHLSL, sizeof(WaveShaderHLSL) - 1,
                "wave",
                WaveIL, _countof(WaveIL),
                /* SpriteCB at b0 — float4x4 ProjMtx, 64 bytes */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(CB);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &PerWaveCB))) {
            DEBUG_ERROR("WaveEffect: PerWaveCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void WaveEffect::Shutdown()
    {
        Safe_Release(PerWaveCB);
        Effect::Shutdown();
    }


    void WaveEffect::Set_Per_Wave(GraphicsDevice& device, const CB& data)
    {
        if (PerWaveCB == nullptr) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(PerWaveCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        memcpy(mapped.pData, &data, sizeof(data));
        ctx->Unmap(PerWaveCB, 0);
        ctx->VSSetConstantBuffers(1, 1, &PerWaveCB);
        ctx->PSSetConstantBuffers(1, 1, &PerWaveCB);
    }


    WaveQueue& WaveQueue::Get()
    {
        static WaveQueue instance;
        return instance;
    }


    bool WaveQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(),
                                /*initial_capacity*/ 12 * 16);   // 12 verts per wave, room for 16
        if (!Create_Effect(device)) {
            VertexBuffer.Shutdown();
            return false;
        }
        Commands.reserve(16);
        Initialized = true;
        return true;
    }


    void WaveQueue::Shutdown()
    {
        Fx.Shutdown();
        VertexBuffer.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    bool WaveQueue::Create_Effect(GraphicsDevice& device)
    {
        return Fx.Initialize(device);
    }


    void WaveQueue::Submit(const WaveDrawCmd& cmd)
    {
        if (!Initialized) return;
        Commands.push_back(cmd);
    }


    void WaveQueue::Clear()
    {
        Commands.clear();
    }


    void WaveQueue::Issue_Cmd(GraphicsDevice& device, const WaveDrawCmd& cmd,
                              int scene_w, int scene_h)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        /**
         *  Build the 6-vertex polygon as a triangle list (4 triangles fanned
         *  from vertex [0]). Vertex order (matches PolygonShapeStruct's enum
         *  going around the polygon):
         *    [0] END_LEFT, [1] END_MIDDLE, [2] END_RIGHT,
         *    [3] START_RIGHT, [4] START_MIDDLE, [5] START_LEFT.
         *  Fan from [0]: (0,1,2), (0,2,3), (0,3,4), (0,4,5). 12 verts total.
         */
        const Point2D (&V)[6] = cmd.Vertices;

        WaveVertex* dst = VertexBuffer.Begin(12);
        if (dst == nullptr) return;

        auto write = [&](int slot, const Point2D& p) {
            dst[slot].Pos[0] = (float)p.X;
            dst[slot].Pos[1] = (float)p.Y;
        };
        write(0,  V[0]); write(1,  V[1]); write(2,  V[2]);
        write(3,  V[0]); write(4,  V[2]); write(5,  V[3]);
        write(6,  V[0]); write(7,  V[3]); write(8,  V[4]);
        write(9,  V[0]); write(10, V[4]); write(11, V[5]);

        VertexBuffer.End();

        /**
         *  Build the per-wave CB. Sonic's radius is the Euclidean distance
         *  from `RadiusRef` (scene-RT pixel coords of `WaveStartMiddle`).
         *  The polygon's actual vertex positions don't matter for the radius
         *  math — only the reference point and the perpendicular displacement.
         */
        WaveEffect::CB cb = {};
        cb.Misc[0]  = (float)(int)cmd.Kind;
        cb.Misc[1]  = (float)cmd.SonicEC;
        cb.Misc[2]  = (float)cmd.LaserMult;
        cb.Misc[3]  = (float)scene_w;

        cb.Geom[0]  = (float)scene_h;

        cb.Start[0] = (float)cmd.RadiusRef.X;
        cb.Start[1] = (float)cmd.RadiusRef.Y;

        cb.Beam[0]  = (float)cmd.PerpDir.X;
        cb.Beam[1]  = (float)cmd.PerpDir.Y;

        Fx.Set_Per_Wave(device, cb);

        /**
         *  Build the projection matrix in the per-effect SpriteCB at b0 (the
         *  effect base class manages a 64-byte CB; we reuse it for ProjMtx).
         *  Same pixel → NDC mapping as PrimitiveQueue.
         */
        const float L = 0.0f;
        const float R = (float)scene_w;
        const float T = 0.0f;
        const float B = (float)scene_h;
        struct ProjCB { float Mtx[16]; } pcb = {};
        pcb.Mtx[0]  = 2.0f / (R - L);
        pcb.Mtx[5]  = 2.0f / (T - B);
        pcb.Mtx[10] = 1.0f;
        pcb.Mtx[12] = (R + L) / (L - R);
        pcb.Mtx[13] = (T + B) / (B - T);
        pcb.Mtx[15] = 1.0f;
        Fx.Set_Constants(device, &pcb);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)scene_w;
        vp.Height   = (float)scene_h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));

        ctx->OMSetDepthStencilState(device.States().Get(EDepthStencil::TestLessEqual_NoWrite), 0);

        const float blend_factor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(device.States().Get(EBlend::Opaque), blend_factor, 0xFFFFFFFFu);

        Fx.Apply(device);

        ID3D11ShaderResourceView* scene_srv = SceneCopy::Get().Get_SRV();
        ctx->PSSetShaderResources(0, 1, &scene_srv);

        ID3D11Buffer* vb = VertexBuffer.Get();
        UINT stride = sizeof(WaveVertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(12, 0);
    }


    void WaveQueue::Flush_Pass(GraphicsDevice& device, int pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }
        if (pass != (int)RenderPass::PostEffects) {
            return;
        }

        /**
         *  Make sure the per-frame scene snapshot exists. Idempotent — the
         *  SHP DistortionQueue and the voxel-predator path may have already
         *  triggered the copy earlier in this PostEffects pass.
         */
        if (!SceneCopy::Get().Ensure_Copied(device)) {
            return;
        }

        /**
         *  Bind the scene RT so our writes land there. PostEffects-pass
         *  callers before us may have rebound; be defensive.
         */
        Bind_Render_Target(device, GpuRenderTarget::Scene);

        const int scene_w = device.Get_Logical_Width();
        const int scene_h = device.Get_Logical_Height();
        if (scene_w <= 0 || scene_h <= 0) {
            return;
        }

        for (const WaveDrawCmd& cmd : Commands) {
            Issue_Cmd(device, cmd, scene_w, scene_h);
        }

        /**
         *  Unbind SceneCopy SRV so subsequent passes (next frame) start clean.
         */
        ID3D11ShaderResourceView* null_srv = nullptr;
        device.Get_Context()->PSSetShaderResources(0, 1, &null_srv);
    }
}
