/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette + remap LUTs.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "palette_lut.h"

#include "graphics_device.h"


namespace Vinifera::Gfx
{
    bool PaletteLUT::Initialize(GraphicsDevice& device)
    {
        if (!PaletteTex.Initialize(device, 256, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                   D3D11_USAGE_DEFAULT, nullptr, 0)) {
            return false;
        }
        if (!RemapTex.Initialize(device, 16, 1, DXGI_FORMAT_R8_UINT,
                                 D3D11_USAGE_DEFAULT, nullptr, 0)) {
            Shutdown();
            return false;
        }

        /**
         *  Default remap = identity (16..31 stay as 16..31).
         */
        Update_Remap(nullptr);
        return true;
    }


    void PaletteLUT::Shutdown()
    {
        PaletteTex.Shutdown();
        RemapTex.Shutdown();
    }


    void PaletteLUT::Update_Palette(const uint8_t* rgb_triples, bool six_bit,
                                    int tint_r, int tint_g, int tint_b)
    {
        if (rgb_triples == nullptr || PaletteTex.Get_Texture() == nullptr) {
            return;
        }

        uint8_t lut[256 * 4] = {};
        for (int i = 0; i < 256; ++i) {
            uint8_t r = rgb_triples[i * 3 + 0];
            uint8_t g = rgb_triples[i * 3 + 1];
            uint8_t b = rgb_triples[i * 3 + 2];
            if (six_bit) {
                r = (uint8_t)((r << 2) | (r >> 4));
                g = (uint8_t)((g << 2) | (g >> 4));
                b = (uint8_t)((b << 2) | (b >> 4));
            }
            int rr = (int)r * tint_r / 1000;
            int gg = (int)g * tint_g / 1000;
            int bb = (int)b * tint_b / 1000;
            if (rr > 255) rr = 255;
            if (gg > 255) gg = 255;
            if (bb > 255) bb = 255;

            lut[i * 4 + 0] = (uint8_t)rr;
            lut[i * 4 + 1] = (uint8_t)gg;
            lut[i * 4 + 2] = (uint8_t)bb;
            lut[i * 4 + 3] = (i == 0) ? 0 : 255;     // index 0 transparent
        }

        PaletteTex.Set_Data(lut, 256 * 4);
    }


    void PaletteLUT::Update_Remap(const uint8_t* remap_indices)
    {
        if (RemapTex.Get_Texture() == nullptr) {
            return;
        }
        uint8_t buf[16];
        if (remap_indices != nullptr) {
            memcpy(buf, remap_indices, 16);
        } else {
            for (int i = 0; i < 16; ++i) buf[i] = (uint8_t)(16 + i);
        }
        RemapTex.Set_Data(buf, 16);
    }
}
