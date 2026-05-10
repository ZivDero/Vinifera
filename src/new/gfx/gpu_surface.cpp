/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU-authoritative surface class.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "gpu_surface.h"

#include "clipline.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "optionsext.h"
#include "primitive_queue.h"
#include "render_pass.h"
#include "rgb.h"
#include "vinifera_globals.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <intrin.h>
#include <unordered_set>


namespace
{
    using Vinifera::Gfx::EBlend;
    using Vinifera::Gfx::GpuRenderTarget;
    using Vinifera::Gfx::PrimitiveDrawCmd;
    using Vinifera::Gfx::PrimitiveKind;
    using Vinifera::Gfx::PrimitiveQueue;
    using Vinifera::Gfx::RectF;

    constexpr int DASH_PATTERN_LENGTH = 16;


    /**
     *  Gate: GPU dispatch is viable for this surface right now.
     */
    bool Should_Queue(GpuRenderTarget target)
    {
        if (target == GpuRenderTarget::None) return false;
        if (Vinifera::Gfx::Device == nullptr) return false;
        if (OptionsExtension != nullptr && OptionsExtension->LegacyRenderer) return false;
        return PrimitiveQueue::Get().Is_Initialized();
    }


    void Color_From_Hicolor(int color, float alpha, float out[4])
    {
        unsigned red = 0;
        unsigned green = 0;
        unsigned blue = 0;
        DSurface::Build_Locolor_Pixel((unsigned)color, &red, &green, &blue);

        out[0] = (float)std::clamp(red, 0u, 255u) / 255.0f;
        out[1] = (float)std::clamp(green, 0u, 255u) / 255.0f;
        out[2] = (float)std::clamp(blue, 0u, 255u) / 255.0f;
        out[3] = std::clamp(alpha, 0.0f, 1.0f);
    }


    void Color_From_RGB(const RGBClass& color, float alpha, float out[4])
    {
        out[0] = (float)color.Get_Red() / 255.0f;
        out[1] = (float)color.Get_Green() / 255.0f;
        out[2] = (float)color.Get_Blue() / 255.0f;
        out[3] = std::clamp(alpha, 0.0f, 1.0f);
    }


