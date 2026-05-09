/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  TMP-tileset loader producing a paletted GPU atlas.
 *
 *          Reads the in-memory `IsoTileSet` produced by vanilla's TMP loader,
 *          walks the per-sub-tile `IsoTileRecord` array, and packs every
 *          sub-tile's diamond pixel data into a shared R8_UINT color atlas,
 *          with matching per-pixel `ZData` in a parallel R8_UINT atlas for
 *          the tile shader's `SV_Depth` corrections.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class Texture2D;


    struct TmpSubTileInfo
    {
        int  X = 0;             // pixel origin within the full multi-cell TMP
        int  Y = 0;             // pixel origin within the full multi-cell TMP
        int  W = 0;             // diamond width (pixels)
        int  H = 0;             // diamond height (pixels)
        int  AtlasX = 0;        // global mega-atlas pixel offset
        int  AtlasY = 0;
        int  Height = 0;        // cell elevation (level units, 0 = flat)
        int  RampType = 0;      // -1 / 0..4 ramp orientation
        bool HasZData = false;
        bool HasExtraData = false;

        /**
         *  Optional extra-graphics rect (cliffs / tall tiles). When
         *  HasExtraData is true, ExtraAtlasX/Y points to its mega-atlas slot.
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
         *  Decompress every sub-tile from the in-memory IsoTileSet into the
         *  shared `TmpAtlas` (allocates one region per sub-tile + extras).
         *  All TmpAssets share the same atlas SRV — only sub-tile UV rects
         *  differ between assets.
         */
        bool Load_From_Memory(GraphicsDevice& device, const void* iso_tileset,
                              const char* debug_name = "<memory>");

        void Unload();

        bool Is_Loaded() const { return !SubTiles.empty(); }

        int                       Sub_Tile_Count() const { return (int)SubTiles.size(); }
        int                       Tile_Pixel_Width() const { return TilePixelWidth; }
        int                       Tile_Pixel_Height() const { return TilePixelHeight; }
        const TmpSubTileInfo*     Get_Sub_Tile(int index) const;

    private:
        std::vector<TmpSubTileInfo> SubTiles;
        int                         TilePixelWidth = 0;
        int                         TilePixelHeight = 0;
        std::string                 SourceName;
    };
}
