/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Vanilla 0..2000 brightness → GPU float tint conversion.
 *
 *          Vanilla TS expresses cell + sprite brightness as an integer in the
 *          0..2000 range with 1000 == "full normal" and 2000 == 2x overbright
 *          (used to drive ion-storm tints and damage-flash highs). Our DX11
 *          path treats it as a per-vertex RGB multiplier in float space so
 *          values above 1.0 round-trip through the IL even though the final
 *          backbuffer is RGBA8 (saturating store).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once


namespace Vinifera::Gfx
{
    /**
     *  Map vanilla brightness (0..2000) to a linear RGB multiplier:
     *    0    -> 0.0  (black)
     *    1000 -> 1.0  (neutral / full normal)
     *    2000 -> 2.0  (max overbright)
     *  Out-of-range values clamp to [0, 2].
     */
    inline float Brightness_To_Tint(int brightness_0_2000)
    {
        float t = (float)brightness_0_2000 / 1000.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 2.0f) t = 2.0f;
        return t;
    }
}
