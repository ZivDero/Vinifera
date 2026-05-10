/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette-LUT font Effect.
 *
 *          Stripped-down sibling to `SpriteEffect` for WWFont glyph rendering.
 *          Samples a paletted R8_UINT glyph atlas, applies a per-cmd 16-byte
 *          remap (passed in the b1 CB), and converts through the existing
 *          `PaletteLUT` (256×1 RGBA8 — same texture sprites use).
 *
 *          Bind layout:
 *            t0 — paletted glyph atlas (R8_UINT)
 *            t1 — palette LUT (RGBA8, 256x1)
 *            s0 — point-clamp sampler
 *            b0 — SpriteBatch ProjMtx
 *            b1 — font-effect parameters (FontEffectParams)
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


    /**
     *  Per-batch parameters: atlas dimensions in pixels and the 16-byte
     *  remap table the shader applies before palette lookup. The remap is
     *  packed as four uint4s for std-cbuffer alignment; each component is
     *  one palette index (0..255).
     */
    struct FontEffectParams
    {
        float    AtlasSize[2];
        uint32_t _Pad0[2];
        uint32_t Remap[16];   // 16 palette indices, one per uint slot
    };


    class FontEffect : public Effect
    {
    public:
        FontEffect() = default;
        ~FontEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Bind the palette LUT (RGBA8 256×1) to t1 on VS+PS. The atlas at
         *  t0 is set by `SpriteBatch::Begin/Draw` as usual.
         */
        void Bind_Palette(GraphicsDevice& device, PaletteLUT& palette);

        /**
         *  Update b1 with per-batch font params (atlas size + remap).
         */
        void Set_Params(GraphicsDevice& device, const FontEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
