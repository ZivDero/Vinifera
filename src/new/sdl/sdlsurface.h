/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  SDL/GDI-backed CPU Surface class.
 *
 *          Pure-CPU surface for menus, dialogs, OwnerDraw, and the legacy
 *          visible-backbuffer compat path. Pixel data lives in a GDI DIB
 *          section that an SDL_Surface wraps; drawing inherits from
 *          XSurface / DSurface base implementations. The GPU-dispatch
 *          duality that lived here through Stage 6 was extracted to
 *          `GpuSurface` (`src/new/gfx/gpu_surface.h`) in Stage 7.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "SDL3/SDL_surface.h"
#include "dsurface.h"


enum SDLSurfaceColorMode {
    COLORMODE_INVALID = -1,
    COLORMODE_555,
    COLORMODE_556,
    COLORMODE_565,
    COLORMODE_655,
};


/**
 *  Pure-CPU surface backed by a GDI DIB section + thin SDL_Surface wrapper.
 *  Used for `HiddenSurface`, `AlternateSurface`, `VisibleSurface` — the
 *  surfaces vanilla writes to via OwnerDraw / direct pixel access / GDI
 *  bitblt. GPU rendering uses `GpuSurface` instead.
 */
class SDLSurface : public DSurface
{
public:
    ~SDLSurface() override;

    /**
     *  Constructs a working surface (not visible).
     */
    SDLSurface(int width, int height);

    /**
     *  SDL → SDL fastpath blit. Falls back to XSurface for cross-class or
     *  translucent blits.
     */
    bool Blit_From(Rect const& dcliprect, Rect const& destrect, Surface const& source, Rect const& scliprect, Rect const& sourcerect, bool trans = false, bool = true) override;
    bool Blit_From(Rect const& destrect, Surface const& source, Rect const& sourcerect, bool trans = false, bool = true) override;
    bool Blit_From(Surface const& source, bool trans = false, bool = true) override;

    /**
     *  SDL_FillSurfaceRect fastpath fills.
     */
    bool Fill_Rect(Rect const& rect, int color) override;
    bool Fill_Rect(Rect const& cliprect, Rect const& fillrect, int color) override;

    /**
     *  GDI device-context interop. Used by vanilla's OwnerDraw / message
     *  pump / dialog paint paths.
     */
    HDC GetDC();
    int ReleaseDC(HDC hdc);

    /**
     *  Create a surface object that represents the currently visible screen.
     */
    static SDLSurface* Create_Primary(void* = nullptr);

    /**
     *  Real Lock/Unlock against the GDI DIB section.
     */
    void* Lock(Point2D point = Point2D(0, 0)) const override;
    bool Unlock() const override;
    bool Can_Lock(int x = 0, int y = 0) const override;

    int Stride() const override;

    /**
     *  Marker for code paths that want to detect SDL-backed surfaces (e.g.
     *  the SDL-fastpath blit recognizes another SDLSurface as a source).
     */
    bool Is_Direct_Draw() const override { return true; }

    bool Can_Blit() const override;
    SDL_Surface* Get_SDL_Surface() const { return SDLSurfacePtr; }

protected:

    /**
     *  The SDL_Surface representation of this surface.
     */
    SDL_Surface* SDLSurfacePtr;

    /**
     *  The GDI representation of this surface.
     *  GDI is what actually owns the memory.
     */
    mutable HDC GDIDC;
    mutable HBITMAP GDIBitmap;
    mutable void* GDIBuffer;

    /**
     *  The surface's pitch.
     */
    int Pitch;

    /**
     *  Pixel format of primary surface.
     */
    static const SDL_PixelFormatDetails* PixelFormat;

private:

    /**
     *  This prevents the creation of a surface in ways that are not
     *  supported.
     */
    SDLSurface(SDLSurface const& rvalue) = delete;
    SDLSurface const operator=(SDLSurface const& rvalue) = delete;
};
