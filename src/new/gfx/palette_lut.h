/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette lookup texture for the GPU sprite shader.
 *
 *          Uploaded as a 256×1 RGBA8 texture; alpha is 0 for index 0
 *          (transparent in TS art) and 255 elsewhere.
 *
 *          Input palette format: 256 entries, each 3 bytes (R, G, B). Values
 *          may be 0..63 (vanilla VGA-era TS palette) — pass `six_bit=true` to
 *          have the LUT do the standard 6-to-8-bit upconvert.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    class PaletteLUT
    {
    public:
        PaletteLUT() = default;
        ~PaletteLUT() = default;

        PaletteLUT(const PaletteLUT&) = delete;
        PaletteLUT& operator=(const PaletteLUT&) = delete;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Rebuild the palette LUT. `rgb_triples` is 256 × 3 bytes. If
         *  `six_bit` is true, each channel is treated as a 0..63 value and
         *  upconverted to 0..255 via `(v << 2) | (v >> 4)`.
         *  `tint_r/g/b` are 0..2000 multipliers (1000 = 100%, matches
         *  LightConvertClass::Apply_Tint).
         */
        void Update_Palette(const uint8_t* rgb_triples, bool six_bit = true,
                            int tint_r = 1000, int tint_g = 1000, int tint_b = 1000);

        Texture2D& Get_Palette_Texture() { return PaletteTex; }

        /**
         *  Layer index into the global `PaletteArray` (assigned on first
         *  `Update_Palette`). -1 until populated. The SpriteEffect samples
         *  the array with this index per-quad so palette-bind state changes
         *  drop out of the SpriteQueue batch key.
         */
        int Layer() const { return ArrayLayer; }

    private:
        Texture2D PaletteTex;   // 256x1 RGBA8 (used by Tile/Font/Shroud/AlphaWrite effects)
        int       ArrayLayer = -1;
    };
}
