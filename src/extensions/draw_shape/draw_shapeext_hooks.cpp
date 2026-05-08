/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2b: per-callsite interception of vanilla Draw_Shape.
 *
 *          Each Patch_Call entry rewrites a single CALL 0x0047C780 inside the
 *          original TS binary so it lands in our `Draw_Shape_Proxy_DX11`
 *          instead. The proxy decides per-call whether to fall through to
 *          vanilla CPU rendering (LegacyRenderer flag set, or surface is not
 *          CompositeSurface) or to translate the call into a SpriteDrawCmd
 *          and Submit() it to the per-frame queue.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "draw_shapeext_hooks.h"

#include "convert.h"
#include "debughandler.h"
#include "drawshape.h"
#include "graphics_device.h"
#include "hooker.h"
#include "optionsext.h"
#include "palette_lut.h"
#include "shapeset.h"
#include "shp_asset.h"
#include "shp_cache.h"
#include "sprite_queue.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"

#include <cstring>


using namespace Vinifera::Gfx;


namespace
{
    inline uint32_t Tint_From_Intensity(int intensity)
    {
        /**
         *  Vanilla `intensity` ranges 0..2000 with 1000 == 100%. Map to a
         *  per-vertex RGB modulate; alpha stays 0xFF (translucency comes from
         *  EffectFlags). Values >1000 (overbright) clamp to 1.0 since the
         *  shader works in linear modulate; the lighting/overbright cases
         *  vanilla TS uses are rare enough that clamping is acceptable for
         *  the first-cut.
         */
        int channel = intensity * 255 / 1000;
        if (channel < 0) channel = 0;
        if (channel > 255) channel = 255;
        const uint32_t c = (uint32_t)channel;
        return (0xFFu << 24) | (c << 16) | (c << 8) | c;
    }

    inline uint32_t Effect_Flags_From_Shape(ShapeFlags_Type flags)
    {
        uint32_t out = 0;
        if (flags & SHAPE_DARKEN)        out |= SEF_DARKEN;
        if (flags & SHAPE_TRANS25)       out |= SEF_TRANSLUCENT25;
        if (flags & SHAPE_TRANS50)       out |= SEF_TRANSLUCENT50;
        if (flags & SHAPE_TRANS75)       out |= SEF_TRANSLUCENT75;
        return out;
    }
}


/**
 *  Proxy entry point. Mirrors vanilla Draw_Shape's signature exactly so the
 *  /Gr (fastcall) ABI matches what the original binary's CALL instruction
 *  expects — same pattern as the existing animext Draw_Shape_Proxy.
 *
 *  @author: Vinifera Stage 2b
 */
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
    Point2D z_off = Point2D(0, 0))
{
    /**
     *  Fall-through cases that always run vanilla CPU code:
     *    - LegacyRenderer flag set (developer A/B switch).
     *    - Target surface is not CompositeSurface — sidebar / hidden / etc.
     *    - GraphicsDevice not initialized yet (pre-video-mode boot path).
     *    - Bad inputs (defensive).
     */
    const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
    if (legacy
        || Vinifera::Gfx::Device == nullptr
        || &surface != CompositeSurface
        || shapefile == nullptr
        || shapenum < 0)
    {
        Draw_Shape(surface, convert, shapefile, shapenum, point, window, flags,
                   remap, height_offset, zgrad, intensity, z_shapefile, z_shapenum, z_off);
        return;
    }

    /**
     *  Resolve the GPU-side asset and palette via process-wide caches. On a
     *  miss we lazy-load; on a permanent failure (malformed SHP) the cache
     *  returns nullptr — fall back to vanilla so the frame isn't visually
     *  broken.
     */
    GraphicsDevice& device = *Vinifera::Gfx::Device;
    ShpAsset* asset = ShpCache::Get().Get_Or_Load(device, shapefile);
    PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(device, &convert);
    if (asset == nullptr || palette == nullptr) {
        Draw_Shape(surface, convert, shapefile, shapenum, point, window, flags,
                   remap, height_offset, zgrad, intensity, z_shapefile, z_shapenum, z_off);
        return;
    }
    const ShpFrameInfo* fi = asset->Get_Frame(shapenum);
    if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
        return;
    }

    /**
     *  Reproduce Draw_Shape's logical-coords math from
     *  D:/Projects/Tiberian-Sun/code/draw.cpp:65-118 to land the sprite at
     *  the same logical position vanilla would have CPU-blitted to.
     *
     *  Note on `height_offset`: vanilla passes it to the inner blitter for
     *  Z-test bias only and never applies it to the destination Y. The
     *  visual effect of altitude (bullets in flight, raised animations) is
     *  already baked into `point.Y` by the caller's screen-coord math, so
     *  applying it here would double-shift bullets vertically.
     */
    (void)height_offset;
    const int logical_w = shapefile->Get_Width();
    const int logical_h = shapefile->Get_Height();
    int x = point.X;
    int y = point.Y;
    if (flags & SHAPE_CENTER) {
        x -= logical_w / 2;
        y -= logical_h / 2;
    }
    if (flags & SHAPE_WIN_REL) {
        x += window.X;
        y += window.Y;
    }
    /**
     *  Fold the per-frame X/Y origin offset (the shape sub-rect's logical
     *  position relative to (0,0)).
     */
    x += fi->X;
    y += fi->Y;

    /**
     *  Logical → backbuffer-pixel scale. CompositeSurface is rendered into at
     *  logical (video) resolution; the present quad scales it up to the
     *  backbuffer. To align our GPU sprite with the surrounding CPU-blitted
     *  scene we apply the same scale to dst.
     */
    const float xscale = (VideoWidth > 0) ? (float)device.Get_Backbuffer_Width()  / (float)VideoWidth  : 1.0f;
    const float yscale = (VideoHeight > 0) ? (float)device.Get_Backbuffer_Height() / (float)VideoHeight : 1.0f;

    SpriteDrawCmd cmd = {};
    cmd.Asset       = asset;
    cmd.Palette     = palette;
    cmd.FrameIndex  = shapenum;
    cmd.Dst.X       = x * xscale;
    cmd.Dst.Y       = y * yscale;
    cmd.Dst.W       = fi->W * xscale;
    cmd.Dst.H       = fi->H * yscale;
    cmd.EffectFlags = Effect_Flags_From_Shape(flags);
    cmd.VertexTint  = Tint_From_Intensity(intensity);

    /**
     *  House-color remap. Vanilla's `remap` is a 256-byte LUT but only the
     *  16-entry slot for indices 16..31 ever differs in practice. Our
     *  PaletteLUT::Update_Remap consumes 16 bytes; copy from offset 16 of the
     *  vanilla table (where the per-house overrides live). Vanilla's
     *  Draw_Shape sets SHAPE_REMAP whenever `remap != NULL`, so test the
     *  pointer directly.
     */
    if (remap != nullptr) {
        cmd.UseRemap = true;
        memcpy(cmd.RemapTable, remap + 16, 16);
    }

    SpriteQueue::Get().Submit(cmd);
}


