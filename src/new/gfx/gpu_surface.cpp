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
#include "tactical_line_queue.h"
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
     *  Lerp two RGBClass entries by `t` in [0, 1]. Mirrors `RGBClass::Lerp`.
     */
    void Lerp_RGB(const RGBClass& a, const RGBClass& b, float t, float out[4])
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const float r = (float)a.Get_Red()   + ((float)b.Get_Red()   - (float)a.Get_Red())   * t;
        const float g = (float)a.Get_Green() + ((float)b.Get_Green() - (float)a.Get_Green()) * t;
        const float bl = (float)a.Get_Blue() + ((float)b.Get_Blue()  - (float)a.Get_Blue())  * t;
        out[0] = std::clamp(r / 255.0f, 0.0f, 1.0f);
        out[1] = std::clamp(g / 255.0f, 0.0f, 1.0f);
        out[2] = std::clamp(bl / 255.0f, 0.0f, 1.0f);
        out[3] = 1.0f;
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


    using Vinifera::Gfx::TacticalLineCmd;
    using Vinifera::Gfx::TacticalLineQueue;
    using Vinifera::Gfx::TacticalLineFlag;
    using Vinifera::Gfx::TLF_NONE;
    using Vinifera::Gfx::TLF_DEPTH_TEST;
    using Vinifera::Gfx::TLF_DEPTH_WRITE;
    using Vinifera::Gfx::TLF_ALPHA_MOD;
    using Vinifera::Gfx::TLF_ALPHA_TEST_BG;
    using Vinifera::Gfx::TLF_ALPHA_TEST_FG;
    using Vinifera::Gfx::TLF_GRADIENT;
    using Vinifera::Gfx::EDepthStencil;


    /**
     *  Match the screen-Y → SceneRT-depth mapping used by `Draw_Shape` /
     *  tile rendering: 1/16000 is the project-wide pixel-to-depth scale
     *  (see [draw_shapeext_hooks.cpp::Depth_From_Screen_Y], [tile_queue.cpp]
     *  setting `ZDataDepthScale`). Tactical lines must land in the same
     *  absolute depth range as the sprites and tiles they get z-tested
     *  against; without the screen-Y baseline the line's depth would
     *  always be near-0, defeating the per-pixel depth discard.
     */
    constexpr float kInvDepthRange = 1.0f / 16000.0f;

    /**
     *  Small bias keeping tactical lines a hair *nearer* than the matching
     *  flat-terrain tile at the same screen Y. Without it, line.depth and
     *  tile.depth land at exactly the same value (`1 - y/16000` from both
     *  sides) and the line races rounding when the depth-state is
     *  `LessEqual` — half the line ends up discarded on contact with the
     *  ground. ~16 pixels worth of bias keeps the line on top while still
     *  leaving plenty of room for taller sprite z-shape pixels to occlude
     *  it.
     */
    constexpr float kTacticalLineDepthBias = 16.0f * kInvDepthRange;

    inline float Tactical_Line_Depth(float logical_y, int vanilla_z)
    {
        float dz = 1.0f - logical_y * kInvDepthRange
                        + (float)vanilla_z * kInvDepthRange
                        - kTacticalLineDepthBias;
        if (dz < 0.001f) dz = 0.001f;
        if (dz > 0.999f) dz = 0.999f;
        return dz;
    }


    /**
     *  Submit one `TacticalLineCmd` covering a line segment with the given
     *  flag bitmask and blend/depth state. Caller-supplied `start` / `end`
     *  must already be biased to surface-absolute coords (matching the
     *  vanilla `Bias_To` step).
     *
     *  `vanilla_z_start` / `vanilla_z_end` are the raw integer z values
     *  that vanilla's CPU rasterizer would have compared against ZBuffer
     *  bytes (e.g., `14 - Z_Lepton_To_Pixel(coord.Z)` from
     *  `Tactical::Draw_3D_Line`). The function bakes them into the same
     *  GPU depth scale that `Depth_From_Screen_Y` produces for sprites /
     *  tiles — that way `if (line_z > scene_z) discard` in the pixel
     *  shader compares apples to apples.
     */
    bool Submit_Tactical_Line(GpuRenderTarget target,
                              Point2D start, Point2D end,
                              const float color_start[4], const float color_end[4],
                              int vanilla_z_start, int vanilla_z_end,
                              uint32_t flags,
                              EBlend blend, EDepthStencil depth)
    {
        if (target == GpuRenderTarget::None) return false;
        if (Vinifera::Gfx::Device == nullptr) return false;
        if (OptionsExtension != nullptr && OptionsExtension->LegacyRenderer) return false;
        if (!TacticalLineQueue::Get().Is_Initialized()) return false;

        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, target, xscale, yscale)) {
            return false;
        }

        TacticalLineCmd cmd = {};
        cmd.X0 = ((float)start.X + 0.5f) * xscale;
        cmd.Y0 = ((float)start.Y + 0.5f) * yscale;
        cmd.X1 = ((float)end.X + 0.5f) * xscale;
        cmd.Y1 = ((float)end.Y + 0.5f) * yscale;
        cmd.Thickness = std::max(1.0f, std::max(xscale, yscale));
        cmd.ZStart = Tactical_Line_Depth((float)start.Y, vanilla_z_start);
        cmd.ZEnd   = Tactical_Line_Depth((float)end.Y,   vanilla_z_end);
        memcpy(cmd.ColorStart, color_start, sizeof(cmd.ColorStart));
        memcpy(cmd.ColorEnd,   color_end,   sizeof(cmd.ColorEnd));
        cmd.Flags = flags;
        cmd.Blend = blend;
        cmd.Depth = depth;
        cmd.Pass = Vinifera::Gfx::Current_Render_Pass();
        cmd.OutputTarget = target;

        TacticalLineQueue::Get().Submit(cmd);
        return true;
    }


    /**
     *  Emit a single 1×1 solid-color quad at (x, y) iff the point lies inside
     *  `cliprect`. Used by the Bresenham-style ports (Plot_Line, Draw_Ellipse,
     *  Draw_Lerped_Line, Put_Pixel_Clipped) so each "pixel" the vanilla
     *  algorithm would have written becomes one PrimitiveQueue quad on the
     *  GPU.
     */
    void Emit_Pixel(GpuRenderTarget target, int x, int y, Rect const& cliprect, const float color[4])
    {
        if (!cliprect.Is_Point_Within(Point2D(x, y))) {
            return;
        }
        Submit_Solid_Rect(target, Rect(x, y, 1, 1), color, EBlend::Opaque);
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


/**
 *  Midpoint Bresenham ellipse (`XSurface::Draw_Ellipse` IDA port).
 *
 *  Walks the four-quadrant ellipse outline in two phases (low-slope and
 *  high-slope), emitting one 1×1 quad per perimeter pixel. Algorithm mirrors
 *  the vanilla binary at `0x006A7910`; we drop the surface-Lock pointer math
 *  and the cardinal-axis `Put_Pixel` calls go through this surface's
 *  vtable (which re-enters GpuSurface::Put_Pixel below).
 */
bool GpuSurface::Draw_Ellipse(Point2D pt, int radius_x, int radius_y, Rect clip, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);

    /* Four cardinal-axis points first. */
    Put_Pixel(Point2D(pt.X, pt.Y + radius_y), color);
    Put_Pixel(Point2D(pt.X, pt.Y - radius_y), color);
    Put_Pixel(Point2D(pt.X + radius_x, pt.Y), color);
    Put_Pixel(Point2D(pt.X - radius_x, pt.Y), color);

    const int a_sq = radius_x * radius_x;
    const int b_sq = radius_y * radius_y;

    /* Phase 1: low slope (|dy/dx| < 1), start at top, walk outward in X. */
    int y = radius_y;
    int x = 0;
    int var1 = 0;
    int var2 = 2 * radius_y * a_sq;
    int delta = (a_sq / 4) - radius_y * a_sq;

    while (true) {
        delta += b_sq + var1;
        if (delta >= 0) {
            var2 -= 2 * a_sq;
            delta -= var2;
            --y;
        }
        var1 += 2 * b_sq;
        ++x;
        if (var1 >= var2) break;

        Emit_Pixel(OutputTarget, pt.X + x, pt.Y + y, clip, rgba);
        Emit_Pixel(OutputTarget, pt.X - x, pt.Y - y, clip, rgba);
        Emit_Pixel(OutputTarget, pt.X + x, pt.Y - y, clip, rgba);
        Emit_Pixel(OutputTarget, pt.X - x, pt.Y + y, clip, rgba);
    }

    /* Phase 2: high slope (|dy/dx| > 1), start at right, walk outward in Y. */
    int x2 = radius_x;
    int y2 = 0;
    int var1b = 2 * radius_x * b_sq;
    int var2b = 0;
    int deltab = (b_sq / 4) - radius_x * b_sq;

    while (true) {
        deltab += a_sq + var2b;
        if (deltab >= 0) {
            var1b -= 2 * b_sq;
            deltab -= var1b;
            --x2;
        }
        var2b += 2 * a_sq;
        ++y2;
        if (var2b > var1b) break;

        Emit_Pixel(OutputTarget, pt.X + x2, pt.Y + y2, clip, rgba);
        Emit_Pixel(OutputTarget, pt.X - x2, pt.Y - y2, clip, rgba);
        Emit_Pixel(OutputTarget, pt.X + x2, pt.Y - y2, clip, rgba);
        Emit_Pixel(OutputTarget, pt.X - x2, pt.Y + y2, clip, rgba);
    }
    return true;
}


