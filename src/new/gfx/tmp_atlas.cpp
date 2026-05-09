/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Shared mega-atlas for all TMP tilesets.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tmp_atlas.h"

#include "debughandler.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    namespace { constexpr int kPad = 1; }


    TmpAtlas& TmpAtlas::Get()
    {
        static TmpAtlas instance;
        return instance;
    }


    bool TmpAtlas::Initialize(GraphicsDevice& device, int width, int height)
    {
        if (Atlas.Get_SRV() != nullptr) {
            return true;
        }
        if (!Atlas.Initialize(device, width, height, DXGI_FORMAT_R8_UINT,
                              D3D11_USAGE_DEFAULT, nullptr, 0)) {
            DEBUG_ERROR("TmpAtlas: failed to create %dx%d atlas.\n", width, height);
            return false;
        }
        Reset();
        DEBUG_INFO("TmpAtlas: %dx%d atlas ready.\n", width, height);
        return true;
    }


    void TmpAtlas::Shutdown()
    {
        Atlas.Shutdown();
        CursorX = CursorY = RowH = 0;
    }


    void TmpAtlas::Reset()
    {
        CursorX = 0;
        CursorY = 0;
        RowH = 0;
    }


    bool TmpAtlas::Allocate_Region(int w, int h, int& out_x, int& out_y)
    {
        if (w <= 0 || h <= 0 || Atlas.Get_SRV() == nullptr) {
            return false;
        }
        const int aw = Atlas.Width();
        const int ah = Atlas.Height();

        if (CursorX + w > aw) {
            CursorX = 0;
            CursorY += RowH + kPad;
            RowH = 0;
        }
        if (CursorY + h > ah) {
            DEBUG_ERROR("TmpAtlas: out of space (need %dx%d, cursor at %d,%d, atlas %dx%d).\n",
                w, h, CursorX, CursorY, aw, ah);
            return false;
        }

        out_x = CursorX;
        out_y = CursorY;
        CursorX += w + kPad;
        if (h > RowH) RowH = h;
        return true;
    }


    bool TmpAtlas::Upload_Region(int x, int y, int w, int h,
                                 const uint8_t* pixels, int pitch_bytes)
    {
        return Atlas.Set_Sub_Data(x, y, w, h, pixels, pitch_bytes);
    }


    long long TmpAtlas::Used_Pixels() const
    {
        if (Atlas.Get_SRV() == nullptr) return 0;
        /**
         *  Approximation: completed rows take their full row width × row
         *  height, plus the current row's used portion. Treats the gap from
         *  pad as used (close enough for telemetry).
         */
        const long long aw = Atlas.Width();
        return (long long)CursorY * aw + (long long)CursorX * RowH;
    }


    long long TmpAtlas::Total_Pixels() const
    {
        if (Atlas.Get_SRV() == nullptr) return 0;
        return (long long)Atlas.Width() * (long long)Atlas.Height();
    }
}
