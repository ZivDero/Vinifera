/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Vinifera-native GPU draw entry points.
 *
 *          A separate API from vanilla's `Draw_Shape` / `Draw_Tile` —
 *          intentionally NOT ABI-compatible. Vinifera code that wants the GPU
 *          pipeline directly should call these instead of the vanilla
 *          functions, and can pass extra parameters the legacy ABI has no
 *          slot for (e.g. predator warp offset).
 *
 *          The vanilla-ABI proxies (`Draw_Shape_Proxy_DX11`, etc.) call into
 *          these functions with default values for the extras (e.g.
 *          `predator_offset = 0`), so legacy call sites get unchanged
 *          behaviour. New call sites that have richer context (e.g. a
 *          TechnoClass with `Get_Predator_Offset()`) call these directly to
 *          unlock the full effect path.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "point.h"
#include "rect.h"


/**
 *  Forward declarations — these are pulled in via `drawshape.h`'s standard
 *  vanilla-side enums, but we only need a handful, so list them locally so
 *  callers don't have to drag the whole vanilla draw API along.
 */
class Surface;
class ConvertClass;
class ShapeSet;
enum ShapeFlags_Type;
enum ZGradientType;


namespace Vinifera::Gfx
{
    /**
     *  GPU equivalent of vanilla's `Draw_Shape` with extras the old ABI
     *  doesn't carry. The function:
     *
     *    - Routes the draw to the appropriate GPU queue (SpriteQueue for
     *      standard sprites, DistortionQueue when SHAPE_PREDATOR is set).
     *    - Falls through to vanilla `Draw_Shape` when the device isn't ready
     *      or the destination isn't a GpuSurface.
     *    - Does NOT handle the EightBitSurface composite-capture path —
     *      that's a vanilla-call-site quirk and lives in the legacy proxy.
     *
     *  Parameters (a stripped, modernised version of `Draw_Shape`):
     *
     *    `predator_offset` — only meaningful when `flags & SHAPE_PREDATOR`.
     *      Signed horizontal sample displacement for the SceneCopy sample in
     *      the distortion shader; vanilla's `Get_Predator_Offset()` returns
     *      `(unit_id + frame) % 400`. Pass 0 when no per-unit value is
     *      available — the result is a static (un-shimmering) cloak.
     *
     *    `remap` is intentionally not exposed: vanilla treats it as dead
     *      code (the GPU path likewise ignores it) and no Vinifera-native
     *      call site has a use for it.
     */
    void GPU_Draw_Shape(Surface&         surface,
                        ConvertClass&    convert,
                        const ShapeSet*  shapefile,
                        int              shapenum,
                        const Point2D&   point,
                        const Rect&      window,
                        ShapeFlags_Type  flags,
                        int              height_offset,
                        ZGradientType    zgrad,
                        int              intensity,
                        const ShapeSet*  z_shapefile,
                        int              z_shapenum,
                        Point2D          z_off,
                        int              predator_offset);
}
