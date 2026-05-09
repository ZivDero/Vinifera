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
         *  Walk the queue in submission order, grouping contiguous commands
         *  that share (Asset, Palette). One TileEffect::Set_Params per
         *  asset (atlas size constant within a batch). Depth-write enabled
         *  so sprites can test against the resulting terrain depth.
         */
        size_t i = 0;
        while (i < Commands.size()) {
            size_t j = i + 1;
            while (j < Commands.size()
                && Commands[j].Asset == Commands[i].Asset
                && Commands[j].Palette == Commands[i].Palette) {
                ++j;
            }

            const TileDrawCmd& head = Commands[i];

            TileEffectParams params = {};
            params.AtlasSize[0] = (float)head.Asset->Get_Atlas().Width();
            params.AtlasSize[1] = (float)head.Asset->Get_Atlas().Height();

            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                        &TileEffectInstance, bb_w, bb_h,
                        EDepthStencil::WriteLessEqual);
            TileEffectInstance.Bind_Palette(device, *head.Palette);
            TileEffectInstance.Set_Params(device, params);

            for (size_t k = i; k < j; ++k) {
                const TileDrawCmd& c = Commands[k];
                const TmpSubTileInfo* st = c.Asset->Get_Sub_Tile(c.SubTileIndex);
                if (st == nullptr || st->W <= 0 || st->H <= 0) {
                    continue;
                }
                const RectF src = { (float)st->AtlasX, (float)st->AtlasY,
                                    (float)st->W,      (float)st->H };
                Batch.Draw(&c.Asset->Get_Atlas(), c.Dst, &src, c.VertexTint, c.DstZ);
            }

            Batch.End(device);
            PerfMonitor::Get().Note_Tile_Batch();
            PerfMonitor::Get().Note_Tile_Draw_Call();
            i = j;
        }

        ID3D11ShaderResourceView* null_srvs[2] = {};
        device.Get_Context()->PSSetShaderResources(0, 2, null_srvs);

        Commands.clear();
    }
}