bool GpuSurface::Put_Pixel(Point2D const& point, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    Rect pixel(point.X, point.Y, 1, 1);
    return Submit_Solid_Rect(OutputTarget, pixel, rgba, EBlend::Opaque);
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
    /**
     *  Vanilla `XSurface::Draw_Line(cliprect, ...)` treats start / end as
     *  cliprect-relative; bias them into surface-absolute coords before
     *  submitting (matches the old `Intersected_Clip_Origin` step the
     *  pre-GpuSurface SDLSurface override did).
     */
    return Submit_Line(OutputTarget, clip,
        Bias_To(startpoint, clip),
        Bias_To(endpoint, clip),
        rgba);
}


/**
 *  Depth-tested, alpha-modulated constant-color line (`DSurface::Draw_Z_Line`
 *  IDA port). `z_start` / `z_end` are z values at the line endpoints
 *  (interpolated by the shader along the line). `write_depth` enables
 *  depth-write — vanilla conditionally writes the interpolated z into the
 *  depth buffer on each visible pixel. Maps to one TacticalLineCmd with
 *  `TLF_DEPTH_TEST | TLF_ALPHA_MOD` plus optional `TLF_DEPTH_WRITE`.
 *
 *  Vanilla source-tree name: `DSurface::Draw_Line_entry_34` (`0x0048EA90`).
 */
