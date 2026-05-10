/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 4 Phase 4.1 Chunk B: neutralize vanilla's CPU AlphaShape blits.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "alphashapeext_hooks.h"

#include "debughandler.h"
#include "hooker.h"


/**
 *  Hook installer. Vanilla's `AlphaShapeClass::Draw_In_Area` and `Draw_All`
 *  are static `__fastcall` methods that walk the global `AlphaShapes` vector
 *  and blit each shape into the CPU `AlphaBuffer` per pixel via
 *  `BrightnessTable[shape][old]`. Now that the GPU pipeline handles alpha
 *  lights through `SpriteQueue::Flush_Alpha_Lights` (writing to AlphaUAV
 *  with the same formula in HLSL), the vanilla path is redundant and would
 *  duplicate work into a CPU buffer that only shroud-cell rendering still
 *  reads.
 *
 *  Replace each function entry with a single `RET` (0xC3) so the call
 *  returns immediately. `__fastcall` puts both args in registers (ECX/EDX),
 *  so no stack adjustment is needed on return.
 */
void AlphaShape_Hooks()
{
    Patch_Byte(0x00412B40, 0xC3);   // AlphaShapeClass::Draw_In_Area(Point2D, Rect)
    Patch_Byte(0x00412F70, 0xC3);   // AlphaShapeClass::Draw_All(Rect)

    DEBUG_INFO("AlphaShape_Hooks: neutralized 2 CPU alpha blit entry points.\n");
}
