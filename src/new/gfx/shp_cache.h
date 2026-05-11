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

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>


class ShapeSet;
class ConvertClass;


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
         *  Look up the palette LUT for `convert`. If not yet cached, decode
         *  the converter's `Translator` field (vanilla's 16-bit RGB565 LUT)
         *  back into 8-bit RGB triples and upload as a 256x1 RGBA8 texture.
         *
         *  Two `ConvertClass*` instances whose decoded RGB bytes are identical
         *  share one `PaletteLUT` — vanilla creates one `LightConvertClass`
         *  per cell, so naive pointer-keying produces hundreds of duplicate
         *  GPU LUTs (and breaks `TileQueue` batching by palette). Content-key
         *  the deduplication so cells under identical lighting share one
         *  texture and collapse into a single draw call.
         *
         *  Returns nullptr on failure.
         */
        PaletteLUT* Get_Or_Build(GraphicsDevice& device, const ConvertClass* convert);

        void Clear();

        /**
         *  Count of unique GPU palettes (post-dedup). The pointer-keyed
         *  alias map can be much larger.
         */
        int Size() const { return (int)Owned.size(); }
        int Alias_Count() const { return (int)ByConvert.size(); }

    private:
        PaletteCache() = default;

        std::vector<std::unique_ptr<PaletteLUT>>           Owned;
        std::unordered_map<const ConvertClass*, PaletteLUT*> ByConvert;
        std::unordered_map<uint64_t,             PaletteLUT*> ByContent;
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