/**
 *  Hook installer. Each Patch_Call rewrites a `CALL 0x0047C780` instruction
 *  inside the original binary to land in our proxy. Addresses come from
 *  cross-referencing 0x0047C780 in IDA / Ghidra; below is the curated set.
 *
 *  Note: animext_hooks.cpp:775 already installs `Patch_Call(0x00414BA9, ...)`
 *  for shadow rendering and depends on the original being called inside its
 *  proxy. We deliberately leave that callsite alone — adding ourselves
 *  there would either fight that proxy or require chaining. The animation
 *  Draw_Shape will be picked up via a *different* call site once we identify
 *  one, or by integrating the shadow logic into our proxy in a follow-up.
 */
void DrawShape_Hooks()
{
    /**
     *  Initialize SpriteQueue lazily on first frame after video mode is set;
     *  not done here. The hook installer runs before video init.
     */

    /**
     *  Curated callsite table. Each entry is the address of a single
     *  `CALL 0x0047C780` instruction inside the original TS binary,
     *  discovered via IDA xrefs to Draw_Shape's entry.
     *
     *  We deliberately avoid 0x00414BA9 (already patched by animext for
     *  shadow rendering) — patching it again would conflict. Animations are
     *  still covered via the other AnimClass::Draw_It callsites below.
     */

    Patch_Call(0x00653D6A, &Draw_Shape_Proxy_DX11);  // UnitClass::Draw_It      — vehicle bodies/shadows
    Patch_Call(0x004D2EAC, &Draw_Shape_Proxy_DX11);  // InfantryClass::Draw_It  — primary infantry frames
    Patch_Call(0x004D319A, &Draw_Shape_Proxy_DX11);  // InfantryClass::Draw_It  — secondary frames (firing/dying)
    Patch_Call(0x005A46E2, &Draw_Shape_Proxy_DX11);  // ParticleClass::Draw_It  — smoke/sparks/trails
    Patch_Call(0x00445E8C, &Draw_Shape_Proxy_DX11);  // BulletClass::Draw_It    — projectile sprites
    Patch_Call(0x00445EEF, &Draw_Shape_Proxy_DX11);  // BulletClass::Draw_It    — projectile shadows
    Patch_Call(0x0063FBA2, &Draw_Shape_Proxy_DX11);  // TerrainClass::Draw_It   — trees/cliffs/objects
    Patch_Call(0x004149B4, &Draw_Shape_Proxy_DX11);  // AnimClass::Draw_It      — anims/explosions (1/3)
    Patch_Call(0x00414AB2, &Draw_Shape_Proxy_DX11);  // AnimClass::Draw_It      — anims/explosions (2/3)
    Patch_Call(0x00414C48, &Draw_Shape_Proxy_DX11);  // AnimClass::Draw_It      — anims/explosions (3/3)
    Patch_Call(0x006352ED, &Draw_Shape_Proxy_DX11);  // TechnoClass::Techno_Draw_Object (1/3) — shared low-level draw
    Patch_Call(0x0063538D, &Draw_Shape_Proxy_DX11);  // TechnoClass::Techno_Draw_Object (2/3)
    Patch_Call(0x006354A8, &Draw_Shape_Proxy_DX11);  // TechnoClass::Techno_Draw_Object (3/3)

    DEBUG_INFO("DrawShape_Hooks: installed 13 Draw_Shape callsite intercepts.\n");
}
