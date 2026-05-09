/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2b: per-callsite interception of vanilla Draw_Shape.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "drawshape.h"

void Draw_Shape_Proxy_DX11(
    Surface& surface,
    ConvertClass& convert,
    const ShapeSet* shapefile,
    int shapenum,
    const Point2D& point,
    const Rect& window,
    ShapeFlags_Type flags = SHAPE_NORMAL,
    const char* remap = nullptr,
    int height_offset = 0,
    ZGradientType zgrad = ZGRAD_GROUND,
    int intensity = 1000,
    const ShapeSet* z_shapefile = nullptr,
    int z_shapenum = 0,
    Point2D z_off = Point2D(0, 0));

void DrawShape_Hooks();
