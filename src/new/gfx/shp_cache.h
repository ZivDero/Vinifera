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


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;
    class ShpAsset;


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
         *  Returns nullptr on failure.
         */
        PaletteLUT* Get_Or_Build(GraphicsDevice& device, const ConvertClass* convert);

        void Clear();

    private:
        PaletteCache() = default;
        std::unordered_map<const ConvertClass*, std::unique_ptr<PaletteLUT>> Map;
    };
}
