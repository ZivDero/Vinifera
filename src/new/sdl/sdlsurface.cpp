/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  SDL Surface class.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sdlsurface.h"

#include "clipline.h"
#include "debughandler.h"
#include "dsurface.h"
#include "graphics_device.h"
#include "optionsext.h"
#include "primitive_queue.h"
#include "render_pass.h"
#include "rgb.h"
#include "sdl_functions.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"

#include <algorithm>
#include <cmath>


/**
 *  The pixel format of the SDL surfaces created.
 */
const SDL_PixelFormatDetails* SDLSurface::PixelFormat = nullptr;


/**
 *  Struct used to create GDI DIB sections.
 */
struct BitmapInfo
{
    BITMAPINFOHEADER Header;
    DWORD Masks[3];
};


namespace
{
    using Vinifera::Gfx::EBlend;
    using Vinifera::Gfx::PrimitiveDrawCmd;
    using Vinifera::Gfx::PrimitiveKind;
    using Vinifera::Gfx::PrimitiveQueue;
    using Vinifera::Gfx::RectF;

    constexpr int DASH_PATTERN_LENGTH = 16;

    bool Can_Queue_Tactical_Primitives(const SDLSurface* surface)
    {
        if (surface == nullptr || Vinifera::Gfx::Device == nullptr) {
            return false;
        }
        if (OptionsExtension != nullptr && OptionsExtension->LegacyRenderer) {
            return false;
        }
        if (!TacticalActive || !ScenarioActive) {
            return false;
        }
        if (!PrimitiveQueue::Get().Is_Initialized()) {
            return false;
        }
        //if (surface->Is_Locked()) {
        //    return false;
        //}

        const Surface* target = surface;
        return target == CompositeSurface
            || target == TileSurface
            || (target == LogicalSurface && (LogicalSurface == CompositeSurface || LogicalSurface == TileSurface));
    }


