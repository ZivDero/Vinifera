/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU binding metadata for legacy Surface instances.
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
     *  Semantic identity of a registered surface. Capabilities and behaviours
     *  (CPU upload? GPU queue routing? Composing?) are derived from the role
     *  by the predicates below — there is intentionally no per-surface
     *  capability bitmask, because every actually-used query maps 1:1 to the
     *  role. Adding behaviour to a role (e.g. "Sidebar can now queue shapes
     *  once SHP routing through `SpriteQueue → SidebarRT` lands") is a single
     *  edit in this header, applying to all surfaces of that role.
     */
    enum class GpuSurfaceRole : unsigned char
    {
        TacticalScene,    // CompositeSurface — GPU-authoritative tactical scene.
        TacticalTile,     // TileSurface     — GPU-authoritative terrain layer.
        Sidebar,          // SidebarSurface  — UI; CPU-uploaded today, GPU later.
        HiddenUI,         // HiddenSurface   — offscreen scratch / menu / OwnerDraw.
        VisibleCompat,    // VisibleSurface  — primary backbuffer compat surface.
    };


    /**
     *  Color render target a registered surface routes its GPU output to.
     *  One target per surface; queue commands originating from a surface
     *  inherit its target and (eventually) bucket by it during flush.
     */
    enum class GpuRenderTarget : unsigned char
    {
        None,         // Surface does not bind a color target itself (HiddenUI scratch).
        Backbuffer,   // Final present target.
        Scene,        // Tactical SceneRT — terrain + sprites + alpha + shroud.
        Sidebar,      // SidebarRT — sidebar UI.
    };


    struct GpuSurfaceTargetDesc
    {
        Surface* SurfacePtr = nullptr;
        GpuSurfaceRole Role = GpuSurfaceRole::HiddenUI;
        Rect LogicalRect = {};
        Rect ScreenRect = {};
        GpuRenderTarget OutputTarget = GpuRenderTarget::None;
    };


    class GpuSurfaceTarget
    {
    public:
        GpuSurfaceTarget() = default;
        explicit GpuSurfaceTarget(const GpuSurfaceTargetDesc& desc) : Desc(desc) {}

        Surface* Get_Surface() const { return Desc.SurfacePtr; }
        GpuSurfaceRole Get_Role() const { return Desc.Role; }
        const Rect& Get_Logical_Rect() const { return Desc.LogicalRect; }
        const Rect& Get_Screen_Rect() const { return Desc.ScreenRect; }
        GpuRenderTarget Get_Output_Target() const { return Desc.OutputTarget; }

        bool Is_Tactical() const
        {
            return Desc.Role == GpuSurfaceRole::TacticalScene
                || Desc.Role == GpuSurfaceRole::TacticalTile;
        }

        /**
         *  CPU upload pipeline (`Upload_Surface` / `Upload_Sidebar_Surface`)
         *  is wired up. TileSurface is the only role with no CPU buffer to
         *  upload — all terrain pixels are produced by the GPU tile queue.
         */
        bool Can_Upload_CPU() const
        {
            return Desc.Role != GpuSurfaceRole::TacticalTile;
        }

        /**
         *  This surface accepts `SpriteQueue` submissions (Draw_Shape proxy).
         *  Sidebar commands bucket onto `SidebarRT` during flush so SHP
         *  cameos land natively on the sidebar without going through the
         *  CPU upload path.
         */
        bool Can_Queue_Shapes() const
        {
            return Desc.Role == GpuSurfaceRole::TacticalScene
                || Desc.Role == GpuSurfaceRole::TacticalTile
                || Desc.Role == GpuSurfaceRole::Sidebar;
        }

        /**
         *  This surface accepts `PrimitiveQueue` submissions (rect / line
         *  primitives from `SDLSurface` virtual draw methods).
         */
        bool Can_Queue_Primitives() const
        {
            return Desc.Role == GpuSurfaceRole::TacticalScene
                || Desc.Role == GpuSurfaceRole::TacticalTile
                || Desc.Role == GpuSurfaceRole::Sidebar;
        }

        /**
         *  This surface owns a render target that gets composited into the
         *  scene (or the backbuffer). TacticalTile shares SceneRT with
         *  TacticalScene and isn't independently composed.
         */
        bool Can_Compose() const
        {
            return Desc.Role == GpuSurfaceRole::TacticalScene
                || Desc.Role == GpuSurfaceRole::Sidebar;
        }

        /**
         *  Compute the scale factor from logical drawing coordinates to the
         *  pixel space of this surface's output render target.
         *
         *      - Scene / Backbuffer: logical (game video) → backbuffer pixels.
         *      - Sidebar: identity (SidebarRT is sized 1:1 to logical sidebar
         *        dims by `Set_Sidebar_Surface_Format`).
         *      - None: identity; returns false.
         */
        bool Logical_To_Render_Target(GraphicsDevice& device, float& xscale, float& yscale) const;
        bool Bind_Color_Target(GraphicsDevice& device) const;

    private:
        GpuSurfaceTargetDesc Desc = {};
    };


    /**
     *  Centralised dispatch from `GpuRenderTarget` enum to the right
     *  `GraphicsDevice::Bind_*` method. Used by the queues during flush to
     *  rebind RT per output-target bucket.
     */
    void Bind_Render_Target(GraphicsDevice& device, GpuRenderTarget target);
}
