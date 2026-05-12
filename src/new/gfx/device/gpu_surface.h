/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU-authoritative surface class. Pixels live in a GPU render target;
 *          vanilla drawing virtuals enqueue GPU commands instead of writing CPU pixels.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "dsurface.h"
#include "gpu_surface_target.h"


class GpuSurface : public DSurface
{
public:
    GpuSurface(int width, int height, Vinifera::Gfx::GpuRenderTarget output_target);
    ~GpuSurface() override;

    /**
     *  Output render target this surface drives. Inherited by every queue
     *  command submitted via this surface's virtuals.
     */
    Vinifera::Gfx::GpuRenderTarget Output_Target() const { return OutputTarget; }

    /**
     *  Surface contract — Lock returns a dummy buffer that satisfies vanilla
     *  callers expecting a non-null pointer, but the contents are discarded.
     *  Visible pixels come from queue submissions and live in the GPU render
     *  target.
     */
    void* Lock(Point2D point = Point2D(0, 0)) const override;
    bool Unlock() const override;
    bool Can_Lock(int x = 0, int y = 0) const override { return true; }
    int Stride() const override { return Pitch; }
    bool Is_Direct_Draw() const override { return false; }
    bool Can_Blit() const override { return false; }

    /**
     *  Drawing virtuals. Unimplemented paths return a safe default and emit
     *  a one-shot DEBUG_WARNING so unexpected vanilla call sites surface in
     *  the log.
     */
    bool Blit_From(Rect const& dcliprect, Rect const& destrect, Surface const& source, Rect const& scliprect, Rect const& sourcerect, bool trans = false, bool a7 = true) override;
    bool Blit_From(Rect const& destrect, Surface const& source, Rect const& sourcerect, bool trans = false, bool a5 = true) override;
    bool Blit_From(Surface const& source, bool trans = false, bool a3 = true) override;
    bool Fill_Rect(Rect const& rect, int color) override;
    bool Fill_Rect(Rect const& cliprect, Rect const& fillrect, int color) override;
    bool Fill(int color) override;
    bool Fill_Rect_Trans(Rect const& rect, RGBClass const& color, int opacity) override;
    bool Draw_Ellipse(Point2D center, int radius_x, int radius_y, Rect clip, int color) override;
    bool Put_Pixel(Point2D const& point, int color) override;
    bool Draw_Line(Point2D const& startpoint, Point2D const& endpoint, int color) override;
    bool Draw_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int color) override;
    /**
     *  Cross-reference table for vanilla source-tree names:
     *
     *      Draw_Z_Line             → DSurface::Draw_Line_entry_34
     *      Brighten_Line           → DSurface::Draw_Line_entry_38
     *      Draw_Gradient_Z_Line    → DSurface::Draw_Line_entry_3C
     *      Draw_Dashed_Alpha_Line  → DSurface::entry_48
     *      Draw_Alpha_Line         → DSurface::entry_4C
     *      Put_Pixel_Clipped       → XSurface::entry_84
     *      Draw_Lerped_Line        → DSurface::entry_90
     */
    bool Draw_Z_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int color, int z_start, int z_end, bool write_depth = false) override;
    bool Brighten_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int brightness, int z_start, int z_end, bool write_depth = false) override;
    bool Draw_Gradient_Z_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, RGBClass const& color, int z_start, int z_end, bool write_depth, bool gradient, bool alpha_modulate, bool unused_flag, float opacity) override;
    bool Plot_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, void (*drawer_callback)(Point2D&)) override;
    int Draw_Dashed_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool pattern[], int offset) override;
    int Draw_Dashed_Alpha_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool pattern[], int offset, bool alpha_test_bg) override;
    bool Draw_Alpha_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool unused = false) override;
    bool Draw_Rect(Rect const& rect, int color) override;
    bool Draw_Rect(Rect const& cliprect, Rect const& rect, int color) override;
    bool Put_Pixel_Clipped(Point2D const& point, int color, Rect const& rect) override;
    bool Draw_Lerped_Line(Rect& cliprect, Point2D& startpoint, Point2D& endpoint, RGBClass& startcolor, RGBClass& endcolor, float& t, float& step) override;

protected:
    Vinifera::Gfx::GpuRenderTarget OutputTarget;

    /**
     *  Per-instance scratch buffer returned by `Lock()`. Writes go here and
     *  are never read. Sized to match the surface's logical dimensions so any
     *  vanilla code that walks pixels stays in-bounds.
     */
    mutable unsigned char* DummyBuffer;
    int Pitch;

private:
    GpuSurface(GpuSurface const&) = delete;
    GpuSurface& operator=(GpuSurface const&) = delete;
};
