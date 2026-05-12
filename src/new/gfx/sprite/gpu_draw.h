/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Vinifera-native GPU draw entry points.
 *
 *          A separate API from vanilla's `Draw_Shape` / `Draw_Tile` —
 *          intentionally NOT ABI-compatible. Accepts extra parameters the
 *          legacy ABI has no slot for (e.g. predator warp offset). Vanilla
 *          `Draw_Shape` intercepts route here with `predator_offset = 0`.
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
    struct SpriteDrawCmd;


    /**
     *  GPU equivalent of vanilla's `Draw_Shape` with extras the old ABI
     *  doesn't carry. The function:
     *
     *    - Routes the draw to the appropriate GPU queue (SpriteQueue for
     *      standard sprites, DistortionQueue when SHAPE_PREDATOR is set).
     *    - Falls through to vanilla `Draw_Shape` when the device isn't ready
     *      or the destination isn't a GpuSurface.
     *
     *  Parameters (a stripped, modernised version of `Draw_Shape`):
     *
     *    `predator_offset` — only meaningful when `flags & SHAPE_PREDATOR`.
     *      Signed horizontal sample displacement for the SceneCopy sample in
     *      the distortion shader; vanilla's `Get_Predator_Offset()` returns
     *      `(unit_id + frame) % 400`. Pass 0 when no per-unit value is
     *      available — the result is a static (un-shimmering) cloak.
     *
     *    `out_cmd` — when non-null, the function builds the SpriteDrawCmd as
     *      usual but does NOT submit it to the SpriteQueue (or DistortionQueue)
     *      — the prepared cmd is written to `*out_cmd` instead. Used by the
     *      unit-scratch composite-replay path so SHP parts of turreted units
     *      can be rendered immediately rather than going through the deferred
     *      queue. Returns whether the cmd was successfully built.
     *
     *    `remap` is intentionally not exposed: vanilla treats it as dead
     *      code (the GPU path likewise ignores it) and no Vinifera-native
     *      call site has a use for it.
     */
    bool GPU_Draw_Shape(Surface&         surface,
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
                        int              predator_offset,
                        SpriteDrawCmd*   out_cmd = nullptr);
}
