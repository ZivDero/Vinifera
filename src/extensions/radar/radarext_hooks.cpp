/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Bridges `RadarSurface` to the D3D11 sidebar pipeline.
 *
 *          `RadarClass::Render_Radar` paints the radar into a CPU-backed
 *          `RadarSurface`; the hook below uploads it into `RadarTex` for the
 *          sidebar render loop to composite onto `SidebarRT`.
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
#include "radar.h"
#include "surface.h"
#include "syringe.h"
#include "tibsun_globals.h"


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


void Radar_Hooks()
{
}
