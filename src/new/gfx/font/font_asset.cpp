/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  WWFont (.FON) loader producing a paletted GPU glyph atlas.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "font_asset.h"

#include "debughandler.h"
#include "graphics_device.h"

#include <cstring>
#include <vector>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Mirrors `WWFontClass::FontType` in wwfont.h. Defined locally so we
         *  don't depend on the hooker-touched TS headers.
         */
#pragma pack(push, 1)
        struct FontHeader
        {
            uint16_t FontLength;
            uint8_t  FontCompress;       // == 2 → new (byte-packed) format
            uint8_t  FontDataBlocks;
            uint16_t InfoBlockOffset;
            uint16_t OffsetBlockOffset;  // 256 × uint16  — per-char data offset
            uint16_t WidthBlockOffset;   // 256 × uint8   — per-char width
            uint16_t DataBlockOffset;    // start of glyph bitmap data
            uint16_t HeightOffset;       // 256 × uint16  — high byte = height, low byte = Y baseline offset
        };
#pragma pack(pop)

        static_assert(sizeof(FontHeader) == 14, "FontHeader layout broken");

        constexpr int kFontInfoMaxHeight = 4;
        constexpr int kFontInfoMaxWidth  = 5;

        constexpr int kAtlasMaxWidth = 1024;
        constexpr int kAtlasPad      = 1;
    }


    const FontGlyphInfo* FontAsset::Get_Glyph(int c) const
    {
        if (c < 0 || c >= 256) {
            return nullptr;
        }
        return &Glyphs[c];
    }


    bool FontAsset::Load_From_Memory(GraphicsDevice& device, const void* font_data)
    {
        if (font_data == nullptr) {
            return false;
        }

        const uint8_t* blob = static_cast<const uint8_t*>(font_data);
        const FontHeader& hdr = *reinterpret_cast<const FontHeader*>(blob);

        if (hdr.FontLength < sizeof(FontHeader)) {
            DEBUG_ERROR("FontAsset: FON blob too small (length=%u).\n", hdr.FontLength);
            return false;
        }
        const size_t blob_size = hdr.FontLength;

        /**
         *  InfoBlock layout (from wwfont.cpp `Raw_Width` / `Raw_Height`):
         *    info[4] = max height (RawHeight)
         *    info[5] = max width  (RawWidth)
         */
        if (hdr.InfoBlockOffset + 6 > blob_size) {
            DEBUG_ERROR("FontAsset: InfoBlock out of range.\n");
            return false;
        }
        RawHeight = blob[hdr.InfoBlockOffset + kFontInfoMaxHeight];
        RawWidth  = blob[hdr.InfoBlockOffset + kFontInfoMaxWidth];

        if (hdr.WidthBlockOffset + 256 > blob_size
            || hdr.OffsetBlockOffset + 512 > blob_size
            || hdr.HeightOffset + 512 > blob_size) {
            DEBUG_ERROR("FontAsset: WidthBlock / OffsetBlock / HeightBlock out of range.\n");
            return false;
        }

        const uint8_t* width_block = blob + hdr.WidthBlockOffset;
        const uint16_t* offset_block = reinterpret_cast<const uint16_t*>(blob + hdr.OffsetBlockOffset);
        const uint16_t* height_block = reinterpret_cast<const uint16_t*>(blob + hdr.HeightOffset);

        const bool new_format = (hdr.FontCompress == 2);

        /**
         *  Pass 1: decode every glyph into a per-glyph pixel buffer and
         *  shelf-pack into an atlas. Glyphs with width == 0 (most chars below
         *  0x20) get an empty entry and contribute no atlas pixels.
         */
        std::vector<std::vector<uint8_t>> glyph_pixels(256);
        int atlas_x = 0;
        int atlas_y = 0;
        int row_h = 0;
        int atlas_w = 0;

        for (int c = 0; c < 256; ++c) {
            FontGlyphInfo& gi = Glyphs[c];
            const int width   = width_block[c];
            const uint16_t ho = height_block[c];
            const int height  = (ho >> 8) & 0xFF;
            const int yoff    = ho & 0xFF;

            gi.W = width;
            gi.H = height;
            gi.YOffset = yoff;

            if (width <= 0 || height <= 0) {
                continue;
            }

            /**
             *  Old style (FontCompress != 2): nibble-packed, rows are
             *  `(width+1)/2` bytes, offset is absolute from the blob start.
             *  New style (FontCompress == 2): byte-packed, rows are `width`
             *  bytes, offset is relative to DataBlockOffset.
             */
            const size_t bytes_per_row = new_format ? (size_t)width : (size_t)((width + 1) / 2);
            const size_t glyph_bytes   = bytes_per_row * (size_t)height;
            const size_t data_base     = new_format
                ? (size_t)offset_block[c] + (size_t)hdr.DataBlockOffset
                : (size_t)offset_block[c];

            if (data_base + glyph_bytes > blob_size) {
                DEBUG_WARNING("FontAsset: glyph %d (W=%d H=%d) overruns blob; skipping.\n", c, width, height);
                gi.W = 0;
                gi.H = 0;
                continue;
            }

            std::vector<uint8_t>& pixels = glyph_pixels[c];
            pixels.resize((size_t)width * (size_t)height, 0);
            const uint8_t* src = blob + data_base;

            if (new_format) {
                /* One byte per pixel, raw. */
                for (int y = 0; y < height; ++y) {
                    memcpy(&pixels[(size_t)y * width], src + (size_t)y * width, width);
                }
            } else {
                /* Nibble-packed: low nibble first, then high nibble. */
                for (int y = 0; y < height; ++y) {
                    const uint8_t* row = src + (size_t)y * bytes_per_row;
                    uint8_t* dst = &pixels[(size_t)y * width];
                    int x = 0;
                    while (x < width) {
                        const uint8_t pair = *row++;
                        dst[x++] = pair & 0x0F;
                        if (x < width) {
                            dst[x++] = (pair & 0xF0) >> 4;
                        }
                    }
                }
            }

            /**
             *  Shelf-pack into the atlas. Same approach used by `ShpAsset`.
             */
            const int placed_w = width + kAtlasPad;
            if (atlas_x + placed_w > kAtlasMaxWidth && atlas_x > 0) {
                atlas_x = 0;
                atlas_y += row_h + kAtlasPad;
                row_h = 0;
            }
            gi.AtlasX = atlas_x;
            gi.AtlasY = atlas_y;
            atlas_x += placed_w;
            if (height > row_h) row_h = height;
            if (atlas_x > atlas_w) atlas_w = atlas_x;
        }

        const int atlas_h = atlas_y + row_h;
        if (atlas_w <= 0 || atlas_h <= 0) {
            DEBUG_ERROR("FontAsset: no glyphs decoded — empty atlas.\n");
            return false;
        }

        /**
         *  Pass 2: assemble the atlas pixel buffer and upload as R8_UINT.
         */
        std::vector<uint8_t> atlas_pixels((size_t)atlas_w * (size_t)atlas_h, 0);
        for (int c = 0; c < 256; ++c) {
            const FontGlyphInfo& gi = Glyphs[c];
            if (gi.W <= 0 || gi.H <= 0) continue;
            const std::vector<uint8_t>& src = glyph_pixels[c];
            for (int y = 0; y < gi.H; ++y) {
                memcpy(&atlas_pixels[(size_t)(gi.AtlasY + y) * atlas_w + gi.AtlasX],
                       &src[(size_t)y * gi.W],
                       (size_t)gi.W);
            }
        }

        if (!Atlas.Initialize(device, atlas_w, atlas_h, DXGI_FORMAT_R8_UINT,
                              D3D11_USAGE_DEFAULT, atlas_pixels.data(), atlas_w)) {
            DEBUG_ERROR("FontAsset: failed to create atlas texture (%dx%d).\n", atlas_w, atlas_h);
            return false;
        }

        DEBUG_INFO("FontAsset: loaded — atlas %dx%d, RawW=%d RawH=%d, format=%s.\n",
            atlas_w, atlas_h, RawWidth, RawHeight, new_format ? "byte-packed" : "nibble-packed");
        return true;
    }


    void FontAsset::Unload()
    {
        Atlas.Shutdown();
        Glyphs.fill({});
        RawWidth = 0;
        RawHeight = 0;
    }
}