    bool Backbuffer_Scale(float& xscale, float& yscale)
    {
        if (Vinifera::Gfx::Device == nullptr || VideoWidth <= 0 || VideoHeight <= 0) {
            return false;
        }

        xscale = (float)Vinifera::Gfx::Device->Get_Backbuffer_Width() / (float)VideoWidth;
        yscale = (float)Vinifera::Gfx::Device->Get_Backbuffer_Height() / (float)VideoHeight;
        return xscale > 0.0f && yscale > 0.0f;
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


    Rect Clip_Fill_Rect(const SDLSurface* surface, Rect const& cliprect, Rect const& fillrect)
    {
        if (surface == nullptr || !cliprect.Is_Valid() || !fillrect.Is_Valid()) {
            return Rect(0, 0, 0, 0);
        }

        Rect crect = Intersect(cliprect, surface->Get_Rect());
        Rect frect = fillrect.Bias_To(cliprect);
        return Intersect(frect, crect);
    }


    bool Queue_Rect(const SDLSurface* surface, Rect const& rect, const float color[4], EBlend blend)
    {
        if (!Can_Queue_Tactical_Primitives(surface) || !rect.Is_Valid()) {
            return false;
        }

        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Backbuffer_Scale(xscale, yscale)) {
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

        PrimitiveQueue::Get().Submit(cmd);
        return true;
    }


    bool Queue_Line(const SDLSurface* surface, Rect const& cliprect, Point2D startpoint, Point2D endpoint, const float color[4])
    {
        if (!Can_Queue_Tactical_Primitives(surface)) {
            return false;
        }

        Rect clip = Intersect(cliprect, surface->Get_Rect());
        if (!clip.Is_Valid() || !Clip_Line(startpoint, endpoint, clip)) {
            return false;
        }

        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Backbuffer_Scale(xscale, yscale)) {
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

        PrimitiveQueue::Get().Submit(cmd);
        return true;
    }


    bool Queue_Dashed_Line(const SDLSurface* surface, Point2D startpoint, Point2D endpoint, int color, bool pattern[], int offset, int& out_offset)
    {
        out_offset = offset;
        if (!Can_Queue_Tactical_Primitives(surface) || pattern == nullptr) {
            return false;
        }

        Rect clip = surface->Get_Rect();
        if (!Clip_Line(startpoint, endpoint, clip)) {
            return true;
        }

        float rgba[4];
        Color_From_Hicolor(color, 1.0f, rgba);

        const int dx = std::abs(endpoint.X - startpoint.X);
        const int sx = startpoint.X < endpoint.X ? 1 : -1;
        const int dy = -std::abs(endpoint.Y - startpoint.Y);
        const int sy = startpoint.Y < endpoint.Y ? 1 : -1;
        int err = dx + dy;

        Point2D segment_start;
        Point2D segment_end;
        bool segment_active = false;

        Point2D p = startpoint;
        for (;;) {
            if (pattern[out_offset & (DASH_PATTERN_LENGTH - 1)]) {
                if (!segment_active) {
                    segment_start = p;
                    segment_active = true;
                }
                segment_end = p;
            } else if (segment_active) {
                Queue_Line(surface, clip, segment_start, segment_end, rgba);
                segment_active = false;
            }

            out_offset = (out_offset + 1) & (DASH_PATTERN_LENGTH - 1);

            if (p == endpoint) {
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
            Queue_Line(surface, clip, segment_start, segment_end, rgba);
        }

        return true;
    }


    bool Queue_Rect_Outline(const SDLSurface* surface, Rect const& cliprect, Rect const& rect, int color)
    {
        if (!Can_Queue_Tactical_Primitives(surface) || !rect.Is_Valid()) {
            return false;
        }

        float rgba[4];
        Color_From_Hicolor(color, 1.0f, rgba);

        Point2D top_left(rect.X, rect.Y);
        Point2D top_right(rect.X + rect.Width - 1, rect.Y);
        Point2D bottom_left(rect.X, rect.Y + rect.Height - 1);
        Point2D bottom_right(rect.X + rect.Width - 1, rect.Y + rect.Height - 1);

        Queue_Line(surface, cliprect, top_left, top_right, rgba);
        if (rect.Height > 1) {
            Queue_Line(surface, cliprect, bottom_left, bottom_right, rgba);
        }
        if (rect.Height > 2) {
            Queue_Line(surface, cliprect, Point2D(rect.X, rect.Y + 1), Point2D(rect.X, rect.Y + rect.Height - 2), rgba);
            if (rect.Width > 1) {
                Queue_Line(surface, cliprect, Point2D(rect.X + rect.Width - 1, rect.Y + 1), Point2D(rect.X + rect.Width - 1, rect.Y + rect.Height - 2), rgba);
            }
        }

        return true;
    }


    Point2D Intersected_Clip_Origin(const SDLSurface* surface, Rect const& cliprect)
    {
        return Intersect(cliprect, surface->Get_Rect()).TopLeft;
    }
}


/**
 *  SDLSurface constructor.
 *
 *  @author: ZivDero
 */
SDLSurface::SDLSurface(int width, int height) :
    DSurface(), // use the default constructor so that we don't initialize the DDraw portions of the surface
    SDLSurfacePtr(nullptr),
    GDIDC(nullptr),
    GDIBitmap(nullptr),
    GDIBuffer(nullptr),
    Pitch(0)
{
    /**
     *  If this is our first surface, fetch the pixel format.
     */
    if (!PixelFormat) {
        PixelFormat = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGB565);
        if (!PixelFormat) {
            DEBUG_ERROR("Failed to get pixel format details for RGB565.\n");
            return;
        }
    }

    /**
     *  Create persistent memory DC and DIB section.
     */
    GDIDC = CreateCompatibleDC(nullptr);
    if (!GDIDC) {
        DEBUG_ERROR("CreateCompatibleDC failed\n");
        return;
    }

    BitmapInfo bmi = {};
    bmi.Header.biSize = sizeof(BITMAPINFOHEADER);
    bmi.Header.biWidth = width;
    bmi.Header.biHeight = -height;
    bmi.Header.biPlanes = 1;
    bmi.Header.biBitCount = PixelFormat->bits_per_pixel;
    bmi.Header.biCompression = BI_BITFIELDS;
    bmi.Masks[0] = PixelFormat->Rmask;
    bmi.Masks[1] = PixelFormat->Gmask;
    bmi.Masks[2] = PixelFormat->Bmask;

    /**
     *  Create DIB section (let GDI allocate memory).
     */
    GDIBitmap = CreateDIBSection(GDIDC, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, &GDIBuffer, nullptr, 0);
    if (!GDIBitmap || !GDIBuffer) {
        DEBUG_ERROR("CreateDIBSection failed! Error = %lu\n", GetLastError());
        DeleteDC(GDIDC);
        GDIDC = nullptr;
        return;
    }

    SelectObject(GDIDC, GDIBitmap);

    DIBSECTION ds = {};
    GetObject(GDIBitmap, sizeof(ds), &ds);
    Pitch = ds.dsBm.bmWidthBytes;

    /**
     *  Create an SDL surface wrapping GDIBuffer.
     */
    SDLSurfacePtr = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGB565, GDIBuffer, Pitch);
    if (SDLSurfacePtr == nullptr) {
        DEBUG_ERROR("SurfacePtr could not be created! SDL Error: %s\n", SDL_GetError());
        return;
    }

