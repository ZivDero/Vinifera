/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Hooks for GPU WWFont rendering.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once


/**
 *  Install the WWFontClass::Print proxy via `Patch_Jump`. Call once during
 *  extension hook setup (next to `Shroud_Hooks` / `AlphaShape_Hooks`).
 */
void WWFont_Hooks();
