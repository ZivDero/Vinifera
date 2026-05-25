/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame sprite queue + flush.
 *
 *          Commands are tagged with the current render phase. `Flush_Pass`
 *          preserves submission order within each phase while batching
 *          compatible contiguous commands.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "alpha_write_effect.h"
#include "gpu_surface_target.h"
#include "palette_lut.h"
#include "render_pass.h"
#include "shp_asset.h"
#include "sprite_batch.h"
#include "sprite_effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    /**
     *  Blitter selection for shapes interacting with the alpha buffer.
     *  `Color` is the normal palette draw to the backbuffer. `AlphaWriteAdd`
     *  and `AlphaWriteMult` write intensity into the AlphaBuffer instead of
     *  color, matching `SHAPE_WRITE_ALPHA` (1<<15) and
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
        RectF        Clip;            // backbuffer-pixel scissor rect; invalid = full target
        RectF        ZSrcUV;          // normalized z-shape atlas UVs
        float        DstZTop;          // depth value [0,1]; 0 = near plane
        float        DstZBottom;
        RenderPass   Pass;
        uint32_t     EffectFlags;     // SEF_* from sprite_effect.h
        float        Tint[4];         // RGBA float, 1.0 = neutral; from Brightness_To_Tint(intensity)
        bool         WriteDepth;      // SHAPE_ZREADWRITE: occlude later sprites
        bool         DisableDepth;    // Non-z UI sprites (pips, select brackets, cameos)
        SpriteDrawMode Mode;          // Color | AlphaWriteAdd | AlphaWriteMult
        GpuRenderTarget OutputTarget = GpuRenderTarget::Scene;
    };


    class SpriteQueue
    {
    public:
        static SpriteQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Push a command onto the back of the queue. Submission order is
         *  preserved as the layer order within each pass.
         */
        void Submit(const SpriteDrawCmd& cmd);

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

        /**
         *  Render one sprite cmd synchronously against the currently bound
         *  render target. Used by the unit-scratch composite path so SHP
         *  parts (turret, body) of a turreted unit can be drawn into the
         *  scratch in submission order alongside voxel parts.
         */
        void Render_Sprite_Immediate(GraphicsDevice& device, const SpriteDrawCmd& cmd,
                                     int target_w, int target_h);

        /**
         *  Render one sprite cmd synchronously into the per-unit scratch RT
         *  (must already be bound by `UnitScratch::Begin_Unit`). Scales the
         *  cmd's screen-space Dst/Clip rects by kUnitScratchSSAA and
         *  dispatches against the physical scratch dims so the SHP fills
         *  the same SSAA-physical region as the voxel sections of the
         *  same composite unit. Counterpart to
         *  `VoxelQueue::Render_Cmd_To_Scratch_Immediate`.
         */
        void Render_Sprite_To_Scratch_Immediate(GraphicsDevice& device,
                                                const SpriteDrawCmd& cmd);

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
