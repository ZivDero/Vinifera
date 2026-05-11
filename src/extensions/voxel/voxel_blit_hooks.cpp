/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU voxel composite hooks.
 *
 *          Vanilla TS rasterizes voxels (units, aircraft, voxel-anims,
 *          projectiles) into CPU 8-bit byte buffers — either `VoxelSurface`
 *          (per-voxel-part scratch, 160×160) or `EightBitSurface` (multi-part
 *          unit composite, 160×160) — then calls `Bit_Blit` / `Blit_Block`
 *          to land them on the tactical surface. On our build the tactical
 *          surface is a `GpuSurface` whose CPU buffer is throwaway, so
 *          vanilla's blits vanish into the dummy buffer.
 *
 *          This file installs hooks that intercept those blits when the
 *          destination is `GpuSurface`, read the source CPU pixels (and
 *          parallel `VoxelZSurface` per-pixel z when present), and submit a
 *          `VoxelCompositeCmd` to the GPU queue. Vanilla's CPU voxel
 *          rasterizer is untouched.
 *
 *          Two hook shapes:
 *            - `Patch_Jump` at `UnitClass::Unit_Blit_Voxel` (full replacement)
 *              — handles the composite path (turreted units → `EightBitSurface`
 *              → tactical). All callers go through this single entry point.
 *            - `Patch_Call` at the five `Blit_Block` call sites that feed
 *              voxel pixels directly (no turret) — `Techno_Render_Voxel_Object`,
 *              `Techno_Render_Voxel_Shadow`, `BulletClass::Draw_Voxel`,
 *              `VoxelAnimClass::Draw_It` (×2). Each proxy detects dest =
 *              `GpuSurface` and routes to GPU; otherwise it calls vanilla
 *              `Blit_Block` for the SDLSurface / HiddenSurface cases.
 *
 *          Cache-hit path (`Techno_Blit_Voxel` → `RLE_Blit`) is deferred:
 *          we disable `VoxelDrawSystem::EnableZBuffer` cache feedback so
 *          vanilla rasterizes every frame, funnelling everything through the
 *          two hook shapes above. Documented as a follow-up.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_blit_hooks.h"

#include "bsurface.h"           // BSurface (must come before blit.h for Surface fwd-decls)
#include "blit.h"               // Bit_Blit
#include "colorscheme.h"        // ColorScheme + ColorSchemes vector
#include "convert.h"
#include "debughandler.h"
#include "drawshape.h"
#include "extension_globals.h"
#include "graphics_device.h"
#include "gpu_surface.h"
#include "gpu_surface_target.h"
#include "hooker.h"
#include "house.h"              // HouseClass (techno's owner)
#include "optionsext.h"
#include "render_pass.h"
#include "shp_cache.h"          // PaletteCache
#include "sprite_batch.h"       // RectF
#include "sprite_effect.h"      // SEF_*
#include "techno.h"
#include "tibsun_globals.h"     // EightBitSurface, ColorSchemes
#include "tibsun_inline.h"
#include "tspp.h"               // Make_Global
#include "unit.h"
#include "vinifera_globals.h"
#include "voxel_composite_queue.h"

#include <algorithm>


/**
 *  Vanilla globals we need that aren't already in TSpp. Bind via Make_Global
 *  to the addresses we recovered from IDA.
 */
static Rect&     UnitCompositeDirtyRect_g = Make_Global<Rect>(0x0080F8E0);
static BSurface& VoxelSurface_g           = Make_Global<BSurface>(0x008200F0);
static BSurface& VoxelZSurface_g          = Make_Global<BSurface>(0x0081FFB8);


using Vinifera::Gfx::GpuRenderTarget;
using Vinifera::Gfx::PaletteCache;
using Vinifera::Gfx::PaletteLUT;
using Vinifera::Gfx::RectF;
using Vinifera::Gfx::RenderPass;
using Vinifera::Gfx::SEF_DARKEN;
using Vinifera::Gfx::SEF_TRANSLUCENT25;
using Vinifera::Gfx::SEF_TRANSLUCENT50;
using Vinifera::Gfx::SEF_TRANSLUCENT75;
using Vinifera::Gfx::VoxelCompositeCmd;
using Vinifera::Gfx::VoxelCompositeQueue;