    /**
     *  Set surface properties.
     */
    BytesPerPixel = PixelFormat->bytes_per_pixel;
    Width = SDLSurfacePtr->w;
    Height = SDLSurfacePtr->h;
}


/**
 *  SDLSurface destructor.
 *
 *  @author: ZivDero
 */
SDLSurface::~SDLSurface()
{
    if (SDLSurfacePtr) {
        SDL_DestroySurface(SDLSurfacePtr);
        SDLSurfacePtr = nullptr;
    }
    if (GDIBitmap) {
        DeleteObject(GDIBitmap);
        GDIBitmap = nullptr;
    }
    if (GDIDC) {
        DeleteDC(GDIDC);
        GDIDC = nullptr;
    }
}


/**
 *  Calculate bit shifts to properly extract channel data.
 *
 *  @author: ZivDero, tomsons26
 */
static void Calculate_Mask_Info(unsigned int mask, unsigned int& right, unsigned int& left)
{
    right = 0;
    left = 0;

    /**
     *  Figure out how far to shift bits to the left.
     */
    for (int index = 0; index < 16; index++) {
        if (mask & 0x01) break;
        mask >>= 1;
        right++;
    }

    /**
     *  Figure out how far to shift bits to the right.
     */
    for (int index = 0; index < 8; index++) {
        if (mask & 0x80) break;
        mask <<= 1;
        left++;
    }
}


/**
 *  With DSurface, this would create the primary (visible) surface.
 *  There is no such thing with SDL, but we take this opportunity to
 *  initialize some static variables used for color conversions.
 *
 *  @author: ZivDero, tomsons26
 */
SDLSurface* SDLSurface::Create_Primary(void*)
{
    DEBUG_INFO("SDLSurface::Create_Primary\n");

    AllowStretchBlits = true;
    AllowHWFill = false;

    DEBUG_INFO("SDLSurface::Create_Primary - Creating surface\n");
    SDLSurface* surface = new SDLSurface(VideoWidth, VideoHeight);

    /**
     *  If this is a hicolor surface, then build the shift values for
     *  building and extracting the colors from the hicolor pixel.
     */
    if (PrimaryColorMode == COLORMODE_INVALID) {
        Calculate_Mask_Info(PixelFormat->Rmask, RedRight, RedLeft);
        Calculate_Mask_Info(PixelFormat->Gmask, GreenRight, GreenLeft);
        Calculate_Mask_Info(PixelFormat->Bmask, BlueRight, BlueLeft);

        /**
         *  Create the halfbright mask.
         */
        HalfbrightMask = static_cast<unsigned short>(Build_Hicolor_Pixel(127, 127, 127));
        QuarterbrightMask = static_cast<unsigned short>(Build_Hicolor_Pixel(63, 63, 63));
        EighthbrightMask = static_cast<unsigned short>(Build_Hicolor_Pixel(31, 31, 31));

        if (BlueRight == 0 && BlueLeft == 3 && GreenRight == 5 && GreenLeft == 3 && RedRight == 10 && RedLeft == 3) {
            PrimaryColorMode = COLORMODE_555;
        } else if (BlueRight == 0 && BlueLeft == 2 && GreenRight == 6 && GreenLeft == 3 && RedRight == 11 && RedLeft == 3) {
            PrimaryColorMode = COLORMODE_556;
        } else if (BlueRight == 0 && BlueLeft == 3 && GreenRight == 5 && GreenLeft == 2 && RedRight == 11 && RedLeft == 3) {
            PrimaryColorMode = COLORMODE_565;
        } else if (BlueRight == 0 && BlueLeft == 3 && GreenRight == 5 && GreenLeft == 3 && RedRight == 11 && RedLeft == 2) {
            PrimaryColorMode = COLORMODE_655;
        }
    }
    DEBUG_INFO("SDLSurface::Create_Primary done\n");

    return surface;
}