    /**
     *  Submit a SolidRect cmd in the surface's render-target space.
     */
    bool Submit_Solid_Rect(GpuRenderTarget target, Rect const& rect, const float color[4], EBlend blend)
    {
        if (!rect.Is_Valid() || !Should_Queue(target)) {
            return false;
        }

        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, target, xscale, yscale)) {
            return false;
        }

        PrimitiveDrawCmd cmd = {};
        cmd.Kind = PrimitiveKind::SolidRect;
        cmd.Pass = Vinifera::Gfx::Current_Render_Pass();
        cmd.Blend = blend;
        cmd.Rect = RectF {
            (float)rect.X * xscale,
            (float)rect.Y * yscale,
            (float)rect.Width * xscale,
            (float)rect.Height * yscale
        };
        cmd.Color[0] = color[0];
        cmd.Color[1] = color[1];
        cmd.Color[2] = color[2];
        cmd.Color[3] = color[3];
        cmd.OutputTarget = target;

        PrimitiveQueue::Get().Submit(cmd);
        return true;
    }


    /**
     *  Submit a Line cmd after clipping to `cliprect`.
     */
    bool Submit_Line(GpuRenderTarget target, Rect const& cliprect, Point2D startpoint, Point2D endpoint, const float color[4])
    {
        if (!Should_Queue(target) || !cliprect.Is_Valid()) {
            return false;
        }

        if (!Clip_Line(startpoint, endpoint, cliprect)) {
            return false;
        }

        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, target, xscale, yscale)) {
            return false;
        }

        PrimitiveDrawCmd cmd = {};
        cmd.Kind = PrimitiveKind::Line;
        cmd.Pass = Vinifera::Gfx::Current_Render_Pass();
        cmd.Blend = EBlend::Opaque;
        cmd.X0 = ((float)startpoint.X + 0.5f) * xscale;
        cmd.Y0 = ((float)startpoint.Y + 0.5f) * yscale;
        cmd.X1 = ((float)endpoint.X + 0.5f) * xscale;
        cmd.Y1 = ((float)endpoint.Y + 0.5f) * yscale;
        cmd.Thickness = std::max(1.0f, std::max(xscale, yscale));
        cmd.Color[0] = color[0];
        cmd.Color[1] = color[1];
        cmd.Color[2] = color[2];
        cmd.Color[3] = color[3];
        cmd.OutputTarget = target;

        PrimitiveQueue::Get().Submit(cmd);
        return true;
    }


    /**
     *  Clip a fillrect against the surface's logical rect and a caller-supplied
     *  cliprect, returning the clipped destination rect (or empty if nothing
     *  is visible).
     */
    Rect Clip_Fill_Rect(Surface const& surface, Rect const& cliprect, Rect const& fillrect)
    {
        if (!cliprect.Is_Valid() || !fillrect.Is_Valid()) {
            return Rect(0, 0, 0, 0);
        }
        Rect crect = Intersect(cliprect, surface.Get_Rect());
        Rect frect = fillrect.Bias_To(cliprect);
        return Intersect(frect, crect);
    }


    /**
     *  One-shot dedup for `WARN_STUB` calls. See header doc on `GpuSurface`.
     */
    struct WarnKey
    {
        const char* Method;
        void*       ReturnAddr;
        bool operator==(WarnKey const& other) const
        {
            return Method == other.Method && ReturnAddr == other.ReturnAddr;
        }
    };

    struct WarnKeyHash
    {
        std::size_t operator()(WarnKey const& key) const noexcept
        {
            const auto a = reinterpret_cast<std::uintptr_t>(key.Method);
            const auto b = reinterpret_cast<std::uintptr_t>(key.ReturnAddr);
            return std::hash<std::uintptr_t>{}(a * 0x9E3779B97F4A7C15ull ^ b);
        }
    };

    void Warn_Stub_Impl(const char* method, void* return_addr)
    {
        static std::unordered_set<WarnKey, WarnKeyHash> warned;
        const WarnKey key { method, return_addr };
        if (warned.insert(key).second) {
            DEBUG_WARNING("GpuSurface::%s stub called from 0x%p (no GPU path yet).\n",
                method, return_addr);
        }
    }
}


#define GPU_SURFACE_WARN_STUB(name) Warn_Stub_Impl((name), _ReturnAddress())


GpuSurface::GpuSurface(int width, int height, Vinifera::Gfx::GpuRenderTarget output_target) :
    DSurface(),                 // skip DirectDraw init — we don't own a DDraw surface
    OutputTarget(output_target),
    DummyBuffer(nullptr),
    Pitch(0)
{
    Width = width;
    Height = height;
    BytesPerPixel = 2;
    Pitch = width * 2;

    /**
     *  Per-instance scratch buffer. Sized to the full logical surface so any
     *  vanilla Lock+write stays in-bounds. Never read, never uploaded.
     */
    const std::size_t bytes = static_cast<std::size_t>(Pitch) * static_cast<std::size_t>(height);
    if (bytes > 0) {
        DummyBuffer = new unsigned char[bytes];
    }
}


GpuSurface::~GpuSurface()
{
    delete[] DummyBuffer;
    DummyBuffer = nullptr;
}


void* GpuSurface::Lock(Point2D point) const
{
    if (DummyBuffer == nullptr || point.X < 0 || point.Y < 0) {
        return nullptr;
    }

    /**
     *  Match the XSurface lock-count discipline so nested Lock/Unlock pairs
     *  balance correctly. We don't gate on the count for buffer validity —
     *  the dummy is always live — but vanilla code expects `Is_Locked()` to
     *  flip after a Lock.
     */
    XSurface::Lock();

    return DummyBuffer + point.Y * Pitch + point.X * BytesPerPixel;
}


bool GpuSurface::Unlock() const
{
    if (LockCount > 0) {
        XSurface::Unlock();
        return true;
    }
    return false;
}


/**
 *  Drawing virtuals — queue dispatch when possible, warn-stub when there
 *  is no GPU equivalent (yet). All "no GPU path" methods log a one-shot
 *  warning naming the caller so unexpected vanilla code paths surface in
 *  the log for follow-up porting.
 */

