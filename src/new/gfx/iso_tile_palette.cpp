/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Single global palette + tint-mask used by every terrain tile draw.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "iso_tile_palette.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "palette.h"
#include "palette_lut.h"
#include "texture2d.h"
#include "tibsun_globals.h"


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  FNV-1a 64-bit over the 256x3 RGB bytes — used to skip the upload
         *  when nothing changed since last `Refresh`.
         */
        static uint64_t Hash_RGB(const uint8_t* bytes, size_t len)
        {
            uint64_t h = 0xcbf29ce484222325ull;
            for (size_t i = 0; i < len; ++i) {
                h ^= bytes[i];
                h *= 0x100000001b3ull;
            }
            return h;
        }
    }


    IsoTilePaletteRes& IsoTilePaletteRes::Get()
    {
        static IsoTilePaletteRes instance;
        return instance;
    }


    bool IsoTilePaletteRes::Initialize(GraphicsDevice& device)
    {
        if (PalLUT == nullptr) {
            PalLUT = new PaletteLUT();
            if (!PalLUT->Initialize(device)) {
                DEBUG_ERROR("IsoTilePaletteRes: PaletteLUT init failed.\n");
                delete PalLUT;
                PalLUT = nullptr;
                return false;
            }
        }
        if (TintMaskTx == nullptr) {
            TintMaskTx = new Texture2D();
            if (!TintMaskTx->Initialize(device, 256, 1, DXGI_FORMAT_R8_UNORM)) {
                DEBUG_ERROR("IsoTilePaletteRes: TintMask Texture2D init failed.\n");
                delete TintMaskTx;
                TintMaskTx = nullptr;
                return false;
            }
        }
        TintMaskUploaded = false;
        LastPaletteHash = 0;
        return Refresh(device);
    }


    void IsoTilePaletteRes::Shutdown()
    {
        if (PalLUT != nullptr) {
            PalLUT->Shutdown();
            delete PalLUT;
            PalLUT = nullptr;
        }
        if (TintMaskTx != nullptr) {
            TintMaskTx->Shutdown();
            delete TintMaskTx;
            TintMaskTx = nullptr;
        }
        TintMaskUploaded = false;
        LastPaletteHash = 0;
    }


    bool IsoTilePaletteRes::Refresh(GraphicsDevice& device)
    {
        if (PalLUT == nullptr || TintMaskTx == nullptr) {
            return false;
        }

        /**
         *  IsoTilePalette stores 8-bit-aligned bytes: vanilla applies `<<= 2`
         *  to every byte at .PAL load time (`isotype.cpp:632-635`), so the
         *  raw 6-bit values (0..63) sit in the upper 6 bits of each byte
         *  (range 0..252, low 2 bits zero). Pass `six_bit=false` so
         *  `Update_Palette` writes them as-is — anything else double-shifts.
         */
        const unsigned char* pal_bytes = (const unsigned char*)IsoTilePalette;
        const uint64_t new_hash = Hash_RGB(pal_bytes, 256 * 3);
        if (new_hash != LastPaletteHash) {
            PalLUT->Update_Palette(pal_bytes, /*six_bit*/ false, 1000, 1000, 1000);
            LastPaletteHash = new_hash;
        }

        /**
         *  DefaultTintMask is a 256-byte bool array. Upload as R8_UNORM —
         *  the shader checks `value > 0.5` to pick the tint vs. intensity path.
         */
        uint8_t fresh_mask[256];
        for (int i = 0; i < 256; ++i) {
            fresh_mask[i] = DefaultTintMask[i] ? 255 : 0;
        }
        if (!TintMaskUploaded || memcmp(fresh_mask, LastTintMask, sizeof(fresh_mask)) != 0) {
            if (!TintMaskTx->Set_Data(fresh_mask, 256)) {
                DEBUG_ERROR("IsoTilePaletteRes: TintMask upload failed.\n");
                return false;
            }
            memcpy(LastTintMask, fresh_mask, sizeof(fresh_mask));
            TintMaskUploaded = true;
        }

        return true;
    }


    bool IsoTilePaletteRes::Is_Ready() const
    {
        return PalLUT != nullptr && TintMaskTx != nullptr && TintMaskUploaded;
    }


    ID3D11ShaderResourceView* IsoTilePaletteRes::Get_Tint_Mask_SRV() const
    {
        return TintMaskTx != nullptr ? TintMaskTx->Get_SRV() : nullptr;
    }
}
