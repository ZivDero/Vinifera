/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Shared paged mega-atlas for SHP (sprite) assets.
 *
 *          Every ShpAsset packs its frames into this shared R8_UINT atlas
 *          instead of owning its own Texture2D. Multi-page: when a SHP's
 *          frame rect won't fit on the current page, a new page is appended.
 *          All frames of a single SHP are co-located on one page (atomic
 *          allocation), so batching across the SpriteQueue collapses to one
 *          texture bind per page rather than per SHP.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    class ShpAtlas
    {
    public:
        static ShpAtlas& Get();

        bool Initialize(GraphicsDevice& device, int page_size = 8192);
        void Shutdown();

        /**
         *  Drop all allocations and underlying page textures. Called from
         *  ShpCache::Clear on video-mode reset / theater swap.
         */
        void Reset();

        /**
         *  Atomically reserve a (w x h) region on a single page. Walks
         *  existing pages first; on miss appends a new page. Returns false
         *  only on hard OOM (page cap exhausted or new page creation fails).
         */
        bool Allocate_Region(int w, int h, int& out_page, int& out_x, int& out_y);

        bool Upload_Region(int page, int x, int y, int w, int h,
                           const uint8_t* pixels, int pitch_bytes);

        Texture2D&       Get_Page(int page);
        const Texture2D& Get_Page(int page) const;

        bool Is_Initialized() const { return Device != nullptr; }

        int  Page_Count() const  { return (int)Pages.size(); }
        int  Page_Width() const  { return PageSize; }
        int  Page_Height() const { return PageSize; }

        long long Used_Pixels() const;
        long long Total_Pixels() const;

    private:
        ShpAtlas() = default;

        struct Page
        {
            Texture2D Tex;
            int CursorX = 0;
            int CursorY = 0;
            int RowH    = 0;
        };

        bool Try_Allocate_On_Page(Page& page, int w, int h, int& out_x, int& out_y);
        Page* Append_Page();

        GraphicsDevice* Device = nullptr;
        int             PageSize = 8192;
        std::vector<std::unique_ptr<Page>> Pages;

        /** Hard cap to keep a runaway atlas from eating gigabytes. */
        static constexpr int kMaxPages = 8;
    };
}
