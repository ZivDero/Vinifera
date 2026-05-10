/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 4 Phase 4.2: GPU shroud / fog alpha-write hooks.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once


/**
 *  Replaces the entries of `CellClass::Draw_Shroud_Or_Fog_Shape` (0x00454E60)
 *  and `CellClass::Draw_Fog_Shape` (0x00455130) with stubs that submit a
 *  ShroudFogQueue command instead of running vanilla's per-pixel CPU blit
 *  into the AlphaBuffer. Flush happens during `SDL_Update_Screen` before
 *  `Flush_Alpha_Lights`.
 */
void Shroud_Hooks();
