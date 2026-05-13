/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame terrain-tile queue + flush.
 *
 *          One TileDrawCmd per cell, tagged with the current render phase. Every tile
 *          draws against a single shared `PaletteLUT` (looked up from
 *          `PaletteCache` keyed on the active `PaletteClass*` — typically
 *          vanilla's `IsoTilePalette`) and the tile-effect-owned tint mask,
 *          so Flush_Pass issues exactly one SpriteBatch pass per output
 *          target with depth-write enabled. Per-cell lighting (RedTint /
 *          GreenTint / BlueTint / TileBrightness) rides in the vertex color
 *          attribute and is unfolded by the tile shader.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "gpu_surface_target.h"
#include "render_pass.h"
#include "sprite_batch.h"
#include "tile_effect.h"
#include "iso_tile_asset.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct TileDrawCmd
    {
        IsoTileAsset*    Asset;
        int          SubTileIndex;
        RectF        Dst;             // backbuffer-pixel space
        RectF        Clip;            // backbuffer-pixel scissor rect; invalid = full target
        float        DstZTop;          // depth value [0,1]; 0 = near plane
        float        DstZBottom;
        RenderPass   Pass;
        /**
         *  Per-cell lighting at the diamond's centre + four cardinal
         *  corners, each RGBA:
         *    [0] = RedTint   / 1000.0   (1.0 = neutral, 2.0 = max overbright)
         *    [1] = GreenTint / 1000.0
         *    [2] = BlueTint  / 1000.0
         *    [3] = TileBrightness / 1000.0
         *
         *  When the [AudioVisual] SmoothLighting rule is on, the four
         *  corner values are the average of own + the corresponding
         *  cardinal neighbour cell. When off, all five are the cell's
         *  own value and `TileQueue::Flush_Pass` emits a uniform quad.
         *  For DrawExtra commands all five are always the own value.
         */
        float        TintC[4];
        float        TintN[4];
        float        TintE[4];
        float        TintS[4];
        float        TintW[4];
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

        void Flush(GraphicsDevice& device);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);

        void Clear();

        bool Has_Commands() const { return !Commands.empty(); }

    private:
        TileQueue() = default;

        SpriteBatch              Batch;
        TileEffect               TileEffectInstance;
        std::vector<TileDrawCmd> Commands;
        bool                     Initialized = false;
    };
}
