/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains functions for the SDL system.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "graphics_device.h"
#include "rect.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"

class Surface;

bool SDL_Allocate_Surfaces(const Rect& hidden_rect, const Rect& composite_rect, const Rect& tile_rect, const Rect& sidebar_rect, bool hidden_first);
bool SDL_Set_Video_Mode(HWND, int width, int height, int bits_per_pixel);
void SDL_Reset_Video_Mode();
void SDL_Update_Visible_Surface(bool flip_mouse, Surface* surface, Rect* rect);
bool SDL_Create_Main_Window(HINSTANCE instance, int width, int height);
void SDL_Destroy_Main_Window();
bool SDL_Update_Screen(Surface* surface);
bool SDL_Should_Scale();
bool SDL_Change_Display_Mode(int width, int height);

/**
 *  Returns the current X-axis scaling factor.
 *
 *  Now that `VideoWidth/Height` track the backbuffer (CPU surfaces live at
 *  display res), the scale that maps display-space mouse/coordinate inputs
 *  into the tactical scene's pixel space is the GPU pipeline's logical-
 *  resolution ratio (SceneRT dims ÷ backbuffer dims). When the user scales
 *  the tactical render (e.g. dynamic logical-res change), this ratio moves
 *  with it.
 *
 *  @author: ZivDero
 */
inline float SDL_XScale()
{
    if (Vinifera::Gfx::Device != nullptr) {
        const int logical_w    = Vinifera::Gfx::Device->Get_Logical_Width();
        const int backbuffer_w = Vinifera::Gfx::Device->Get_Backbuffer_Width();
        if (logical_w > 0 && backbuffer_w > 0) {
            return static_cast<float>(logical_w) / static_cast<float>(backbuffer_w);
        }
    }
    return static_cast<float>(VideoWidth) / static_cast<float>(SDLWindowWidth);
}

/**
 *  Returns the current Y-axis scaling factor. Mirrors `SDL_XScale`.
 *
 *  @author: ZivDero
 */
inline float SDL_YScale()
{
    if (Vinifera::Gfx::Device != nullptr) {
        const int logical_h    = Vinifera::Gfx::Device->Get_Logical_Height();
        const int backbuffer_h = Vinifera::Gfx::Device->Get_Backbuffer_Height();
        if (logical_h > 0 && backbuffer_h > 0) {
            return static_cast<float>(logical_h) / static_cast<float>(backbuffer_h);
        }
    }
    return static_cast<float>(VideoHeight) / static_cast<float>(SDLWindowHeight);
}

