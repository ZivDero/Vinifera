/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  WWFont (.FON) loader producing a paletted GPU glyph atlas.
 *
 *          Decodes all 256 glyphs from vanilla's WWFontClass font blob into a
 *          single R8_UINT atlas (palette indices 0..15, 0 = transparent).
 *          Each glyph carries its width, height, baseline Y offset, and the
 *          (AtlasX, AtlasY) of its top-left pixel in the atlas. Mirrors the
 *          shape of `ShpAsset` so the existing palette-LUT shader path can be
 *          reused with a font-specific 16-byte remap LUT.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <array>
#include <cstdint>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct FontGlyphInfo
    {
        int W = 0;             // glyph width in pixels (from WidthBlock[c])
        int H = 0;             // glyph height in pixels (high byte of HeightOffset[c])
        int YOffset = 0;       // vertical baseline offset (low byte of HeightOffset[c])
        int AtlasX = 0;        // top-left pixel of glyph in the atlas
        int AtlasY = 0;
    };


    class FontAsset
    {
    public:
        FontAsset() = default;
        ~FontAsset() = default;

        FontAsset(const FontAsset&) = delete;
        FontAsset& operator=(const FontAsset&) = delete;

        /**
         *  Decode all 256 glyphs from `font_data` (a pointer to a vanilla
         *  `.FON` blob with a `FontType` header), pack into a single
         *  R8_UINT atlas, and upload it to the GPU. Returns false on
         *  malformed input or upload failure.
         */
        bool Load_From_Memory(GraphicsDevice& device, const void* font_data);

        void Unload();

        bool Is_Loaded() const { return Atlas.Get_SRV() != nullptr; }

        int                  Raw_Width() const  { return RawWidth; }
        int                  Raw_Height() const { return RawHeight; }
        const FontGlyphInfo* Get_Glyph(int c) const;
        Texture2D&           Get_Atlas()       { return Atlas; }
        const Texture2D&     Get_Atlas() const { return Atlas; }

    private:
        Texture2D                          Atlas;
        std::array<FontGlyphInfo, 256>     Glyphs = {};
        int                                RawWidth = 0;
        int                                RawHeight = 0;
    };
}
