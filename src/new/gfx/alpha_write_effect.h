/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 4 alpha-write Effect.
 *
 *          PS_5_0 shader bound with `RWTexture2D<unorm float>` over the
 *          GraphicsDevice's AlphaUAV. Reads the current alpha pixel and
 *          writes back the vanilla `BrightnessTable[shape][old]` formula
 *          (multiplicative modulation, saturating at 255). Source is the
 *          alpha-shape's R8_UINT atlas; transparent palette index 0 is
 *          discarded so the alpha buffer keeps its existing value where the
 *          shape is empty.
 *
 *          Bind layout:
 *            t0 — alpha-shape paletted atlas (R8_UINT)
 *            u0 — AlphaUAV (RWTexture2D<unorm float>)
 *            b0 — SpriteBatch ProjMtx (float4x4, 64 bytes)
 *            b1 — AlphaWriteEffectParams (AtlasSize)
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


    struct AlphaWriteEffectParams
    {
        float    AtlasSize[2];
        uint32_t _Pad[2];
    };


    class AlphaWriteEffect : public Effect
    {
    public:
        AlphaWriteEffect() = default;
        ~AlphaWriteEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Update the per-batch atlas-size CB (b1). Call before SpriteBatch::End.
         */
        void Set_Params(GraphicsDevice& device, const AlphaWriteEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
