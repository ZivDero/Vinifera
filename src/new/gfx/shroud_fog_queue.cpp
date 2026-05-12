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
#include "shp_atlas.h"
#include "shp_asset.h"
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
        /**
         *  AlphaBuffer is sized to vanilla's logical render resolution
         *  (matches SceneRT), so dst coords pass through unscaled. Was a
         *  bb→logical scale before the SceneRT/AlphaBuffer move to logical.
         */
        const int target_w = device.Get_Logical_Width();
        const int target_h = device.Get_Logical_Height();
        const float xscale = 1.0f;
        const float yscale = 1.0f;

        /**
         *  AlphaBuffer covers the full LogicalSurface, but TacticalRect only
         *  covers the tactical viewport (excluding e.g. the top tabs.shp bar
         *  and any sidebar). Cells whose drawpoint lands in those non-tactical
         *  regions would otherwise scribble shroud/fog into the alpha buffer
         *  there, and bleed through translucent UI. Clip the rasterizer to
         *  TacticalRect so the PS only runs inside the tactical viewport.
         */
        const RectF tactical_clip = {
            (float)TacticalRect.X,
            (float)TacticalRect.Y,
            (float)TacticalRect.Width,
            (float)TacticalRect.Height,
        };

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
                        &Effect, target_w, target_h, EDepthStencil::None);

            ShroudFogEffectParams params = {};
            params.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
            params.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
            params.Mode         = (uint32_t)head.Mode;
            Effect.Set_Params(device, params);

            if (head.Asset->Atlas_Page() < 0) {
                Batch.End(device);
                ++draws;
                i = j;
                continue;
            }
            Texture2D& page_tex = ShpAtlas::Get().Get_Page(head.Asset->Atlas_Page());

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

                Batch.Draw(&page_tex, dst, &src, identity_tint, 0.0f, 0.0f,
                           /*z_uv*/ nullptr, &tactical_clip);
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
