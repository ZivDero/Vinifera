/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Hooks for SpotLightClass GPU port.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "spotlight_hooks.h"

#include "graphics_device.h"
#include "hooker.h"
#include "hooker_macros.h"
#include "iomap.h"
#include "ovrlight.h"
#include "rect.h"
#include "rules.h"
#include "scenario.h"
#include "spotlight_queue.h"
#include "tactical.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"


/**
 *  Vanilla maps the per-instance `Radius` animation counter (0..89, ticked
 *  +8 per frame by `AI()` until `>= SPOTLIGHT_MAX_RADIUS` deletes the
 *  instance) to a precomputed surface index 0..73. Indices 0..63 are the
 *  main concentric-ring surfaces; 64..73 are the trailing-fade variants
 *  that vanilla AI can't reach without `Set_Radius()`.
 *
 *  Direct copy of `_index_table` from vanilla `ovrlight.cpp`.
 */
static const char kIndexTable[SpotLightClass::SPOTLIGHT_MAX_RADIUS
                            + SpotLightClass::SPOTLIGHT_EXTRA_SURFACE_COUNT] = {
     5, 10, 15, 20, 25, 30, 35, 40,
    45, 50, 55, 60, 61, 62, 63, 63,
    63, 62, 61, 60, 59, 58, 57, 56,
    55, 54, 53, 52, 51, 50, 49, 48,
    47, 46, 45, 44, 43, 42, 41, 40,
    39, 38, 37, 36, 35, 34, 33, 32,
    31, 30, 29, 28, 27, 26, 25, 24,
    23, 22, 21, 20, 19, 18, 17, 16,
    15, 14, 13, 12, 11, 10,  9,  8,
     7,  6,  5,  4,  3,  2,  1,  0,
    64, 65, 66, 67, 68, 69, 70, 71,
    72, 73,
};


/**
 *  Replacement class. ABI-compatible with vanilla `SpotLightClass`. Used as
 *  the `Patch_Jump` target for `Draw_It`.
 */
class SpotLightClassExt : public SpotLightClass
{
public:
    void _Draw_It();
};


void SpotLightClassExt::_Draw_It()
{
    if (Vinifera::Gfx::Device == nullptr) return;
    if (!Vinifera::Gfx::SpotLightQueue::Get().Is_Initialized()) return;

    /**
     *  Vanilla skips the draw when the world position projects off-screen
     *  or is fully fogged. Mirror that — both checks are cheap and avoid
     *  submitting useless commands.
     */
    Point2D pixel;
    if (!TacticalMap->Coord_To_Pixel(Position, pixel)) return;
    if (Scen->Special.IsFogOfWar && Map.Is_Fogged(Position)) return;

    Vinifera::Gfx::SpotLightDrawCmd cmd{};
    cmd.Center = Point2D(pixel.X + TacticalRect.X, pixel.Y + TacticalRect.Y);

    const int raw_index = kIndexTable[Radius];
    if (raw_index < SpotLightClass::SPOTLIGHT_SURFACE_COUNT) {
        /**
         *  Concentric-ring surfaces (warhead `Combat_Lighting` path).
         *  Vanilla's One_Time builds these as nested filled circles with
         *  outer radius `2 * scaled_index + 1` and a brightness gradient
         *  from 0 at the boundary to `4*scaled_index - 2` at the centre.
         */
        const int scaled = raw_index * Size / SpotLightClass::SPOTLIGHT_SURFACE_COUNT;
        cmd.EffectiveRadius = (float)(2 * scaled + 1);
        cmd.UniformMask     = -1.0f;
    } else {
        /**
         *  Extra surfaces 64..73 (BuildingLightClass path; vanilla calls
         *  `Set_Radius(80..89)` directly). One_Time builds these as a SINGLE
         *  filled circle of CONSTANT colour `128 - 6*k` and radius
         *  `k + 48*SpotlightRadius/358.4`. Tiny uniform disc, not a gradient.
         */
        const int k = raw_index - SpotLightClass::SPOTLIGHT_SURFACE_COUNT;
        const float radius = (float)k
                           + (48.0f * (float)Rule->SpotlightRadius) / 358.4f;
        cmd.EffectiveRadius = radius;
        cmd.UniformMask     = (float)(128 - 6 * k);
    }

    Vinifera::Gfx::SpotLightQueue::Get().Submit(cmd);
}


/**
 *  Main function for patching the hooks.
 */
void SpotLight_Hooks()
{
    /**
     *  Replace vanilla's CPU rasteriser. It locks `LogicalSurface` and
     *  blits a precomputed radial-gradient mask via per-pixel multiplicative
     *  brighten — useless on our GPU pipeline since the surface isn't
     *  CPU-readable.
     */
    Patch_Jump(0x0058E5D0, &SpotLightClassExt::_Draw_It);
}
