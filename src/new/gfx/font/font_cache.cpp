/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Process-wide cache of WWFont GPU assets keyed on font-data pointer.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "font_cache.h"

#include "font_asset.h"


namespace Vinifera::Gfx
{
    FontCache& FontCache::Get()
    {
        static FontCache instance;
        return instance;
    }


    FontAsset* FontCache::Get_Or_Load(GraphicsDevice& device, const void* font_data)
    {
        if (font_data == nullptr) {
            return nullptr;
        }

        auto it = Map.find(font_data);
        if (it != Map.end()) {
            return it->second.get();
        }

        auto asset = std::make_unique<FontAsset>();
        if (!asset->Load_From_Memory(device, font_data)) {
            return nullptr;
        }
        FontAsset* raw = asset.get();
        Map.emplace(font_data, std::move(asset));
        return raw;
    }


    void FontCache::Clear()
    {
        Map.clear();
    }
}
