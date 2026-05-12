/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU binding metadata for legacy Surface instances.
 *
 *          After Stage 7's class split, a registered `Surface*` is either an
 *          `SDLSurface` (CPU-rendered) or a `GpuSurface` (GPU-rendered).
 *          Class identity drives all dispatch. This registry holds purely
 *          informational metadata: which render target the surface routes
 *          to (when applicable), and its logical/screen rectangles for
 *          composite math.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "rect.h"


class Surface;


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class RenderTarget2D;


    /**
     *  Color render target a registered surface routes its GPU output to.
     */
    enum class GpuRenderTarget : unsigned char
    {
        None,         // Surface does not bind a color target itself.
        Backbuffer,   // Final present target.
        Scene,        // Tactical SceneRT — terrain + sprites + alpha + shroud.
        Sidebar,      // SidebarRT — sidebar UI.
    };


    struct GpuSurfaceTargetDesc
    {
        Surface*        SurfacePtr = nullptr;
        Rect            LogicalRect = {};
        Rect            ScreenRect = {};
        GpuRenderTarget OutputTarget = GpuRenderTarget::None;
    };


    class GpuSurfaceTarget
    {
    public:
        GpuSurfaceTarget() = default;
        explicit GpuSurfaceTarget(const GpuSurfaceTargetDesc& desc) : Desc(desc) {}

        Surface*        Get_Surface() const { return Desc.SurfacePtr; }
        const Rect&     Get_Logical_Rect() const { return Desc.LogicalRect; }
        const Rect&     Get_Screen_Rect() const { return Desc.ScreenRect; }
        GpuRenderTarget Get_Output_Target() const { return Desc.OutputTarget; }

        /**
         *  Compute logical → render-target pixel scaling for this surface's
         *  output target.
         */
        bool Logical_To_Render_Target(GraphicsDevice& device, float& xscale, float& yscale) const;

    private:
        GpuSurfaceTargetDesc Desc = {};
    };


    /**
     *  Centralised dispatch from `GpuRenderTarget` enum to the right
     *  `GraphicsDevice::Bind_*` method.
     */
    void Bind_Render_Target(GraphicsDevice& device, GpuRenderTarget target);

    /**
     *  Compute logical → render-target pixel scaling for the given target.
     *  Usable from anywhere that knows the target enum directly without
     *  needing a registry lookup.
     */
    bool Logical_To_Render_Target(GraphicsDevice& device, GpuRenderTarget target, float& xscale, float& yscale);
}
