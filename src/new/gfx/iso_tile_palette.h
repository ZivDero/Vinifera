/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Single global palette + tint-mask used by every terrain tile draw.
 *
 *          Vanilla TS bakes per-cell lighting into a per-LightConvertClass
 *          63-level Translator (~32 KB per drawer × dozens-to-hundreds of
 *          drawers on a typical map). Routing that through `PaletteCache`
 *          produced one GPU LUT per drawer and split `TileQueue` into one
 *          batch per drawer.
 *
 *          This module flips the model: ONE GPU palette (the un-tinted
 *          `IsoTilePalette` art palette) and ONE 256-byte tint-mask SRV
 *          (`_default_mask`, picking which palette indices accept RGB tint).
 *          Per-cell tint + brightness travel through the existing vertex
 *          color attribute (`v.col.rgba`). The tile shader replicates vanilla's
 *          `Apply_Tint` and `AlphaLightingRemap` math in float.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;
    class Texture2D;


    class IsoTilePaletteRes
    {
    public:
        static IsoTilePaletteRes& Get();

        /**
         *  Builds the palette LUT and tint-mask SRV from vanilla's globals
         *  (`IsoTilePalette` @ 0x00804128, `DefaultTintMask` @ 0x007004D0).
         *  Safe to call before vanilla has populated those — `Refresh()` will
         *  upload again when called later. Returns true if the LUT + mask are
         *  ready; false on D3D failure.
         */
        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Re-upload from vanilla globals. Call this on video-mode reset and
         *  on the first frame after game start (palettes are typically loaded
         *  during scenario init, after our `Initialize` runs).
         */
        bool Refresh(GraphicsDevice& device);

        bool Is_Ready() const;

        PaletteLUT*                Get_Palette_LUT() const { return PalLUT; }
        ID3D11ShaderResourceView*  Get_Tint_Mask_SRV() const;

    private:
        IsoTilePaletteRes() = default;
        ~IsoTilePaletteRes() = default;
        IsoTilePaletteRes(const IsoTilePaletteRes&) = delete;
        IsoTilePaletteRes& operator=(const IsoTilePaletteRes&) = delete;

        PaletteLUT* PalLUT     = nullptr;
        Texture2D*  TintMaskTx = nullptr;
        uint64_t    LastPaletteHash = 0;
        uint8_t     LastTintMask[256] = {};
        bool        TintMaskUploaded = false;
    };
}