bool GpuSurface::Draw_Z_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int color, int z_start, int z_end, bool write_depth)
{
    Rect clip = Intersect(cliprect, Get_Rect());
    Point2D s = Bias_To(startpoint, clip);
    Point2D e = Bias_To(endpoint,   clip);

    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);

    uint32_t flags = TLF_DEPTH_TEST | TLF_ALPHA_MOD;
    if (write_depth) flags |= TLF_DEPTH_WRITE;

    const EDepthStencil depth_state = write_depth ? EDepthStencil::WriteLessEqual
                                                  : EDepthStencil::TestLessEqual_NoWrite;

    return Submit_Tactical_Line(OutputTarget, s, e, rgba, rgba, z_start, z_end,
                                flags, EBlend::Premultiplied, depth_state);
}


/**
 *  Depth-tested "brighten existing scene" line (`DSurface::Brighten_Line`
 *  IDA port). Vanilla reads the existing surface pixel and adds
 *  `(brightness * channel) >> 8` per channel (saturated) — there is no
 *  color input, the line tints whatever is already there. On GPU a true
 *  read-modify-write of the same RT we're drawing into needs an
 *  intermediate copy. The cheap approximation: emit a neutral additive
 *  tint scaled by `brightness/256`, which writes a uniform brighten
 *  regardless of the underlying pixel color.
 *
 *  Vanilla source-tree name: `DSurface::Draw_Line_entry_38` (`0x0048C150`).
 */
