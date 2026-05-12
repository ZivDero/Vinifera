/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Function-entry intercept of vanilla `Draw_Shape` (`0x0047C780`).
 *
 *          A single `DEFINE_HOOK` at the function entry routes every call --
 *          internal vanilla self-calls, all 100+ patched CALL sites, and our
 *          own C++ callers -- through a single decision point. The hook
 *          grabs the fastcall args (ECX/EDX + 13 stack dwords), then decides
 *          per-call whether to:
 *
 *            - Capture for the turreted-unit composite pipeline (when the
 *              destination is `EightBitSurface`) and skip vanilla.
 *            - Translate into a SpriteDrawCmd via `GPU_Draw_Shape` when the
 *              destination is a `GpuSurface` and skip vanilla.
 *            - Fall through to vanilla CPU rendering (return 0) for any
 *              other destination -- HiddenSurface / AlternateSurface /
 *              menu / pre-video-init bootstraps.
 *
 *          Skipping vanilla is done by returning the address of its trailing
 *          `retn 34h` (0x0047CC02). At hook entry ESP still points at the
 *          caller's return address and the 13 stack args are intact, so the
 *          `retn 34h` cleans up correctly without needing the function's
 *          prologue to have run.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "draw_shapeext_hooks.h"

#include "drawshape.h"
#include "gpu_draw.h"
#include "gpu_surface.h"
#include "graphics_device.h"
#include "hooker.h"
#include "hooker_macros.h"
#include "syringe.h"
#include "point.h"
#include "rect.h"
#include "surface.h"
#include "tibsun_globals.h"
#include "unit_composite.h"
#include "vinifera_globals.h"


/**
 *  Vanilla `Draw_Shape` uses __fastcall: ECX = Surface*, EDX = ConvertClass*,
 *  remaining 13 args on the stack starting at [esp+4] (the return address is
 *  at [esp+0]). The function ends with `retn 34h` at 0x0047CC02 which pops
 *  those 13 args. We jump there to skip the body when we handle the call.
 *
 *  Hook size 7 covers the first two instructions exactly
 *  (`sub esp, 64h` + `mov eax, [esp+7Ch]`) so the syringe trampoline
 *  replays a clean instruction boundary when we return 0.
 */
DEFINE_HOOK(0x0047C780, _Draw_Shape_Intercept, 7)
{
    GET(Surface*,           surface,       ECX);
    GET(ConvertClass*,      convert,       EDX);
    GET_STACK(ShapeSet*,    shapefile,     0x04);
    GET_STACK(int,          shapenum,      0x08);
    GET_STACK(Point2D*,     point_ptr,     0x0C);
    GET_STACK(Rect*,        cliprect_ptr,  0x10);
    GET_STACK(ShapeFlags_Type, flags,      0x14);
    /* 0x18: const char* remap -- vanilla `convert->RemapTable = remap` is dead
     *       code on our pipeline (per-house colours baked at scenario init), so
     *       we deliberately drop it. */
    GET_STACK(int,          height_offset, 0x1C);
    GET_STACK(ZGradientType, zgrad,        0x20);
    GET_STACK(int,          intensity,     0x24);
    GET_STACK(ShapeSet*,    z_shapefile,   0x28);
    GET_STACK(int,          z_shapenum,    0x2C);
    GET_STACK(int,          z_xoff,        0x30);
    GET_STACK(int,          z_yoff,        0x34);

    const Point2D z_off(z_xoff, z_yoff);

    /**
     *  Turreted-unit composite mode. Vanilla swaps `LogicalSurface` to the
     *  160x160 `EightBitSurface` scratch and draws every section (body /
     *  turret / barrel -- SHP or voxel) at (80,80)-relative coords; the
     *  composite reaches the screen via `UnitClass::Unit_Blit_Voxel`.
     *
     *  CPU-rasterizing into that scratch is wasted work on our pipeline
     *  (Unit_Blit_Voxel is hooked to a no-op for GpuSurface destinations
     *  and the composite is replayed through the GPU pipeline instead),
     *  so we defer into the shared pending queue and skip vanilla. The
     *  queue is drained from `_Unit_Blit_Voxel` in voxel_blit_hooks.cpp,
     *  which re-invokes `Draw_Shape` (-> back into this hook with the
     *  real tactical surface) -- the `surface == EightBitSurface` check
     *  is then false and the GPU path below handles the draw.
     */
    if (surface == EightBitSurface) {
        Composite_Push_Shape(convert, shapefile, shapenum, *point_ptr,
                             flags, height_offset, zgrad, intensity,
                             z_shapefile, z_shapenum, z_off);
        return 0x0047CC02;
    }

    /**
     *  GPU pipeline path. Mirrors `GPU_Draw_Shape`'s own preconditions so
     *  we never enter the function only to have it fall straight back to
     *  vanilla -- destinations that aren't `GpuSurface` (HiddenSurface,
     *  AlternateSurface, menu/cameo hidden buffers) and pre-video-init
     *  bootstraps stay on the vanilla CPU blit by returning 0.
     */
    if (Vinifera::Gfx::Device != nullptr
        && dynamic_cast<GpuSurface*>(surface) != nullptr
        && shapefile != nullptr
        && shapenum >= 0)
    {
        Vinifera::Gfx::GPU_Draw_Shape(*surface, *convert, shapefile, shapenum,
                                      *point_ptr, *cliprect_ptr, flags,
                                      height_offset, zgrad, intensity,
                                      z_shapefile, z_shapenum, z_off,
                                      /*predator_offset*/ 0);
        return 0x0047CC02;
    }

    return 0;
}


/**
 *  Hook installer. The single `DEFINE_HOOK` above does all the work; this
 *  function exists for the `Extension_Hooks()` wiring.
 */
void DrawShape_Hooks()
{
}
