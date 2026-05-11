/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  SDL/GDI-backed CPU Surface class.
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
#include "tibsun_functions.h"
#include "tibsun_globals.h"


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

    /**
     *  CPU surfaces (`HiddenSurface`, `AlternateSurface`, `VisibleSurface`)
     *  are sized to the *backbuffer* (display) resolution rather than the
     *  vanilla video-mode logical resolution. The tactical scene no longer
     *  rounds through them (the GPU pipeline writes directly to `SceneRT`
     *  at logical res and upscales on present), so they exist purely for
     *  menus, VQA, dialogs, and the score/escape overlays. Allocating them
     *  at backbuffer dims means WinAPI dialogs render crisply at the
     *  display's true pixel size and present 1:1, with no upscale-induced
     *  blockiness.
     */
    int width = VideoWidth;
    int height = VideoHeight;
    if (Vinifera::Gfx::Device != nullptr) {
        const int bb_w = Vinifera::Gfx::Device->Get_Backbuffer_Width();
        const int bb_h = Vinifera::Gfx::Device->Get_Backbuffer_Height();
        if (bb_w > 0 && bb_h > 0) {
            width = bb_w;
            height = bb_h;
        }
    }

    DEBUG_INFO("SDLSurface::Create_Primary - Creating surface (%dx%d)\n", width, height);
    SDLSurface* surface = new SDLSurface(width, height);

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
 *  Blit from one surface to this one. SDL → SDL fastpath uses
 *  SDL_BlitSurfaceScaled; everything else (translucent, cross-class) falls
 *  back to the XSurface CPU blit.
 *
 *  @author: ZivDero, tomsons26
 */
bool SDLSurface::Blit_From(Rect const& dcliprect, Rect const& destrect, Surface const& ssource, Rect const& scliprect, Rect const& sourcerect, bool trans, bool)
{
    if (!dcliprect.Is_Valid() || !scliprect.Is_Valid() || !destrect.Is_Valid() || !sourcerect.Is_Valid()) return false;

    /**
     *  For non-SDL surfaces, or if a trans blit is requested, let the
     *  XSurface CPU blitter handle the operation.
     */
    if (!ssource.Is_Direct_Draw() || trans) {
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


bool SDLSurface::Blit_From(Rect const& destrect, Surface const& source, Rect const& sourcerect, bool trans, bool a5)
{
    return SDLSurface::Blit_From(Get_Rect(), destrect, source, source.Get_Rect(), sourcerect, trans, a5);
}


bool SDLSurface::Blit_From(Surface const& source, bool trans, bool a3)
{
    return SDLSurface::Blit_From(Get_Rect(), Get_Rect(), source, source.Get_Rect(), source.Get_Rect(), trans, a3);
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
 *  Fills a rectangle with clipping control. SDL_FillSurfaceRect fastpath.
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
     *  clipping rectangle. This could result in no fill operation being
     *  performed if the desired fill rectangle has been completely clipped
     *  away.
     */
    frect = Intersect(frect, crect);
    if (!frect.Is_Valid()) return false;

    SDL_Rect rect;
    rect.x = frect.X;
    rect.y = frect.Y;
    rect.w = frect.Width;
    rect.h = frect.Height;

    return SDL_FillSurfaceRect(SDLSurfacePtr, &rect, color);
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
