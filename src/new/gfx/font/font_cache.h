/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Process-wide cache of WWFont GPU assets keyed on font-data pointer.
 *
 *          Raw font-data pointer as the stable identity key. Lazy-build on
 *          first hit; flushed on video-mode reset.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <memory>
#include <unordered_map>


namespace Vinifera::Gfx
{
    class FontAsset;
    class GraphicsDevice;


    class FontCache
    {
    public:
        static FontCache& Get();

        /**
         *  Look up the asset for `font_data`. If not yet cached, decode and
         *  upload the glyph atlas. Returns nullptr on load failure.
         */
        FontAsset* Get_Or_Load(GraphicsDevice& device, const void* font_data);

        void Clear();

        int Size() const { return (int)Map.size(); }

    private:
        FontCache() = default;
        std::unordered_map<const void*, std::unique_ptr<FontAsset>> Map;
    };
}
