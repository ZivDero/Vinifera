/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette-LUT sprite Effect.
 *
 *          A pixel-shader uber-effect that takes paletted (R8_UINT) atlas
 *          textures and converts them to RGBA via a 256-entry palette LUT,
 *          with optional house-color remap and the SHAPE_DARKEN /
 *          SHAPE_TRANSLUCENT* flags collapsed into shader uniforms.
 *
 *          Bind layout:
 *            t0 — paletted atlas (R8_UINT)
 *            t1 — palette array (Texture2DArray, RGBA8, 256x1xN), shared
 *            t3 — z-shape atlas (R8_UINT)
 *            t4 — alpha buffer (R8_UNORM)
 *            s0 — point-clamp sampler (atlas is loaded, not sampled)
 *            b0 — SpriteBatch ProjMtx
 *            b1 — palette-effect parameters (SpriteEffectParams)
 *
 *          Per-vertex inputs carry palette layer + per-quad SEF_* flags so
 *          the SpriteQueue can mix palettes and SHAPE_DARKEN inside a single
 *          batch. Dual-source blend (EBlend::DualSourceBlend) expresses both
 *          Premultiplied and DARKEN compositing without a state change.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>

#include "effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;
    class Texture2D;


    /**
     *  Per-draw flags. The shader's PerDrawFlags uniform is the bitwise-OR of
     *  these.
     */
    enum SpriteEffectFlag : uint32_t
    {
        SEF_NONE             = 0,
        SEF_DARKEN           = 1u << 1,     // per-vertex: shape used as DARKEN mask
        SEF_USE_ZSHAPE       = 1u << 5,     // per-vertex: sample z-shape atlas for depth
        /**
         *  Skip the alpha-buffer modulation. Set per-batch (in the CB Flags
         *  uniform) when the bucket isn't `Scene`: the shared `AlphaTex` is
         *  sized to the backbuffer and holds tactical alpha-light / shroud
         *  data, so sampling it from sidebar cameos would bleed the tactical
         *  view through.
         */
        SEF_NO_ALPHA_BUFFER  = 1u << 6,
        /**
         *  Per-vertex: PS treats `v.pos.z` as the encoded y-bias for the
         *  `1.5 - SV_y/16000 - v.pos.z - eps` per-pixel formula. Set for
         *  sprites that need a screen-Y gradient across their footprint
         *  (ZGRAD_GROUND overlays, ZGRAD_45DEG ramps). When clear, PS
         *  treats `v.pos.z` as the final depth value (CPU baked it from
         *  `bottom_y`); use this for flat sprites (buildings, walls,
         *  ZGRAD_NONE/90DEG) so they sort against tiles like vanilla
         *  instead of pushing their upper pixels behind tile-bottom z.
         */
        SEF_PIXEL_DEPTH      = 1u << 7,
    };


    struct SpriteEffectParams
    {
        float    AtlasSize[2];      // pixels — used for int2(uv * AtlasSize) -> Load() coord
        float    ZShapeAtlasSize[2];
        float    ZShapeDepthScale;
        uint32_t Flags;
        uint32_t _Pad1[2];
    };


    class SpriteEffect : public Effect
    {
    public:
        SpriteEffect() = default;
        ~SpriteEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Bind_Palette_Array(GraphicsDevice& device);  // bind PaletteArray at t1
        void Set_Params(GraphicsDevice& device, const SpriteEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