bool GpuSurface::Brighten_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int brightness, int z_start, int z_end, bool write_depth)
{
    Rect clip = Intersect(cliprect, Get_Rect());
    Point2D s = Bias_To(startpoint, clip);
    Point2D e = Bias_To(endpoint,   clip);

    const float factor = std::clamp((float)brightness / 256.0f, 0.0f, 1.0f);
    float rgba[4] = { factor, factor, factor, 1.0f };

    uint32_t flags = TLF_DEPTH_TEST;
    if (write_depth) flags |= TLF_DEPTH_WRITE;

    const EDepthStencil depth_state = write_depth ? EDepthStencil::WriteLessEqual
                                                  : EDepthStencil::TestLessEqual_NoWrite;

    return Submit_Tactical_Line(OutputTarget, s, e, rgba, rgba, z_start, z_end,
                                flags, EBlend::Additive, depth_state);
}


/**
 *  Depth-tested gradient laser line (`DSurface::Draw_Gradient_Z_Line` IDA
 *  port). `color` is the line tint, `opacity` is in [0, 1], the bools
 *  control depth-write / alpha-mod / gradient modes. Maps to a gradient
 *  TacticalLine with additive blend (the visual approximation of vanilla's
 *  "scene + tinted line" effect).
 *
 *  Vanilla source-tree name: `DSurface::Draw_Line_entry_3C` (`0x0048CC00`).
 */
bool GpuSurface::Draw_Gradient_Z_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, RGBClass const& color, int z_start, int z_end, bool write_depth, bool gradient, bool alpha_modulate, bool unused_flag, float opacity)
{
    Rect clip = Intersect(cliprect, Get_Rect());
    Point2D s = Bias_To(startpoint, clip);
    Point2D e = Bias_To(endpoint,   clip);

    const float op = std::clamp(opacity, 0.0f, 1.0f);
    float rgba_start[4];
    float rgba_end[4];
    Color_From_RGB(color, op, rgba_start);
    rgba_start[0] *= op;
    rgba_start[1] *= op;
    rgba_start[2] *= op;
    rgba_end[0] = 0.0f;
    rgba_end[1] = 0.0f;
    rgba_end[2] = 0.0f;
    rgba_end[3] = 0.0f;

    /**
     *  Vanilla layout has four bool toggles; the first three drive depth-
     *  write, gradient enable, and alpha-buffer modulate respectively. The
     *  fourth is unused in observed call sites; retain it in the signature
     *  for vanilla parity. If a specific caller turns out to depend on a
     *  different mapping, this is where to adjust.
     */
    uint32_t flags = TLF_DEPTH_TEST;
    if (write_depth)    flags |= TLF_DEPTH_WRITE;
    if (gradient)       flags |= TLF_GRADIENT;
    if (alpha_modulate) flags |= TLF_ALPHA_MOD;
    (void)unused_flag;

    const EDepthStencil depth_state = write_depth ? EDepthStencil::WriteLessEqual
                                                  : EDepthStencil::TestLessEqual_NoWrite;

    return Submit_Tactical_Line(OutputTarget, s, e, rgba_start, rgba_end, z_start, z_end,
                                flags, EBlend::Additive, depth_state);
}


/**
 *  Bresenham line that invokes a caller-supplied callback once per pixel
 *  (`XSurface::Plot_Line` IDA port, `0x006A7150`). The callback is user
 *  code — it may itself call back into the surface via Put_Pixel or any
 *  other method, which on `GpuSurface` re-routes through GPU primitive
 *  submissions. We do not pre-Lock the surface; vanilla's Unlock at the
 *  end of the routine is a no-op for us.
 */
