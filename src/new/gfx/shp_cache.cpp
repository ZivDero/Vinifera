/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Process-wide ShpCache and PaletteCache implementations.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shp_cache.h"

#include "convert.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "palette.h"
#include "palette_lut.h"
#include "shapeset.h"
#include "shp_asset.h"
#include "iso_tile_asset.h"
#include "iso_tile_atlas.h"

#include <cstdio>
#include <cstring>


namespace Vinifera::Gfx
{
    ShpCache& ShpCache::Get()
    {
        static ShpCache instance;
        return instance;
    }


    ShpAsset* ShpCache::Get_Or_Load(GraphicsDevice& device, const ShapeSet* shapefile)
    {
        if (shapefile == nullptr) {
            return nullptr;
        }
        auto it = Map.find(shapefile);
        if (it != Map.end()) {
            return it->second.get();
        }

        auto asset = std::make_unique<ShpAsset>();
        char dbg[64];
        std::snprintf(dbg, sizeof(dbg), "ShapeSet@%p", static_cast<const void*>(shapefile));
        if (!asset->Load_From_Memory(device, shapefile, /*size unknown*/ 0, dbg)) {
            /**
             *  Cache the negative result so we don't keep retrying — but use
             *  a null entry so callers know it's a permanent failure.
             */
            Map.emplace(shapefile, nullptr);
            return nullptr;
        }
        ShpAsset* raw = asset.get();
        Map.emplace(shapefile, std::move(asset));
        return raw;
    }


    void ShpCache::Clear()
    {
        Map.clear();
    }


    PaletteCache& PaletteCache::Get()
    {
        static PaletteCache instance;
        return instance;
    }


    PaletteLUT* PaletteCache::Get_Or_Build(GraphicsDevice& device, const ConvertClass* convert)
    {
        if (convert == nullptr) {
            return nullptr;
        }
        auto it = ByConvert.find(convert);
        if (it != ByConvert.end()) {
            return it->second.get();
        }

        /**
         *  Decode vanilla's 16-bit RGB565 Translator back into 8-bit RGB.
         *  Translator is a public member; TS always runs in 16-bit so we
         *  treat the LUT as `unsigned short[256]`. The 5/6/5 split is
         *  upconverted to 8-bit via bit-replicate. If Translator is null
         *  (uninitialized converter), fall back to a grayscale identity so
         *  we still render *something* recognizable instead of crashing.
         */
        const void* translator = const_cast<ConvertClass*>(convert)->Translator;

        uint8_t rgb_triples[256 * 3] = {};
        if (translator != nullptr) {
            const uint16_t* lut16 = static_cast<const uint16_t*>(translator);
            for (int i = 0; i < 256; ++i) {
                const uint16_t v = lut16[i];
                const uint8_t r5 = (uint8_t)((v >> 11) & 0x1F);
                const uint8_t g6 = (uint8_t)((v >> 5) & 0x3F);
                const uint8_t b5 = (uint8_t)(v & 0x1F);
                rgb_triples[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
                rgb_triples[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
                rgb_triples[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
            }
        } else {
            for (int i = 0; i < 256; ++i) {
                rgb_triples[i * 3 + 0] = (uint8_t)i;
                rgb_triples[i * 3 + 1] = (uint8_t)i;
                rgb_triples[i * 3 + 2] = (uint8_t)i;
            }
        }

        auto lut = std::make_unique<PaletteLUT>();
        if (!lut->Initialize(device)) {
            return nullptr;
        }
        /**
         *  six_bit=false because the values we just decoded are already 8-bit.
         *  No additional tint — the Translator already had any LightConvert
         *  tint applied at the time the converter was built.
         */
        lut->Update_Palette(rgb_triples, /*six_bit*/ false, 1000, 1000, 1000);

        PaletteLUT* raw = lut.get();
        ByConvert.emplace(convert, std::move(lut));
        return raw;
    }


    PaletteLUT* PaletteCache::Get_Or_Build(GraphicsDevice& device, const PaletteClass* palette,
                                           bool six_bit)
    {
        if (palette == nullptr) {
            return nullptr;
        }
        auto it = ByPalette.find(palette);
        if (it != ByPalette.end()) {
            return it->second.get();
        }

        auto lut = std::make_unique<PaletteLUT>();
        if (!lut->Initialize(device)) {
            return nullptr;
        }
        const unsigned char* rgb_bytes = (const unsigned char*)(*palette);
        lut->Update_Palette(rgb_bytes, six_bit, 1000, 1000, 1000);

        PaletteLUT* raw = lut.get();
        ByPalette.emplace(palette, std::move(lut));
        return raw;
    }


    void PaletteCache::Clear()
    {
        ByConvert.clear();
        ByPalette.clear();
    }


    IsoTileCache& IsoTileCache::Get()
    {
        static IsoTileCache instance;
        return instance;
    }


    IsoTileAsset* IsoTileCache::Get_Or_Load(GraphicsDevice& device, const void* iso_tileset)
    {
        if (iso_tileset == nullptr) {
            return nullptr;
        }
        auto it = Map.find(iso_tileset);
        if (it != Map.end()) {
            return it->second.get();
        }

        auto asset = std::make_unique<IsoTileAsset>();
        char dbg[64];
        std::snprintf(dbg, sizeof(dbg), "IsoTileSet@%p", iso_tileset);
        if (!asset->Load_From_Memory(device, iso_tileset, dbg)) {
            Map.emplace(iso_tileset, nullptr);
            return nullptr;
        }
        IsoTileAsset* raw = asset.get();
        Map.emplace(iso_tileset, std::move(asset));
        return raw;
    }


    void IsoTileCache::Clear()
    {
        Map.clear();
        /* Reset shelf-pack cursor too — re-loaded assets will repopulate. */
        IsoTileAtlas::Get().Reset();
    }
}
