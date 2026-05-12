/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Pending-queue API for vanilla's EightBitSurface composite path.
 *
 *          Vanilla TS draws turreted units (Titan, Wolverine, ...) by swapping
 *          LogicalSurface to a 160x160 scratch EightBitSurface, drawing every
 *          section (body / turret / barrel — SHP or voxel) at (80, 80)-relative
 *          coords, then composing the scratch onto the real tactical surface
 *          via UnitClass::Unit_Blit_Voxel.
 *
 *          To route those draws through our GPU pipeline we capture each
 *          Draw_Shape / Draw_Voxel call made under the EightBitSurface and
 *          replay them in submission order from `_Unit_Blit_Voxel`, using
 *          the real screen position the blit was called with.
 *
 *          A SINGLE FIFO covers both kinds — vanilla interleaves shape and
 *          voxel calls within one unit (e.g. Titan: body SHP, optional barrel
 *          voxel, turret SHP, optional barrel voxel above), so parallel
 *          queues would lose layering between the two pipelines.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "drawshape.h"   // ShapeFlags_Type, ZGradientType, Surface, ConvertClass, ShapeSet
#include "matrix3d.h"
#include "point.h"
#include "rect.h"


/**
 *  Forward declarations to avoid pulling heavy headers into every TU that
 *  uses the composite queue.
 */
struct VoxelObject;
namespace Vinifera { namespace Gfx { class GraphicsDevice; } }


/**
 *  Enqueue a Draw_Voxel call captured under composite mode. Args mirror the
 *  inputs to Submit_Voxel_Object so replay can re-invoke directly.
 */
void Composite_Push_Voxel(VoxelObject const& voxeldata,
                          unsigned int       frame,
                          const Matrix3D&    matrix,
                          const Point2D&     buffer_drawpoint,
                          const Rect&        cliprect,
                          int                brightness,
                          float              alpha,
                          int                color_scheme,
                          int                z_adjust,
                          bool               is_predator         = false,
                          int                predator_warp_pixels = 0);


/**
 *  Enqueue a Draw_Shape call captured under composite mode. Args mirror the
 *  vanilla `Draw_Shape` signature so replay can re-invoke with a translated
 *  point and the real tactical window.
 *
 *  `convert` is stored as a raw pointer — ColorSchemes[] converters live for
 *  the scenario duration and the queue is drained within the same frame.
 *
 *  `surface` (always EightBitSurface at capture time) and `remap` (dead in
 *  the proxy — see `(void)remap` there) are deliberately not captured.
 */
void Composite_Push_Shape(ConvertClass*       convert,
                          const ShapeSet*     shapefile,
                          int                 shapenum,
                          const Point2D&      buffer_point,
                          ShapeFlags_Type     flags,
                          int                 height_offset,
                          ZGradientType       zgrad,
                          int                 intensity,
                          const ShapeSet*     z_shapefile,
                          int                 z_shapenum,
                          const Point2D&      z_off);


/**
 *  Drain the queue. For each pending record, translate the buffer drawpoint
 *  to the real screen position (`xyoff + (buffer_dp - (80, 80))` plus a
 *  composite Y bias that matches vanilla's blit pipeline) and re-issue the
 *  draw against `dst_surface` with `rect` as the cliprect.
 *
 *  `shape_convert_override` is the unit's house-aware ConvertClass. Vanilla
 *  draws body/turret SHPs into EightBitSurface using `EightBitDrawer` (an
 *  8-bit passthrough) and applies house colors later during composite. Our
 *  GPU path needs to apply house colors at draw time, so for every shape
 *  record we substitute this converter for the captured one (if non-null).
 *
 *  Clears the queue at the end.
 */
void Composite_Replay(Surface&       dst_surface,
                      Point2D        xyoff,
                      const Rect&    rect,
                      ConvertClass*  shape_convert_override);


/**
 *  Drain the GPU-deferred composite-unit queue. `Composite_Replay` snapshots
 *  each unit's pending records + drawpoint into this queue rather than doing
 *  GPU work immediately — its caller (the vanilla `Unit_Blit_Voxel` hook)
 *  runs during CPU-side `Tactical::Render`, before `Bind_Scene_Target` has
 *  bound + cleared the scene RT and before any of the queue flushes have
 *  written terrain/sprite content. Doing GPU work at that point would either
 *  hit an unbound/stale RT or get overdrawn by the subsequent flushes.
 *
 *  This drain function must be called inside the per-pass loop, AFTER the
 *  tile / sprite / voxel flushes for `ObjectLayer` so the unit composites
 *  layer correctly with terrain depth and other ObjectLayer content.
 */
void Composite_Process_Deferred(Vinifera::Gfx::GraphicsDevice& device);


/**
 *  Returns true if no records are pending.
 */
bool Composite_Is_Empty();