bool GpuSurface::Plot_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, void (*drawer_callback)(Point2D&))
{
    if (drawer_callback == nullptr) {
        return false;
    }

    Rect clip = Intersect(cliprect, Get_Rect());
    Point2D start = Bias_To(startpoint, clip);
    Point2D end   = Bias_To(endpoint,   clip);

    if (!Clip_Line(start, end, clip)) {
        return false;
    }

    if (start.X > end.X) {
        std::swap(start, end);
    }

    if (start.Y == end.Y) {
        /* Horizontal. */
        const int dx = end.X - start.X;
        for (int i = 0; i <= dx; ++i) {
            Point2D p(start.X + i, end.Y);
            drawer_callback(p);
        }
    } else if (start.X == end.X) {
        /* Vertical. */
        int y_lo = std::min(start.Y, end.Y);
        int dy = std::abs(start.Y - end.Y);
        for (int i = 0; i <= dy; ++i) {
            Point2D p(start.X, y_lo + i);
            drawer_callback(p);
        }
    } else {
        /* Diagonal Bresenham — two-octant split (low / high slope). */
        const int dx = end.X - start.X;
        int dy = end.Y - start.Y;
        const int sy = (dy < 0) ? -1 : 1;
        dy = std::abs(dy);
        const int dx2 = 2 * dx;
        const int dy2 = 2 * dy;

        if (dx > dy) {
            /* Low slope. */
            int delta = dy2 - dx;
            int yy = start.Y;
            for (int x = start.X; x <= end.X; ++x) {
                Point2D p(x, yy);
                drawer_callback(p);
                if (delta > 0) {
                    delta -= dx2;
                    yy += sy;
                }
                delta += dy2;
            }
        } else {
            /* High slope. */
            int delta = dx2 - dy;
            int xx = start.X;
            int yy = start.Y;
            for (int i = 0; i <= dy; ++i) {
                Point2D p(xx, yy);
                drawer_callback(p);
                if (delta > 0) {
                    delta -= dy2;
                    ++xx;
                }
                delta += dx2;
                yy += sy;
            }
        }
    }

    return true;
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


/**
 *  Alpha-mask dashed line (`DSurface::Draw_Dashed_Alpha_Line` source
 *  port). Walks Bresenham, advances the 16-entry dash pattern per pixel;
 *  for each "on" pixel emits a 1×1 TacticalLineCmd with the chosen
 *  alpha-mask flag (TLF_ALPHA_TEST_BG when `alpha_test_bg` true → write
 *  only where alpha == 0, the shroud-only mask; TLF_ALPHA_TEST_FG when
 *  false → write only where alpha != 0, the lit-only mask). Returns the
 *  advanced pattern index.
 *
 *  Vanilla source-tree name: `DSurface::entry_48` (`0x0048F4B0`).
 */
int GpuSurface::Draw_Dashed_Alpha_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool pattern[], int pattern_index, bool alpha_test_bg)
{
    if (pattern == nullptr) {
        return pattern_index;
    }

    Rect clip = Get_Rect();
    Point2D start = startpoint;
    Point2D end = endpoint;
    int pattern_step = 1;

    if (start.X > end.X) {
        std::swap(start, end);
        const int dx = std::abs(start.X - end.X);
        const int dy = std::abs(start.Y - end.Y) + 1;
        const int len = std::max(dx, dy);
        pattern_index = (pattern_index + len) % 16;
        pattern_step = -1;
    }

    if (!Clip_Line(start, end, clip)) {
        return pattern_index;
    }

    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    const uint32_t flags = alpha_test_bg ? TLF_ALPHA_TEST_BG : TLF_ALPHA_TEST_FG;

    /**
     *  Per-pixel walk so we can advance pattern_index correctly. Each "on"
     *  pixel becomes one 1×1 line. The alpha mask is enforced per-pixel by
     *  the shader sampling AlphaTex.
     */
    auto emit_if_on = [&](int x, int y) {
        if (pattern[pattern_index & 15]) {
            Submit_Tactical_Line(OutputTarget, Point2D(x, y), Point2D(x, y),
                                 rgba, rgba, 0, 0,
                                 flags, EBlend::Opaque, EDepthStencil::None);
        }
        pattern_index = (pattern_index + pattern_step) & 15;
    };

    if (start.Y == end.Y) {
        for (int x = start.X; x <= end.X; ++x) emit_if_on(x, start.Y);
    } else if (start.X == end.X) {
        const int y_lo = std::min(start.Y, end.Y);
        const int dy = std::abs(end.Y - start.Y);
        for (int i = 0; i <= dy; ++i) emit_if_on(start.X, y_lo + i);
    } else {
        const int dx = end.X - start.X;
        int dy = end.Y - start.Y;
        const int sy = (dy < 0) ? -1 : 1;
        dy = std::abs(dy);
        const int dx2 = 2 * dx;
        const int dy2 = 2 * dy;

        if (dx > dy) {
            int delta = dy2 - dx;
            int yy = start.Y;
            for (int i = 0; i <= dx; ++i) {
                emit_if_on(start.X + i, yy);
                if (delta > 0) {
                    delta -= dx2;
                    yy += sy;
                }
                delta += dy2;
            }
        } else {
            int delta = dx2 - dy;
            int xx = start.X;
            int yy = start.Y;
            for (int i = 0; i <= dy; ++i) {
                emit_if_on(xx, yy);
                if (delta > 0) {
                    delta -= dy2;
                    ++xx;
                }
                delta += dx2;
                yy += sy;
            }
        }
    }

    return pattern_index;
}


