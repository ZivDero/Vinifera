/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  TMP loader producing a paletted GPU atlas.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tmp_asset.h"

#include "debughandler.h"
#include "graphics_device.h"

#include <algorithm>
#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Mirrors vanilla `IsoTileRecord` from
         *  D:/Projects/Tiberian-Sun/code/isotype.h:16-37 with #pragma pack(4).
         *  Defined locally so we don't include the hooker-touched TS header.
         *
         *  Pixel data follows immediately after this struct (sizeof = 52
         *  bytes). ZData at `record + ZDataOffset` if IsHasZData. ExtraData
         *  at `record + ExtraOffset` if IsHasExtraData.
         */
#pragma pack(push, 4)
        struct IsoTileRecord
        {
            int32_t X;
            int32_t Y;
            int32_t ExtraOffset;
            int32_t ZDataOffset;
            int32_t ExtraZOffset;
            int32_t ExtraX;
            int32_t ExtraY;
            int32_t ExtraWidth;
            int32_t ExtraHeight;
            uint32_t Flags;             // bitfield: bit0=HasExtraData, bit1=HasZData, bit2=Randomized
            uint8_t  Height;
            int8_t   TileType;
            int8_t   RampType;
            uint8_t  LowR, LowG, LowB;
            uint8_t  HighR, HighG, HighB;
        };

        /**
         *  Mirrors vanilla `IsoTileSet` from isotype.h:39-107. Header is 4
         *  ints; `Tiles[]` is a flex array of `IsoTileRecord*` pointers
         *  (count = MapWidth * MapHeight).
         */
        struct IsoTileSet
        {
            int32_t MapWidth;
            int32_t MapHeight;
            int32_t Width;
            int32_t Height;
            IsoTileRecord* Tiles[1];     // flex array
        };
