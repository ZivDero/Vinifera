/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame sprite queue + flush.
 *
 *          The Draw_Shape proxy submits draw commands here tagged with the
 *          current vanilla Tactical::Render phase. Flush_Pass preserves
 *          submission order within each phase while batching compatible
 *          contiguous commands.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "alpha_write_effect.h"
#include "palette_lut.h"
#include "render_pass.h"
#include "shp_asset.h"
#include "sprite_batch.h"
#include "sprite_effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    /**
     *  Mirrors vanilla's blitter selection for shapes interacting with the
     *  alpha buffer. `Color` is the normal palette draw to the backbuffer
     *  (the only mode that existed before Phase 4.1). `AlphaWriteAdd` and
     *  `AlphaWriteMult` write alpha intensity into the AlphaBuffer instead
     *  of color, matching `SHAPE_WRITE_ALPHA` (1<<15) and
     *  `SHAPE_WRITE_ALPHA_MULT` (1<<8) respectively.
     */
    enum class SpriteDrawMode : uint8_t
    {
        Color = 0,
        AlphaWriteAdd,
        AlphaWriteMult,
    };


    struct SpriteDrawCmd
    {
        ShpAsset*    Asset;
        ShpAsset*    ZAsset;
        PaletteLUT*  Palette;
        int          FrameIndex;
        RectF        Dst;             // backbuffer-pixel space
        RectF        ZSrcUV;          // normalized z-shape atlas UVs
        float        DstZTop;          // depth value [0,1]; 0 = near plane
        float        DstZBottom;
        RenderPass   Pass;
        uint32_t     EffectFlags;     // SEF_* from sprite_effect.h
        float        Tint[4];         // RGBA float, 1.0 = neutral; from Brightness_To_Tint(intensity)
        bool         UseRemap;
        bool         WriteDepth;      // SHAPE_ZREADWRITE: occlude later sprites
        bool         DisableDepth;    // Non-z UI sprites (pips, select brackets, cameos)
        SpriteDrawMode Mode;          // Color | AlphaWriteAdd | AlphaWriteMult
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
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);

        /**
         *  GPU port of vanilla's `AlphaShapeClass::Draw_In_Area` /
         *  `Draw_All` blits. Runs once per frame (not per pass) before any
         *  pass binds the alpha SRV for reading. Iterates the global
         *  `AlphaShapes` vector and submits one alpha-write quad per active
         *  shape; the shader applies vanilla's BrightnessTable formula
         *  multiplicatively to the AlphaUAV.
         */
        void Flush_Alpha_Lights(GraphicsDevice& device);

        void Clear();

    private:
        SpriteQueue() = default;

        SpriteBatch                Batch;
        SpriteEffect               PalEffect;
        AlphaWriteEffect           AlphaEffect;
        std::vector<SpriteDrawCmd> Commands;
        bool                       Initialized = false;
    };
}
