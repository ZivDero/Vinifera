/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Shared paged mega-atlas for SHP assets.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shp_atlas.h"

#include "debughandler.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    namespace { constexpr int kPad = 1; }


    ShpAtlas& ShpAtlas::Get()
    {
        static ShpAtlas instance;
        return instance;
    }


    bool ShpAtlas::Initialize(GraphicsDevice& device, int page_size)
    {
        if (Is_Initialized()) {
            return true;
        }
        Device = &device;
        PageSize = page_size;
        DEBUG_INFO("ShpAtlas: ready (page size %dx%d, max %d pages).\n",
            PageSize, PageSize, kMaxPages);
        return true;
    }


    void ShpAtlas::Shutdown()
    {
        Pages.clear();
        Device = nullptr;
    }


    void ShpAtlas::Reset()
    {
        /**
         *  Drop the page textures outright. They're R8_UINT 8K×8K (64 MB
         *  each) and we'd otherwise hold them across a theater swap with
         *  stale contents. Re-loaded SHPs will allocate fresh pages.
         */
        Pages.clear();
    }


    bool ShpAtlas::Try_Allocate_On_Page(Page& page, int w, int h, int& out_x, int& out_y)
    {
        if (w > PageSize || h > PageSize) {
            return false;
        }
        int cx = page.CursorX;
        int cy = page.CursorY;
        int rh = page.RowH;
        if (cx + w > PageSize) {
            cx = 0;
            cy += rh + kPad;
            rh = 0;
        }
        if (cy + h > PageSize) {
            return false;
        }
        out_x = cx;
        out_y = cy;
        page.CursorX = cx + w + kPad;
        page.CursorY = cy;
        if (h > rh) rh = h;
        page.RowH = rh;
        return true;
    }


    ShpAtlas::Page* ShpAtlas::Append_Page()
    {
        if (Device == nullptr || (int)Pages.size() >= kMaxPages) {
            return nullptr;
        }
        auto page = std::make_unique<Page>();
        if (!page->Tex.Initialize(*Device, PageSize, PageSize, DXGI_FORMAT_R8_UINT,
                                  D3D11_USAGE_DEFAULT, nullptr, 0)) {
            DEBUG_ERROR("ShpAtlas: failed to create %dx%d page %d.\n",
                PageSize, PageSize, (int)Pages.size());
            return nullptr;
        }
        DEBUG_INFO("ShpAtlas: allocated page %d (%dx%d).\n",
            (int)Pages.size(), PageSize, PageSize);
        Page* raw = page.get();
        Pages.push_back(std::move(page));
        return raw;
    }


    bool ShpAtlas::Allocate_Region(int w, int h, int& out_page, int& out_x, int& out_y)
    {
        if (w <= 0 || h <= 0 || !Is_Initialized()) {
            return false;
        }
        for (int i = 0; i < (int)Pages.size(); ++i) {
            if (Try_Allocate_On_Page(*Pages[i], w, h, out_x, out_y)) {
                out_page = i;
                return true;
            }
        }
        Page* fresh = Append_Page();
        if (fresh == nullptr) {
            DEBUG_ERROR("ShpAtlas: out of space (need %dx%d, %d pages already allocated).\n",
                w, h, (int)Pages.size());
            return false;
        }
        if (!Try_Allocate_On_Page(*fresh, w, h, out_x, out_y)) {
            return false;
        }
        out_page = (int)Pages.size() - 1;
        return true;
    }


    bool ShpAtlas::Upload_Region(int page, int x, int y, int w, int h,
                                 const uint8_t* pixels, int pitch_bytes)
    {
        if (page < 0 || page >= (int)Pages.size()) {
            return false;
        }
        return Pages[page]->Tex.Set_Sub_Data(x, y, w, h, pixels, pitch_bytes);
    }


    Texture2D& ShpAtlas::Get_Page(int page)
    {
        return Pages[page]->Tex;
    }


    const Texture2D& ShpAtlas::Get_Page(int page) const
    {
        return Pages[page]->Tex;
    }


    long long ShpAtlas::Used_Pixels() const
    {
        long long total = 0;
        for (const auto& p : Pages) {
            /**
             *  Approximation matching IsoTileAtlas: completed rows take their
             *  full page width × row height, plus the current row's used
             *  portion.
             */
            total += (long long)p->CursorY * (long long)PageSize
                   + (long long)p->CursorX * (long long)p->RowH;
        }
        return total;
    }


    long long ShpAtlas::Total_Pixels() const
    {
        return (long long)Pages.size() * (long long)PageSize * (long long)PageSize;
    }
}
