/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Shroud / fog alpha-write Effect.
 *
 *          PS_5_0 shader bound with `RWTexture2D<unorm float>` over the
 *          GraphicsDevice's AlphaUAV. Two modes selected by a uniform:
 *
 *            Mode 0 (ShroudOverwrite): vanilla `Draw_Shroud_Or_Fog_Shape`
 *              formula. Reads the SHP atlas pixel; writes it directly into
 *              the alpha buffer unless the source byte is 0xFE (vanilla's
 *              transparent skip).
 *
 *            Mode 1 (FogAdditive): vanilla `Draw_Fog_Shape` formula. Reads
 *              the SHP atlas pixel; if `<= 0x7F`, additively darkens the
 *              existing alpha value (`new = max(0, old + shape - 127)`)
 *              with the special-case "if old == 127, just store shape".
 *
 *          Bind layout:
 *            t0 — shroud / fog SHP paletted atlas (R8_UINT)
 *            u0 — AlphaUAV (RWTexture2D<unorm float>)
 *            b0 — SpriteBatch ProjMtx (float4x4, 64 bytes)
 *            b1 — ShroudFogEffectParams (AtlasSize + Mode)
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>

#include "effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    enum class ShroudFogMode : uint32_t
    {
        ShroudOverwrite = 0,
        FogAdditive     = 1,
    };


    struct ShroudFogEffectParams
    {
        float    AtlasSize[2];
        uint32_t Mode;
        uint32_t _Pad;
    };


    class ShroudFogEffect : public Effect
    {
    public:
        ShroudFogEffect() = default;
        ~ShroudFogEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Set_Params(GraphicsDevice& device, const ShroudFogEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
