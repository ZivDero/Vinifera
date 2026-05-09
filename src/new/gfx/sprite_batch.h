/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lightweight 2D sprite batcher.
 *
 *          Begin -> Draw -> End. Vertices are appended to a dynamic VB; on
 *          End, all queued sprites are drawn with one DrawIndexed call per
 *          texture-state group.
 *
 *          The default Effect is a textured-quad shader (Tex × VertexColor).
 *          A caller can pass a custom Effect to Begin() (e.g. the Stage 2a
 *          palette-LUT Effect); the caller is responsible for binding any
 *          extra SRVs the Effect needs (palette LUT, remap LUT) before
 *          calling End().
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>
#include <vector>

#include "dynamic_buffer.h"
#include "effect.h"
#include "states.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class Texture2D;


    struct SpriteVertex
    {
        float    Pos[3];        // x, y, z (z = 0 lands at the near plane)
        float    UV[2];
        uint32_t Color;         // RGBA8 (R in low byte)
    };


    /**
     *  Axis-aligned rectangle in pixels (or atlas UV space, depending on
     *  context). Mirrors the common Vinifera Rect convention.
     */
    struct RectF
    {
        float X, Y, W, H;

        bool Is_Valid() const { return W > 0.0f && H > 0.0f; }
    };


    class SpriteBatch
    {
    public:
        SpriteBatch() = default;
        ~SpriteBatch();

        SpriteBatch(const SpriteBatch&) = delete;
        SpriteBatch& operator=(const SpriteBatch&) = delete;

        bool Initialize(GraphicsDevice& device, int max_quads_per_batch = 2048);
        void Shutdown();

        /**
         *  Start a batch. Subsequent Draw() calls collect sprites; End() flushes.
         *  - blend:      blend state (default Premultiplied)
         *  - sampler:    sampler (default PointClamp - suitable for paletted art)
         *  - effect:     custom effect (default the built-in textured-quad effect)
         *  - target_w/h: dimensions of the current render target in pixels
         */
        void Begin(GraphicsDevice& device,
                   EBlend blend = EBlend::Premultiplied,
                   ESampler sampler = ESampler::PointClamp,
                   Effect* effect = nullptr,
                   int target_w = 0, int target_h = 0,
                   EDepthStencil depth = EDepthStencil::None);

        /**
         *  Draw `texture` at backbuffer-pixel rect `dst`. `src` is in pixels
         *  within the texture; nullptr means "the whole texture". `color` is
         *  RGBA8 modulate. `z` is the depth value (0 = near, 1 = far) used
         *  by the shared depth buffer; default is 0 so that callers without
         *  a depth scheme draw at the near plane.
         */
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src, uint32_t color, float z = 0.0f);

        /**
         *  Draw with vertical depth interpolation. Useful for terrain/cliff
         *  quads where the artwork spans multiple screen rows and must write
         *  a depth gradient instead of one constant depth.
         */
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  uint32_t color, float z_top, float z_bottom);

        /**
         *  Convenience: draw at (x, y) with the texture's natural size.
         */
        void Draw(Texture2D* texture, float x, float y, uint32_t color = 0xFFFFFFFFu, float z = 0.0f);

        void End(GraphicsDevice& device);

    private:
        struct SpriteCB
        {
            float ProjMtx[16];
        };

        bool Create_Default_Effect(GraphicsDevice& device);
        void Flush_Group(GraphicsDevice& device, Texture2D* texture, int vertex_offset, int quad_count);

        DynamicVertexBuffer<SpriteVertex> VertexBuffer;
        ID3D11Buffer*                     IndexBuffer = nullptr;     // static, max_quads * 6 indices
        Effect                            DefaultEffect;
        Effect*                           ActiveEffect = nullptr;
        EBlend                            ActiveBlend = EBlend::Premultiplied;
        ESampler                          ActiveSampler = ESampler::PointClamp;
        EDepthStencil                     ActiveDepth = EDepthStencil::None;
        int                               TargetWidth = 0;
        int                               TargetHeight = 0;
        int                               MaxQuads = 0;

        struct PendingSprite
        {
            Texture2D* Tex;
            SpriteVertex V[4];
        };
        std::vector<PendingSprite> Pending;
        bool BatchOpen = false;
    };
}
