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
#include "iso_tile_atlas.h"
#include "palette_lut.h"
#include "perf_monitor.h"
#include "extension_globals.h"
#include "rulesext.h"
#include "shp_cache.h"
#include "tibsun_globals.h"

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
        if (!Initialized || cmd.Asset == nullptr) {
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
         *  Stable-sort by OutputTarget only. Every tile uses the single
         *  shared palette (looked up via `PaletteCache::Get_Or_Build`), so
         *  there's no secondary key — one `Batch.Begin/End` per bucket.
         */
        std::stable_sort(pass_commands.begin(), pass_commands.end(),
            [](const TileDrawCmd& a, const TileDrawCmd& b) {
                return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
            });

        PaletteLUT* shared_palette = PaletteCache::Get().Get_Or_Build(device, &IsoTilePalette, /*six_bit*/ false);
        if (shared_palette == nullptr) {
            return;
        }

        Texture2D& shared_atlas = IsoTileAtlas::Get().Get_Texture();

        TileEffectParams params = {};
        params.AtlasSize[0] = (float)shared_atlas.Width();
        params.AtlasSize[1] = (float)shared_atlas.Height();
        params.ZDataDepthScale = 1.0f / 16000.0f;
        ID3D11ShaderResourceView* z_atlas_srv = IsoTileAtlas::Get().Get_Z_Texture().Get_SRV();

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
            const int target_w = is_sidebar ? device.Get_Sidebar_Target_Width()  : device.Get_Logical_Width();
            const int target_h = is_sidebar ? device.Get_Sidebar_Target_Height() : device.Get_Logical_Height();
            /**
             *  Strict LESS matches vanilla's blitter semantics (see
             *  sprite_queue.cpp for the longer comment). Adjacent tile cells
             *  with equal gradient z at overlap pixels fail the back-cell's
             *  test, so front-drawn-first wins — consistent with vanilla's
             *  front-to-back iteration of CellRedraw.
             */
            const EDepthStencil depth_state = is_sidebar
                ? EDepthStencil::None
                : EDepthStencil::WriteLess;

            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                        &TileEffectInstance, target_w, target_h, depth_state);
            TileEffectInstance.Bind_Palette(device, *shared_palette);
            TileEffectInstance.Set_Params(device, params);
            device.Get_Context()->PSSetShaderResources(2, 1, &z_atlas_srv);

            /**
             *  Alpha buffer at PS slot 3. Populated for the frame by
             *  `SpriteQueue::Flush_Alpha_Lights`, sampled per-pixel by the
             *  tile shader as the dynamic alpha-byte input to vanilla's
             *  `AlphaLightingRemap` formula.
             */
            ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
            device.Get_Context()->PSSetShaderResources(3, 1, &alpha_srv);

            /**
             *  Tint mask at PS slot 4. Owned by `TileEffect`; built once from
             *  vanilla's `DefaultTintMask` at effect init.
             */
            TileEffectInstance.Bind_Tint_Mask(device);

            /**
             *  Per-pass sample of `[AudioVisual] SmoothLighting`. When on
             *  and the command is a base-diamond draw, emit a 5-vertex
             *  fan with per-corner tints so terrain lighting interpolates
             *  smoothly between cell centres. Otherwise (DrawExtra or
             *  rule off) fall back to the existing flat quad.
             */
            const bool smooth_lighting = (RuleExtension != nullptr) && RuleExtension->IsSmoothLighting;

            for (size_t k = bucket_start; k < bucket_end; ++k) {
                const TileDrawCmd& c = pass_commands[k];
                const IsoTileSubTileInfo* st = c.Asset->Get_Sub_Tile(c.SubTileIndex);
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

                const RectF* clip = c.Clip.Is_Valid() ? &c.Clip : nullptr;
                if (!c.DrawExtra && smooth_lighting) {
                    Batch.Draw_Tile_Cell(&shared_atlas, c.Dst, &src,
                                         c.TintC, c.TintN, c.TintE, c.TintS, c.TintW,
                                         c.DstZTop, c.DstZBottom, nullptr, clip,
                                         /*layer*/ 0, /*flags*/ 0);
                } else {
                    Batch.Draw(&shared_atlas, c.Dst, &src, c.TintC, c.DstZTop, c.DstZBottom, nullptr, clip);
                }
            }

            Batch.End(device);
            PerfMonitor::Get().Note_Tile_Batch();
            PerfMonitor::Get().Note_Tile_Draw_Call();

            bucket_start = bucket_end;
        }

        ID3D11ShaderResourceView* null_srvs[5] = {};
        device.Get_Context()->PSSetShaderResources(0, 5, null_srvs);
    }
}
