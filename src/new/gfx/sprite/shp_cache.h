/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Process-wide caches keyed by vanilla TS pointers.
 *
 *  - `ShpCache`     — `const ShapeSet*` -> `ShpAsset` (paletted GPU atlas).
 *  - `PaletteCache` — `const ConvertClass*` -> `PaletteLUT` (256x1 RGBA8 LUT).
 *
 *  Both caches are lazy: `Get_Or_Load` builds the entry on the first hit and
 *  reuses it thereafter. The vanilla pointers are stable for the SHP / palette
 *  lifetime, which makes them valid keys. Both caches are flushed on
 *  video-mode reset since their textures live on the destroyed device.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>


class ShapeSet;
class ConvertClass;
class PaletteClass;


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;
    class ShpAsset;
    class IsoTileAsset;


    class ShpCache
    {
    public:
        static ShpCache& Get();

        /**
         *  Look up the asset for `shapefile`. If not yet cached, decompress
         *  it from memory and upload the atlas to the GPU. Returns nullptr on
         *  load failure (e.g. malformed SHP).
         */
        ShpAsset* Get_Or_Load(GraphicsDevice& device, const ShapeSet* shapefile);

        void Clear();

        int Size() const { return (int)Map.size(); }

    private:
        ShpCache() = default;
        std::unordered_map<const ShapeSet*, std::unique_ptr<ShpAsset>> Map;
    };


    class PaletteCache
    {
    public:
        static PaletteCache& Get();

        /**
         *  Look up the palette LUT for `convert`. Decodes the converter's
         *  16-bit RGB565 `Translator` into 8-bit RGB and uploads as a 256x1
         *  RGBA8 texture. Cached on the pointer. Returns nullptr on failure.
         */
        PaletteLUT* Get_Or_Build(GraphicsDevice& device, const ConvertClass* convert);

        /**
         *  Look up the palette LUT for a raw `PaletteClass*` (no converter).
         *  Used for the tile renderer's lookup of `IsoTilePalette`: the
         *  un-tinted 256-entry art palette, tint + intensity applied in the
         *  shader. `six_bit=false` for `IsoTilePalette` (its bytes are
         *  pre-shifted by vanilla's `<<= 2` loop in `Init_Theater`).
         */
        PaletteLUT* Get_Or_Build(GraphicsDevice& device, const PaletteClass* palette,
                                 bool six_bit = false);

        void Clear();

        int Size() const { return (int)(ByConvert.size() + ByPalette.size()); }

    private:
        PaletteCache() = default;

        std::unordered_map<const ConvertClass*, std::unique_ptr<PaletteLUT>> ByConvert;
        std::unordered_map<const PaletteClass*, std::unique_ptr<PaletteLUT>> ByPalette;
    };


    class IsoTileCache
    {
    public:
        static IsoTileCache& Get();

        /**
         *  Look up the atlas for `iso_tileset` (vanilla's `IsoTileSet*`,
         *  passed in as void* since the layout is mirrored locally inside
         *  IsoTileAsset). Lazy-builds on first hit. Returns nullptr on failure.
         */
        IsoTileAsset* Get_Or_Load(GraphicsDevice& device, const void* iso_tileset);

        void Clear();

        int Size() const { return (int)Map.size(); }

    private:
        IsoTileCache() = default;
        std::unordered_map<const void*, std::unique_ptr<IsoTileAsset>> Map;
    };
}
