/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2b: per-callsite interception of vanilla Draw_Shape.
 *
 *          Each Patch_Call entry rewrites a single CALL 0x0047C780 inside the
 *          original TS binary so it lands in our `Draw_Shape_Proxy_DX11`
 *          instead. The proxy decides per-call whether to fall through to
 *          vanilla CPU rendering (destination isn't a `GpuSurface`) or to
 *          translate the call into a SpriteDrawCmd and Submit() it to the
 *          per-frame queue.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "draw_shapeext_hooks.h"

#include "debughandler.h"
#include "drawshape.h"
#include "gpu_draw.h"
#include "hooker.h"
#include "surface.h"
#include "tibsun_globals.h"
#include "unit_composite.h"


using namespace Vinifera::Gfx;


/**
 *  Proxy entry point. Mirrors vanilla Draw_Shape's signature exactly so the
 *  /Gr (fastcall) ABI matches what the original binary's CALL instruction
 *  expects — same pattern as the existing animext Draw_Shape_Proxy.
 *
 *  Vinifera-native code wanting the GPU pipeline directly should call
 *  `Vinifera::Gfx::GPU_Draw_Shape` (see gpu_draw.h) which takes extra
 *  parameters this ABI-locked entry can't carry (predator offset, etc.).
 *  This proxy passes `predator_offset = 0` — works for non-cloaked sprites
 *  and degrades to a static cloak (no shimmer) for SHAPE_PREDATOR-flagged
 *  legacy callers.
 *
 *  @author: Vinifera Stage 2b
 */
void Draw_Shape_Proxy_DX11(
    Surface& surface,
    ConvertClass& convert,
    const ShapeSet* shapefile,
    int shapenum,
    const Point2D& point,
    const Rect& window,
    ShapeFlags_Type flags,
    const char* remap,
    int height_offset,
    ZGradientType zgrad,
    int intensity,
    const ShapeSet* z_shapefile,
    int z_shapenum,
    Point2D z_off)
{
    /**
     *  Turreted-unit composite mode capture. Vanilla swaps LogicalSurface
     *  to the 160x160 EightBitSurface scratch and draws every section
     *  (body / turret / barrel — SHP or voxel) at (80, 80)-relative coords;
     *  the final composite reaches the screen via UnitClass::Unit_Blit_Voxel.
     *
     *  Vanilla CPU-rasterizing into that scratch would be wasted work
     *  (Unit_Blit_Voxel is hooked to a no-op for GpuSurface destinations
     *  and the composite is replayed through the GPU pipeline instead),
     *  so we defer the call into the shared pending queue and bail. The
     *  queue is drained from `_Unit_Blit_Voxel` in voxel_blit_hooks.cpp,
     *  which translates each captured buffer point to its real screen
     *  position and re-invokes this proxy with the real tactical surface
     *  — at which point `&surface == EightBitSurface` is false and we
     *  take the normal GPU path below. No recursion.
     */
    if (&surface == EightBitSurface) {
        Composite_Push_Shape(&convert, shapefile, shapenum, point, flags,
                             height_offset, zgrad, intensity,
                             z_shapefile, z_shapenum, z_off);
        return;
    }

    /**
     *  Forward to the Vinifera-native draw entry. The vanilla `remap` arg is
     *  dead code (TS bakes per-house colors into the converter at scenario
     *  init) so we don't pass it through. `predator_offset = 0` produces a
     *  static (un-shimmering) cloak for SHAPE_PREDATOR draws coming from
     *  legacy call sites; Vinifera-native sites with a TechnoClass should
     *  bypass this proxy and call GPU_Draw_Shape with the real offset.
     */
    (void)remap;
    GPU_Draw_Shape(surface, convert, shapefile, shapenum, point, window, flags,
                   height_offset, zgrad, intensity, z_shapefile, z_shapenum, z_off,
                   /*predator_offset*/ 0);
}


/**
 *  Hook installer. Each Patch_Call rewrites a `CALL 0x0047C780` instruction
 *  inside the original binary to land in our proxy. Addresses come from
 *  cross-referencing 0x0047C780 in IDA.
 *
 *  Note: animext_hooks.cpp:775 already installs `Patch_Call(0x00414BA9, ...)`
 *  for shadow rendering and depends on the original being called inside its
 *  proxy. We deliberately leave that callsite alone — adding ourselves
 *  there would either fight that proxy or require chaining. The animation
 *  Draw_Shape will be picked up via a *different* call site once we identify
 *  one, or by integrating the shadow logic into our proxy in a follow-up.
 */
void DrawShape_Hooks()
{
    /**
     *  Initialize SpriteQueue lazily on first frame after video mode is set;
     *  not done here. The hook installer runs before video init.
     */

    /**
     *  Callsite table. Each entry is the address of a single
     *  `CALL 0x0047C780` instruction inside the original TS binary,
     *  discovered via IDA xrefs to Draw_Shape's entry.
     *
     *  We deliberately avoid 0x00414BA9 (already patched by animext for
     *  shadow rendering) — patching it again would conflict. Animations are
     *  still covered via the other AnimClass::Draw_It callsites below.
     */

    Patch_Call(0x00653D6A, &Draw_Shape_Proxy_DX11);  // UnitClass::Draw_It      — vehicle bodies/shadows
    Patch_Call(0x004D2EAC, &Draw_Shape_Proxy_DX11);  // InfantryClass::Draw_It  — primary infantry frames
    Patch_Call(0x004D319A, &Draw_Shape_Proxy_DX11);  // InfantryClass::Draw_It  — secondary frames (firing/dying)
    Patch_Call(0x005A46E2, &Draw_Shape_Proxy_DX11);  // ParticleClass::Draw_It  — smoke/sparks/trails
    Patch_Call(0x00445E8C, &Draw_Shape_Proxy_DX11);  // BulletClass::Draw_It    — projectile sprites
    Patch_Call(0x00445EEF, &Draw_Shape_Proxy_DX11);  // BulletClass::Draw_It    — projectile shadows
    Patch_Call(0x0063FBA2, &Draw_Shape_Proxy_DX11);  // TerrainClass::Draw_It   — trees/cliffs/objects
    Patch_Call(0x004149B4, &Draw_Shape_Proxy_DX11);  // AnimClass::Draw_It      — anims/explosions (1/3)
    Patch_Call(0x00414AB2, &Draw_Shape_Proxy_DX11);  // AnimClass::Draw_It      — anims/explosions (2/3)
    Patch_Call(0x00414C48, &Draw_Shape_Proxy_DX11);  // AnimClass::Draw_It      — anims/explosions (3/3)
    Patch_Call(0x006352ED, &Draw_Shape_Proxy_DX11);  // TechnoClass::Techno_Draw_Object (1/3) — shared low-level draw
    Patch_Call(0x0063538D, &Draw_Shape_Proxy_DX11);  // TechnoClass::Techno_Draw_Object (2/3)
    Patch_Call(0x006354A8, &Draw_Shape_Proxy_DX11);  // TechnoClass::Techno_Draw_Object (3/3)

    Patch_Call(0x00428920, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00428A0A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00428B0D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00454E48, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004555CF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004557AB, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00455B21, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00484DC2, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00485D05, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004861FF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004863FC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004865E9, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EB24, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EE60, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EEDD, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EF2A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049F0E9, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049F21C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004EC8C6, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004F5C9F, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B091, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B38D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B6A4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B9BF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BBCD, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BC72, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BE3A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BEC4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00572622, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00572772, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0058C83D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0058D38C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB545, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB5A5, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB5F9, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB655, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB6B1, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005ADEB1, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B8E57, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B8F14, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B8F97, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B9639, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005BC8C4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005BCCD6, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E375C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E39B7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E3C77, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E3EC3, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E3F1C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E448A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E44E7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E6C70, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E6D8E, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E7045, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E7094, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F1743, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F367A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F371F, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F3777, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F37BF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F52EE, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F533E, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F5527, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005FB5A5, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E4E8, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E562, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E6DC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E758, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E91D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00612510, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x006127AC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00612AB0, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0061718B, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062BE85, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C556, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C5D0, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C6B7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C947, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C9F7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x006376A4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637880, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063796A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637A58, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637B36, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637BCD, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637CAC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063FBEB, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063FD25, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063FD6B, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00653282, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00653E96, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00661B64, &Draw_Shape_Proxy_DX11);

    DEBUG_INFO("DrawShape_Hooks: installed 105 Draw_Shape callsite intercepts.\n");
}
