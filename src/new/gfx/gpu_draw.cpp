/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Vinifera-native GPU draw entry points.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "gpu_draw.h"

#include "brightness.h"
#include "convert.h"
#include "debughandler.h"
#include "distortion_queue.h"
#include "drawshape.h"
#include "gpu_surface.h"
#include "gpu_surface_target.h"
#include "graphics_device.h"
#include "palette_lut.h"
#include "render_pass.h"
#include "shapeset.h"
#include "shp_asset.h"
#include "shp_atlas.h"
#include "shp_cache.h"
#include "sprite_queue.h"
#include "surface.h"
#include "vinifera_globals.h"


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Master feature gate for the SHP screen-space warp/distortion
         *  effect on SHAPE_PREDATOR draws. When false, predator routing is
         *  bypassed and cloaked SHPs fall back to plain SHAPE_TRANSLUCENT
         *  rendering. The DistortionQueue and SceneCopy infrastructure stays
         *  compiled in; flip back to true to re-enable the warp path.
         */
        constexpr bool kPredatorWarpEnabled = false;


        inline void Tint_From_Intensity_And_Flags(int intensity, ShapeFlags_Type flags, float out[4])
        {
            /**
             *  Vanilla `intensity` ranges 0..2000 with 1000 == 100% (full normal)
             *  and 2000 == 2x overbright. Brightness_To_Tint maps linearly into
             *  a [0, 2] RGB multiplier; the float vertex tint preserves values
             *  above 1.0 through to the shader.
             *
             *  Translucency is folded into alpha here rather than passed as a
             *  shader flag — the shader already does `c.a *= v.col.a` and the
             *  premultiplied blend handles the rest, so this is mathematically
             *  identical to the old SEF_TRANSLUCENT* branches while keeping
             *  translucent shapes batchable with opaque ones.
             */
            const float t = Brightness_To_Tint(intensity);
            float a = 1.0f;
            /**
             *  `SHAPE_TRANSLUCENT75` is the bitwise OR of `SHAPE_TRANSLUCENT25 |
             *  SHAPE_TRANSLUCENT50` (see tibsun_defines.h), so independent
             *  `flags & SHAPE_TRANSLUCENT*` tests miscompare — a unit with
             *  only SHAPE_TRANSLUCENT50 (bit 2) would also match the
             *  TRANSLUCENT75 mask via that same bit and get an extra
             *  `*= 0.25`. Compare the full 2-bit field once and dispatch.
             */
            const unsigned tmask = (unsigned)flags & (unsigned)SHAPE_TRANSLUCENT75;
            if (tmask == (unsigned)SHAPE_TRANSLUCENT75)       a *= 0.25f;
            else if (tmask == (unsigned)SHAPE_TRANSLUCENT50)  a *= 0.5f;
            else if (tmask == (unsigned)SHAPE_TRANSLUCENT25)  a *= 0.75f;
            out[0] = t;
            out[1] = t;
            out[2] = t;
            out[3] = a;
        }


        inline uint32_t Effect_Flags_From_Shape(ShapeFlags_Type flags)
        {
            uint32_t out = 0;
            if (flags & SHAPE_DARKEN) out |= SEF_DARKEN;
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


    bool GPU_Draw_Shape(Surface&         surface,
                        ConvertClass&    convert,
                        const ShapeSet*  shapefile,
                        int              shapenum,
                        const Point2D&   point,
                        const Rect&      window,
                        ShapeFlags_Type  flags,
                        int              height_offset,
                        ZGradientType    zgrad,
                        int              intensity,
                        const ShapeSet*  z_shapefile,
                        int              z_shapenum,
                        Point2D          z_off,
                        int              predator_offset,
                        SpriteDrawCmd*   out_cmd)
    {
        /**
         *  Fall back to vanilla CPU draw when the GPU pipeline can't take
         *  this call:
         *    - Target surface isn't a `GpuSurface` — SDLSurface destinations
         *      (HiddenSurface / AlternateSurface / VisibleSurface, menus,
         *      cameos, hidden buffers) keep using vanilla's CPU blit.
         *      `CompositeSurface` and `TileSurface` are both `GpuSurface`.
         *    - GraphicsDevice not initialized yet (pre-video-mode boot path).
         *    - Bad inputs (defensive).
         */
        GpuSurface* gpu_surface = dynamic_cast<GpuSurface*>(&surface);
        if (Vinifera::Gfx::Device == nullptr
            || gpu_surface == nullptr
            || shapefile == nullptr
            || shapenum < 0)
        {
            if (out_cmd == nullptr) {
                Draw_Shape(surface, convert, shapefile, shapenum, point, window, flags,
                           /*remap*/ nullptr,
                           height_offset, zgrad, intensity, z_shapefile, z_shapenum, z_off);
            }
            return false;
        }

        GraphicsDevice& device = *Vinifera::Gfx::Device;

        ShpAsset*   asset   = ShpCache::Get().Get_Or_Load(device, shapefile);
        PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(device, &convert);
        if (asset == nullptr || palette == nullptr) {
            if (out_cmd == nullptr) {
                Draw_Shape(surface, convert, shapefile, shapenum, point, window, flags,
                           /*remap*/ nullptr,
                           height_offset, zgrad, intensity, z_shapefile, z_shapenum, z_off);
            }
            return false;
        }
        const ShpFrameInfo* fi = asset->Get_Frame(shapenum);
        if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
            return false;
        }

        ShpAsset* z_asset = nullptr;
        const ShpFrameInfo* z_fi = nullptr;
        if (z_shapefile != nullptr && z_shapenum >= 0) {
            z_asset = ShpCache::Get().Get_Or_Load(device, z_shapefile);
            if (z_asset != nullptr) {
                z_fi = z_asset->Get_Frame(z_shapenum);
                if (z_fi == nullptr || z_fi->W <= 0 || z_fi->H <= 0) {
                    z_asset = nullptr;
                    z_fi = nullptr;
                }
            }
        }

        /**
         *  Reproduce Draw_Shape's logical-coords math so the sprite lands at
         *  the same logical position vanilla would have CPU-blitted to.
         *
         *  `height_offset` is for depth bias only; vanilla never applies it
         *  to the destination Y, and neither do we.
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
        x += fi->X;
        y += fi->Y;

        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Logical_To_Render_Target(device, gpu_surface->Output_Target(), xscale, yscale)) {
            if (out_cmd == nullptr) {
                Draw_Shape(surface, convert, shapefile, shapenum, point, window, flags,
                           /*remap*/ nullptr,
                           height_offset, zgrad, intensity, z_shapefile, z_shapenum, z_off);
            }
            return false;
        }

        const Rect clipped_window = Intersect(window, surface.Get_Rect());
        if (!clipped_window.Is_Valid()) {
            return false;
        }

        /**
         *  SHAPE_PREDATOR — stealth/cloak refraction. Routes to the
         *  DistortionQueue which copies the Scene RT and samples it at a
         *  warp offset, then blends with the SHP's palette color. The
         *  ratio comes from SHAPE_TRANSLUCENT*:
         *    SHAPE_TRANSLUCENT75 → 75% background (most see-through)
         *    SHAPE_TRANSLUCENT50 → 50% / 50%
         *    SHAPE_TRANSLUCENT25 → 25% background
         *    none of the above   → 50% default
         *
         *  `predator_offset` is the per-call shimmer offset — vanilla's
         *  `TechnoClass::Get_Predator_Offset()` returns `(id + frame) % 400`
         *  for a per-unit animated value. The vanilla-ABI proxy passes 0,
         *  which produces a static (un-shimmering) cloak; Vinifera-native
         *  call sites with a TechnoClass* should pass the real value.
         */
        if ((flags & SHAPE_PREDATOR) && !kPredatorWarpEnabled) {
            /**
             *  Warp disabled — fall back to plain translucency. Vanilla
             *  pairs SHAPE_PREDATOR with a SHAPE_TRANSLUCENT* bit, but if
             *  the caller didn't set one, default to 50% so the cloaked
             *  unit at least shows up as half-transparent rather than
             *  fully opaque.
             */
            if (!(flags & (SHAPE_TRANSLUCENT25 | SHAPE_TRANSLUCENT50 | SHAPE_TRANSLUCENT75))) {
                flags = ShapeFlags_Type(flags | SHAPE_TRANSLUCENT50);
            }
            flags = ShapeFlags_Type(flags & ~SHAPE_PREDATOR);
        }

        if (flags & SHAPE_PREDATOR) {
            /**
             *  Same `SHAPE_TRANSLUCENT75` collision risk as the tint path —
             *  compare the 2-bit field once.
             */
            const unsigned tmask = (unsigned)flags & (unsigned)SHAPE_TRANSLUCENT75;
            float blend_ratio = 0.5f;
            if (tmask == (unsigned)SHAPE_TRANSLUCENT75)       blend_ratio = 0.75f;
            else if (tmask == (unsigned)SHAPE_TRANSLUCENT50)  blend_ratio = 0.5f;
            else if (tmask == (unsigned)SHAPE_TRANSLUCENT25)  blend_ratio = 0.25f;

            DistortionDrawCmd dcmd = {};
            dcmd.Asset       = asset;
            dcmd.Palette     = palette;
            dcmd.FrameIndex  = shapenum;
            dcmd.Dst.X       = x * xscale;
            dcmd.Dst.Y       = y * yscale;
            dcmd.Dst.W       = fi->W * xscale;
            dcmd.Dst.H       = fi->H * yscale;
            dcmd.Clip.X      = clipped_window.X * xscale;
            dcmd.Clip.Y      = clipped_window.Y * yscale;
            dcmd.Clip.W      = clipped_window.Width * xscale;
            dcmd.Clip.H      = clipped_window.Height * yscale;
            dcmd.Pass        = Current_Render_Pass();
            dcmd.WarpOffsetPixels = predator_offset;
            dcmd.BlendRatio  = blend_ratio;
            dcmd.OutputTarget = gpu_surface->Output_Target();

            const float kSpriteEpsilon = 5e-5f;
            const float depth_bias_y = (float)-height_offset;
            const float bottom_y = (float)(y + fi->H) + depth_bias_y;
            const float top_y    = (flags & SHAPE_ZGRAD) && (zgrad == ZGRAD_GROUND)
                                 ? bottom_y - (float)fi->H
                                 : bottom_y;
            dcmd.DstZTop    = Depth_From_Screen_Y(top_y)    - kSpriteEpsilon;
            dcmd.DstZBottom = Depth_From_Screen_Y(bottom_y) - kSpriteEpsilon;

            /**
             *  Predator branch never feeds out_cmd — the unit-scratch
             *  composite path doesn't currently support predator units (the
             *  warp pipeline reads SceneCopy directly). If out_cmd was
             *  requested, just return false without submitting; the caller
             *  treats it as a build failure.
             */
            if (out_cmd != nullptr) {
                return false;
            }
            DistortionQueue::Get().Submit(dcmd);
            return true;
        }

        SpriteDrawCmd cmd = {};
        cmd.Asset       = asset;
        cmd.ZAsset      = z_asset;
        cmd.Palette     = palette;
        cmd.FrameIndex  = shapenum;
        cmd.Dst.X       = x * xscale;
        cmd.Dst.Y       = y * yscale;
        cmd.Dst.W       = fi->W * xscale;
        cmd.Dst.H       = fi->H * yscale;
        cmd.Clip.X      = clipped_window.X * xscale;
        cmd.Clip.Y      = clipped_window.Y * yscale;
        cmd.Clip.W      = clipped_window.Width * xscale;
        cmd.Clip.H      = clipped_window.Height * yscale;
        cmd.Pass        = Current_Render_Pass();
        cmd.EffectFlags = Effect_Flags_From_Shape(flags);
        Tint_From_Intensity_And_Flags(intensity, flags, cmd.Tint);
        const bool z_active = (flags & SHAPE_ZREAD) || (flags & SHAPE_ZGRAD) || (flags & SHAPE_ZREADWRITE);
        const bool z_write = (flags & SHAPE_ZREADWRITE);
        if (z_asset != nullptr && z_fi != nullptr) {
            /**
             *  Mirror vanilla Draw_Shape's z-shape sampling origin:
             *    zpoint = z_off - ((logical_size / 2) - visible_frame.xy)
             *    zpoint += z_frame.xy
             */
            Point2D zpoint = z_off;
            zpoint.X -= logical_w / 2 - fi->X;
            zpoint.Y -= logical_h / 2 - fi->Y;
            zpoint.X += z_fi->X;
            zpoint.Y += z_fi->Y;

            const float ztw = (float)ShpAtlas::Get().Page_Width();
            const float zth = (float)ShpAtlas::Get().Page_Height();
            if (ztw > 0.0f && zth > 0.0f) {
                cmd.ZSrcUV.X = ((float)z_fi->AtlasX + (float)zpoint.X) / ztw;
                cmd.ZSrcUV.Y = ((float)z_fi->AtlasY + (float)zpoint.Y) / zth;
                cmd.ZSrcUV.W = (float)fi->W / ztw;
                cmd.ZSrcUV.H = (float)fi->H / zth;
            } else {
                cmd.ZAsset = nullptr;
            }
        }

        /**
         *  Depth: WAE-style screen-Y normalization. Larger screen Y means
         *  the object is closer to the camera (front of the iso view), so it
         *  gets a smaller depth value. Per-vertex Z gradient (DstZTop vs
         *  DstZBottom) is gated on SHAPE_ZGRAD specifically:
         *    - SHAPE_ZGRAD + ZGRAD_GROUND: full gradient.
         *    - SHAPE_ZGRAD + ZGRAD_45DEG: half gradient (cliff/ramp face).
         *    - SHAPE_ZGRAD + ZGRAD_90DEG: no gradient (vertical structure).
         *    - SHAPE_ZGRAD off, or ZGRAD_NONE: no gradient.
         */
        {
            const float kSpriteEpsilon = 5e-5f;
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

        cmd.WriteDepth = z_write;

        /**
         *  Vanilla never z-tests Draw_Shape calls that select a non-z
         *  blitter (selection brackets, transport / ammo / health pips,
         *  build-state overlays, cameos). They're 2D UI laid over the
         *  tactical view; submission order handles inter-overlay layering.
         */
        cmd.DisableDepth = !z_active;
        if (Is_Cell_Shadow_Pass(cmd.Pass)) {
            cmd.DisableDepth = false;
        }
        if (cmd.DisableDepth) {
            cmd.WriteDepth = false;
        }

        /**
         *  Alpha-buffer write modes. SHAPE_WRITE_ALPHA / SHAPE_WRITE_ALPHA_MULT
         *  redirect the draw away from the backbuffer and into the alpha
         *  buffer (vehicle headlights, muzzle flashes, searchlight cones).
         */
        if (flags & SHAPE_WRITE_ALPHA) {
            cmd.Mode = SpriteDrawMode::AlphaWriteAdd;
            cmd.DisableDepth = true;
            cmd.WriteDepth = false;
        } else if (flags & SHAPE_WRITE_ALPHA_MULT) {
            cmd.Mode = SpriteDrawMode::AlphaWriteMult;
            cmd.DisableDepth = true;
            cmd.WriteDepth = false;
        } else {
            cmd.Mode = SpriteDrawMode::Color;
        }

        cmd.OutputTarget = gpu_surface->Output_Target();

        if (out_cmd != nullptr) {
            *out_cmd = cmd;
            return true;
        }
        SpriteQueue::Get().Submit(cmd);
        return true;
    }
}