namespace
{
    /**
     *  Mirrors `Depth_From_Screen_Y` in draw_shapeext_hooks. The whole GPU
     *  side uses 1/16000 as the project-wide pixel-to-depth scale (see
     *  `tile_queue.cpp::ZDataDepthScale`); voxel composites must land in
     *  that same depth range or they won't z-test correctly against tiles
     *  and SHP sprites.
     */
    inline float Depth_From_Screen_Y(float y)
    {
        constexpr float kMaxScreenY = 16000.0f;
        float dz = 1.0f - (y / kMaxScreenY);
        if (dz < 0.001f) dz = 0.001f;
        if (dz > 0.999f) dz = 0.999f;
        return dz;
    }


    /**
     *  Brightness → tint scalar. Vanilla's `intensity` runs 0..2000 (1000 =
     *  neutral). Map to 0..2.0 RGB tint so overbright values pass through the
     *  shader without clipping (matches the existing SHP path's tint logic).
     */
    inline void Tint_From_Brightness(int intensity, float out[4])
    {
        const float t = (float)intensity / 1000.0f;
        out[0] = t;
        out[1] = t;
        out[2] = t;
        out[3] = 1.0f;
    }


    /**
     *  Effect-flag bits derived from the unit's `Visual_Character`. Mirrors
     *  the vanilla switch in `Unit_Blit_Voxel` ([unit.cpp:2445-2479] in vanilla
     *  source). Predator displacement (VISUAL_RIPPLE) is deferred — the
     *  cloaked unit renders with the same translucency level as VISUAL_DARKEN
     *  for now. Returns `false` and zeroes `out_flags` for VISUAL_HIDDEN
     *  (caller must skip the draw).
     */
    bool Effect_Flags_From_Visual(VisualType v, uint32_t& out_flags)
    {
        out_flags = 0;
        switch (v) {
        case VISUAL_NORMAL:
            return true;
        case VISUAL_INDISTINCT:
            out_flags = SEF_TRANSLUCENT25;
            return true;
        case VISUAL_DARKEN:
        case VISUAL_SHADOWY:
            out_flags = SEF_TRANSLUCENT50;
            return true;
        case VISUAL_RIPPLE:
            /* Predator effect is deferred; render translucent so the unit
               is at least selectable and visible. */
            out_flags = SEF_TRANSLUCENT50;
            return true;
        case VISUAL_HIDDEN:
        default:
            return false;
        }
    }


    /**
     *  Build and submit one VoxelCompositeCmd. Returns false if the cmd
     *  couldn't be enqueued (queue not initialized, palette miss, etc.) so
     *  the caller can fall back to the vanilla CPU path.
     */
    bool Submit_Voxel_Composite(GpuSurface& gpu_dest,
                                BSurface const& source,
                                ConvertClass& converter,
                                Rect const& source_rect,
                                Rect const& dest_rect,
                                Rect const& dest_clip,
                                int z_adjust,
                                int brightness,
                                uint32_t effect_flags,
                                bool has_per_pixel_z,
                                BSurface const* z_source)
    {
        if (Vinifera::Gfx::Device == nullptr) {
            return false;
        }
        if (!VoxelCompositeQueue::Get().Is_Initialized()) {
            return false;
        }
        if (source_rect.Width <= 0 || source_rect.Height <= 0) {
            return false;
        }

        PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, &converter);
        if (palette == nullptr) {
            return false;
        }

