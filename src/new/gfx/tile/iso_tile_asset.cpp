/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Isometric-tileset loader producing a paletted GPU atlas.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "iso_tile_asset.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "iso_tile_atlas.h"

#include <algorithm>
#include <cstring>
#include <vector>


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

    }


    const IsoTileSubTileInfo* IsoTileAsset::Get_Sub_Tile(int index) const
    {
        if (index < 0 || index >= (int)SubTiles.size()) {
            return nullptr;
        }
        return &SubTiles[index];
    }


    bool IsoTileAsset::Load_From_Memory(GraphicsDevice& device, const void* iso_tileset,
                                    const char* debug_name)
    {
        Unload();
        if (iso_tileset == nullptr) {
            return false;
        }
        if (debug_name == nullptr) debug_name = "<memory>";
        SourceName = debug_name;

        IsoTileAtlas& atlas = IsoTileAtlas::Get();
        if (!atlas.Initialize(device)) {
            return false;
        }

        const IsoTileSet* set = static_cast<const IsoTileSet*>(iso_tileset);
        const int sub_count = set->MapWidth * set->MapHeight;
        if (sub_count <= 0 || sub_count > 8192) {
            DEBUG_ERROR("IsoTileAsset: '%s' has bad sub-tile count %d.\n", debug_name, sub_count);
            return false;
        }

        /**
         *  TS tiles are stored as a packed asymmetric diamond, 576 bytes:
         *    rows 0..11 widths  4, 8, ..., 48 (sum 312)
         *    rows 12..22 widths 44, 40, ..., 4 (sum 264)
         *    row 23 is implicitly empty
         *  The bounding box is 48×24 (NOT 48×23) — the empty 24th row is
         *  required for adjacent cells to stack without 1-pixel seams in
         *  multi-subtile tiles. TS terrain cells are 48×24; record->X/Y are
         *  only for composing all sub-tiles into a full multi-cell tile image.
         */
        constexpr int kDiamondW = 48;
        constexpr int kDiamondH = 24;
        TilePixelWidth  = kDiamondW;
        TilePixelHeight = kDiamondH;

        SubTiles.resize(sub_count);

        for (int i = 0; i < sub_count; ++i) {
            const IsoTileRecord* record = set->Tiles[i];
            IsoTileSubTileInfo& s = SubTiles[i];

            if (record == nullptr) {
                continue;
            }

            s.X = record->X;
            s.Y = record->Y;
            s.W = kDiamondW;
            s.H = kDiamondH;
            s.Height = record->Height;
            s.RampType = record->RampType;
            s.HasZData = (record->Flags & FLAG_HAS_Z_DATA) != 0;
            s.HasExtraData = (record->Flags & FLAG_HAS_EXTRA_DATA) != 0;

            /**
             *  Unpack 576 bytes of packed-diamond pixel data into a 48x24
             *  grid with zero-padded corners (24th row stays all zero).
             *  The shader's `idx == 0` discard produces the diamond shape.
             */
            const uint8_t* src = reinterpret_cast<const uint8_t*>(record) + sizeof(IsoTileRecord);
            const uint8_t* zsrc = (s.HasZData && record->ZDataOffset > 0)
                ? reinterpret_cast<const uint8_t*>(record) + record->ZDataOffset
                : nullptr;
            uint8_t unpacked[kDiamondW * kDiamondH] = {};
            uint8_t unpacked_z[kDiamondW * kDiamondH] = {};
            int src_off = 0;
            for (int y = 0; y < kDiamondH; ++y) {
                int width;
                if (y < 12) {
                    width = 4 + 4 * y;                 // 4, 8, ..., 48
                } else if (y < 23) {
                    width = 4 * (23 - y);              // 44, 40, ..., 4
                } else {
                    width = 0;                          // row 23 is empty
                }
                if (width == 0) continue;
                const int x_start = (kDiamondW - width) / 2;
                memcpy(&unpacked[y * kDiamondW + x_start], &src[src_off], (size_t)width);
                if (zsrc != nullptr) {
                    memcpy(&unpacked_z[y * kDiamondW + x_start], &zsrc[src_off], (size_t)width);
                }
                src_off += width;
            }

            if (!atlas.Allocate_Region(s.W, s.H, s.AtlasX, s.AtlasY)) {
                DEBUG_ERROR("IsoTileAsset: '%s' atlas full at sub-tile %d.\n", debug_name, i);
                s.W = s.H = 0;
                continue;
            }
            atlas.Upload_Region(s.AtlasX, s.AtlasY, s.W, s.H, unpacked, s.W);
            atlas.Upload_Z_Region(s.AtlasX, s.AtlasY, s.W, s.H, unpacked_z, s.W);

            /**
             *  Optional extra graphics (cliffs / walls / ramp bodies).
             *  Stored as a flat ExtraWidth × ExtraHeight rect (no diamond
             *  packing) at record + ExtraOffset. Followed by ExtraZData
             *  which we ignore for Stage 3.0. Only set HasExtraData=true
             *  if we actually upload — otherwise the renderer would point
             *  to a stale atlas slot.
             */
            bool extra_uploaded = false;
            if (s.HasExtraData) {
                s.ExtraX = record->ExtraX;
                s.ExtraY = record->ExtraY;
                s.ExtraW = record->ExtraWidth;
                s.ExtraH = record->ExtraHeight;
                if (s.ExtraW > 0 && s.ExtraH > 0 && record->ExtraOffset > 0) {
                    const uint8_t* extra =
                        reinterpret_cast<const uint8_t*>(record) + record->ExtraOffset;
                    if (atlas.Allocate_Region(s.ExtraW, s.ExtraH, s.ExtraAtlasX, s.ExtraAtlasY)) {
                        atlas.Upload_Region(s.ExtraAtlasX, s.ExtraAtlasY,
                                            s.ExtraW, s.ExtraH, extra, s.ExtraW);
                        std::vector<uint8_t> extra_z((size_t)s.ExtraW * (size_t)s.ExtraH);
                        if (s.HasZData && record->ExtraZOffset > 0) {
                            const uint8_t* extra_z_src =
                                reinterpret_cast<const uint8_t*>(record) + record->ExtraZOffset;
                            memcpy(extra_z.data(), extra_z_src, extra_z.size());
                        }
                        atlas.Upload_Z_Region(s.ExtraAtlasX, s.ExtraAtlasY,
                                              s.ExtraW, s.ExtraH, extra_z.data(), s.ExtraW);
                        extra_uploaded = true;
                    }
                }
            }
            s.HasExtraData = extra_uploaded;
        }

        DEBUG_INFO("IsoTileAsset: '%s' loaded — %d sub-tiles into shared atlas.\n",
            debug_name, sub_count);
        return true;
    }


    void IsoTileAsset::Unload()
    {
        SubTiles.clear();
        TilePixelWidth = 0;
        TilePixelHeight = 0;
        SourceName.clear();
        /* The shared atlas is not freed here — IsoTileCache::Clear handles it. */
    }
}
