/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette-LUT tile Effect.
 *
 *          Renders an isometric tile diamond from a paletted (R8_UINT) atlas
 *          via a 256-entry palette LUT. Per-vertex tint replaces vanilla's
 *          per-tile color-remap pass — cell brightness is just a multiply at
 *          the fragment, smoothly interpolated across the diamond.
 *
 *          Bind layout:
 *            t0 — paletted atlas (R8_UINT)
 *            t1 — palette LUT   (RGBA8, 256x1)
 *            s0 — point-clamp sampler
 *            b0 — SpriteBatch ProjMtx (shared)
 *            b1 — TileEffectParams (atlas size)
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>

#include "effect.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;


    struct TileEffectParams
    {
        float    AtlasSize[2];      // pixels — used for int2(uv * AtlasSize) -> Load
        float    ZDataDepthScale;
        float    _Pad;
    };


    class TileEffect : public Effect
    {
    public:
        TileEffect() = default;
        ~TileEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Bind_Palette(GraphicsDevice& device, PaletteLUT& palette);   // bind palette LUT at t1
        void Bind_Tint_Mask(GraphicsDevice& device);                       // bind tint mask at t4
        void Set_Params(GraphicsDevice& device, const TileEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
        Texture2D     TintMaskTex;
    };
}