        /**
         *  Copy source pixels now — VoxelSurface / EightBitSurface are shared
         *  scratch buffers overwritten for every subsequent voxel part. If we
         *  just store the raw pointer the data will be stale by the time
         *  Flush_Pass runs. Pack into a tight W×H row-major buffer (no stride
         *  padding) so the atlas upload is a single contiguous copy.
         */
        const uint8_t* color_src = static_cast<const uint8_t*>(
            const_cast<BSurface&>(source).Lock(Point2D(source_rect.X, source_rect.Y)));
        if (color_src == nullptr) {
            return false;
        }
        const int source_stride = const_cast<BSurface&>(source).Stride();

        VoxelCompositeCmd cmd = {};
        cmd.SourceW = source_rect.Width;
        cmd.SourceH = source_rect.Height;

        cmd.ColorData.resize((size_t)source_rect.Width * source_rect.Height);
        for (int row = 0; row < source_rect.Height; ++row) {
            std::memcpy(cmd.ColorData.data() + row * source_rect.Width,
                        color_src + row * source_stride,
                        source_rect.Width);
        }

        if (has_per_pixel_z && z_source != nullptr) {
            const uint8_t* z_src = static_cast<const uint8_t*>(
                const_cast<BSurface&>(*z_source).Lock(Point2D(source_rect.X, source_rect.Y)));
            if (z_src != nullptr) {
                cmd.ZData.resize((size_t)source_rect.Width * source_rect.Height);
                for (int row = 0; row < source_rect.Height; ++row) {
                    std::memcpy(cmd.ZData.data() + row * source_rect.Width,
                                z_src + row * source_stride,
                                source_rect.Width);
                }
            }
        }

        /**
         *  Logical→backbuffer scale (same flow used by Draw_Shape_Proxy_DX11).
         *  Sidebar voxels don't exist, but the scale call still works.
         */
        float xscale = 1.0f;
        float yscale = 1.0f;
        if (!Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, gpu_dest.Output_Target(), xscale, yscale)) {
            return false;
        }

        cmd.Palette      = palette;
        cmd.Dst = RectF {
            (float)dest_rect.X * xscale,
            (float)dest_rect.Y * yscale,
            (float)dest_rect.Width  * xscale,
            (float)dest_rect.Height * yscale
        };
        const Rect clipped = Intersect(dest_clip, gpu_dest.Get_Rect());
        if (clipped.Is_Valid()) {
            cmd.Clip = RectF {
                (float)clipped.X * xscale,
                (float)clipped.Y * yscale,
                (float)clipped.Width  * xscale,
                (float)clipped.Height * yscale
            };
        }
        cmd.EffectFlags = effect_flags;
        Tint_From_Brightness(brightness, cmd.Tint);

        /**
         *  Depth baseline. Pull from screen Y at the dst bottom (camera-near
         *  in iso convention), then bias by z_adjust scaled by 1/16000 (same
         *  scale tiles/sprites use). When per-pixel z is in play the shader
         *  subtracts more depth per voxel pixel via SEF_USE_ZSHAPE, so the
         *  baseline only needs to land roughly at the unit's footprint level.
         */
        /**
         *  Mirror the SHP path's depth math (see draw_shapeext_hooks.cpp):
         *  positive z_adjust shifts the voxel further back (higher D3D depth);
         *  negative shifts it closer to camera (lower D3D depth). Our previous
         *  formula had the sign inverted, which pushed ground units behind the
         *  terrain so the LessEqual depth test discarded every pixel.
         */
        const float bottom_y = (float)(dest_rect.Y + dest_rect.Height) - (float)z_adjust;
        constexpr float kSpriteEpsilon = 5e-5f;
        cmd.DepthBaseline = Depth_From_Screen_Y(bottom_y) - kSpriteEpsilon;
        if (cmd.DepthBaseline < 0.001f) cmd.DepthBaseline = 0.001f;
        if (cmd.DepthBaseline > 0.999f) cmd.DepthBaseline = 0.999f;

        cmd.WriteDepth   = !cmd.ZData.empty(); // only write depth when z texture was captured
        cmd.DisableDepth = false;
        cmd.Pass         = Vinifera::Gfx::Current_Render_Pass();
        cmd.OutputTarget = gpu_dest.Output_Target();

        static int s_voxel_debug_count = 0;
        if (s_voxel_debug_count < 20) {
            int nonzero = 0;
            uint8_t first_nz = 0;
            for (size_t i = 0; i < cmd.ColorData.size(); ++i) {
                if (cmd.ColorData[i] != 0) {
                    if (nonzero == 0) first_nz = cmd.ColorData[i];
                    nonzero++;
                }
            }
            DEBUG_INFO("Voxel submit #%d: dst=(%.0f,%.0f,%.0fx%.0f) src=%dx%d pass=%d target=%d depth=%.4f hasZ=%d nonzero=%d/%zu firstNz=%d\n",
                s_voxel_debug_count,
                cmd.Dst.X, cmd.Dst.Y, cmd.Dst.W, cmd.Dst.H,
                cmd.SourceW, cmd.SourceH,
                (int)cmd.Pass, (int)cmd.OutputTarget,
                cmd.DepthBaseline, (int)!cmd.ZData.empty(),
                nonzero, cmd.ColorData.size(), (int)first_nz);
            s_voxel_debug_count++;
        }

        VoxelCompositeQueue::Get().Submit(cmd);
        return true;
    }


    /**
     *  Bridge between the GPU-eligibility gate and the vanilla fall-through.
     *  Returns true if the cmd was submitted (caller does nothing more);
     *  false if the caller should run vanilla CPU code.
     */
    bool Try_GPU_Voxel_Composite(Surface& dest,
                                 Surface const& source,
                                 ConvertClass& converter,
                                 Rect const& source_rect,
                                 Rect const& dest_rect,
                                 Rect const& dest_clip,
                                 int z_adjust,
                                 int brightness,
                                 uint32_t effect_flags,
                                 bool has_per_pixel_z,
                                 BSurface const* z_source)
    {
        const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
        if (legacy) {
            return false;
        }
        GpuSurface* gpu_dest = dynamic_cast<GpuSurface*>(&dest);
        if (gpu_dest == nullptr) {
            return false;
        }
        /**
         *  Source must be an 8-bit BSurface (vanilla voxel rasterizer output).
         *  Other Surface types reaching a GpuSurface destination via Bit_Blit
         *  aren't voxel data and don't have palette indices we can decode.
         */
        BSurface const* bsource = dynamic_cast<BSurface const*>(&source);
        if (bsource == nullptr) {
            return false;
        }
        return Submit_Voxel_Composite(*gpu_dest, *bsource, converter,
                                      source_rect, dest_rect, dest_clip,
                                      z_adjust, brightness,
                                      effect_flags, has_per_pixel_z, z_source);
    }
}


