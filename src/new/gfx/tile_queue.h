/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame terrain-tile queue + flush.
 *
 *          The Draw_Tile proxy submits a TileDrawCmd per cell tagged with
 *          the current vanilla Tactical::Render phase. Flush_Pass groups
 *          commands that share (asset, palette) and issues a SpriteBatch pass
 *          per group with depth-write enabled.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "gpu_surface_target.h"
#include "palette_lut.h"
#include "render_pass.h"
#include "sprite_batch.h"
#include "tile_effect.h"
#include "tmp_asset.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct TileDrawCmd
    {
        TmpAsset*    Asset;
        PaletteLUT*  Palette;
        int          SubTileIndex;
        RectF        Dst;             // backbuffer-pixel space
        RectF        Clip;            // backbuffer-pixel scissor rect; invalid = full target
        float        DstZTop;          // depth value [0,1]; 0 = near plane
        float        DstZBottom;
        RenderPass   Pass;
        float        Tint[4];         // per-cell brightness modulate (1.0 = neutral, 2.0 = max overbright)
        bool         DrawExtra;       // false = base diamond; true = extra rect (cliff/wall body)
        GpuRenderTarget OutputTarget = GpuRenderTarget::Scene;
    };


    class TileQueue
    {
    public:
        static TileQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Submit(const TileDrawCmd& cmd);

        /**
         *  Issue all queued tile draws to the back buffer with depth-write
         *  enabled, then clear the queue. Call before SpriteQueue::Flush.
         */
        void Flush(GraphicsDevice& device);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);

        void Clear();

    private:
        TileQueue() = default;

        SpriteBatch              Batch;
        TileEffect               TileEffectInstance;
        std::vector<TileDrawCmd> Commands;
        bool                     Initialized = false;
    };
}