/**
 *  Blit from one surface to this one.
 *
 *  @author: ZivDero, tomsons26
 */
bool SDLSurface::Blit_From(Rect const& dcliprect, Rect const& destrect, Surface const& ssource, Rect const& scliprect, Rect const& sourcerect, bool trans, bool)
{
    if (!dcliprect.Is_Valid() || !scliprect.Is_Valid() || !destrect.Is_Valid() || !sourcerect.Is_Valid()) return false;

    bool use_xsurface = false;

    /**
     *  For non-SDL surfaces, or if a trans blit is requested, let vanilla
     *  blitters handle the blit.
     */
    if (!ssource.Is_Direct_Draw() == true || trans == true) {
        use_xsurface = true;
    }

    if (use_xsurface == true) {
        return XSurface::Blit_From(destrect, ssource, sourcerect, trans, true);
    }

    Rect drect = destrect;
    Rect srect = sourcerect;

    Rect swindow = Intersect(scliprect, ssource.Get_Rect());
    Rect dwindow = Intersect(dcliprect, Get_Rect());

    if (!Blit_Clip(drect, dwindow, srect, swindow)) {
        return false;
    }

    SDL_Surface* src_surf = static_cast<SDLSurface const&>(ssource).Get_SDL_Surface();
    SDL_Surface* dst_surf = this->Get_SDL_Surface();

    if (!src_surf || !dst_surf) {
        return false;
    }

    SDL_Rect src {srect.X + swindow.X, srect.Y + swindow.Y, srect.Width, srect.Height};
    SDL_Rect dst {drect.X + dwindow.X, drect.Y + dwindow.Y, drect.Width, drect.Height};

    SDL_SetSurfaceBlendMode(src_surf, SDL_BLENDMODE_NONE);
    return SDL_BlitSurfaceScaled(src_surf, &src, dst_surf, &dst, SDL_SCALEMODE_LINEAR);
}


/**
 *  This routine will fill the specified rectangle.
 *
 *  @author: ZivDero, tomsons26
 */
bool SDLSurface::Fill_Rect(Rect const& fillrect, int color)
{
    return SDLSurface::Fill_Rect(Get_Rect(), fillrect, color);
}


/**
 *  Fills a rectangle with clipping control.
 *
 *  @author: ZivDero, tomsons26
 */
bool SDLSurface::Fill_Rect(Rect const& cliprect, Rect const& fillrect, int color)
{
    if (SDLSurfacePtr == nullptr || !fillrect.Is_Valid()) return false;

    /**
     *  Ensure that the clipping rectangle is legal.
     */
    Rect crect = Intersect(cliprect, Get_Rect());

    /**
     *  Bias the fill rect to the clipping rectangle.
     */
    Rect frect = fillrect.Bias_To(cliprect);

    /**
     *  Find the region that should be filled after being clipped by the
     *  clipping rectangle. This could result in no fill operation being performed
     *  if the desired fill rectangle has been completely clipped away.
     */
    frect = Intersect(frect, crect);
    if (!frect.Is_Valid()) return false;

    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    if (Queue_Rect(this, frect, rgba, EBlend::Opaque)) {
        return true;
    }

    SDL_Rect rect;
    rect.x = frect.X;
    rect.y = frect.Y;
    rect.w = frect.Width;
    rect.h = frect.Height;

    return SDL_FillSurfaceRect(SDLSurfacePtr, &rect, color);
}