/**
 *  Alpha-mask line (`DSurface::Draw_Alpha_Line` source port). Like the
 *  dashed variant but no dash pattern — every pixel is "on", just gated by
 *  the alpha mask. Maps to a single TacticalLineCmd with `TLF_ALPHA_TEST_FG`.
 *
 *  Vanilla source-tree name: `DSurface::entry_4C` (`0x0048FB90`).
 */
bool GpuSurface::Draw_Alpha_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool /*unused*/)
{
    Rect clip = Get_Rect();
    Point2D s = Bias_To(startpoint, clip);
    Point2D e = Bias_To(endpoint,   clip);

    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);

    return Submit_Tactical_Line(OutputTarget, s, e, rgba, rgba, 0, 0,
                                TLF_ALPHA_TEST_FG, EBlend::Opaque, EDepthStencil::None);
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

    /**
     *  `rect` is cliprect-relative (matches vanilla `XSurface::Draw_Rect`);
     *  bias it into surface-absolute coords by adding the intersected clip
     *  origin. Same step the pre-GpuSurface SDLSurface override did via
     *  `rect.Bias_To(Intersect(cliprect, Get_Rect()))`.
     */
    Rect clip = Intersect(cliprect, Get_Rect());
    Rect biased = rect.Bias_To(clip);

    Point2D top_left(biased.X, biased.Y);
    Point2D top_right(biased.X + biased.Width - 1, biased.Y);
    Point2D bottom_left(biased.X, biased.Y + biased.Height - 1);
    Point2D bottom_right(biased.X + biased.Width - 1, biased.Y + biased.Height - 1);

    Submit_Line(OutputTarget, clip, top_left, top_right, rgba);
    if (biased.Height > 1) {
        Submit_Line(OutputTarget, clip, bottom_left, bottom_right, rgba);
    }
    if (biased.Height > 2) {
        Submit_Line(OutputTarget, clip,
            Point2D(biased.X, biased.Y + 1),
            Point2D(biased.X, biased.Y + biased.Height - 2),
            rgba);
        if (biased.Width > 1) {
            Submit_Line(OutputTarget, clip,
                Point2D(biased.X + biased.Width - 1, biased.Y + 1),
                Point2D(biased.X + biased.Width - 1, biased.Y + biased.Height - 2),
                rgba);
        }
    }
    return true;
}


