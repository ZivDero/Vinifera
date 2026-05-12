/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for `WaveClass` GPU rendering (sonic + laser beams).
 *
 *          Vanilla rasterises the beam polygon on the CPU (`Set_Sonic_Pixel`,
 *          `Set_Laser_Pixel`) against the CPU surface. The GPU pipeline uses a
 *          PostEffects-pass shader instead:
 *          for each captured wave we emit the 6-vertex polygon as a triangle
 *          fan, sample `SceneCopy` (the per-frame scene snapshot) in the PS,
 *          apply type-specific blend math (cyan boost + sine-modulated
 *          perpendicular displacement for sonic; pure red boost for laser),
 *          and write the result back to the scene RT. Reuses the same
 *          `SceneCopy` / `RenderPass::PostEffects` infrastructure as the
 *          predator/cloak effect.
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


    enum class WaveKind : uint8_t
    {
        Sonic = 0,
        Laser = 1,
    };


    struct WaveDrawCmd
    {
        WaveKind  Kind = WaveKind::Sonic;

        /**
         *  Six polygon vertices in scene-RT pixel space — the active wave
         *  pulse hexagon. Order matches `PolygonShapeStruct`'s enum:
         *    [0] END_LEFT, [1] END_MIDDLE, [2] END_RIGHT,
         *    [3] START_RIGHT, [4] START_MIDDLE, [5] START_LEFT.
         *  END_* is the leading edge of the pulse (closer to target), START_*
         *  the trailing edge. The hexagon goes around in that order so a
         *  triangle fan from vertex [0] produces the correct geometry:
         *    (0,1,2), (0,2,3), (0,3,4), (0,4,5).
         */
        Point2D   Vertices[6];

        /**
         *  Perpendicular displacement direction in scene pixels (one of the
         *  8 `DirectionStrides` cardinals from the vanilla beam-facing
         *  lookup). Sonic uses this to offset SceneCopy samples; laser
         *  ignores it.
         */
        Point2D   PerpDir = { 0, 0 };

        /**
         *  Reference point for the sonic ripple's radius lookup, in scene-RT
         *  pixel coords. Vanilla uses the static `WaveStartMiddle` here —
         *  pixels at equal Euclidean distance from this point share the same
         *  sine phase, producing the concentric-ring ripple pattern that
         *  radiates from the wave's source.
         */
        Point2D   RadiusRef = { 0, 0 };

        int       SonicEC   = 0;     // animation phase (sonic only)
        int       LaserMult = 0;     // pre-computed red-boost (laser only, 0..255)
    };


    /**
     *  Custom effect for wave rendering. The shader is unified for both wave
     *  kinds — the PS branches on `Misc.x` to select sonic-warp-and-cyan or
     *  laser-red-boost math.
     */
    class WaveEffect : public Effect
    {
    public:
        struct CB
        {
            float Misc[4];   // x=kind, y=SonicEC, z=LaserMult, w=SceneW
            float Geom[4];   // x=SceneH, yzw=unused
            float Start[4];  // xy=StartMiddle, zw=BeamAxisNormalized
            float Beam[4];   // xy=PerpDirPixels, zw=unused
        };

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Set_Per_Wave(GraphicsDevice& device, const CB& data);

    private:
        ID3D11Buffer* PerWaveCB = nullptr;
    };


    class WaveQueue
    {
    public:
        static WaveQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const WaveDrawCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, /* RenderPass */ int pass);
        void Clear();

    private:
        WaveQueue() = default;

        bool Create_Effect(GraphicsDevice& device);
        void Issue_Cmd(GraphicsDevice& device, const WaveDrawCmd& cmd,
                       int scene_w, int scene_h);

        struct WaveVertex
        {
            float Pos[2];     // scene-RT pixel coords; VS projects to NDC
        };

        DynamicVertexBuffer<WaveVertex> VertexBuffer;
        WaveEffect                       Fx;
        std::vector<WaveDrawCmd>         Commands;
        bool                             Initialized = false;
    };
}
