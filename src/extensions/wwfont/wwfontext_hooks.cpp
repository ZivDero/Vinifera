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
#include "primitive_queue.h"
#include "render_pass.h"
#include "shp_cache.h"      // PaletteCache lives next to ShpCache
#include "vinifera_globals.h"
#include "wwfont.h"

#include <cstdint>
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
    using Vinifera::Gfx::EBlend;
    using Vinifera::Gfx::FontAsset;
    using Vinifera::Gfx::FontCache;
    using Vinifera::Gfx::FontDrawCmd;
    using Vinifera::Gfx::FontGlyphInfo;
    using Vinifera::Gfx::FontQueue;
    using Vinifera::Gfx::GpuRenderTarget;
    using Vinifera::Gfx::PaletteCache;
    using Vinifera::Gfx::PaletteLUT;
    using Vinifera::Gfx::PrimitiveDrawCmd;
    using Vinifera::Gfx::PrimitiveKind;
    using Vinifera::Gfx::PrimitiveQueue;
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
    GpuSurface* gpu = dynamic_cast<GpuSurface*>(&surface);
    if (Vinifera::Gfx::Device == nullptr
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

    /**
     *  Vanilla `WWFontClass::Print` rasterizes every pixel of each character
     *  cell (`WidthBlock[c] × Raw_Height`), writing `fontpalette[pixel_idx]`
     *  for every one. Palette-index-0 pixels — which dominate the cell's
     *  empty surround and inter-character padding — get painted in the back
     *  color the caller stuffed into `fontpalette[0]`.
     *
     *  Our atlas only stores each glyph's tight visible bbox, so the cell's
     *  outer padding is never sampled. To mimic vanilla's behaviour we emit
     *  one PrimitiveQueue solid rect per cell as a pre-pass background;
     *  PrimitiveQueue flushes before FontQueue (see `sdl_functions.cpp`) so
     *  the glyph lands on top.
     *
     *  When `remap[0] == 0` the caller wanted a transparent surround (vanilla
     *  pixel-write of 0 → fontpalette[0] == 0 == no-op for that pixel), so
     *  skip the background entirely.
     */
    const unsigned char back_idx = rmap[0];
    const bool want_background = (back_idx != 0)
        && (convertref.Translator != nullptr)
        && PrimitiveQueue::Get().Is_Initialized();
    float back_rgba[4] = { 0, 0, 0, 0 };
    if (want_background) {
        /**
         *  Decode the back palette color from the same Translator that
         *  `PaletteCache::Get_Or_Build` uses to build the GPU LUT, so the
         *  on-screen background matches the on-screen foreground exactly.
         */
        const uint16_t v = static_cast<const uint16_t*>(convertref.Translator)[back_idx];
        const uint8_t r5 = static_cast<uint8_t>((v >> 11) & 0x1F);
        const uint8_t g6 = static_cast<uint8_t>((v >> 5)  & 0x3F);
        const uint8_t b5 = static_cast<uint8_t>( v        & 0x1F);
        back_rgba[0] = static_cast<float>((r5 << 3) | (r5 >> 2)) / 255.0f;
        back_rgba[1] = static_cast<float>((g6 << 2) | (g6 >> 4)) / 255.0f;
        back_rgba[2] = static_cast<float>((b5 << 3) | (b5 >> 2)) / 255.0f;
        back_rgba[3] = 1.0f;
    }

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

        const int cell_w = Char_Pixel_Width((char)c);

        /**
         *  Per-cell background fill (see comment above the loop). Issued
         *  even for control chars / zero-width glyphs would be a no-op
         *  (cell_w == 0), but those are filtered by the `< 0x20` early-out
         *  above so we only get here for visible advance.
         */
        if (want_background && cell_w > 0) {
            PrimitiveDrawCmd bg = {};
            bg.Kind = PrimitiveKind::SolidRect;
            bg.Pass = pass;
            bg.Blend = EBlend::Opaque;
            bg.Rect = RectF {
                (float)xpos * xscale,
                (float)ypos * yscale,
                (float)cell_w * xscale,
                (float)line_height * yscale
            };
            bg.Color[0] = back_rgba[0];
            bg.Color[1] = back_rgba[1];
            bg.Color[2] = back_rgba[2];
            bg.Color[3] = back_rgba[3];
            bg.OutputTarget = target;
            PrimitiveQueue::Get().Submit(bg);
        }

        const FontGlyphInfo* gi = asset->Get_Glyph(c);
        if (gi == nullptr || gi->W <= 0 || gi->H <= 0) {
            /**
             *  Char_Pixel_Width returns 0 for control chars and `WidthBlock[c] +
             *  FontXSpacing` otherwise — same advance vanilla uses.
             */
            xpos += cell_w;
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

        xpos += cell_w;
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