/**
 *  Put a single pixel iff the point lies inside `rect`. Source-faithful port
 *  of `XSurface::Put_Pixel_Clipped` — vanilla just gates Put_Pixel on
 *  Is_Point_Within.
 *
 *  Vanilla source-tree name: `XSurface::entry_84` (`0x006A7550`).
 */
bool GpuSurface::Put_Pixel_Clipped(Point2D const& point, int color, Rect const& rect)
{
    if (!rect.Is_Point_Within(point)) {
        return false;
    }
    return Put_Pixel(point, color);
}


/**
 *  Gradient line with ping-pong color interpolation
 *  (`DSurface::Draw_Lerped_Line` source port, IDA-verified). Walks
 *  Bresenham, computes a per-pixel lerp(startColor, endColor, t), and
 *  emits each pixel as a 1×1 quad. The `t` / `step` are by-reference so a
 *  caller can chain multiple segments through the same gradient state.
 *
 *  Vanilla source-tree name: `DSurface::entry_90` (`0x0048E4B0`).
 */
bool GpuSurface::Draw_Lerped_Line(Rect& cliprect, Point2D& startpoint, Point2D& endpoint, RGBClass& startcolor, RGBClass& endcolor, float& t, float& step)
{
    Rect clip = Intersect(cliprect, Get_Rect());
    Point2D start = Bias_To(startpoint, clip);
    Point2D end   = Bias_To(endpoint,   clip);

    if (!Clip_Line(start, end, clip)) {
        return false;
    }
    if (start.X > end.X) {
        std::swap(start, end);
    }

    auto advance_t = [&]() {
        t += step;
        if (t < 0.0f) {
            if (step < 0.0f) t = 0.0f;
            step = -step;
        } else if (t > 1.0f) {
            if (step > 0.0f) t = 1.0f;
            step = -step;
        }
    };

    if (start.Y == end.Y) {
        /* Horizontal. */
        const int dx = end.X - start.X;
        for (int i = 0; i <= dx; ++i) {
            float rgba[4];
            Lerp_RGB(startcolor, endcolor, t, rgba);
            Emit_Pixel(OutputTarget, start.X + i, start.Y, clip, rgba);
            advance_t();
        }
    } else if (start.X == end.X) {
        /* Vertical. */
        const int sy = (start.Y > end.Y) ? -1 : 1;
        const int dy = std::abs(end.Y - start.Y);
        for (int i = 0; i <= dy; ++i) {
            float rgba[4];
            Lerp_RGB(startcolor, endcolor, t, rgba);
            Emit_Pixel(OutputTarget, start.X, start.Y + i * sy, clip, rgba);
            advance_t();
        }
    } else {
        const int dx = end.X - start.X;
        int dy = end.Y - start.Y;
        const int sy = (dy < 0) ? -1 : 1;
        dy = std::abs(dy);
        const int dx2 = 2 * dx;
        const int dy2 = 2 * dy;

        if (dx > dy) {
            /* Low slope. */
            int delta = dy2 - dx;
            int yy = start.Y;
            for (int i = 0; i <= dx; ++i) {
                float rgba[4];
                Lerp_RGB(startcolor, endcolor, t, rgba);
                Emit_Pixel(OutputTarget, start.X + i, yy, clip, rgba);
                advance_t();
                if (delta > 0) {
                    delta -= dx2;
                    yy += sy;
                }
                delta += dy2;
            }
        } else {
            /* High slope. */
            int delta = dx2 - dy;
            int xx = 0;
            int yy = start.Y;
            for (int i = 0; i <= dy; ++i) {
                float rgba[4];
                Lerp_RGB(startcolor, endcolor, t, rgba);
                Emit_Pixel(OutputTarget, start.X + xx, yy, clip, rgba);
                advance_t();
                if (delta > 0) {
                    delta -= dy2;
                    ++xx;
                }
                delta += dx2;
                yy += sy;
            }
        }
    }

    return true;
}
