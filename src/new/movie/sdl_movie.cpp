/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  SDL helpers for movie playback.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sdl_movie.h"


// TODO: develop's Media Foundation movie path renders via SDL_Renderer, which
// the DX11 rendering branch removed. These stubs keep the project building;
// fullscreen MP4/WMV/MPG/AVI playback needs to be reimplemented on top of
// Vinifera::Gfx::Device before it works on this branch.

bool SDL_Movie_Present_Frame(const MovieVideoFrame &, const Rect &)
{
    return false;
}


bool SDL_Movie_Repaint()
{
    return false;
}


void SDL_Movie_Shutdown()
{
}
