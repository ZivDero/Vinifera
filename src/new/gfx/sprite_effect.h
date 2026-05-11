/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2a palette-LUT sprite Effect.
 *
 *          A pixel-shader uber-effect that takes paletted (R8_UINT) atlas
 *          textures and converts them to RGBA via a 256-entry palette LUT,
 *          with optional house-color remap and the SHAPE_DARKEN /
 *          SHAPE_TRANSLUCENT* flags collapsed into shader uniforms.
 *
 *          Bind layout:
 *            t0 — paletted atlas (R8_UINT)
 *            t1 — palette LUT (RGBA8, 256x1)
 *            t3 — z-shape atlas (R8_UINT, optional)
 *            t4 — alpha buffer (R8_UNORM)
 *            s0 — point-clamp sampler (atlas is loaded, not sampled, but UVs use s0 for the LUTs)
 *            b0 — SpriteBatch ProjMtx
 *            b1 — palette-effect parameters (SpriteEffectParams)
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
        SEF_DARKEN           = 1u << 1,
        SEF_USE_ZSHAPE       = 1u << 5,
        /**
         *  Skip the alpha-buffer modulation. The shared `AlphaTex` is sized
         *  to the scene (backbuffer) and contains tactical alpha-light /
         *  shroud data; sampling it from non-Scene render targets (sidebar
         *  cameos, future menu sprites) bleeds tactical lighting and shroud
         *  through. Set this flag for any bucket whose `OutputTarget` is not
         *  `Scene`.
         */
        SEF_NO_ALPHA_BUFFER  = 1u << 6,
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

        /**
         *  Bind the palette LUT to t1 on both VS and PS slots. Call after
         *  SpriteBatch::Begin (which sets up t0) but before SpriteBatch::End.
         */
        void Bind_Palette(GraphicsDevice& device, PaletteLUT& palette);

        /**
         *  Update per-draw effect parameters in the b1 CB. Call before
         *  SpriteBatch::End.
         */
        void Set_Params(GraphicsDevice& device, const SpriteEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
