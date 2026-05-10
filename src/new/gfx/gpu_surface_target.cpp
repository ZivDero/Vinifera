/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU binding metadata for legacy Surface instances.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "gpu_surface_target.h"

#include "graphics_device.h"
#include "tibsun_globals.h"


namespace Vinifera::Gfx
{
    bool GpuSurfaceTarget::Logical_To_Render_Target(GraphicsDevice& device, float& xscale, float& yscale) const
    {
        switch (Get_Output_Target()) {
        case GpuRenderTarget::Scene:
        case GpuRenderTarget::Backbuffer:
            xscale = 1.0f;
            yscale = 1.0f;
            if (VideoWidth <= 0 || VideoHeight <= 0) {
                return false;
            }
            xscale = (float)device.Get_Backbuffer_Width() / (float)VideoWidth;
            yscale = (float)device.Get_Backbuffer_Height() / (float)VideoHeight;
            return xscale > 0.0f && yscale > 0.0f;

        case GpuRenderTarget::Sidebar:
            /**
             *  SidebarRT is sized by `Set_Sidebar_Surface_Format` to match
             *  logical sidebar dimensions, so logical coords pass through
             *  unscaled.
             */
            xscale = 1.0f;
            yscale = 1.0f;
            return true;

        case GpuRenderTarget::None:
        default:
            xscale = 1.0f;
            yscale = 1.0f;
            return false;
        }
    }


    bool GpuSurfaceTarget::Bind_Color_Target(GraphicsDevice& device) const
    {
        const GpuRenderTarget target = Get_Output_Target();
        Bind_Render_Target(device, target);
        return target != GpuRenderTarget::None;
    }


    void Bind_Render_Target(GraphicsDevice& device, GpuRenderTarget target)
    {
        switch (target) {
        case GpuRenderTarget::Scene:
            device.Bind_Scene_Target();
            break;
        case GpuRenderTarget::Sidebar:
            device.Bind_Sidebar_Target();
            break;
        case GpuRenderTarget::Backbuffer:
            device.Bind_Backbuffer();
            break;
        case GpuRenderTarget::None:
        default:
            break;
        }
    }
}
