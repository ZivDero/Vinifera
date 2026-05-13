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
 *          Pass a custom Effect to Begin() to override it; bind any extra SRVs
 *          the effect needs before calling End().
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
        float    ZUV[2];        // normalized UV into optional z-shape atlas
        float    Tint[4];       // RGBA float, 1.0 = neutral, >1.0 = overbright
        uint32_t Layer;         // palette layer in the shared PaletteArray (0 = default)
        uint32_t Flags;         // per-vertex effect flags (SEF_DARKEN, SEF_USE_ZSHAPE, ...)
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
         *  within the texture (nullptr = whole texture). `color` is RGBA8
         *  modulate. `z` is the depth value (0 = near, 1 = far); default 0.
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
         *  Draw with a second UV rectangle for optional z-shape sampling.
         *  `z_uv` is already normalized to the z-shape atlas; nullptr means
         *  no z-shape data for this quad.
         */
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  uint32_t color, float z_top, float z_bottom, const RectF* z_uv);
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  uint32_t color, float z_top, float z_bottom,
                  const RectF* z_uv, const RectF* clip);

        /**
         *  Float-tint variants. RGBA components in the [0, ~2.0] range; used
         *  by the brightness pipeline (vanilla 0..2000 brightness → 0..2.0
         *  RGB multiplier) so overbright values can pass through the vertex
         *  shader before the RT format saturates.
         *
         *  `layer` is the palette index in the shared `PaletteArray`; effects
         *  that don't sample the palette ignore it. `flags` is a bitmask of
         *  `SpriteEffectFlag` values applied per-pixel by the SpriteEffect
         *  shader (SEF_DARKEN, SEF_USE_ZSHAPE).
         */
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  const float tint[4], float z_top, float z_bottom);
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  const float tint[4], float z_top, float z_bottom, const RectF* z_uv);
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  const float tint[4], float z_top, float z_bottom,
                  const RectF* z_uv, const RectF* clip);
        void Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                  const float tint[4], float z_top, float z_bottom,
                  const RectF* z_uv, const RectF* clip,
                  uint32_t layer, uint32_t flags);

        /**
         *  Draw a 9-vertex / 8-triangle grid filling `dst` for terrain
         *  tile cells. Vertices: rect corners, edge midpoints, centre.
         *  Per-vertex tints land at:
         *    centre              → tint_c (cell's own lighting)
         *    top/right/bottom/left edge midpoints (= iso-diamond N/E/S/W
         *                          corners)             → tint_n / e / s / w
         *    rect corners        → tint_c (outside the iso-diamond shape;
         *                          alpha-discarded by the tile shader on a
         *                          standard tile)
         *  Used by terrain-tile rendering to produce a smooth lighting
         *  gradient that matches neighbouring cells at the diamond edges.
         *  The full-rect coverage matches the alpha-discard path used by
         *  the quad emitter, so boundary pixels are not lost to the
         *  rasterizer's top-left fill rule.
         */
        void Draw_Tile_Cell(Texture2D* texture, const RectF& dst, const RectF* src,
                            const float tint_c[4], const float tint_n[4],
                            const float tint_e[4], const float tint_s[4],
                            const float tint_w[4],
                            float z_top, float z_bottom,
                            const RectF* z_uv, const RectF* clip,
                            uint32_t layer, uint32_t flags);

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
        void Flush_Group(GraphicsDevice& device, Texture2D* texture, const D3D11_RECT& scissor,
                         int index_offset, int index_count);

        DynamicVertexBuffer<SpriteVertex> VertexBuffer;
        DynamicIndexBuffer                IndexBuffer;
        Effect                            DefaultEffect;
        Effect*                           ActiveEffect = nullptr;
        EBlend                            ActiveBlend = EBlend::Premultiplied;
        ESampler                          ActiveSampler = ESampler::PointClamp;
        EDepthStencil                     ActiveDepth = EDepthStencil::None;
        int                               TargetWidth = 0;
        int                               TargetHeight = 0;
        int                               MaxQuads = 0;

        enum class PendingMode : uint8_t { Quad, TileCell };

        struct PendingSprite
        {
            Texture2D*   Tex;
            /**
             *  Quad uses V[0..3] (TL, TR, BR, BL).
             *  TileCell uses V[0..8] in row-major order: TL, TM, TR / ML,
             *  CC, MR / BL, BM, BR — a 3x3 grid filling the full rect.
             */
            SpriteVertex V[9];
            RectF        Clip;
            PendingMode  Mode;
            bool         UseClip;
        };
        std::vector<PendingSprite> Pending;
        bool BatchOpen = false;
    };
}