/**
 *  Fills a translucent rectangle.
 *
 *  @author: Vinifera GPU primitive shim
 */
bool SDLSurface::Fill_Rect_Trans(Rect const& rect, RGBClass const& color, int opacity)
{
    Rect clipped = Clip_Fill_Rect(this, Get_Rect(), rect);
    if (clipped.Is_Valid()) {
        float rgba[4];
        Color_From_RGB(color, (float)opacity / 100.0f, rgba);
        if (Queue_Rect(this, clipped, rgba, EBlend::AlphaBlend)) {
            return true;
        }
    }

    return DSurface::Fill_Rect_Trans(rect, color, opacity);
}


/**
 *  Draws a single pixel.
 *
 *  @author: Vinifera GPU primitive shim
 */
bool SDLSurface::Put_Pixel(Point2D const& point, int color)
{
    Rect rect(point.X, point.Y, 1, 1);
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    if (Queue_Rect(this, rect, rgba, EBlend::Opaque)) {
        return true;
    }

    return XSurface::Put_Pixel(point, color);
}


/**
 *  Draws a solid line.
 *
 *  @author: Vinifera GPU primitive shim
 */
bool SDLSurface::Draw_Line(Point2D const& startpoint, Point2D const& endpoint, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    if (Queue_Line(this, Get_Rect(), startpoint, endpoint, rgba)) {
        return true;
    }

    return XSurface::Draw_Line(startpoint, endpoint, color);
}


bool SDLSurface::Draw_Line(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int color)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    const Point2D origin = Intersected_Clip_Origin(this, cliprect);
    if (Queue_Line(this, cliprect, startpoint + origin, endpoint + origin, rgba)) {
        return true;
    }

    return XSurface::Draw_Line(cliprect, startpoint, endpoint, color);
}


bool SDLSurface::Draw_Line_entry_34(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int color, int a5, int a6, bool a7)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    const Point2D origin = Intersected_Clip_Origin(this, cliprect);
    if (Queue_Line(this, cliprect, startpoint + origin, endpoint + origin, rgba)) {
        return true;
    }

    return DSurface::Draw_Line_entry_34(cliprect, startpoint, endpoint, color, a5, a6, a7);
}


bool SDLSurface::Draw_Line_entry_38(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, int a4, int a5, int a6, bool a7)
{
    float rgba[4];
    Color_From_Hicolor(a4, 1.0f, rgba);
    const Point2D origin = Intersected_Clip_Origin(this, cliprect);
    if (Queue_Line(this, cliprect, startpoint + origin, endpoint + origin, rgba)) {
        return true;
    }

    return DSurface::Draw_Line_entry_38(cliprect, startpoint, endpoint, a4, a5, a6, a7);
}


bool SDLSurface::Draw_Line_entry_3C(Rect const& cliprect, Point2D const& startpoint, Point2D const& endpoint, RGBClass const& color, int a5, int a6, bool a7, bool a8, bool a9, bool a10, float a11)
{
    float rgba[4];
    Color_From_RGB(color, 1.0f, rgba);
    const Point2D origin = Intersected_Clip_Origin(this, cliprect);
    if (Queue_Line(this, cliprect, startpoint + origin, endpoint + origin, rgba)) {
        return true;
    }

    return DSurface::Draw_Line_entry_3C(cliprect, startpoint, endpoint, color, a5, a6, a7, a8, a9, a10, a11);
}


/**
 *  Draws a dashed line. The vanilla pattern length is 16 entries.
 *
 *  @author: Vinifera GPU primitive shim
 */
int SDLSurface::Draw_Dashed_Line(Point2D const& startpoint, Point2D const& endpoint, int color, bool pattern[], int offset)
{
    int out_offset = offset;
    if (Queue_Dashed_Line(this, startpoint, endpoint, color, pattern, offset, out_offset)) {
        return out_offset;
    }

    return XSurface::Draw_Dashed_Line(startpoint, endpoint, color, pattern, offset);
}


