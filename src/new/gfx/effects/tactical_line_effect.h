/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Depth- and alpha-aware tactical line Effect.
 *
 *          Drives `TacticalLineQueue`. Each quad spans a Bresenham-style line
 *          segment between two endpoints with an interpolation parameter `t`
 *          along its length. The pixel shader:
 *            - optionally samples the SceneRT depth SRV and discards on
 *              z-test fail (TLF_DEPTH_TEST)
 *            - optionally samples the alpha buffer and either discards on
 *              mask test (TLF_ALPHA_TEST_BG / _FG) or modulates the output
 *              (TLF_ALPHA_MOD)
 *            - optionally lerps between two endpoint colors (TLF_GRADIENT)
 *            - always outputs interpolated z via SV_Depth; the bound depth
 *              state controls whether it actually writes.
 *
 *          Bind layout:
 *            t0 — SceneRT depth SRV
 *            t1 — Alpha buffer SRV
 *            b0 — ProjMtx (shared with SpriteBatch)
 *            b1 — TacticalLineEffectParams
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


    enum TacticalLineFlag : uint32_t
    {
        TLF_NONE           = 0,
        TLF_DEPTH_TEST     = 1u << 0,
        TLF_DEPTH_WRITE    = 1u << 1,    // honoured by depth state, also affects shader doc
        TLF_ALPHA_MOD      = 1u << 2,    // c.rgb *= alpha/127  (laser brightening cone)
        TLF_ALPHA_TEST_BG  = 1u << 3,    // discard if alpha != 0 (shroud-only mask)
        TLF_ALPHA_TEST_FG  = 1u << 4,    // discard if alpha == 0 (lit-only mask)
        TLF_GRADIENT       = 1u << 5,
    };


    struct TacticalLineEffectParams
    {
        float    ColorStart[4];
        float    ColorEnd[4];
        float    ZStart;
        float    ZEnd;
        uint32_t Flags;
        uint32_t _Pad;
    };


    class TacticalLineEffect : public Effect
    {
    public:
        TacticalLineEffect() = default;
        ~TacticalLineEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Bind SceneDepth (t0) and AlphaTex (t1) to PS slots.
         */
        void Bind_Sources(GraphicsDevice& device);

        /**
         *  Update per-batch CB.
         */
        void Set_Params(GraphicsDevice& device, const TacticalLineEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
