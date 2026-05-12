/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame shroud / fog alpha-write queue.
 *
 *          One command per cell submitted during the vanilla render walk.
 *          Writes the AlphaUAV with vanilla's shroud-overwrite / fog-additive
 *          formulas.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "shp_asset.h"
#include "shroud_fog_effect.h"
#include "sprite_batch.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct ShroudFogDrawCmd
    {
        ShpAsset*     Asset;        // SHROUD.SHP or FOG.SHP atlas
        int           FrameIndex;
        int           ScreenX;      // backbuffer-pixel space (post xscale)
        int           ScreenY;      // backbuffer-pixel space (post yscale)
        ShroudFogMode Mode;
    };


    class ShroudFogQueue
    {
    public:
        static ShroudFogQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Submit(const ShroudFogDrawCmd& cmd);
        void Flush(GraphicsDevice& device);
        void Clear();

    private:
        ShroudFogQueue() = default;

        SpriteBatch                   Batch;
        ShroudFogEffect               Effect;
        std::vector<ShroudFogDrawCmd> Commands;
        bool                          Initialized = false;
    };
}
