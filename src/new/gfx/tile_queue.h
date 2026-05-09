/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame terrain-tile queue + flush.
 *
 *          The Draw_Tile proxy submits a TileDrawCmd per cell during
 *          Tactical::Render's terrain pass. Flush groups commands that share
 *          (asset, palette) and issues a SpriteBatch pass per group with
 *          depth-write enabled, so subsequent sprite draws can depth-test
 *          against the terrain.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "palette_lut.h"
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
        float        DstZ;             // depth value [0,1]; 0 = near plane
        uint32_t     VertexTint;      // per-cell brightness modulate
        bool         DrawExtra;       // false = base diamond; true = extra rect (cliff/wall body)
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

        void Clear();

    private:
        TileQueue() = default;

        SpriteBatch              Batch;
        TileEffect               TileEffectInstance;
        std::vector<TileDrawCmd> Commands;
        bool                     Initialized = false;
    };
}