bool GpuSurface::Blit_From(Rect const&, Rect const&, Surface const&, Rect const&, Rect const&, bool, bool)
{
    /**
     *  Blit_From needs source-pixel readback (when the source is an
     *  `SDLSurface` with real CPU pixels) or an RT-to-RT copy (when source
     *  is another `GpuSurface`). Neither path is wired yet — they land with
     *  the recovery chunks (radar minimap, etc.).
     */
    GPU_SURFACE_WARN_STUB("Blit_From(rects)");
    return false;
}


bool GpuSurface::Blit_From(Rect const&, Surface const&, Rect const&, bool, bool)
{
    GPU_SURFACE_WARN_STUB("Blit_From(dst,src)");
    return false;
}


bool GpuSurface::Blit_From(Surface const&, bool, bool)
{
    GPU_SURFACE_WARN_STUB("Blit_From(src)");
    return false;
}


bool GpuSurface::Fill_Rect(Rect const& rect, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    Rect clipped = Intersect(rect, Get_Rect());
    return Submit_Solid_Rect(OutputTarget, clipped, rgba, EBlend::Opaque);
}


bool GpuSurface::Fill_Rect(Rect const& cliprect, Rect const& fillrect, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    Rect clipped = Clip_Fill_Rect(*this, cliprect, fillrect);
    return Submit_Solid_Rect(OutputTarget, clipped, rgba, EBlend::Opaque);
}


bool GpuSurface::Fill(int color)
{
    return Fill_Rect(Get_Rect(), color);
}


bool GpuSurface::Fill_Rect_Trans(Rect const& rect, RGBClass const& color, int opacity)
{
    /**
     *  Vanilla's `Fill_Rect_Trans` blends the destination toward `color`
     *  with the given `opacity` (0..255). The closest GPU primitive is a
     *  premultiplied-alpha rect with `color * opacity` written and the
     *  destination keeping `(1 - opacity)` of itself.
     */
    const float alpha = std::clamp((float)opacity / 255.0f, 0.0f, 1.0f);
    float rgba[4];
    Color_From_RGB(color, alpha, rgba);
    /**
     *  Premultiply colour by alpha — `EBlend::Premultiplied` expects the
     *  source already in premultiplied form.
     */
    rgba[0] *= alpha;
    rgba[1] *= alpha;
    rgba[2] *= alpha;
    Rect clipped = Intersect(rect, Get_Rect());
    return Submit_Solid_Rect(OutputTarget, clipped, rgba, EBlend::Premultiplied);
}


bool GpuSurface::Draw_Ellipse(Point2D, int, int, Rect, int)
{
    GPU_SURFACE_WARN_STUB("Draw_Ellipse");
    return false;
}


bool GpuSurface::Put_Pixel(Point2D const& point, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    Rect pixel(point.X, point.Y, 1, 1);
    return Submit_Solid_Rect(OutputTarget, pixel, rgba, EBlend::Opaque);
}


int GpuSurface::Get_Pixel(Point2D const&)
{
    /**
     *  Reading a pixel back from the GPU requires a CPU readback / staging
     *  texture. No vanilla code path in our audit reads pixels from a
     *  tactical surface in performance-sensitive contexts — warn-stub.
     */
    GPU_SURFACE_WARN_STUB("Get_Pixel");
    return 0;
}


bool GpuSurface::Draw_Line(Point2D const& startpoint, Point2D const& endpoint, int color)
{
    return Draw_Line(Get_Rect(), startpoint, endpoint, color);
}


bool GpuSurface::Draw_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    Rect clip = Intersect(cliprect, Get_Rect());
    return Submit_Line(OutputTarget, clip, startpoint, endpoint, rgba);
}


bool GpuSurface::Draw_Line_entry_34(Rect const&, Point2D const&, Point2D const&, int, int, int, bool)
{
    GPU_SURFACE_WARN_STUB("Draw_Line_entry_34");
    return false;
}


bool GpuSurface::Draw_Line_entry_38(Rect const&, Point2D const&, Point2D const&, int, int, int, bool)
{
    GPU_SURFACE_WARN_STUB("Draw_Line_entry_38");
    return false;
}


