/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  TMP-tileset loader producing a paletted GPU atlas.
 *
 *          Reads the in-memory `IsoTileSet` produced by vanilla's TMP loader,
 *          walks the per-sub-tile `IsoTileRecord` array, and packs every
 *          sub-tile's diamond pixel data into a single R8_UINT atlas.
 *          Per-pixel `ZData` is read but unused in Stage 3.0 (deferred to a
 *          future stage that wires `SV_Depth` corrections).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct TmpSubTileInfo
    {
        int  X = 0;             // logical pixel X origin within the cell diamond
        int  Y = 0;             // logical pixel Y origin within the cell diamond
        int  W = 0;             // diamond width (pixels)
        int  H = 0;             // diamond height (pixels)
        int  AtlasX = 0;        // atlas pixel offset
        int  AtlasY = 0;
        int  Height = 0;        // cell elevation (level units, 0 = flat)
        int  RampType = 0;      // -1 / 0..4 ramp orientation
        bool HasZData = false;
        bool HasExtraData = false;

        /**
         *  Optional extra-graphics rect (cliffs / tall tiles). When
         *  HasExtraData is true, the atlas slot extends beyond W/H to
         *  include the extra rect; ExtraAtlasX/Y point to that region.
         */
        int  ExtraX = 0;
        int  ExtraY = 0;
        int  ExtraW = 0;
        int  ExtraH = 0;
        int  ExtraAtlasX = 0;
        int  ExtraAtlasY = 0;
    };


    class TmpAsset
    {
    public:
        TmpAsset() = default;
        ~TmpAsset() = default;

        TmpAsset(const TmpAsset&) = delete;
        TmpAsset& operator=(const TmpAsset&) = delete;

        /**
         *  Build the atlas from the in-memory IsoTileSet pointer that vanilla
         *  produced. The pointer's lifetime is owned by the engine; we copy
         *  pixel data into a GPU texture.
         */
        bool Load_From_Memory(GraphicsDevice& device, const void* iso_tileset,
                              const char* debug_name = "<memory>");

        void Unload();

        bool Is_Loaded() const { return Atlas.Get_SRV() != nullptr; }

        int                       Sub_Tile_Count() const { return (int)SubTiles.size(); }
        int                       Tile_Pixel_Width() const { return TilePixelWidth; }
        int                       Tile_Pixel_Height() const { return TilePixelHeight; }
        const TmpSubTileInfo*     Get_Sub_Tile(int index) const;

        Texture2D&                Get_Atlas() { return Atlas; }
        const Texture2D&          Get_Atlas() const { return Atlas; }

    private:
        Texture2D                   Atlas;
        std::vector<TmpSubTileInfo> SubTiles;
        int                         TilePixelWidth = 0;
        int                         TilePixelHeight = 0;
        std::string                 SourceName;
    };
}
