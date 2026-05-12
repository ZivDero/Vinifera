/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame screen-space distortion queue + flush.
 *
 *          Handles SHAPE_PREDATOR (stealth-tank cloak) and is the home for
 *          future WaveClass-style refraction effects. All distortion draws run
 *          in the `RenderPass::PostEffects` slot after the main scene is
 *          complete — at flush time the queue copies the Scene RT to a scene-
 *          copy SRV and renders each quad with a shader that samples the copy
 *          at a warp offset and blends with the sprite color.
 *
 *          Vanilla's per-pixel `BlitTransLucent*ZReadWarp<ushort>` blitters
 *          read `dest[warp_offset]` and blend at 25/50/75 ratios. Our shader
 *          does the GPU equivalent: `lerp(palette[shp], scene_copy[uv+warp],
 *          blend_ratio)`. The Alpha-variants' per-pixel noise shimmer is a
 *          deliberate omission for the first pass; the displacement alone
 *          reproduces the cloak well enough to look right in motion.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "effect.h"
#include "gpu_surface_target.h"
#include "palette_lut.h"
#include "render_pass.h"
#include "shp_asset.h"
#include "sprite_batch.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct DistortionDrawCmd
    {
        ShpAsset*       Asset;
        PaletteLUT*     Palette;
        int             FrameIndex;
        RectF           Dst;           // backbuffer-pixel space (post-scale)
        RectF           Clip;          // backbuffer-pixel scissor; invalid = full target
        float           DstZTop;        // depth value [0,1]; 0 = near plane
        float           DstZBottom;
        int             WarpOffsetPixels;   // signed x-offset for sampling SceneCopy
        float           BlendRatio;     // 0..1; 0 = full sprite, 1 = full background
        RenderPass      Pass;
        GpuRenderTarget OutputTarget = GpuRenderTarget::Scene;
    };


    /**
     *  Effect bundle for the distortion shader (VS+PS+InputLayout). Owns a
     *  small per-batch constant buffer for AtlasSize/SceneSize uniforms.
     */
    class DistortionEffect : public Effect
    {
    public:
        struct Params
        {
            float    AtlasSize[2];
            float    SceneSize[2];
        };

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Set_Params(GraphicsDevice& device, const Params& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };


    class DistortionQueue
    {
    public:
        static DistortionQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Submit(const DistortionDrawCmd& cmd);

        /**
         *  Flushes nothing unless `pass == PostEffects` AND there are queued
         *  commands. On flush: triggers `SceneCopy::Ensure_Copied`, binds the
         *  shared SceneCopy SRV at t2, and issues one quad per command.
         */
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);

        void Clear();

        bool Is_Initialized() const { return Initialized; }

    private:
        DistortionQueue() = default;

        SpriteBatch                  Batch;
        DistortionEffect             FxEffect;
        std::vector<DistortionDrawCmd> Commands;

        bool                         Initialized = false;
    };
}