bool GpuSurface::Draw_Line_entry_3C(Rect const&, Point2D const&, Point2D const&, RGBClass const&, int, int, bool, bool, bool, bool, float)
{
    GPU_SURFACE_WARN_STUB("Draw_Line_entry_3C");
    return false;
}


bool GpuSurface::Plot_Line(Rect const&, Point2D const&, Point2D const&, void (*)(Point2D&))
{
    GPU_SURFACE_WARN_STUB("Plot_Line");
    return false;
}


int GpuSurface::Draw_Dashed_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool pattern[], int offset)
{
    if (pattern == nullptr) {
        return offset;
    }

    Point2D start = startpoint;
    Point2D end = endpoint;
    Rect clip = Get_Rect();
    if (!Clip_Line(start, end, clip)) {
        return offset;
    }

    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);

    const int dx = std::abs(end.X - start.X);
    const int sx = start.X < end.X ? 1 : -1;
    const int dy = -std::abs(end.Y - start.Y);
    const int sy = start.Y < end.Y ? 1 : -1;
    int err = dx + dy;

    Point2D segment_start;
    Point2D segment_end;
    bool segment_active = false;

    Point2D p = start;
    int out_offset = offset;
    for (;;) {
        if (pattern[out_offset & (DASH_PATTERN_LENGTH - 1)]) {
            if (!segment_active) {
                segment_start = p;
                segment_active = true;
            }
            segment_end = p;
        } else if (segment_active) {
            Submit_Line(OutputTarget, clip, segment_start, segment_end, rgba);
            segment_active = false;
        }

        out_offset = (out_offset + 1) & (DASH_PATTERN_LENGTH - 1);

        if (p == end) {
            break;
        }

        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            p.X += sx;
        }
        if (e2 <= dx) {
            err += dx;
            p.Y += sy;
        }
    }

    if (segment_active) {
        Submit_Line(OutputTarget, clip, segment_start, segment_end, rgba);
    }

    return out_offset;
}


int GpuSurface::entry_48(Point2D const&, Point2D const&, int, bool[], int offset, bool)
{
    GPU_SURFACE_WARN_STUB("entry_48");
    return offset;
}


bool GpuSurface::entry_4C(Point2D const&, Point2D const&, int, bool)
{
    GPU_SURFACE_WARN_STUB("entry_4C");
    return false;
}


bool GpuSurface::Draw_Rect(Rect const& rect, int color)
{
    return Draw_Rect(Get_Rect(), rect, color);
}


bool GpuSurface::Draw_Rect(Rect const& cliprect, Rect const& rect, int color)
{
    if (!rect.Is_Valid()) {
        return false;
    }

    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);

    Point2D top_left(rect.X, rect.Y);
    Point2D top_right(rect.X + rect.Width - 1, rect.Y);
    Point2D bottom_left(rect.X, rect.Y + rect.Height - 1);
    Point2D bottom_right(rect.X + rect.Width - 1, rect.Y + rect.Height - 1);

    Submit_Line(OutputTarget, cliprect, top_left, top_right, rgba);
    if (rect.Height > 1) {
        Submit_Line(OutputTarget, cliprect, bottom_left, bottom_right, rgba);
    }
    if (rect.Height > 2) {
        Submit_Line(OutputTarget, cliprect,
            Point2D(rect.X, rect.Y + 1),
            Point2D(rect.X, rect.Y + rect.Height - 2),
            rgba);
        if (rect.Width > 1) {
            Submit_Line(OutputTarget, cliprect,
                Point2D(rect.X + rect.Width - 1, rect.Y + 1),
                Point2D(rect.X + rect.Width - 1, rect.Y + rect.Height - 2),
                rgba);
        }
    }
    return true;
}


bool GpuSurface::entry_84(Point2D const&, int, Rect const&)
{
    GPU_SURFACE_WARN_STUB("entry_84");
    return false;
}


bool GpuSurface::entry_90(Rect&, Point2D&, Point2D&, RGBClass&, RGBClass&, float&, float&)
{
    GPU_SURFACE_WARN_STUB("entry_90");
    return false;
}
