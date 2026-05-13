/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  OwnerDraw / dialog-widget surface redirection.
 *
 *          `OwnerDrawAlternate` is the window-res scratch surface that
 *          replaces vanilla's `AlternateSurface` for OwnerDraw call sites
 *          (the latter is now at logical res for movies / menus). The
 *          `Patch_Dword` batch in `OwnerDraw_Hooks` rewrites the 32-bit
 *          immediate operands of `mov reg, ds:[AlternateSurface]`
 *          instructions in ownrdraw.cpp / dialog code to read from this
 *          global instead.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

class SDLSurface;

extern SDLSurface* OwnerDrawAlternate;
extern SDLSurface* OwnerDrawVisible;

/**
 *  Window-pixel dimensions, kept in sync with the backbuffer / SDL
 *  window. OwnerDraw position math (DrawDialogBack, MoveDialog,
 *  Center_Window_Within_Window) reads vanilla's `VideoWidth/Height` for
 *  dialog centering; those advertise the logical resolution now, so the
 *  relevant call sites are Patch_Dword'd to read these globals instead.
 */
extern int OwnerDrawWidth;
extern int OwnerDrawHeight;

void OwnerDraw_Hooks();