int SDLSurface::entry_48(Point2D const& startpoint, Point2D const& endpoint, int color, bool pattern[], int offset, bool a6)
{
    int out_offset = offset;
    if (Queue_Dashed_Line(this, startpoint, endpoint, color, pattern, offset, out_offset)) {
        return out_offset;
    }

    return DSurface::entry_48(startpoint, endpoint, color, pattern, offset, a6);
}


bool SDLSurface::entry_4C(Point2D const& startpoint, Point2D const& endpoint, int color, bool a4)
{
    float rgba[4];
    Color_From_Hicolor(color, 1.0f, rgba);
    if (Queue_Line(this, Get_Rect(), startpoint, endpoint, rgba)) {
        return true;
    }

    return DSurface::entry_4C(startpoint, endpoint, color, a4);
}


bool SDLSurface::Draw_Rect(Rect const& rect, int color)
{
    if (Queue_Rect_Outline(this, Get_Rect(), rect, color)) {
        return true;
    }

    return XSurface::Draw_Rect(rect, color);
}


bool SDLSurface::Draw_Rect(Rect const& cliprect, Rect const& rect, int color)
{
    if (Queue_Rect_Outline(this, cliprect, rect.Bias_To(Intersect(cliprect, Get_Rect())), color)) {
        return true;
    }

    return XSurface::Draw_Rect(cliprect, rect, color);
}


bool SDLSurface::entry_84(Point2D const& point, int color, Rect const& rect)
{
    if (rect.Is_Point_Within(point) && Get_Rect().Is_Point_Within(point)) {
        float rgba[4];
        Color_From_Hicolor(color, 1.0f, rgba);
        if (Queue_Rect(this, Rect(point.X, point.Y, 1, 1), rgba, EBlend::Opaque)) {
            return true;
        }
    }

    return XSurface::entry_84(point, color, rect);
}


/**
 *  Get the windows device context from our surface.
 *
 *  @author: ZivDero
 */
HDC SDLSurface::GetDC()
{
    if (GDIDC == nullptr) {
        return nullptr;
    }

    LockCount++;
    return GDIDC;
}


/**
 *  Release the windows device context from our surface.
 *
 *  @author: ZivDero
 */
int SDLSurface::ReleaseDC(HDC hdc)
{
    if (!GDIDC || hdc != GDIDC) {
        return 0;
    }

    if (LockCount > 0) {
        LockCount--;
    }

    return 1;
}


/**
 *  Fetches the bytes between rows.
 *
 *  @author: ZivDero
 */
int SDLSurface::Stride() const
{
    return Pitch;
}


/**
 *  Fetches a working pointer into surface memory.
 *
 *  @author: ZivDero, tomsons26
 */
void* SDLSurface::Lock(Point2D point) const
{
    if (point.X < 0 || point.Y < 0) return nullptr;

    if (LockCount == 0) {
        if (SDL_MUSTLOCK(SDLSurfacePtr)) {
            if (!SDL_LockSurface(SDLSurfacePtr)) {
                return nullptr; // failed to lock
            }
        }
        LockPtr = SDLSurfacePtr->pixels;
    }
    XSurface::Lock();
    return static_cast<char*>(LockPtr) + point.Y * Stride() + point.X * Bytes_Per_Pixel();
}


/**
 *  Returns if the surface can be locked.
 *
 *  @author: ZivDero
 */
bool SDLSurface::Can_Lock(int x, int y) const
{
    return SDLSurfacePtr != nullptr;
}


/**
 *  Returns if the surface can be blitted to.
 *
 *  @author: ZivDero
 */
bool SDLSurface::Can_Blit() const
{
    return SDLSurfacePtr != nullptr;
}


/**
 *  Unlock a previously locked surface.
 *
 *  @author: ZivDero
 */
bool SDLSurface::Unlock() const
{
    if (LockCount > 0) {
        XSurface::Unlock();
        if (LockCount == 0) {
            if (SDL_MUSTLOCK(SDLSurfacePtr)) {
                SDL_UnlockSurface(SDLSurfacePtr);
            }
            LockPtr = nullptr;
        }
        return true;
    }
    return false;
}