#pragma pack(pop)

        static_assert(sizeof(IsoTileRecord) == 52, "IsoTileRecord layout broken");

        constexpr uint32_t FLAG_HAS_EXTRA_DATA = 1u << 0;
        constexpr uint32_t FLAG_HAS_Z_DATA     = 1u << 1;

        constexpr int kAtlasMaxWidth = 4096;
        constexpr int kAtlasPad      = 1;
    }


    const TmpSubTileInfo* TmpAsset::Get_Sub_Tile(int index) const
    {
        if (index < 0 || index >= (int)SubTiles.size()) {
            return nullptr;
        }
        return &SubTiles[index];
    }


    bool TmpAsset::Load_From_Memory(GraphicsDevice& device, const void* iso_tileset,
                                    const char* debug_name)
    {
        Unload();
        if (iso_tileset == nullptr) {
            return false;
        }
        if (debug_name == nullptr) debug_name = "<memory>";
        SourceName = debug_name;

        const IsoTileSet* set = static_cast<const IsoTileSet*>(iso_tileset);
        const int sub_count = set->MapWidth * set->MapHeight;
        if (sub_count <= 0 || sub_count > 8192) {
            DEBUG_ERROR("TmpAsset: '%s' has bad sub-tile count %d.\n", debug_name, sub_count);
            return false;
        }
        TilePixelWidth  = set->Width;
        TilePixelHeight = set->Height;
        if (TilePixelWidth <= 0 || TilePixelHeight <= 0) {
            DEBUG_ERROR("TmpAsset: '%s' has bad tile pixel dims %dx%d.\n",
                debug_name, TilePixelWidth, TilePixelHeight);
            return false;
        }

        SubTiles.resize(sub_count);

        /**
         *  Pass 1: walk records, decompress (no-op since TMP is uncompressed)
         *  per-sub-tile pixel data, allocate atlas slots via shelf packing.
         */
        std::vector<std::vector<uint8_t>> sub_pixels(sub_count);
        std::vector<std::vector<uint8_t>> extra_pixels(sub_count);

        int atlas_x = 0;
        int atlas_y = 0;
        int row_h = 0;
        int atlas_w = 0;

        const int diamond_bytes = TilePixelWidth * TilePixelHeight;

        for (int i = 0; i < sub_count; ++i) {
            const IsoTileRecord* record = set->Tiles[i];
            TmpSubTileInfo& s = SubTiles[i];

            if (record == nullptr) {
                /* Empty slot — leave SubTiles[i] zero-sized. */
                continue;
            }

            s.X = record->X;
            s.Y = record->Y;
            s.W = TilePixelWidth;
            s.H = TilePixelHeight;
            s.Height = record->Height;
            s.RampType = record->RampType;
            s.HasZData = (record->Flags & FLAG_HAS_Z_DATA) != 0;
            s.HasExtraData = (record->Flags & FLAG_HAS_EXTRA_DATA) != 0;

            /**
             *  Base pixel data lives at `record + sizeof(IsoTileRecord)`.
             */
            const uint8_t* base_pixels =
                reinterpret_cast<const uint8_t*>(record) + sizeof(IsoTileRecord);
            sub_pixels[i].assign(base_pixels, base_pixels + diamond_bytes);

            /**
             *  Optional extra graphics (cliffs etc.).
             */
            if (s.HasExtraData) {
                s.ExtraX = record->ExtraX;
                s.ExtraY = record->ExtraY;
                s.ExtraW = record->ExtraWidth;
                s.ExtraH = record->ExtraHeight;
                if (s.ExtraW > 0 && s.ExtraH > 0 && record->ExtraOffset > 0) {
                    const uint8_t* extra =
                        reinterpret_cast<const uint8_t*>(record) + record->ExtraOffset;
                    extra_pixels[i].assign(extra, extra + (size_t)s.ExtraW * (size_t)s.ExtraH);
                }
            }

            /**
             *  Atlas placement: base diamond, then extras alongside.
             */
            const int placed_w = s.W + (s.HasExtraData && s.ExtraW > 0 ? s.ExtraW + kAtlasPad : 0) + kAtlasPad;
            const int placed_h = std::max(s.H, s.HasExtraData ? s.ExtraH : 0);

            if (atlas_x + placed_w > kAtlasMaxWidth && atlas_x > 0) {
                atlas_x = 0;
                atlas_y += row_h + kAtlasPad;
                row_h = 0;
            }

            s.AtlasX = atlas_x;
            s.AtlasY = atlas_y;
            int cursor = atlas_x + s.W + kAtlasPad;
            if (s.HasExtraData && s.ExtraW > 0) {
                s.ExtraAtlasX = cursor;
                s.ExtraAtlasY = atlas_y;
                cursor += s.ExtraW + kAtlasPad;
            }
            atlas_x = cursor;
            if (placed_h > row_h) row_h = placed_h;
            if (atlas_x > atlas_w) atlas_w = atlas_x;
        }

        const int atlas_h = atlas_y + row_h;
        if (atlas_w <= 0 || atlas_h <= 0) {
            DEBUG_ERROR("TmpAsset: '%s' yielded an empty atlas.\n", debug_name);
            return false;
        }

        /**
         *  Pass 2: assemble the atlas pixel buffer.
         */
        std::vector<uint8_t> atlas_pixels((size_t)atlas_w * (size_t)atlas_h, 0);
        for (int i = 0; i < sub_count; ++i) {
            const TmpSubTileInfo& s = SubTiles[i];
            if (s.W <= 0 || s.H <= 0) continue;

            const std::vector<uint8_t>& base = sub_pixels[i];
            for (int y = 0; y < s.H; ++y) {
                memcpy(&atlas_pixels[(s.AtlasY + y) * atlas_w + s.AtlasX],
                       &base[y * s.W],
                       (size_t)s.W);
            }

            if (s.HasExtraData && s.ExtraW > 0 && s.ExtraH > 0) {
                const std::vector<uint8_t>& extra = extra_pixels[i];
                if (!extra.empty()) {
                    for (int y = 0; y < s.ExtraH; ++y) {
                        memcpy(&atlas_pixels[(s.ExtraAtlasY + y) * atlas_w + s.ExtraAtlasX],
                               &extra[y * s.ExtraW],
                               (size_t)s.ExtraW);
                    }
                }
            }
        }

        if (!Atlas.Initialize(device, atlas_w, atlas_h, DXGI_FORMAT_R8_UINT,
                              D3D11_USAGE_DEFAULT, atlas_pixels.data(), atlas_w)) {
            DEBUG_ERROR("TmpAsset: '%s' failed to create atlas texture (%dx%d).\n",
                debug_name, atlas_w, atlas_h);
            return false;
        }

        DEBUG_INFO("TmpAsset: '%s' loaded — %d sub-tiles, atlas %dx%d.\n",
            debug_name, sub_count, atlas_w, atlas_h);
        return true;
    }


    void TmpAsset::Unload()
    {
        Atlas.Shutdown();
        SubTiles.clear();
        TilePixelWidth = 0;
        TilePixelHeight = 0;
        SourceName.clear();
    }
}
