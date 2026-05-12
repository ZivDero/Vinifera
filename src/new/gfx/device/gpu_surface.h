/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU-authoritative surface class.
 *
 *          Counterpart to `SDLSurface`. Where `SDLSurface` owns a CPU GDI/DIB
 *          pixel buffer and dispatches drawing operations against it (with an
 *          optional GPU-queue fast path), `GpuSurface` has no real CPU pixel
 *          buffer — its pixels live in a GPU render target (`SceneRT` /
 *          `SidebarRT`). Vanilla code that calls `Lock()` receives a dummy
 *          buffer that no one ever reads; vanilla draws via vtable virtuals
 *          either enqueue onto the GPU command queues (for Stage 1, the
 *          virtuals are warn-stubs; queue dispatch is wired up incrementally
 *          as `CompositeSurface` and `SidebarSurface` migrate over).
 *
 *          Inherits from `DSurface` so vanilla code that casts a `Surface*`
 *          to `DSurface*` finds the right vtable slots. The DirectDraw
 *          internals of `DSurface` stay zeroed / unused.
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
     *  Drawing virtuals. Stage 1 keeps these as warn-stubs (return safe
     *  default + one-shot DEBUG_WARNING naming the caller) so any vanilla
     *  code path we hadn't anticipated surfaces itself in the log. Queue
     *  dispatch implementations land incrementally as Composite / Sidebar
     *  migrate to this class.
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
     *  Cross-reference table for vanilla source-tree names (the names
     *  used in the historical TS source dump under `Tiberian-Sun/code`):
     *
     *      Draw_Z_Line             → DSurface::Draw_Line_entry_34   (0x0048EA90)
     *      Brighten_Line           → DSurface::Draw_Line_entry_38   (0x0048C150)
     *      Draw_Gradient_Z_Line    → DSurface::Draw_Line_entry_3C   (0x0048CC00)
     *      Draw_Dashed_Alpha_Line  → DSurface::entry_48             (0x0048F4B0)
     *      Draw_Alpha_Line         → DSurface::entry_4C             (0x0048FB90)
     *      Put_Pixel_Clipped       → XSurface::entry_84             (0x006A7550)
     *      Draw_Lerped_Line        → DSurface::entry_90             (0x0048E4B0)
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
