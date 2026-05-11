/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for depth/alpha-aware tactical lines.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tactical_line_queue.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "perf_monitor.h"

#include <algorithm>
#include <cmath>
#include <cstring>


namespace Vinifera::Gfx
{
    TacticalLineQueue& TacticalLineQueue::Get()
    {
        static TacticalLineQueue instance;
        return instance;
    }


    bool TacticalLineQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(), 4096);
        if (!Effect.Initialize(device)) {
            VertexBuffer.Shutdown();
            return false;
        }
        Commands.reserve(512);
        Initialized = true;
        return true;
    }


    void TacticalLineQueue::Shutdown()
    {
        Effect.Shutdown();
        VertexBuffer.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void TacticalLineQueue::Submit(const TacticalLineCmd& cmd)
    {
        if (!Initialized) {
            return;
        }
        Commands.push_back(cmd);
        PerfMonitor::Get().Note_Tactical_Line_Submit();
    }


    void TacticalLineQueue::Clear()
    {
        Commands.clear();
    }


    void TacticalLineQueue::Draw_Cmd(GraphicsDevice& device, const TacticalLineCmd& cmd, int target_w, int target_h)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) {
            return;
        }

        /**
         *  Build a 6-vertex quad spanning the line endpoints with thickness
         *  applied perpendicular to the line direction. `t` interpolates
         *  along the line so the pixel shader can lerp z and color.
         */
        const float dx = cmd.X1 - cmd.X0;
        const float dy = cmd.Y1 - cmd.Y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float thickness = std::max(1.0f, cmd.Thickness);
        const float half = thickness * 0.5f;

        float nx = 0.0f;
        float ny = 0.0f;
        if (len > 1e-4f) {
            nx = -dy / len * half;
            ny =  dx / len * half;
        } else {
            /* Degenerate: emit a 1-pixel quad at the start. */
            nx = half;
            ny = half;
        }

        LineVertex verts[6];
        verts[0] = { { cmd.X0 + nx, cmd.Y0 + ny }, 0.0f };
        verts[1] = { { cmd.X1 + nx, cmd.Y1 + ny }, 1.0f };
        verts[2] = { { cmd.X1 - nx, cmd.Y1 - ny }, 1.0f };
        verts[3] = { { cmd.X0 + nx, cmd.Y0 + ny }, 0.0f };
        verts[4] = { { cmd.X1 - nx, cmd.Y1 - ny }, 1.0f };
        verts[5] = { { cmd.X0 - nx, cmd.Y0 - ny }, 0.0f };

        LineVertex* dst = VertexBuffer.Begin(6);
        if (dst == nullptr) {
            return;
        }
        memcpy(dst, verts, sizeof(verts));
        VertexBuffer.End();

        /**
         *  Set the b0 ProjMtx (Effect::Initialize created the CB, the SpriteCB).
         *  Build the orthographic projection just like `PrimitiveQueue::Draw_Group`.
         */
        const float L = 0.0f;
        const float R = (float)target_w;
        const float T = 0.0f;
        const float B = (float)target_h;
        struct ProjCB { float Mtx[16]; } cb = {};
        cb.Mtx[0]  = 2.0f / (R - L);
        cb.Mtx[5]  = 2.0f / (T - B);
        cb.Mtx[10] = 1.0f;
        cb.Mtx[12] = (R + L) / (L - R);
        cb.Mtx[13] = (T + B) / (B - T);
        cb.Mtx[15] = 1.0f;
        Effect.Set_Constants(device, &cb);

        TacticalLineEffectParams params = {};
        memcpy(params.ColorStart, cmd.ColorStart, sizeof(params.ColorStart));
        memcpy(params.ColorEnd,   cmd.ColorEnd,   sizeof(params.ColorEnd));
        params.ZStart = cmd.ZStart;
        params.ZEnd   = cmd.ZEnd;
        params.Flags  = cmd.Flags;
        Effect.Set_Params(device, params);

        D3D11_VIEWPORT vp = {};
        vp.Width  = (float)target_w;
        vp.Height = (float)target_h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));
        ctx->OMSetDepthStencilState(device.States().Get(cmd.Depth), 0);

        const float blend_factor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(device.States().Get(cmd.Blend), blend_factor, 0xFFFFFFFFu);

        Effect.Apply(device);
        Effect.Bind_Sources(device);

        ID3D11Buffer* vb = VertexBuffer.Get();
        UINT stride = sizeof(LineVertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(6, 0);
        PerfMonitor::Get().Note_Tactical_Line_Draw_Call();
    }


    void TacticalLineQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        std::vector<TacticalLineCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const TacticalLineCmd& cmd : Commands) {
            if (cmd.Pass == pass) {
                pass_commands.push_back(cmd);
            }
        }
        if (pass_commands.empty()) {
            return;
        }

        /**
         *  Bucket by output target. Within a bucket we just walk the cmds in
         *  order; each one binds its own depth/blend state since they vary
         *  per-cmd (depth-write vs read-only, opaque vs additive, etc.).
         */
        std::stable_sort(pass_commands.begin(), pass_commands.end(),
            [](const TacticalLineCmd& a, const TacticalLineCmd& b) {
                return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
            });

        size_t bucket_start = 0;
        while (bucket_start < pass_commands.size()) {
            const GpuRenderTarget bucket_target = pass_commands[bucket_start].OutputTarget;
            size_t bucket_end = bucket_start + 1;
            while (bucket_end < pass_commands.size()
                && pass_commands[bucket_end].OutputTarget == bucket_target) {
                ++bucket_end;
            }

            if (bucket_target == GpuRenderTarget::None) {
                static bool warned = false;
                if (!warned) {
                    DEBUG_WARNING("TacticalLineQueue: dropping %zu commands with OutputTarget::None.\n",
                        bucket_end - bucket_start);
                    warned = true;
                }
                bucket_start = bucket_end;
                continue;
            }

            Bind_Render_Target(device, bucket_target);

            const bool is_sidebar = (bucket_target == GpuRenderTarget::Sidebar);
            const int target_w = is_sidebar ? device.Get_Sidebar_Target_Width()  : device.Get_Backbuffer_Width();
            const int target_h = is_sidebar ? device.Get_Sidebar_Target_Height() : device.Get_Backbuffer_Height();

            for (size_t i = bucket_start; i < bucket_end; ++i) {
                TacticalLineCmd cmd = pass_commands[i];
                if (is_sidebar) {
                    /**
                     *  Sidebar has no DSV bound and no scene-relative alpha;
                     *  strip those flags and force depth-off so a Sidebar-
                     *  bucketed tactical-line cmd doesn't try to sample
                     *  unbound resources or write to a missing DSV.
                     */
                    cmd.Flags &= ~(TLF_DEPTH_TEST | TLF_DEPTH_WRITE | TLF_ALPHA_MOD
                                 | TLF_ALPHA_TEST_BG | TLF_ALPHA_TEST_FG);
                    cmd.Depth = EDepthStencil::None;
                }
                Draw_Cmd(device, cmd, target_w, target_h);
            }

            bucket_start = bucket_end;
        }

        /**
         *  Unbind PS SRVs so subsequent passes start clean.
         */
        ID3D11ShaderResourceView* null_srvs[2] = {};
        device.Get_Context()->PSSetShaderResources(0, 2, null_srvs);
    }
}
