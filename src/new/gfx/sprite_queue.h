/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame sprite queue + flush.
 *
 *          The Draw_Shape proxy submits draw commands here in vanilla's
 *          submission order. SpriteQueue::Flush groups commands that share
 *          (asset, palette, effect-flags, remap) and issues a SpriteBatch
 *          pass per group, rendering on top of the present-quad output.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "palette_lut.h"
#include "shp_asset.h"
#include "sprite_batch.h"
#include "sprite_effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct SpriteDrawCmd
    {
        ShpAsset*    Asset;
        PaletteLUT*  Palette;
        int          FrameIndex;
        RectF        Dst;             // backbuffer-pixel space
        float        DstZTop;          // depth value [0,1]; 0 = near plane
        float        DstZBottom;
        uint32_t     EffectFlags;     // SEF_* from sprite_effect.h
        uint32_t     VertexTint;      // RGBA8, derived from `intensity`
        bool         UseRemap;
        bool         WriteDepth;      // SHAPE_Z_READ_WRITE: occlude later sprites
        uint8_t      RemapTable[16];  // copy of caller's `remap` arg
    };


    class SpriteQueue
    {
    public:
        static SpriteQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Push a command onto the back of the queue. No deduplication, no
         *  reordering — vanilla's submission order *is* the layer order until
         *  Stage 3 introduces a depth buffer.
         */
        void Submit(const SpriteDrawCmd& cmd);

        /**
         *  Issue all queued draws to the back buffer, then clear the queue.
         *  Call after the present-quad upload and before ImGui renders.
         */
        void Flush(GraphicsDevice& device);

        void Clear();

    private:
        SpriteQueue() = default;

        SpriteBatch                Batch;
        SpriteEffect               PalEffect;
        std::vector<SpriteDrawCmd> Commands;
        bool                       Initialized = false;
    };
}
