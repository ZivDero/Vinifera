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
        if (!Initialized || Commands.empty()) {
            Commands.clear();
            return;
        }

        const int bb_w = device.Get_Backbuffer_Width();
        const int bb_h = device.Get_Backbuffer_Height();

        device.Bind_Backbuffer();

        /**
         *  All TmpAssets share the global TmpAtlas, so the only batch
         *  boundary is Palette. Sort by Palette so contiguous runs collapse
         *  into one DrawIndexed per distinct palette. Safe to reorder
         *  because tiles depth-test+depth-write — visibility is determined
         *  by Z, not draw order.
         */
        std::sort(Commands.begin(), Commands.end(),
            [](const TileDrawCmd& a, const TileDrawCmd& b) {
                return a.Palette < b.Palette;
            });

        Texture2D& shared_atlas = TmpAtlas::Get().Get_Texture();

        TileEffectParams params = {};
        params.AtlasSize[0] = (float)shared_atlas.Width();
        params.AtlasSize[1] = (float)shared_atlas.Height();
        params.ZDataDepthScale = 1.0f / 16000.0f;
        ID3D11ShaderResourceView* z_atlas_srv = TmpAtlas::Get().Get_Z_Texture().Get_SRV();

        size_t i = 0;
        while (i < Commands.size()) {
            size_t j = i + 1;
            while (j < Commands.size() && Commands[j].Palette == Commands[i].Palette) {
                ++j;
            }

            const TileDrawCmd& head = Commands[i];

            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                        &TileEffectInstance, bb_w, bb_h,
                        EDepthStencil::WriteLessEqual);
            TileEffectInstance.Bind_Palette(device, *head.Palette);
            TileEffectInstance.Set_Params(device, params);
            device.Get_Context()->PSSetShaderResources(2, 1, &z_atlas_srv);

            for (size_t k = i; k < j; ++k) {
                const TileDrawCmd& c = Commands[k];
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
                Batch.Draw(&shared_atlas, c.Dst, &src, c.VertexTint, c.DstZTop, c.DstZBottom);
            }

            Batch.End(device);
            PerfMonitor::Get().Note_Tile_Batch();
            PerfMonitor::Get().Note_Tile_Draw_Call();
            i = j;
        }

        ID3D11ShaderResourceView* null_srvs[3] = {};
        device.Get_Context()->PSSetShaderResources(0, 3, null_srvs);

        Commands.clear();
    }
}
