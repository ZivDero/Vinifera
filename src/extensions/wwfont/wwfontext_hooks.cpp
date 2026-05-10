/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU WWFont glyph rendering hooks.
 *
 *          Replaces vanilla `WWFontClass::Print` with a proxy that decomposes
 *          the call into per-glyph `FontDrawCmd`s on `FontQueue` when the
 *          destination surface is a `GpuSurface`; falls through to vanilla
 *          CPU rendering when it isn't (menus / dialogs / OwnerDraw
 *          surfaces). Layout / spacing / kerning logic mirrors
 *          `WWFontClass::Print` from D:\Projects\Tiberian-Sun\code\wwfont.cpp.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "wwfontext_hooks.h"

#include "convert.h"
#include "debughandler.h"
#include "font_asset.h"
#include "font_cache.h"
#include "font_queue.h"
#include "gpu_surface.h"
#include "gpu_surface_target.h"
#include "graphics_device.h"
#include "hooker.h"
#include "optionsext.h"
#include "render_pass.h"
#include "shp_cache.h"      // PaletteCache lives next to ShpCache
#include "vinifera_globals.h"
#include "wwfont.h"

#include <cstring>


/**
 *  Fake class for implementing a new member function with access to the
 *  vanilla `WWFontClass` `this` pointer. Must not contain a constructor or
 *  destructor or new fields — it must be ABI-identical to `WWFontClass`.
 *  Prefix override names with `_` so they cannot accidentally virtualize.
 */
class WWFontClassExt : public WWFontClass
{
public:
    Point2D _Print(char const* string, Surface& surface, Rect const& cliprect,
                   Point2D const& drawpoint, ConvertClass const& convertref,
                   unsigned char const* remap) const;
};


Point2D WWFontClassExt::_Print(char const* string, Surface& surface, Rect const& cliprect,
                               Point2D const& drawpoint, ConvertClass const& convertref,
                               unsigned char const* remap) const
{
    using Vinifera::Gfx::FontAsset;
    using Vinifera::Gfx::FontCache;
    using Vinifera::Gfx::FontDrawCmd;
    using Vinifera::Gfx::FontGlyphInfo;
    using Vinifera::Gfx::FontQueue;
    using Vinifera::Gfx::GpuRenderTarget;
    using Vinifera::Gfx::PaletteCache;
    using Vinifera::Gfx::PaletteLUT;
    using Vinifera::Gfx::RectF;
    using Vinifera::Gfx::RenderPass;

    /**
     *  `Get_Font_Data` is non-const in vanilla `WWFontClass` even though it
     *  only reads a pointer. Cast `this` to access it from our const proxy.
     */
    void* font_data = const_cast<WWFontClassExt*>(this)->Get_Font_Data();

    /**
     *  GPU-eligibility pre-check. Any failure falls back to the original
     *  vanilla `Print` so menus / dialogs / `SDLSurface` callers keep working.
     */
    const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
    GpuSurface* gpu = dynamic_cast<GpuSurface*>(&surface);
    if (legacy
        || Vinifera::Gfx::Device == nullptr
        || gpu == nullptr
        || string == nullptr
        || *string == '\0'
        || font_data == nullptr
        || !FontQueue::Get().Is_Initialized()) {
        return WWFontClass::Print(string, surface, cliprect, drawpoint, convertref, remap);
    }

    FontAsset* asset = FontCache::Get().Get_Or_Load(*Vinifera::Gfx::Device, font_data);
    PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, &convertref);
    if (asset == nullptr || palette == nullptr) {
        return WWFontClass::Print(string, surface, cliprect, drawpoint, convertref, remap);
    }

    /**
     *  Vanilla builds an identity 0..15 remap if the caller passed null.
     */
    static const unsigned char identity_remap[16] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };
    const unsigned char* rmap = (remap != nullptr) ? remap : identity_remap;

    /**
     *  Trivial reject if the print position is already outside the clip rect
     *  (matches vanilla's early-out).
     */
    const int startx = drawpoint.X + cliprect.X;
    int xpos = startx;
    int ypos = drawpoint.Y + cliprect.Y;
    if (xpos >= cliprect.X + cliprect.Width || ypos >= cliprect.Y + cliprect.Height) {
        return drawpoint;
    }

    /**
     *  Logical → render-target pixel scale. Same source of truth used by the
     *  other GPU proxies; for the Sidebar target this is identity.
     */
    float xscale = 1.0f;
    float yscale = 1.0f;
    if (!Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, gpu->Output_Target(), xscale, yscale)) {
        return WWFontClass::Print(string, surface, cliprect, drawpoint, convertref, remap);
    }

    const RectF clip_rt {
        (float)cliprect.X * xscale,
        (float)cliprect.Y * yscale,
        (float)cliprect.Width * xscale,
        (float)cliprect.Height * yscale
    };

    /**
     *  Line advance for `\n` — vanilla uses `Raw_Height + max(0, yspacing)`
     *  where `yspacing = FontYSpacing + Raw_Width/16`. `Get_Height()` is the
     *  public accessor that returns `Raw_Height + max(0, FontYSpacing)`. The
     *  `Raw_Width/16` fudge-factor difference is sub-pixel on every TS font
     *  we care about; for `\n` line advance this is close enough.
     */
    const int line_height = Get_Height();

    const RenderPass pass = Vinifera::Gfx::Current_Render_Pass();
    const GpuRenderTarget target = gpu->Output_Target();

    for (const char* p = string; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;

        /**
         *  Both `\r` and `\n` advance Y in vanilla `WWFontClass::Print` —
         *  they only differ in which X they reset to: `\r` returns to the
         *  original draw column (`startx`), `\n` returns to the clip-rect
         *  left edge (`cliprect.X`). Multi-line text using either delimiter
         *  needs the Y bump.
         */
        if (c == '\r') { xpos = startx;       ypos += line_height; continue; }
        if (c == '\n') { xpos = cliprect.X;   ypos += line_height; continue; }
        if (c < 0x20)  { continue; }

        const FontGlyphInfo* gi = asset->Get_Glyph(c);
        if (gi == nullptr || gi->W <= 0 || gi->H <= 0) {
            /**
             *  Char_Pixel_Width returns 0 for control chars and `WidthBlock[c] +
             *  FontXSpacing` otherwise — same advance vanilla uses.
             */
            xpos += Char_Pixel_Width((char)c);
            continue;
        }

        FontDrawCmd cmd = {};
        cmd.Asset = asset;
        cmd.Palette = palette;
        cmd.Glyph = c;
        cmd.Dst = RectF {
            (float)xpos * xscale,
            (float)(ypos + gi->YOffset) * yscale,
            (float)gi->W * xscale,
            (float)gi->H * yscale
        };
        cmd.Clip = clip_rt;
        memcpy(cmd.RemapTable, rmap, 16);
        cmd.Pass = pass;
        cmd.OutputTarget = target;
        FontQueue::Get().Submit(cmd);

        xpos += Char_Pixel_Width((char)c);
    }

    /**
     *  Vanilla returns the (cliprect-relative) next-print position so chained
     *  prints can pick up where the last one left off.
     */
    return Point2D(xpos - cliprect.X, ypos - cliprect.Y);
}


void WWFont_Hooks()
{
    Change_Virtual_Address(0x006DA394, Get_Func_Address(&WWFontClassExt::_Print));

    DEBUG_INFO("WWFont_Hooks: WWFontClass::Print redirected to GPU FontQueue.\n");
}
