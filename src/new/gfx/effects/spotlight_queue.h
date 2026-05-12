/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for `SpotLightClass` GPU rendering.
 *
 *          Vanilla blits a 256x128 precomputed radial-gradient mask onto the
 *          CPU LogicalSurface in `SpotLightClass::Draw_It`, doing a per-pixel
 *          multiplicative brighten: `dest.rgb = saturate(dest.rgb + dest.rgb
 *          * mask.byte / 256)`. The mask byte comes from a precomputed atlas
 *          of 64 concentric-ring textures (one per animation frame), plus 10
 *          tail-fade variants.
 *
 *          On the GPU pipeline we never lock LogicalSurface, so those writes
 *          are lost. This module re-implements the effect as a `PostEffects`-
 *          pass quad: per spotlight, render a 256x128 quad sampling the
 *          per-frame `SceneCopy` snapshot, compute the radial mask byte
 *          analytically in the PS (matches vanilla's concentric-ring math
 *          via a continuous linear falloff), apply the brighten, and write
 *          the result opaquely to the scene RT.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "dynamic_buffer.h"
#include "effect.h"
#include "point.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct SpotLightDrawCmd
    {
        /**
         *  Center of the 256x128 quad in scene-RT pixel coords. The quad
         *  spans (Center - {128, 64}) to (Center + {128, 64}).
         */
        Point2D Center = { 0, 0 };

        /**
         *  Effective radius `R` (in source-space pixels) used by the PS for
         *  the visible disc boundary. For the concentric-ring case (warhead
         *  spotlights, animation index 0..63) this is `2 * scaled_index + 1`
         *  which feeds vanilla's falloff `intensity = 2*R - 2*dist - 2`. For
         *  the uniform-disc case (BuildingLight extra surfaces, index 64..73)
         *  this is the single Draw_Circle radius built in `One_Time`,
         *  `k + 48 * Rule->SpotlightRadius / 358.4`.
         */
        float   EffectiveRadius = 0.0f;

        /**
         *  Uniform mask value used for the BuildingLight extra-surface case.
         *  Vanilla's `One_Time` second loop draws a single filled circle of
         *  constant colour `128 - 6*k` (k = 0..9) into each extra surface.
         *  Negative => use the concentric-ring (warhead) falloff math instead.
         */
        float   UniformMask = -1.0f;
    };


    class SpotLightEffect : public Effect
    {
    public:
        struct CB
        {
            float Misc[4];      // x=EffectiveRadius, y=QuadTopLeft.x, z=QuadTopLeft.y, w=SceneW
            float Geom[4];      // x=SceneH, yzw=unused
        };

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Set_Per_Light(GraphicsDevice& device, const CB& data);

    private:
        ID3D11Buffer* PerLightCB = nullptr;
    };


    class SpotLightQueue
    {
    public:
        static SpotLightQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const SpotLightDrawCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, int pass);
        void Clear();

    private:
        SpotLightQueue() = default;

        void Issue_Cmd(GraphicsDevice& device, const SpotLightDrawCmd& cmd,
                       int scene_w, int scene_h);

        struct Vertex
        {
            float Pos[2];
        };

        DynamicVertexBuffer<Vertex>      VertexBuffer;
        SpotLightEffect                  Fx;
        std::vector<SpotLightDrawCmd>    Commands;
        bool                             Initialized = false;
    };
}