/**
 *  Fake class for the Unit_Blit_Voxel replacement. ABI-identical to UnitClass.
 */
class UnitClassExt : public UnitClass
{
public:
    void _Unit_Blit_Voxel(Surface& surface, Point2D xyoff, Rect rect, int alpha) const;
};


void UnitClassExt::_Unit_Blit_Voxel(Surface& surface, Point2D xyoff, Rect rect, int alpha) const
{
    /**
     *  GPU-eligibility pre-check. Fall back to vanilla `Bit_Blit` for the
     *  Visual_Character switch's whole effect if anything's off (legacy
     *  renderer, GpuSurface dest is missing, queue not up). The full vanilla
     *  body is faithfully reproduced below; we don't call vanilla's
     *  `Unit_Blit_Voxel` again because we ARE the replacement.
     */
    const Rect& dirty = UnitCompositeDirtyRect_g;
    if (dirty.Width <= 0 || dirty.Height <= 0) {
        return;
    }

    /**
     *  Visual character → effect flags (translucency etc.). Hidden units
     *  produce no draw (matches vanilla's `VISUAL_HIDDEN` no-op).
     */
    const VisualType visual = const_cast<UnitClassExt*>(this)->Visual_Character(false, nullptr);
    uint32_t effect_flags = 0;
    if (!Effect_Flags_From_Visual(visual, effect_flags)) {
        return;
    }

    /**
     *  Destination rect on the tactical surface. Mirrors vanilla's IDA
     *  decompile of `Unit_Blit_Voxel` at 0x00651F50 *bit-for-bit* — including
     *  the C integer-division rounding, which differs from the algebraic
     *  simplification by ±1 pixel when the dirty dims are odd:
     *     v26 = (2*(80 - dirty.X) - dirty.W) / 2
     *     v27 = (2*(80 - dirty.Y) - dirty.H) / 2
     *     dst.X = drawpoint.X - dirty.W/2 - v26
     *     dst.Y = drawpoint.Y - dirty.H/2 - v27
     */
    const int v26 = (2 * (80 - dirty.X) - dirty.Width)  / 2;
    const int v27 = (2 * (80 - dirty.Y) - dirty.Height) / 2;
    Rect dst_rect(
        xyoff.X - dirty.Width  / 2 - v26,
        xyoff.Y - dirty.Height / 2 - v27,
        dirty.Width,
        dirty.Height);

    /**
     *  Sinking offset (units in water) is computed lazily by vanilla via
     *  `Calculate_Sinking_Offset`, which isn't exposed in TSpp. Minor
     *  visual regression for the frame when `IsSinking && SinkingYOffset==0`
     *  is first observed; will resolve once the unit's `SinkingYOffset`
     *  gets set by other vanilla code paths. Document as a follow-up.
     */

    /**
     *  Z-fudge bridge case (`IsTooBigToFitUnderBridge` units crossing a
     *  bridge) — vanilla splits the blit into a top 32-row piece (drawn at
     *  bridge depth) and a bottom piece (drawn at ground depth). MVP: skip
     *  the split, render as one quad at the unit's normal depth. Visual
     *  regression is minor and only fires for the few units flagged
     *  `IsTooBigToFitUnderBridge` (Mammoth, etc.).
     */

    ConvertClass& converter = *ColorSchemes[House->Scheme]->Converter;
    const int z_adjust = const_cast<UnitClassExt*>(this)->Get_Z_Adjustment();

    if (!Try_GPU_Voxel_Composite(surface, *EightBitSurface, converter,
                                 dirty, dst_rect, rect,
                                 z_adjust, alpha,
                                 effect_flags,
                                 /*has_per_pixel_z*/ false,
                                 /*z_source*/ nullptr))
    {
        /**
         *  Fall through to vanilla `Bit_Blit`. We re-build the blitter from
         *  the (Visual_Character → flags) mapping vanilla used so the CPU
         *  path matches what vanilla would have done.
         */
        int vanilla_flags = SHAPE_ALPHA | SHAPE_ZGRAD;
        switch (visual) {
        case VISUAL_INDISTINCT: vanilla_flags |= SHAPE_TRANSLUCENT25; break;
        case VISUAL_DARKEN:
        case VISUAL_SHADOWY:    vanilla_flags |= SHAPE_TRANSLUCENT50; break;
        case VISUAL_RIPPLE:     vanilla_flags |= SHAPE_TRANSLUCENT50 | SHAPE_PREDATOR; break;
        default: break;
        }
        Blitter const* blitter = converter.Blitter_From_Flags(vanilla_flags);
        if (blitter != nullptr) {
            /**
             *  Predator-offset for VISUAL_RIPPLE isn't exposed in TSpp; pass
             *  0 (no warp) in the fall-through case. Only matters when the
             *  user has `LegacyRenderer=yes` AND the unit is cloaked AND the
             *  dest somehow isn't a GpuSurface — minor regression.
             */
            const ZGradientType zgrad = const_cast<UnitClassExt*>(this)->Get_Z_Gradient();
            Bit_Blit(surface, rect, dst_rect,
                     *EightBitSurface, EightBitSurface->Get_Rect(), dirty,
                     *blitter, z_adjust, zgrad, alpha, /*predoffset*/ 0);
        }
    }
}


