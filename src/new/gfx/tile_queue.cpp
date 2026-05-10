/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame terrain-tile queue + flush.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tile_queue.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "perf_monitor.h"
#include "tmp_atlas.h"

#include <algorithm>


namespace Vinifera::Gfx
{
    TileQueue& TileQueue::Get()
    {
        static TileQueue instance;
        return instance;
    }


    bool TileQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        if (!Batch.Initialize(device, /*max_quads_per_batch*/ 8192)) {
            return false;
        }
        if (!TileEffectInstance.Initialize(device)) {
            Batch.Shutdown();
            return false;
        }
        Commands.reserve(4096);
        Initialized = true;
        return true;
    }


    void TileQueue::Shutdown()
    {
        TileEffectInstance.Shutdown();
        Batch.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void TileQueue::Submit(const TileDrawCmd& cmd)
    {
        if (!Initialized || cmd.Asset == nullptr || cmd.Palette == nullptr) {
            return;
        }
        Commands.push_back(cmd);
        PerfMonitor::Get().Note_Tile_Submit();
    }


    void TileQueue::Clear()
    {
        Commands.clear();
    }


    void TileQueue::Flush(GraphicsDevice& device)
    {
        for (int pass = 0; pass < (int)RenderPass::Count; ++pass) {
            Flush_Pass(device, (RenderPass)pass);
        }
        Commands.clear();
    }


    void TileQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        std::vector<TileDrawCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const TileDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass) {
                pass_commands.push_back(cmd);
            }
        }
        if (pass_commands.empty()) {
            return;
        }

        /**
         *  Sort by (OutputTarget, Palette). Terrain is scene-only by
         *  construction, so the outer bucketing degenerates to one bucket
         *  (`Scene`) in practice — but threading it keeps the queue
         *  consistent with `SpriteQueue` / `PrimitiveQueue` and would
         *  correctly handle a hypothetical future tile target.
         */
        std::sort(pass_commands.begin(), pass_commands.end(),
            [](const TileDrawCmd& a, const TileDrawCmd& b) {
                if (a.OutputTarget != b.OutputTarget) {
                    return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
                }
                return a.Palette < b.Palette;
            });

        Texture2D& shared_atlas = TmpAtlas::Get().Get_Texture();

        TileEffectParams params = {};
        params.AtlasSize[0] = (float)shared_atlas.Width();
        params.AtlasSize[1] = (float)shared_atlas.Height();
        params.ZDataDepthScale = 1.0f / 16000.0f;
        ID3D11ShaderResourceView* z_atlas_srv = TmpAtlas::Get().Get_Z_Texture().Get_SRV();

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
                    DEBUG_WARNING("TileQueue: dropping %zu commands with OutputTarget::None.\n",
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
            const EDepthStencil depth_state = is_sidebar
                ? EDepthStencil::None
                : EDepthStencil::WriteLessEqual;

            size_t i = bucket_start;
            while (i < bucket_end) {
                size_t j = i + 1;
                while (j < bucket_end && pass_commands[j].Palette == pass_commands[i].Palette) {
                    ++j;
                }

                const TileDrawCmd& head = pass_commands[i];

                Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                            &TileEffectInstance, target_w, target_h, depth_state);
                TileEffectInstance.Bind_Palette(device, *head.Palette);
                TileEffectInstance.Set_Params(device, params);
                device.Get_Context()->PSSetShaderResources(2, 1, &z_atlas_srv);

                /**
                 *  Alpha buffer at PS slot 3. Populated for the frame by
                 *  `SpriteQueue::Flush_Alpha_Lights`, sampled per-pixel by the
                 *  tile shader to modulate brightness for alpha lights.
                 */
                ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
                device.Get_Context()->PSSetShaderResources(3, 1, &alpha_srv);

                for (size_t k = i; k < j; ++k) {
                    const TileDrawCmd& c = pass_commands[k];
                    const TmpSubTileInfo* st = c.Asset->Get_Sub_Tile(c.SubTileIndex);
                    if (st == nullptr) continue;

                    RectF src;
                    if (c.DrawExtra) {
                        if (!st->HasExtraData || st->ExtraW <= 0 || st->ExtraH <= 0) continue;
                        src = { (float)st->ExtraAtlasX, (float)st->ExtraAtlasY,
                                (float)st->ExtraW,       (float)st->ExtraH };
                    } else {
                        if (st->W <= 0 || st->H <= 0) continue;
                        src = { (float)st->AtlasX, (float)st->AtlasY,
                                (float)st->W,       (float)st->H };
                    }
                    Batch.Draw(&shared_atlas, c.Dst, &src, c.Tint, c.DstZTop, c.DstZBottom, nullptr,
                               c.Clip.Is_Valid() ? &c.Clip : nullptr);
                }

                Batch.End(device);
                PerfMonitor::Get().Note_Tile_Batch();
                PerfMonitor::Get().Note_Tile_Draw_Call();
                i = j;
            }

            bucket_start = bucket_end;
        }

        ID3D11ShaderResourceView* null_srvs[4] = {};
        device.Get_Context()->PSSetShaderResources(0, 4, null_srvs);
    }
}
