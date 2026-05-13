/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Bridges `RadarSurface` and ingame radar movies to the D3D11
 *          sidebar pipeline.
 *
 *          `RadarClass::Render_Radar` paints the radar minimap into a
 *          CPU-backed `RadarSurface`; the first hook uploads it into
 *          `RadarTex`. `Movie_Queue_Ingame` is patched to swap the VQA
 *          decoder's destination from `SidebarSurface` (a stub `GpuSurface`)
 *          to a private movie-sized `SDLSurface`; a second hook at the end
 *          of `RadarClass::Play_Movie` uploads each decoded frame into
 *          `MovieTex`. The sidebar render loop composites both textures
 *          onto `SidebarRT` at `Map.RadarRect`.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "radarext_hooks.h"

#include "graphics_device.h"
#include "hooker.h"
#include "hooker_macros.h"
#include "mouse.h"
#include "movie.h"
#include "playmovie.h"
#include "radar.h"
#include "sdlsurface.h"
#include "surface.h"
#include "syringe.h"
#include "tibsun_globals.h"
#include "vqa.h"


/**
 *  Hook span `0x005BC8C9..0x005BC91A` covers the inner
 *  `if (Width > 0 && Height > 0) SidebarSurface->Blit_From(...)` of
 *  `RadarClass::Render_Radar`. SidebarRT is cleared every frame, so the radar
 *  must be redrawable from `RadarTex` regardless of `LastDrawRect`; upload
 *  the whole `RadarSurface` unconditionally (~30 KB) and skip the original
 *  call.
 */
DEFINE_HOOK(0x005BC8C9, _RadarClass_Render_Radar_Upload_To_GPU, 5)
{
    Surface* radar_surf = Map.RadarSurface;
    if (radar_surf != nullptr) {
        const int w = radar_surf->Get_Width();
        const int h = radar_surf->Get_Height();
        if (w > 0 && h > 0) {
            void* pixels = radar_surf->Lock();
            if (pixels != nullptr) {
                Vinifera::Gfx::Device->Upload_Radar_Surface(
                    pixels, radar_surf->Stride(), w, h);
                radar_surf->Unlock();
            }
        }
    }
    return 0x005BC91A;
}


/**
 *  CPU-backed buffer the VQA decoder writes into. Sized to the movie's
 *  native dimensions on each `Movie_Queue_Ingame_Replacement` call.
 */
static SDLSurface* RadarMovieSurface = nullptr;


/**
 *  Replaces vanilla `Movie_Queue_Ingame` (`0x00564630`). Both
 *  `Play_Ingame_Movie` overloads funnel into this. We arrive here with a
 *  freshly-built `VQHandle` whose `DrawSurface` still points at the stub
 *  `SidebarSurface`; swap it for a movie-sized `SDLSurface` and reset
 *  `InitialRect` to the origin so the VQA lock callback decodes at
 *  `(0, 0)` of our surface.
 */
static void __fastcall _Movie_Queue_Ingame_Replacement(VQHandle* handle)
{
    if (handle == nullptr || handle->VQA == nullptr) {
        return;
    }

    const int movie_w = handle->VQA->Get_Width();
    const int movie_h = handle->VQA->Get_Height();
    if (movie_w <= 0 || movie_h <= 0) {
        return;
    }

    if (RadarMovieSurface == nullptr
        || RadarMovieSurface->Get_Width()  != movie_w
        || RadarMovieSurface->Get_Height() != movie_h) {
        delete RadarMovieSurface;
        RadarMovieSurface = new SDLSurface(movie_w, movie_h);
        RadarMovieSurface->Fill(0);
    }

    handle->DrawSurface = RadarMovieSurface;
    // InitialRect = decode position in our surface (origin).
    // StretchRect = sidebar destination at the radar's fixed movie
    // display box (140x110). The GPU scales the movie's native frame
    // into that box.
    static constexpr int kMovieDisplayWidth  = 140;
    static constexpr int kMovieDisplayHeight = 110;
    handle->InitialRect = Rect(0, 0, movie_w, movie_h);
    handle->StretchRect = Rect(Map.RadX + Map.RadOffX,
                               Map.RadY + Map.RadOffY,
                               kMovieDisplayWidth, kMovieDisplayHeight);

    IngameVQ.Add(handle);
}


/**
 *  Hook at the start of `RadarClass::Play_Movie`'s epilogue
 *  (`pop edi / pop esi / pop ebx / add esp, 1Ch`). `Movie_Advance_Frame`
 *  has already run by this point so `RadarMovieSurface` holds the latest
 *  decoded frame. Upload the whole CPU buffer to `MovieTex` for the
 *  sidebar render loop to composite.
 */
DEFINE_HOOK(0x005BCE85, _RadarClass_Play_Movie_Upload_Frame, 6)
{
    if (RadarMovieSurface != nullptr && IngameVQ.Count() > 0) {
        void* pixels = RadarMovieSurface->Lock();
        if (pixels != nullptr) {
            Vinifera::Gfx::Device->Upload_Sidebar_Movie_Surface(
                pixels,
                RadarMovieSurface->Stride(),
                RadarMovieSurface->Get_Width(),
                RadarMovieSurface->Get_Height());
            RadarMovieSurface->Unlock();
        }
    }
    return 0;
}


void Radar_Hooks()
{
    Patch_Jump(0x00564630, &_Movie_Queue_Ingame_Replacement);
}
