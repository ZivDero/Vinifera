/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for SpotLightClass GPU rendering.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "spotlight_queue.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "gpu_surface_target.h"
#include "render_pass.h"
#include "scene_copy.h"
#include "states.h"

#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  SpotLight shader — see `src/new/gfx/shaders/spotlight.hlsl`.
         */
        const D3D11_INPUT_ELEMENT_DESC SpotLightIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    bool SpotLightEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device, "SPOTLIGHT",
                                SpotLightIL, _countof(SpotLightIL),
                                /* SpriteCB at b0 — float4x4 ProjMtx, 64 bytes */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(CB);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &PerLightCB))) {
            DEBUG_ERROR("SpotLightEffect: PerLightCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void SpotLightEffect::Shutdown()
    {
        Safe_Release(PerLightCB);
        Effect::Shutdown();
    }


    void SpotLightEffect::Set_Per_Light(GraphicsDevice& device, const CB& data)
    {
        if (PerLightCB == nullptr) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(PerLightCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        memcpy(mapped.pData, &data, sizeof(data));
        ctx->Unmap(PerLightCB, 0);
        ctx->VSSetConstantBuffers(1, 1, &PerLightCB);
        ctx->PSSetConstantBuffers(1, 1, &PerLightCB);
    }


    SpotLightQueue& SpotLightQueue::Get()
    {
        static SpotLightQueue instance;
        return instance;
    }


    bool SpotLightQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(),
                                /*initial_capacity*/ 6 * 16);   // 6 verts per quad, room for 16
        if (!Fx.Initialize(device)) {
            VertexBuffer.Shutdown();
            return false;
        }
        Commands.reserve(16);
        Initialized = true;
        return true;
    }


    void SpotLightQueue::Shutdown()
    {
        Fx.Shutdown();
        VertexBuffer.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void SpotLightQueue::Submit(const SpotLightDrawCmd& cmd)
    {
        if (!Initialized) return;
        Commands.push_back(cmd);
    }


    void SpotLightQueue::Clear()
    {
        Commands.clear();
    }


    void SpotLightQueue::Issue_Cmd(GraphicsDevice& device, const SpotLightDrawCmd& cmd,
                                   int scene_w, int scene_h)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        /**
         *  Build a 256x128 quad centered on cmd.Center. Two triangles, 6
         *  vertices in triangle-list order (TL, TR, BR, TL, BR, BL).
         */
        const float left   = (float)(cmd.Center.X - 128);
        const float top    = (float)(cmd.Center.Y - 64);
        const float right  = left + 256.0f;
        const float bottom = top  + 128.0f;

        Vertex* dst = VertexBuffer.Begin(6);
        if (dst == nullptr) return;
        dst[0].Pos[0] = left;  dst[0].Pos[1] = top;
        dst[1].Pos[0] = right; dst[1].Pos[1] = top;
        dst[2].Pos[0] = right; dst[2].Pos[1] = bottom;
        dst[3].Pos[0] = left;  dst[3].Pos[1] = top;
        dst[4].Pos[0] = right; dst[4].Pos[1] = bottom;
        dst[5].Pos[0] = left;  dst[5].Pos[1] = bottom;
        VertexBuffer.End();

        SpotLightEffect::CB cb = {};
        cb.Misc[0] = cmd.EffectiveRadius;
        cb.Misc[1] = left;
        cb.Misc[2] = top;
        cb.Misc[3] = (float)scene_w;
        cb.Geom[0] = (float)scene_h;
        cb.Geom[1] = cmd.UniformMask;
        Fx.Set_Per_Light(device, cb);

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

        /**
         *  Vanilla's CPU blitter writes spotlight pixels unconditionally
         *  inside the 256x128 area — no depth test, no depth write. We must
         *  match that: a depth test against terrain breaks on elevated
         *  tiles whose `depth_y = y_off + tile_h + cell_level*LEVEL_PIXEL_H_1`
         *  pushes them in front of a spotlight quad sampled at the same
         *  screen-Y, culling the disc on any map with cliffs.
         */
        ctx->OMSetDepthStencilState(device.States().Get(EDepthStencil::None), 0);

        const float blend_factor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(device.States().Get(EBlend::Opaque), blend_factor, 0xFFFFFFFFu);

        Fx.Apply(device);

        ID3D11ShaderResourceView* scene_srv = SceneCopy::Get().Get_SRV();
        ctx->PSSetShaderResources(0, 1, &scene_srv);

        ID3D11Buffer* vb = VertexBuffer.Get();
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(6, 0);
    }


    void SpotLightQueue::Flush_Pass(GraphicsDevice& device, int pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }
        if (pass != (int)RenderPass::PostEffects) {
            return;
        }

        if (!SceneCopy::Get().Ensure_Copied(device)) {
            return;
        }

        Bind_Render_Target(device, GpuRenderTarget::Scene);

        const int scene_w = device.Get_Logical_Width();
        const int scene_h = device.Get_Logical_Height();
        if (scene_w <= 0 || scene_h <= 0) {
            return;
        }

        for (const SpotLightDrawCmd& cmd : Commands) {
            Issue_Cmd(device, cmd, scene_w, scene_h);
        }

        ID3D11ShaderResourceView* null_srv = nullptr;
        device.Get_Context()->PSSetShaderResources(0, 1, &null_srv);
    }
}
