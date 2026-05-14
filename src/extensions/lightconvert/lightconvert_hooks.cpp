/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Refresh the GPU `PaletteCache` whenever vanilla mutates a
 *          `LightConvertClass`'s `Translator` table via `Apply_Tint`. Without
 *          this, ion-storm transitions (ion.cpp calls `Apply_Tint` on every
 *          TileDrawer and ColorScheme converter) leave the GPU sprite/voxel/
 *          font paths sampling a stale snapshot of the pre-ion palette.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "lightconvert_hooks.h"

#include "extension_globals.h"
#include "hooker.h"
#include "hooker_macros.h"
#include "lightconvert.h"
#include "shp_cache.h"
#include "syringe.h"
#include "vinifera_globals.h"


/**
 *  Post-hook on the single exit point of `LightConvertClass::Apply_Tint`
 *  (epilogue at 0x00502FF5). The function has one return path — the
 *  `IntensityLevels < 1` early-bail at 0x00502BA2 also `jl`s straight here —
 *  and `this` is preserved in `ebx` from the prologue (`mov ebx, ecx` at
 *  0x00502B94) right up to the `pop ebx` at 0x00502FF8.
 *
 *  Vanilla mutates `Translator` in place during the body, so by the time we
 *  observe the function exiting the table already holds the new ion or
 *  normal tint. `PaletteCache::Refresh` re-decodes it into the cached
 *  PaletteLUT (reusing the existing Texture2D + PaletteArray layer; no GPU
 *  texture churn) so subsequent sprite/voxel/font draws sample the fresh
 *  palette.
 *
 *  Stolen-byte count: `pop edi; pop esi; pop ebp` = 3 bytes; we round up to
 *  the Syringe minimum patch size of 5.
 */
DEFINE_HOOK(0x00502FF5, _LightConvertClass_Apply_Tint_Refresh_Cache, 7)
{
    GET_REGISTER_STATIC(LightConvertClass*, that, ebx);
    if (that != nullptr) {
        Vinifera::Gfx::PaletteCache::Get().Refresh(that);
    }
    return 0;
}