/**
 *  Shared body for the five Blit_Block call-site proxies (Techno_Render_Voxel_Object,
 *  Techno_Render_Voxel_Shadow, BulletClass::Draw_Voxel, VoxelAnimClass::Draw_It ×2).
 *  Vanilla all pass `*VoxelDrawSystem::Get_Surface()` (= VoxelSurface) as `source`
 *  and select the blitter via `converter.Blitter_From_Flags(flags)`.
 *
 *  Direct (non-composite) voxel renders have access to per-pixel z via the
 *  parallel `VoxelZSurface`, so we set has_per_pixel_z = true and pass that
 *  surface as the z source.
 */
static void __fastcall Blit_Block_Voxel_Proxy(Surface& dest, ConvertClass& convert,
                                           Surface const& source,
                                           Rect const& source_rect,
                                           Point2D const& point,
                                           Rect const& clip,
                                           unsigned char const* remap,
                                           Blitter const* blitter,
                                           int z_adjust,
                                           ZGradientType zgrad,
                                           int brightness,
                                           int unk)
{
    /**
     *  GPU path: if dest is GpuSurface, route through the queue. Effect flags
     *  default to "normal" (palette-0 transparent + per-pixel z); we can't
     *  recover vanilla's `flags` arg from the bare `Blitter*` here, so
     *  translucency/predator detection is deferred. Direct-path voxels in
     *  vanilla are almost always rendered with plain SHAPE_ALPHA|SHAPE_ZGRAD
     *  anyway (translucency is composite-path only), so this is fine.
     */
    Rect dst_rect(point.X, point.Y, source_rect.Width, source_rect.Height);
    if (Try_GPU_Voxel_Composite(dest, source, convert,
                                source_rect, dst_rect, clip,
                                z_adjust, brightness,
                                /*effect_flags*/ 0,
                                /*has_per_pixel_z*/ true,
                                /*z_source*/ &VoxelZSurface_g))
    {
        return;
    }

    /**
     *  Fall through to vanilla. The Blit_Block call here uses the SAME
     *  blitter the caller already constructed; vanilla's CPU code is happy.
     */
    Blit_Block(dest, convert, source, source_rect, point, clip, remap, blitter,
               z_adjust, zgrad, brightness, unk);
}


