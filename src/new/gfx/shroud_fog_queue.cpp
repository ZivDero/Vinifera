/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame shroud / fog alpha-write queue.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shroud_fog_queue.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "perf_monitor.h"
#include "tibsun_globals.h"

#include <algorithm>


namespace Vinifera::Gfx
{
    ShroudFogQueue& ShroudFogQueue::Get()
    {
        static ShroudFogQueue instance;
        return instance;
    }


    bool ShroudFogQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        /**
         *  Per-frame submission count is bounded by the visible cell rect
         *  times two (shroud + fog per cell). At full HD that's well under
         *  16k cells, so 16384 quads/batch fits typical scenes in a single
         *  batch per (Mode, Asset) group.
         */
        if (!Batch.Initialize(device, /*max_quads_per_batch*/ 16384)) {
            return false;
        }
        if (!Effect.Initialize(device)) {
            Batch.Shutdown();
            return false;
        }
        Commands.reserve(4096);
        Initialized = true;
        return true;
    }


    void ShroudFogQueue::Shutdown()
    {
        Effect.Shutdown();
        Batch.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void ShroudFogQueue::Submit(const ShroudFogDrawCmd& cmd)
    {
        if (!Initialized || cmd.Asset == nullptr) {
            return;
        }
        Commands.push_back(cmd);
    }


    void ShroudFogQueue::Clear()
    {
        Commands.clear();
    }


    void ShroudFogQueue::Flush(GraphicsDevice& device)
    {
        if (!Initialized) {
            return;
        }

        ID3D11UnorderedAccessView* uav = device.Get_Alpha_UAV();
        if (uav == nullptr || Commands.empty()) {
            Commands.clear();
            return;
        }

        ID3D11DeviceContext* ctx = device.Get_Context();
        const int bb_w = device.Get_Backbuffer_Width();
        const int bb_h = device.Get_Backbuffer_Height();

        /**
         *  Logical → backbuffer-pixel scale, applied uniformly to dst
         *  position and extents so the GPU quad covers the same pixel
         *  rect the CPU blitter would have written. Matches the math
         *  used by `Flush_Alpha_Lights` and `Draw_Shape_Proxy_DX11`.
         */
        const float xscale = (VideoWidth > 0)  ? (float)bb_w / (float)VideoWidth  : 1.0f;
        const float yscale = (VideoHeight > 0) ? (float)bb_h / (float)VideoHeight : 1.0f;

        /**
         *  Sort by (Mode, Asset) so contiguous draws share the same
         *  EffectCB and atlas — collapses N per-cell submissions into one
         *  DrawIndexed per (Mode, Asset) group. Vanilla submits shroud +
         *  fog interleaved per cell, so without sorting we'd Begin/End
         *  per command. Within a group, shroud / fog write order doesn't
         *  matter because each pixel is owned by exactly one shroud
         *  command and one fog command — they don't overlap each other.
         */
        std::sort(Commands.begin(), Commands.end(),
            [](const ShroudFogDrawCmd& a, const ShroudFogDrawCmd& b) {
                if (a.Mode != b.Mode) return (uint32_t)a.Mode < (uint32_t)b.Mode;
                return a.Asset < b.Asset;
            });

        /**
         *  Detach AlphaSRV from any prior pass before binding the UAV — D3D
         *  forbids same-resource SRV+UAV in one draw. Same precaution as
         *  `Flush_Alpha_Lights`.
         */
        ID3D11ShaderResourceView* null_srvs[4] = {};
        ctx->PSSetShaderResources(0, 4, null_srvs);
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            0, nullptr, nullptr, 0, 1, &uav, nullptr);

        int submitted = 0;
        int draws = 0;
        const float identity_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        size_t i = 0;
        while (i < Commands.size()) {
            size_t j = i + 1;
            while (j < Commands.size()
                && Commands[j].Mode  == Commands[i].Mode
                && Commands[j].Asset == Commands[i].Asset) {
                ++j;
            }

            const ShroudFogDrawCmd& head = Commands[i];

            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                        &Effect, bb_w, bb_h, EDepthStencil::None);

            ShroudFogEffectParams params = {};
            params.AtlasSize[0] = (float)head.Asset->Get_Atlas().Width();
            params.AtlasSize[1] = (float)head.Asset->Get_Atlas().Height();
            params.Mode         = (uint32_t)head.Mode;
            Effect.Set_Params(device, params);

            for (size_t k = i; k < j; ++k) {
                const ShroudFogDrawCmd& c = Commands[k];
                const ShpFrameInfo* fi = c.Asset->Get_Frame(c.FrameIndex);
                if (fi == nullptr || fi->W <= 0 || fi->H <= 0) continue;

                RectF dst;
                dst.X = (float)(c.ScreenX + fi->X) * xscale;
                dst.Y = (float)(c.ScreenY + fi->Y) * yscale;
                dst.W = (float)fi->W * xscale;
                dst.H = (float)fi->H * yscale;
                const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY,
                                    (float)fi->W,      (float)fi->H };

                Batch.Draw(&c.Asset->Get_Atlas(), dst, &src, identity_tint, 0.0f, 0.0f);
                ++submitted;
            }

            Batch.End(device);
            ++draws;
            i = j;
        }

        /**
         *  Detach UAV so subsequent passes (alpha-lights, tile/sprite reads)
         *  can bind AlphaSRV / RTV without conflict.
         */
        ID3D11UnorderedAccessView* null_uav = nullptr;
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            0, nullptr, nullptr, 0, 1, &null_uav, nullptr);

        PerfMonitor::Get().Set_Shroud_Fog(submitted);
        PerfMonitor::Get().Set_Shroud_Fog_Draws(draws);
        Commands.clear();
    }
}
