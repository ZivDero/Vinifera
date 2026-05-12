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
         *  Wave shader — see `src/new/gfx/shaders/wave.hlsl`.
         */
        const D3D11_INPUT_ELEMENT_DESC WaveIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    bool WaveEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device, "WAVE",
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