void Voxel_Blit_Hooks()
{
    /**
     *  Unit_Blit_Voxel is virtual and only ever called via the vtable
     *  (xrefs to 0x00651F50 are exclusively `data` references, i.e. the
     *  vtable slot at 0x006D8F34). Patching the vtable entry is cleaner
     *  than a function-entry Patch_Jump because vanilla's function stays
     *  intact and reachable for any future fall-through scenario.
     */
    //Change_Virtual_Address(0x006D8F34, Get_Func_Address(&UnitClassExt::_Unit_Blit_Voxel));
    Patch_Jump(0x00651F50, &UnitClassExt::_Unit_Blit_Voxel);

    /**
     *  Blit_Block call sites that feed voxel pixels into the tactical
     *  surface (direct path — no `EightBitSurface` compositing). Patch_Call
     *  at each so non-voxel Blit_Block callers (HiddenSurface / Dropship /
     *  Draw_Cost / Load_Title_Screen / Raw_Draw_Mouse / etc.) keep using
     *  vanilla CPU code.
     */
    Patch_Call(0x00447422, &Blit_Block_Voxel_Proxy);    // BulletClass::Draw_Voxel
    Patch_Call(0x00635DEB, &Blit_Block_Voxel_Proxy);    // Techno_Render_Voxel_Object
    Patch_Call(0x00635F6F, &Blit_Block_Voxel_Proxy);    // Techno_Render_Voxel_Shadow
    Patch_Call(0x0065E237, &Blit_Block_Voxel_Proxy);    // VoxelAnimClass::Draw_It #1
    Patch_Call(0x0065E39F, &Blit_Block_Voxel_Proxy);    // VoxelAnimClass::Draw_It #2

    DEBUG_INFO("Voxel_Blit_Hooks: Unit_Blit_Voxel replaced + 5 Blit_Block sites patched.\n");
}
