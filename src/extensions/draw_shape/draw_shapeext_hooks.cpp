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
        if (flags & SHAPE_TRANSLUCENT25) out |= SEF_TRANSLUCENT25;
        if (flags & SHAPE_TRANSLUCENT50) out |= SEF_TRANSLUCENT50;
        if (flags & SHAPE_TRANSLUCENT75) out |= SEF_TRANSLUCENT75;
        return out;
    }

    inline float Depth_From_Screen_Y(float y)
    {
        const float kMaxScreenY = 16000.0f;
        float dz = 1.0f - (y / kMaxScreenY);
        if (dz < 0.001f) dz = 0.001f;
        if (dz > 0.999f) dz = 0.999f;
        return dz;
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
    ShapeFlags_Type flags,
    const char* remap,
    int height_offset,
    ZGradientType zgrad,
    int intensity,
    const ShapeSet* z_shapefile,
    int z_shapenum,
    Point2D z_off)
{
    /**
     *  Fall-through cases that always run vanilla CPU code:
     *    - LegacyRenderer flag set (developer A/B switch).
     *    - Target surface isn't one of the tactical buffers — sidebar /
     *      hidden / cameo / etc. The `CompositeSurface` and `TileSurface`
     *      globals get swapped during the tile pass (tactical.cpp:806-810),
     *      so we accept whichever is the in-flight target. Buildings, trees,
     *      and cell shadows render with `LogicalSurface = TileSurface` during
     *      the tile pass; units / anims / particles render post-tile with
     *      `LogicalSurface = CompositeSurface`. Both must reach the GPU
     *      pipeline, otherwise the GPU tile pass overwrites their CPU pixels.
     *    - GraphicsDevice not initialized yet (pre-video-mode boot path).
     *    - Bad inputs (defensive).
     */
    const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
    const bool tactical_surface = (&surface == CompositeSurface) || (&surface == TileSurface);
    if (legacy
        || Vinifera::Gfx::Device == nullptr
        || !tactical_surface
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
     *  only the depth calculation below consumes it.
     */
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
     *  Depth: WAE-style screen-Y normalization. Larger screen Y means the
     *  object is closer to the camera (front of the isometric view), so it
     *  gets a smaller depth value. Map (0..16000) → (1.0..0.0); subtract a
     *  per-class epsilon so identical-Y sprites of different categories
     *  tiebreak deterministically (unit-vs-overlay, projectile-vs-unit, etc).
     *  Tiles use the same scale (with epsilon=0), so sprites depth-test
     *  correctly against terrain.
     *
     *  `height_offset` in vanilla is a Z-test bias on the cell side of the
     *  per-pixel comparison — a *negative* value (e.g. for an aircraft at
     *  altitude) effectively pulls the shape forward of the cell. Mirror
     *  that here by adding `-height_offset` to bottom_y: larger screen Y →
     *  smaller dz → closer to the camera.
     *
     *  Per-vertex Z gradient (DstZTop vs DstZBottom):
     *    - SHAPE_ZGRAD + ZGRAD_GROUND → full gradient. Top pixel maps to a
     *      cell one sprite-height further back; smaller screen Y → larger
     *      dz at the top vertex.
     *    - SHAPE_ZGRAD + ZGRAD_45DEG → half gradient (cliff/ramp face).
     *    - SHAPE_ZGRAD + ZGRAD_90DEG → no gradient (vertical structure;
     *      every pixel sits at the cell-foot's depth — buildings, units).
     *    - SHAPE_ZGRAD off, or ZGRAD_NONE → no gradient.
     */
    {
        const float kSpriteEpsilon = 5e-5f;     // keeps equal-depth object pixels just in front of terrain
        const float depth_bias_y = (float)-height_offset;
        const float bottom_y = (float)(y + fi->H) + depth_bias_y;
        float top_y = bottom_y;

        if (flags & SHAPE_ZGRAD) {
            if (zgrad == ZGRAD_GROUND) {
                top_y = bottom_y - (float)fi->H;
            } else if (zgrad == ZGRAD_45DEG) {
                top_y = bottom_y - (float)fi->H * 0.5f;
            }
            /* ZGRAD_90DEG / ZGRAD_NONE: keep top_y = bottom_y. */
        }

        cmd.DstZTop = Depth_From_Screen_Y(top_y) - kSpriteEpsilon;
        cmd.DstZBottom = Depth_From_Screen_Y(bottom_y) - kSpriteEpsilon;
    }

    /**
     *  SHAPE_ZREADWRITE means vanilla's blitter writes per-pixel Z as it
     *  draws (used by buildings and similar large vertical structures so
     *  things drawn afterwards behind them are correctly occluded). Mark
     *  this command so SpriteQueue::Flush picks the depth-write state.
     */
    cmd.WriteDepth = (flags & SHAPE_ZREADWRITE) != 0;

    /**
     *  Vanilla never z-tests Draw_Shape calls that lack SHAPE_ZGRAD
     *  (selection brackets, transport / ammo / health pips, build-state
     *  overlays, cameos). They're 2D UI laid over the tactical view; their
     *  quads extend down into screen rows belonging to the next-front cell,
     *  whose tile depth is closer than the sprite's foot-derived depth, so
     *  hardware depth-test would clip them at the bottom. Submission order
     *  handles inter-overlay layering.
     */
    cmd.OverlayMode = (flags & SHAPE_ZGRAD) == 0;
    if (cmd.OverlayMode) {
        cmd.WriteDepth = false;
    }

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
 *  cross-referencing 0x0047C780 in IDA.
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
     *  Callsite table. Each entry is the address of a single
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

    Patch_Call(0x00428920, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00428A0A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00428B0D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00454E48, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004555CF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004557AB, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00455B21, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00484DC2, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00485D05, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004861FF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004863FC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004865E9, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EB24, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EE60, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EEDD, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049EF2A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049F0E9, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0049F21C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004EC8C6, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x004F5C9F, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B091, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B38D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B6A4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056B9BF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BBCD, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BC72, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BE3A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0056BEC4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00572622, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00572772, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0058C83D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0058D38C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB545, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB5A5, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB5F9, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB655, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005AB6B1, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005ADEB1, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B8E57, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B8F14, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B8F97, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005B9639, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005BC8C4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005BCCD6, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E375C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E39B7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E3C77, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E3EC3, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E3F1C, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E448A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E44E7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E6C70, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E6D8E, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E7045, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005E7094, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F1743, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F367A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F371F, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F3777, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F37BF, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F52EE, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F533E, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005F5527, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x005FB5A5, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E4E8, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E562, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E6DC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E758, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0060E91D, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00612510, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x006127AC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00612AB0, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0061718B, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062BE85, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C556, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C5D0, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C6B7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C947, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0062C9F7, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x006376A4, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637880, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063796A, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637A58, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637B36, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637BCD, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00637CAC, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063FBEB, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063FD25, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x0063FD6B, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00653282, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00653E96, &Draw_Shape_Proxy_DX11);
    Patch_Call(0x00661B64, &Draw_Shape_Proxy_DX11);

    DEBUG_INFO("DrawShape_Hooks: installed 105 Draw_Shape callsite intercepts.\n");
}
