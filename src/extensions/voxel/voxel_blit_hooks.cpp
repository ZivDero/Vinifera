/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU voxel pipeline hooks.
 *
 *          Vanilla TS rasterizes voxels on the CPU into a 256×256 byte buffer
 *          via the chain `Draw_Voxel → Techno_Render_Voxel_Object → VoxelDraw-
 *          System::Render`, then blits the buffer to the tactical surface
 *          with `Blit_Block`. This file installs Patch_Jumps at the outer
 *          per-object wrappers so the entire CPU rasterization path is
 *          skipped — we read the same args, build per-section transforms
 *          from `(matrix, MotLib, VoxelCameraMatrix)`, and submit
 *          `VoxelDrawCmd`s to the GPU queue. Vanilla's CPU `VoxelDrawBuffer`,
 *          `VoxelZSurface`, and `StaticBufferClass` cache stay dormant.
 *
 *          Hook table:
 *            - TechnoClass::Draw_Voxel              0x006354E0
 *            - TechnoClass::Techno_Draw_Voxel_Shadow 0x00635860
 *            - BulletClass::Draw_Voxel              0x004472C0
 *            - VoxelAnimClass::Draw_It              0x0065E050
 *            - UnitClass::Unit_Blit_Voxel           0x00651F50  (no-op for
 *                                                                GpuSurface)
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_blit_hooks.h"

#include "building.h"
#include "buildingtype.h"
#include "bullet.h"
#include "bullettype.h"
#include "colorscheme.h"
#include "convert.h"
#include "debughandler.h"
#include "drawshape.h"
#include "foot.h"
#include "gpu_draw.h"
#include "gpu_surface.h"
#include "gpu_surface_target.h"
#include "graphics_device.h"
#include "hooker.h"
#include "house.h"
#include "map.h"
#include "matrix3d.h"
#include "motionlib.h"
#include "mouse.h"
#include "objecttype.h"
#include "render_pass.h"
#include "shp_cache.h"
#include "sprite_batch.h"
#include "sprite_queue.h"
#include "tactical.h"
#include "techno.h"
#include "tibsun_globals.h"
#include "unit.h"
#include "unit_composite.h"
#include "unit_scratch.h"
#include "unittype.h"
#include "voxel_asset.h"
#include "voxel_effect.h"
#include "voxel_queue.h"
#include "voxelanim.h"
#include "voxelanimtype.h"
#include "voxelinit.h"
#include "voxellib.h"
#include "voxelobj.h"

#include "voxel.hh"


#include <algorithm>
#include <cmath>


using Vinifera::Gfx::GpuRenderTarget;
using Vinifera::Gfx::PaletteCache;
using Vinifera::Gfx::PaletteLUT;
using Vinifera::Gfx::RectF;
using Vinifera::Gfx::RenderPass;
using Vinifera::Gfx::VEF_SHADOW;
using Vinifera::Gfx::VoxelAsset;
using Vinifera::Gfx::VoxelAssetCache;
using Vinifera::Gfx::VoxelDrawCmd;
using Vinifera::Gfx::VoxelEffectParams;
using Vinifera::Gfx::VoxelQueue;
using Vinifera::Gfx::VoxelSectionMesh;
using Vinifera::Gfx::VoxelUnitGroup;
using Vinifera::Gfx::kUnitScratchOrigin;
using Vinifera::Gfx::SpriteDrawCmd;
using Vinifera::Gfx::SpriteQueue;
using Vinifera::Gfx::UnitScratch;


namespace
{
    /**
     *  Capture buffer for turreted-unit composite mode. Vanilla swaps
     *  `LogicalSurface` to a 160x160 `EightBitSurface` and draws every
     *  section (body / turret / barrel — SHP or voxel) at (80,80)-relative
     *  coords; `Unit_Blit_Voxel` then composes the scratch onto the real
     *  tactical surface at the unit's actual screen position `xdrawpoint`.
     *
     *  When we detect we're being called in this composite mode, we capture
     *  each section's args into ONE shared FIFO instead of submitting. The
     *  single queue preserves the interleave between SHP and voxel calls
     *  within a single Titan-style unit (vanilla `unit.cpp:2838-2852` mixes
     *  body shape, optional voxel barrel, turret shape, optional voxel
     *  barrel-above-turret) — parallel queues would lose that layering.
     *  `_Unit_Blit_Voxel` drains the queue in order and replays each entry
     *  at `xyoff + (buffer_dp - (80, 80))`.
     */
    struct PendingVoxelDraw
    {
        VoxelObject const* voxeldata;
        Matrix3D           matrix;
        Point2D            buffer_drawpoint;
        Rect               cliprect;
        unsigned int       frame;
        int                brightness;
        float              alpha;
        int                color_scheme;
        int                z_adjust;

        /**
         *  Predator (VISUAL_RIPPLE) carries through composite-defer to the
         *  GPU queue. Captured at push time so `Composite_Replay` can re-
         *  submit with the same warp offset (the offset reflects the unit
         *  at draw time; reusing the captured value preserves the per-unit
         *  shimmer phase even though replay runs later in the frame).
         */
        bool               is_predator;
        int                predator_warp_pixels;
    };

    struct PendingShapeDraw
    {
        ConvertClass*   convert;
        const ShapeSet* shapefile;
        int             shapenum;
        Point2D         buffer_point;
        ShapeFlags_Type flags;
        int             height_offset;
        ZGradientType   zgrad;
        int             intensity;
        const ShapeSet* z_shapefile;
        int             z_shapenum;
        Point2D         z_off;
    };

    enum class CompositeKind { Voxel, Shape };

    struct PendingComposite
    {
        CompositeKind    kind;
        PendingVoxelDraw voxel;
        PendingShapeDraw shape;
    };

    static std::vector<PendingComposite> g_pending_composite;
    static const Point2D kCompositeOrigin(80, 80);


    /**
     *  GPU-deferred per-unit composite. `Composite_Replay` snapshots one
     *  unit's captured records + drawpoint here; `Composite_Process_Deferred`
     *  drains and renders them later in the GPU pass loop.
     */
    struct DeferredComposite
    {
        std::vector<PendingComposite> records;
        Point2D                        xyoff;
        Rect                           rect;
        ConvertClass*                  shape_convert_override;
    };

    static std::vector<DeferredComposite> g_deferred_composites;

    /**
     *  Vanilla's `Unit_Blit_Voxel` composes the 160x160 scratch onto the real
     *  surface with a ~16 px Y shift baked in (origin not fully traced; the
     *  same constant the voxel transform already adds via `kVoxelYBias` for
     *  non-composite voxels). Voxels in composite mode pick it up automa-
     *  tically through `Build_Section_Params`; SHP replays need it added
     *  explicitly to land in the same place.
     */
    static constexpr int kCompositeYBias = 16;


    /**
     *  Project-wide pixel-to-depth scale shared with tiles + sprites.
     *  See tile_queue.cpp::ZDataDepthScale and the SHP path. Keeping it
     *  identical avoids voxel-vs-tile z-fighting at the same screen-Y.
     */
    constexpr float kPixelToDepth = 1.0f / 16000.0f;


    /**
     *  Brightness 0..2000 (1000 = neutral) -> RGB tint 0..2.0, with the
     *  visual-character alpha folded into the W channel.
     */
    inline void Tint_From_Brightness_And_Alpha(int brightness, float alpha, float out[4])
    {
        const float t = static_cast<float>(brightness) / 1000.0f;
        out[0] = t;
        out[1] = t;
        out[2] = t;
        out[3] = alpha;
    }


    /**
     *  Visual_Character → render-effect descriptor. `alpha` is the
     *  conventional translucency for VISUAL_DARKEN / VISUAL_INDISTINCT /
     *  VISUAL_SHADOWY; `is_predator` flags VISUAL_RIPPLE which routes to
     *  the distortion pass (scene-copy refraction + lerp). Mirrors vanilla
     *  TS's per-state mapping. Returns false for VISUAL_HIDDEN (don't
     *  render at all).
     */
    struct VisualFx
    {
        float alpha       = 1.0f;
        bool  is_predator = false;
    };

    bool VisualFx_From_Visual(VisualType v, VisualFx& out)
    {
        out = VisualFx{};
        switch (v) {
        case VISUAL_NORMAL:
            return true;
        case VISUAL_INDISTINCT:
            out.alpha = 0.75f;
            return true;
        case VISUAL_DARKEN:
        case VISUAL_SHADOWY:
            out.alpha = 0.5f;
            return true;
        case VISUAL_RIPPLE:
            // Alpha unused in the predator path — the PS owns the blend.
            out.alpha       = 1.0f;
            out.is_predator = true;
            return true;
        case VISUAL_HIDDEN:
        default:
            return false;
        }
    }

    /**
     *  Apply only the rotation part of an affine Matrix3D to a Vector3
     *  (no translation). Used to transform `VoxelLightSource` from world
     *  space into voxel-local space — the shader's normal lookup is in
     *  voxel-local frame and the dot product needs a matching basis.
     *
     *  Matrix3D in TS is row-major 3x4 (last column = translation).
     */
    Vector3 Rotate_Vector(const Matrix3D& m, const Vector3& v)
    {
        Vector3 r;
        r.X = m[0][0] * v.X + m[0][1] * v.Y + m[0][2] * v.Z;
        r.Y = m[1][0] * v.X + m[1][1] * v.Y + m[1][2] * v.Z;
        r.Z = m[2][0] * v.X + m[2][1] * v.Y + m[2][2] * v.Z;
        return r;
    }


    /**
     *  Inverse-rotate for an orthonormal rotation (transpose). HVA
     *  matrices are TS's animation-frame transforms and are orthonormal
     *  per axis; for non-orthonormal locomotion frames this is an
     *  approximation but errors are small at our quantization.
     */
    Vector3 Inverse_Rotate_Vector(const Matrix3D& m, const Vector3& v)
    {
        Vector3 r;
        r.X = m[0][0] * v.X + m[1][0] * v.Y + m[2][0] * v.Z;
        r.Y = m[0][1] * v.X + m[1][1] * v.Y + m[2][1] * v.Z;
        r.Z = m[0][2] * v.X + m[1][2] * v.Y + m[2][2] * v.Z;
        return r;
    }


    /**
     *  Build VoxelEffectParams for one section. `section_world` is the
     *  unit's world matrix already composed with the HVA frame matrix.
     *  `mesh` supplies XSize/YSize/ZSize and the 8 projected box corners.
     */
    void Build_Section_Params(const VoxelSectionMesh& mesh,
                              const Matrix3D& section_world,
                              const Point2D& point,
                              int brightness,
                              float alpha,
                              bool is_shadow,
                              int z_adjust,
                              VoxelEffectParams& out)
    {
        const Matrix3D final_mtx = Vinifera::Gfx::Device != nullptr
                                 ? (VoxelCameraMatrix * section_world)
                                 : section_world;

        /**
         *  Project the four box corners we need (plus the opposite of the
         *  origin for centroid math). Vanilla's iso matrix maps voxel +Z
         *  to positive screen-space Y, so `Prep_For_Object` (voxdrsys.cpp:190)
         *  negates each projected Y to bring voxel-up to the negative-Y
         *  (top-of-screen) half. Mirror that here.
         */
        auto project = [&](VoxelBoundsType bound_idx) {
            Vector3 v = final_mtx * mesh.Bounds[bound_idx];
            v.Y = -v.Y;
            return v;
        };
        const Vector3 c0    = project(VOXEL_BOUNDS_BBL);
        const Vector3 cx    = project(VOXEL_BOUNDS_BBR);
        const Vector3 cy    = project(VOXEL_BOUNDS_BFL);
        const Vector3 cz    = project(VOXEL_BOUNDS_TBL);
        const Vector3 c_opp = project(VOXEL_BOUNDS_TFR);

        // Centroid of the projected bounding box, used only for depth
        // normalization. Screen-space X/Y do NOT use this offset: vanilla's
        // VoxelDrawSystem::Render builds region.Point such that the centroid
        // offset cancels through the buffer-to-screen blit, so the on-screen
        // position of voxel grid (0,0,0) reduces to `point + projected_BBL`.
        // See voxdrsys.cpp:336-341 — region.Point.X = center.X - width/2 - 4
        // and region.Bounds.X = 128 - width/2 - 4 cancel the `+128 - center.X`
        // applied in Render_Object when computing screen position.
        const Vector3 center = (c0 + c_opp) * 0.5f;

        /**
         *  Debug: dump projection details for the first few non-shadow voxel
         *  submissions so we can see actual matrix and projected-corner values.
         */
        static int s_debug_count = 0;
        if (!is_shadow && s_debug_count < 4) {
            DEBUG_INFO("VoxelProj #%d: point=(%d,%d) Xs=%d Ys=%d Zs=%d nt=%d\n"
                       "  section_world:\n"
                       "    [%.3f %.3f %.3f %.3f]\n"
                       "    [%.3f %.3f %.3f %.3f]\n"
                       "    [%.3f %.3f %.3f %.3f]\n"
                       "  BBL_local=(%.2f,%.2f,%.2f) -> proj_c0=(%.2f,%.2f,%.2f)\n"
                       "  BBR_local=(%.2f,%.2f,%.2f) -> proj_cx=(%.2f,%.2f,%.2f)\n"
                       "  BFL_local=(%.2f,%.2f,%.2f) -> proj_cy=(%.2f,%.2f,%.2f)\n"
                       "  TBL_local=(%.2f,%.2f,%.2f) -> proj_cz=(%.2f,%.2f,%.2f)\n"
                       "  center=(%.2f,%.2f,%.2f)\n",
                       s_debug_count, point.X, point.Y,
                       static_cast<int>(mesh.XSize), static_cast<int>(mesh.YSize), static_cast<int>(mesh.ZSize), static_cast<int>(mesh.NormalType),
                       section_world[0][0], section_world[0][1], section_world[0][2], section_world[0][3],
                       section_world[1][0], section_world[1][1], section_world[1][2], section_world[1][3],
                       section_world[2][0], section_world[2][1], section_world[2][2], section_world[2][3],
                       mesh.Bounds[VOXEL_BOUNDS_BBL].X, mesh.Bounds[VOXEL_BOUNDS_BBL].Y, mesh.Bounds[VOXEL_BOUNDS_BBL].Z,
                       c0.X, c0.Y, c0.Z,
                       mesh.Bounds[VOXEL_BOUNDS_BBR].X, mesh.Bounds[VOXEL_BOUNDS_BBR].Y, mesh.Bounds[VOXEL_BOUNDS_BBR].Z,
                       cx.X, cx.Y, cx.Z,
                       mesh.Bounds[VOXEL_BOUNDS_BFL].X, mesh.Bounds[VOXEL_BOUNDS_BFL].Y, mesh.Bounds[VOXEL_BOUNDS_BFL].Z,
                       cy.X, cy.Y, cy.Z,
                       mesh.Bounds[VOXEL_BOUNDS_TBL].X, mesh.Bounds[VOXEL_BOUNDS_TBL].Y, mesh.Bounds[VOXEL_BOUNDS_TBL].Z,
                       cz.X, cz.Y, cz.Z,
                       center.X, center.Y, center.Z);
            s_debug_count++;
        }

        // T0: where voxel (0,0,0) (= BBL corner in voxel grid) lands on
        // screen. vanilla's buffer→screen blit cancels the centroid offset
        // (see comment on `center` above), so screen pos = point + projected
        // BBL with no centroid subtraction. For T0.Z we DO subtract the
        // centroid so voxel_z = 0 at the section centroid (for depth).
        //
        // TUNABLE: kVoxelYBias shifts every voxel down on screen. Empirically
        // observed ~17 px constant offset across unit types. Source not yet
        // traced through vanilla — likely a fixed offset in the voxel-art
        // pipeline or rendering hook we haven't found. Constant means it's
        // probably tied to a static value (LEVEL_PIXEL_H, half tile height,
        // etc.) rather than per-section bounds.
        constexpr float kVoxelYBias = 16.0f;
        out.T0[0] = static_cast<float>(point.X) + c0.X;
        out.T0[1] = static_cast<float>(point.Y) + c0.Y + kVoxelYBias;
        // T0.z = ABSOLUTE projected iso_z of BBL (no centroid subtraction).
        // This makes voxel_z in the shader an absolute iso_z value shared
        // across all sections of the unit, so a turret sitting on top of
        // the body gets larger voxel_z than the body's voxels and sorts in
        // front. With per-section centroid relative voxel_z, both sections'
        // voxel_z ranges were centered on zero and inter-section ordering
        // was arbitrary.
        out.T0[2] = c0.Z;
        // T0.w = unit drawpoint Y in screen pixels. Shader uses this as the
        // depth baseline so every voxel of a section shares one base depth,
        // anchored to the unit's drawpoint rather than its individual screen
        // pixel. That's what makes inter-unit sorting work: two units at
        // different drawpoints get distinct depth ranges that don't intermix
        // at overlapping pixels — without this, back-unit top voxels would
        // beat front-unit body voxels because they happen to have a larger
        // voxel_z at the same pixel.
        out.T0[3] = static_cast<float>(point.Y);

        // Shadow: shift the entire shadow along the shadow-light vector so the
        // shadow falls away from the unit in the light direction (vanilla
        // applies this in Prep_For_Shadow — see voxdrsys.cpp:155). The light
        // vector is in iso-projected screen space; vanilla applies the Y-flip
        // AFTER adding the light, so we negate Y here to match (our corners
        // are already Y-flipped). Per-voxel deltas T1/T2/T3 don't change since
        // the light offset is the same for every corner and cancels out.
        if (is_shadow) {
            out.T0[0] += VoxelShadowLightSource.X;
            out.T0[1] += -VoxelShadowLightSource.Y;
        }

        // T1/T2/T3: per-voxel-step deltas along the section's X/Y/Z axes.
        const float x_size = static_cast<float>(std::max<int>(1, mesh.XSize));
        const float y_size = static_cast<float>(std::max<int>(1, mesh.YSize));
        const float z_size = static_cast<float>(std::max<int>(1, mesh.ZSize));

        out.T1[0] = (cx.X - c0.X) / x_size;
        out.T1[1] = (cx.Y - c0.Y) / x_size;
        out.T1[2] = (cx.Z - c0.Z) / x_size;
        out.T1[3] = 0.0f;

        out.T2[0] = (cy.X - c0.X) / y_size;
        out.T2[1] = (cy.Y - c0.Y) / y_size;
        out.T2[2] = (cy.Z - c0.Z) / y_size;
        out.T2[3] = 0.0f;

        out.T3[0] = (cz.X - c0.X) / z_size;
        out.T3[1] = (cz.Y - c0.Y) / z_size;
        out.T3[2] = (cz.Z - c0.Z) / z_size;
        out.T3[3] = 0.0f;

        // Lighting in voxel-local space (inverse-rotated through section_world).
        const Vector3 light_local = Inverse_Rotate_Vector(section_world, VoxelLightSource);
        const float light_len = std::sqrt(light_local.X * light_local.X
                                        + light_local.Y * light_local.Y
                                        + light_local.Z * light_local.Z);
        const float inv_len = light_len > 1e-5f ? (1.0f / light_len) : 1.0f;
        out.LightDir[0] = light_local.X * inv_len;
        out.LightDir[1] = light_local.Y * inv_len;
        out.LightDir[2] = light_local.Z * inv_len;

        // Normal table base offset — slots 0..3 of the packed normals buffer
        // hold VoxelNormals1..4 (one slot per `VoxelNormalType` enum value
        // 1..4; NORMAL_NONE=0 has no data). 256-entry strides.
        const int normal_type = mesh.NormalType;
        out.LightDir[3] = static_cast<float>(std::max(0, normal_type - 1) * 256);

        Tint_From_Brightness_And_Alpha(brightness, alpha, out.Tint);

        // Misc.x = z_adjust in pixels (vanilla SHP convention: negative for
        // elevated objects, returned by TechnoClass::Get_Z_Adjustment()). The
        // shader adds `Misc.x * Misc.y` to depth so a flying unit's voxels
        // sort at the depth of the ground cell underneath rather than where
        // their iso-projected screen_y happens to land. Shadows are on the
        // ground regardless of altitude, so the shadow path passes 0.
        //
        // Misc.y = pixel-to-depth scale (1/16000) shared with tiles/sprites.
        //
        // Misc.z is dual-use depending on path:
        //   - object path: per-section kObjectEps. With drawpoint-anchored
        //     base (depth = 1 - drawpoint_Y * Misc.y - kObjEps - voxel_z*...),
        //     kObjEps must cover (a) the screen-Y distance from drawpoint to
        //     the section's lowest projected pixel and (b) the worst-case
        //     |voxel_z| contribution. Without that, voxels below the draw-
        //     point would land at depth > terrain-at-that-pixel and get
        //     occluded by terrain.
        //   - shadow path: 0.5, used as the shadow's output alpha.
        //
        // Misc.w = bitflags (VEF_SHADOW = 1).
        // kObjEps must satisfy depth < terrain_at_pixel for every voxel of
        // the section. The constraint at each corner is:
        //   kObjEps > screen_y_offset / 16000 + max(0, -voxel_z) * kVZS
        // (the second term accounts for back voxels whose negative absolute
        // iso_z adds to depth via the -voxel_z*kVZS term in the shader).
        // Take the max over all 8 corners.
        // Keep in lockstep with the HLSL voxel shader's kVoxelZScale —
        // both must use the same value so the per-section kObjectEps we
        // compute here actually covers the depth range the shader emits.
        // Bumped from 1e-5 to 1e-4 to give voxel-vs-voxel within a section
        // enough depth headroom to resolve iso-projection ties; the old
        // value was barely above float precision near depth=0.98, leading
        // to z-fighting "dithering" on dense voxel surfaces like the
        // Disruptor's turret.
        const float kVoxelZScale = 1.0e-4f;
        float max_eps_needed = 0.0f;
        for (int i = 0; i < VOXEL_BOUNDS_MAX; ++i) {
            Vector3 v = final_mtx * mesh.Bounds[i];
            const float screen_y_offset = -v.Y + kVoxelYBias;
            const float back_z_contribution = std::max(0.0f, -v.Z) * kVoxelZScale;
            const float eps_this_corner = std::max(0.0f, screen_y_offset) * kPixelToDepth + back_z_contribution;
            max_eps_needed = std::max(eps_this_corner, max_eps_needed);
        }
        const float kObjectEps_section = max_eps_needed + 1.0e-4f;

        out.Misc[0] = is_shadow ? 0.0f : static_cast<float>(z_adjust);
        out.Misc[1] = kPixelToDepth;
        out.Misc[2] = is_shadow ? 0.5f : kObjectEps_section;
        out.Misc[3] = is_shadow ? static_cast<float>(VEF_SHADOW) : 0.0f;
    }


    /**
     *  Submit all sections (or one) of a voxel object to the GPU queue.
     */
    void Submit_Voxel_Object(VoxelObject const& voxeldata,
                             unsigned int frame,
                             const Matrix3D& matrix,
                             const Point2D& point,
                             const Rect& cliprect,
                             ConvertClass& converter,
                             int brightness,
                             float alpha,
                             int z_adjust,
                             int single_layer = -1,
                             bool is_predator = false,
                             int predator_warp_pixels = 0)
    {
        if (Vinifera::Gfx::Device == nullptr) return;
        if (!VoxelQueue::Get().Is_Initialized()) return;

        VoxelLibraryClass* voxlib = voxeldata.VoxelLibrary;
        MotionLibraryClass* motlib = voxeldata.MotionLibrary;
        if (voxlib == nullptr || voxlib->Load_Failed()) return;

        VoxelAsset* asset = VoxelAssetCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, voxlib);
        if (asset == nullptr) return;

        PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, &converter);
        if (palette == nullptr) return;

        /**
         *  Predator commands always flush in PostEffects — they sample
         *  SceneCopy which is only valid after the main scene is rendered.
         *  Non-predator commands honor the current render-pass context.
         */
        const RenderPass pass = is_predator
                              ? RenderPass::PostEffects
                              : Vinifera::Gfx::Current_Render_Pass();

        float xscale = 1.0f, yscale = 1.0f;
        Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, GpuRenderTarget::Scene, xscale, yscale);

        RectF clip = {};
        if (cliprect.Width > 0 && cliprect.Height > 0) {
            clip = RectF {
                static_cast<float>(cliprect.X) * xscale,
                static_cast<float>(cliprect.Y) * yscale,
                static_cast<float>(cliprect.Width)  * xscale,
                static_cast<float>(cliprect.Height) * yscale
            };
        }

        const int layer_count = voxlib->Get_Layer_Count();
        const int frame_lo = (single_layer >= 0) ? single_layer : 0;
        const int frame_hi = (single_layer >= 0) ? (single_layer + 1) : layer_count;

        /**
         *  Decide whether this draw goes through the composite (scratch RT)
         *  path. Composite avoids per-pixel depth-blend compounding for
         *  units where multiple voxels rasterize to the same screen pixel
         *  (multi-section) and for translucent units where any compounding
         *  would push effective alpha toward fully opaque.
         *
         *  Composite trigger: section count > 1 OR alpha < 1.
         *
         *  Bullets / debris / single-section opaque voxels stay on the fast
         *  batched path (no scratch RT overhead).
         *
         *  Predator units don't currently use the scratch path — the warp
         *  pipeline samples SceneCopy directly. If we re-enable predator
         *  later this routing may need to merge.
         */
        const int draw_section_count = frame_hi - frame_lo;
        const bool is_composite = !is_predator
                               && (draw_section_count > 1 || alpha < 0.9999f);

        int unit_group_id = -1;
        if (is_composite) {
            /**
             *  Compute the unit's scene-side depth from its drawpoint Y,
             *  same math used by single-voxel rendering. This is the
             *  depth the composite blit tests against scene depth so the
             *  unit gets occluded by closer geometry.
             */
            const float unit_depth = std::clamp(
                1.0f - static_cast<float>(point.Y) * kPixelToDepth - 1.0e-4f,
                1.0e-4f, 0.9999f);
            VoxelUnitGroup group;
            group.Drawpoint  = point;
            group.Alpha      = alpha;
            group.SceneDepth = unit_depth;
            group.Clip       = clip;
            unit_group_id = VoxelQueue::Get().Allocate_Unit_Group(group);
        }

        for (int layer = frame_lo; layer < frame_hi && layer < layer_count; ++layer) {
            const VoxelSectionMesh* mesh = asset->Get_Section(layer, 0);
            if (mesh == nullptr || mesh->VertexCount == 0) continue;

            Matrix3D section_world = matrix;
            if (motlib != nullptr && !motlib->Load_Failed()) {
                /**
                 *  MotionLibrary stores `LayerMatrices[layer + Get_Section_Count() * frame]`
                 *  per the source drop. TSpp's accessor naming is inverted but the
                 *  underlying memory layout is the same.
                 */
                const Matrix3D* matrices = &motlib->Get_Layer_Matrices();
                const int section_count = motlib->Get_Section_Count();   // = vanilla "LayerCount"
                const int frame_count   = motlib->Get_Layer_Count();     // = vanilla "FrameCount"
                if (section_count > 0 && frame_count > 0) {
                    const int safe_frame = static_cast<int>(frame % (unsigned)frame_count);
                    const Matrix3D& hva = matrices[layer + section_count * safe_frame];
                    section_world = matrix * hva;
                }
            }

            VoxelDrawCmd cmd;
            cmd.Mesh         = mesh;
            cmd.Palette      = palette;
            cmd.IsShadow     = false;
            cmd.WriteDepth   = true;
            cmd.DisableDepth = false;
            cmd.Clip         = clip;
            cmd.Pass         = pass;
            cmd.OutputTarget = GpuRenderTarget::Scene;

            /**
             *  For composite cmds, force per-section alpha to 1.0. The scratch
             *  RT receives opaque writes (each section's fragment fully wins
             *  the depth test); the unit's overall translucency is applied
             *  once at composite-blit time. This is what avoids the per-
             *  voxel-blend compounding.
             */
            const float section_alpha = is_composite ? 1.0f : alpha;
            Build_Section_Params(*mesh, section_world, point, brightness, section_alpha,
                                 /*is_shadow*/ false, z_adjust, cmd.Params);

            if (is_composite) {
                /**
                 *  Re-target the section's screen origin to scratch-local
                 *  coords. T0.x/y currently encode the absolute scene
                 *  drawpoint; subtract `(drawpoint - scratch_origin)` so
                 *  voxel (0,0,0) lands at the scratch's local origin.
                 *  T0.w (unit_y, used for depth baseline) keeps the real
                 *  drawpoint Y — depth math is unchanged, only the screen
                 *  projection shifts to local space.
                 */
                const float dx = static_cast<float>(point.X) - static_cast<float>(kUnitScratchOrigin.X);
                const float dy = static_cast<float>(point.Y) - static_cast<float>(kUnitScratchOrigin.Y);
                cmd.Params.T0[0] -= dx;
                cmd.Params.T0[1] -= dy;
                cmd.UnitGroupID   = unit_group_id;
            }

            /**
             *  Stamp predator state. Build_Section_Params doesn't know about
             *  predator (the shader-side fields live in Params.Predator),
             *  so we fill those slots and the cmd-level flag here.
             */
            if (is_predator) {
                cmd.IsPredator         = true;
                cmd.PredatorWarpPixels = predator_warp_pixels;
                cmd.PredatorBlendRatio = 0.5f;   // vanilla VISUAL_RIPPLE = 50/50
                cmd.Params.Predator[0] = static_cast<float>(predator_warp_pixels);
                cmd.Params.Predator[1] = cmd.PredatorBlendRatio;
                // Predator[2..3] (scene_w/h) patched in by VoxelQueue at issue.
            }

            VoxelQueue::Get().Submit(cmd);
        }
    }


    /**
     *  Render a voxel object's sections IMMEDIATELY into the currently-bound
     *  RT (the unit scratch). Used by the composite-replay path so voxel and
     *  SHP parts interleave in their captured submission order rather than
     *  going through the two deferred queues (which flush at different times
     *  and lose cross-queue ordering).
     *
     *  Mirrors `Submit_Voxel_Object`'s section iteration but with:
     *    - T0 shifted to scratch-local origin (so voxel (0,0,0) lands at
     *      kUnitScratchOrigin)
     *    - alpha forced to 1.0 per-section (the unit's overall translucency
     *      is applied once at composite-blit time)
     *    - immediate `Render_Cmd_Immediate` instead of queue submission
     *
     *  Returns the unit's drawpoint (for the caller to assemble the composite
     *  blit's scene origin).
     */
    void Render_Voxel_Object_To_Scratch_Immediate(VoxelObject const& voxeldata,
                                                  unsigned int frame,
                                                  const Matrix3D& matrix,
                                                  const Point2D& point,
                                                  const Rect& /*cliprect*/,
                                                  ConvertClass& converter,
                                                  int brightness,
                                                  int z_adjust,
                                                  int single_layer = -1)
    {
        if (Vinifera::Gfx::Device == nullptr) return;
        if (!VoxelQueue::Get().Is_Initialized()) return;

        VoxelLibraryClass* voxlib = voxeldata.VoxelLibrary;
        MotionLibraryClass* motlib = voxeldata.MotionLibrary;
        if (voxlib == nullptr || voxlib->Load_Failed()) return;

        VoxelAsset* asset = VoxelAssetCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, voxlib);
        if (asset == nullptr) return;

        PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, &converter);
        if (palette == nullptr) return;

        const int layer_count = voxlib->Get_Layer_Count();
        const int frame_lo = (single_layer >= 0) ? single_layer : 0;
        const int frame_hi = (single_layer >= 0) ? (single_layer + 1) : layer_count;

        for (int layer = frame_lo; layer < frame_hi && layer < layer_count; ++layer) {
            const VoxelSectionMesh* mesh = asset->Get_Section(layer, 0);
            if (mesh == nullptr || mesh->VertexCount == 0) continue;

            Matrix3D section_world = matrix;
            if (motlib != nullptr && !motlib->Load_Failed()) {
                const Matrix3D* matrices = &motlib->Get_Layer_Matrices();
                const int section_count = motlib->Get_Section_Count();
                const int frame_count   = motlib->Get_Layer_Count();
                if (section_count > 0 && frame_count > 0) {
                    const int safe_frame = static_cast<int>(frame % (unsigned)frame_count);
                    const Matrix3D& hva = matrices[layer + section_count * safe_frame];
                    section_world = matrix * hva;
                }
            }

            VoxelDrawCmd cmd;
            cmd.Mesh         = mesh;
            cmd.Palette      = palette;
            cmd.IsShadow     = false;
            cmd.WriteDepth   = true;
            cmd.DisableDepth = false;
            cmd.OutputTarget = GpuRenderTarget::Scene;   // unused on immediate path

            /**
             *  `point` is the section's SCRATCH-LOCAL position (caller did
             *  the buffer_drawpoint → scratch translation). Build_Section_
             *  Params writes T0.x = point.X + c0.X directly; no further
             *  shift needed. Sections at different buffer_drawpoints
             *  produce distinct scratch positions, preserving the per-
             *  section layout from the vanilla composite capture.
             */
            Build_Section_Params(*mesh, section_world, point, brightness,
                                 /*alpha*/ 1.0f, /*is_shadow*/ false, z_adjust, cmd.Params);

            VoxelQueue::Get().Render_Cmd_Immediate(*Vinifera::Gfx::Device, cmd,
                                                    Vinifera::Gfx::kUnitScratchWidth,
                                                    Vinifera::Gfx::kUnitScratchHeight,
                                                    /*is_sidebar*/ false);
        }
    }


    /**
     *  SHAPE_TRANSLUCENT* → effective alpha. Same masked-compare we use in
     *  gpu_draw.cpp; SHAPE_TRANSLUCENT75 = bit1|bit2 so independent bit-tests
     *  miscompare. Single 2-bit-field decode.
     */
    float Alpha_From_Sprite_Flags(ShapeFlags_Type flags)
    {
        const unsigned tmask = static_cast<unsigned>(flags) & static_cast<unsigned>(SHAPE_TRANSLUCENT75);
        if (tmask == static_cast<unsigned>(SHAPE_TRANSLUCENT75))      return 0.25f;
        if (tmask == static_cast<unsigned>(SHAPE_TRANSLUCENT50))      return 0.5f;
        if (tmask == static_cast<unsigned>(SHAPE_TRANSLUCENT25))      return 0.75f;
        return 1.0f;
    }


    /**
     *  Submit a single section as a flat ground-projected shadow.
     */
    void Submit_Voxel_Shadow(VoxelObject const& voxeldata,
                             int layer_index,
                             const Matrix3D& matrix,
                             const Point2D& point,
                             const Rect& cliprect,
                             ConvertClass& converter)
    {
        if (Vinifera::Gfx::Device == nullptr) return;
        if (!VoxelQueue::Get().Is_Initialized()) return;

        VoxelLibraryClass* voxlib = voxeldata.VoxelLibrary;
        if (voxlib == nullptr || voxlib->Load_Failed()) return;

        VoxelAsset* asset = VoxelAssetCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, voxlib);
        if (asset == nullptr) return;

        PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(*Vinifera::Gfx::Device, &converter);
        if (palette == nullptr) return;

        const VoxelSectionMesh* mesh = asset->Get_Section(layer_index, 0);
        if (mesh == nullptr || mesh->ShadowVertexCount == 0) return;

        float xscale = 1.0f, yscale = 1.0f;
        Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, GpuRenderTarget::Scene, xscale, yscale);

        RectF clip = {};
        if (cliprect.Width > 0 && cliprect.Height > 0) {
            clip = RectF {
                static_cast<float>(cliprect.X) * xscale,
                static_cast<float>(cliprect.Y) * yscale,
                static_cast<float>(cliprect.Width)  * xscale,
                static_cast<float>(cliprect.Height) * yscale
            };
        }

        // Apply HVA frame 0 to the matrix — vanilla's Techno_Render_Voxel_Shadow
        // does `mtx2 = matrix * MotLib->Get_Layer_Matrix(layer_index, 0)` and
        // passes mtx2 as the `motion` arg to Prep_For_Shadow. Without this,
        // sections whose HVA frame 0 has non-identity translation render the
        // shadow at an offset position.
        MotionLibraryClass* motlib = voxeldata.MotionLibrary;
        Matrix3D section_world = matrix;
        if (motlib != nullptr && !motlib->Load_Failed()) {
            const Matrix3D* matrices = &motlib->Get_Layer_Matrices();
            const int section_count = motlib->Get_Section_Count();
            const int frame_count   = motlib->Get_Layer_Count();
            if (section_count > 0 && frame_count > 0
                && layer_index >= 0 && layer_index < section_count) {
                const Matrix3D& hva = matrices[layer_index];   // frame 0
                section_world = matrix * hva;
            }
        }

        VoxelDrawCmd cmd;
        cmd.Mesh         = mesh;
        cmd.Palette      = palette;
        cmd.IsShadow     = true;
        cmd.WriteDepth   = false;
        cmd.DisableDepth = false;
        cmd.Clip         = clip;
        cmd.Pass         = Vinifera::Gfx::Current_Render_Pass();
        cmd.OutputTarget = GpuRenderTarget::Scene;

        Build_Section_Params(*mesh, section_world, point, /*brightness*/ 500, /*alpha*/ 1.0f,
                             /*is_shadow*/ true, /*z_adjust*/ 0, cmd.Params);

        VoxelQueue::Get().Submit(cmd);
    }
}


/**
 *  Composite queue API. Implementations live outside the anonymous namespace
 *  so they can satisfy the external-linkage declarations in
 *  `unit_composite.h`, but they freely reference the anon-namespace storage
 *  (`g_pending_composite`, `kCompositeOrigin`) and the file-local helpers
 *  (`Submit_Voxel_Object`).
 */
void Composite_Push_Voxel(VoxelObject const& voxeldata,
                          unsigned int       frame,
                          const Matrix3D&    matrix,
                          const Point2D&     buffer_drawpoint,
                          const Rect&        cliprect,
                          int                brightness,
                          float              alpha,
                          int                color_scheme,
                          int                z_adjust,
                          bool               is_predator,
                          int                predator_warp_pixels)
{
    PendingComposite rec;
    rec.kind = CompositeKind::Voxel;
    rec.voxel.voxeldata            = &voxeldata;
    rec.voxel.matrix               = matrix;
    rec.voxel.buffer_drawpoint     = buffer_drawpoint;
    rec.voxel.cliprect             = cliprect;
    rec.voxel.frame                = frame;
    rec.voxel.brightness           = brightness;
    rec.voxel.alpha                = alpha;
    rec.voxel.color_scheme         = color_scheme;
    rec.voxel.z_adjust             = z_adjust;
    rec.voxel.is_predator          = is_predator;
    rec.voxel.predator_warp_pixels = predator_warp_pixels;
    g_pending_composite.push_back(rec);
}


void Composite_Push_Shape(ConvertClass*       convert,
                          const ShapeSet*     shapefile,
                          int                 shapenum,
                          const Point2D&      buffer_point,
                          ShapeFlags_Type     flags,
                          int                 height_offset,
                          ZGradientType       zgrad,
                          int                 intensity,
                          const ShapeSet*     z_shapefile,
                          int                 z_shapenum,
                          const Point2D&      z_off)
{
    PendingComposite rec;
    rec.kind = CompositeKind::Shape;
    rec.shape.convert       = convert;
    rec.shape.shapefile     = shapefile;
    rec.shape.shapenum      = shapenum;
    rec.shape.buffer_point  = buffer_point;
    rec.shape.flags         = flags;
    rec.shape.height_offset = height_offset;
    rec.shape.zgrad         = zgrad;
    rec.shape.intensity     = intensity;
    rec.shape.z_shapefile   = z_shapefile;
    rec.shape.z_shapenum    = z_shapenum;
    rec.shape.z_off         = z_off;
    g_pending_composite.push_back(rec);
}


bool Composite_Is_Empty()
{
    return g_pending_composite.empty();
}


void Composite_Replay(Surface&       dst_surface,
                      Point2D        xyoff,
                      const Rect&    rect,
                      ConvertClass*  shape_convert_override)
{
    if (g_pending_composite.empty()) {
        return;
    }

    /**
     *  Non-GpuSurface destination (cameos, hidden buffers): we'd need to
     *  fall back to vanilla CPU compositing — out of scope. Just drop the
     *  captures to avoid them leaking into the next composite.
     */
    GpuSurface* gpu_dest = dynamic_cast<GpuSurface*>(&dst_surface);
    if (gpu_dest == nullptr) {
        g_pending_composite.clear();
        return;
    }

    /**
     *  Predator (VISUAL_RIPPLE) routing. If any captured voxel record is
     *  flagged as predator, treat the WHOLE unit as predator: re-issue each
     *  record through the normal deferred queues. Voxel records go through
     *  Submit_Voxel_Object with `is_predator=true` — that routes them to
     *  VoxelQueue with `Pass=PostEffects + IsPredator=true`, and VoxelQueue's
     *  PostEffects branch renders them via `VoxelDistortionEffect` which
     *  samples SceneCopy with the warp offset (vanilla's per-pixel
     *  `BlitTransLucent*ZReadWarp` equivalent).
     *
     *  Why bypass the scratch path for predator units:
     *    - The distortion shader needs `SceneCopy` (captured at the start
     *      of PostEffects). The scratch composite would happen earlier in
     *      the pass loop, before SceneCopy is valid.
     *    - The distortion shader's per-pixel SceneCopy sample relies on
     *      `SV_Position.xy` being scene-space pixels. Rendering into a
     *      256x256 scratch would give scratch-local pixels instead, so
     *      we'd be sampling SceneCopy from the wrong location.
     *    - Vanilla itself doesn't compose predator voxels through the
     *      EightBitSurface scratch — it runs the warp blitter directly
     *      on the final scene buffer (see unit.cpp:2459-2470).
     *
     *  SHP records in a predator unit re-issue via `Draw_Shape` (which
     *  routes back through the function-entry intercept), so cloaked SHP
     *  parts stay on the SpriteQueue's normal translucent path.
     */
    bool unit_is_predator = false;
    for (const PendingComposite& rec : g_pending_composite) {
        if (rec.kind == CompositeKind::Voxel && rec.voxel.is_predator) {
            unit_is_predator = true;
            break;
        }
    }

    if (unit_is_predator) {
        for (const PendingComposite& rec : g_pending_composite) {
            if (rec.kind == CompositeKind::Voxel) {
                const PendingVoxelDraw& p = rec.voxel;
                if (p.voxeldata == nullptr) continue;
                ColorScheme* scheme = ColorSchemes[p.color_scheme];
                if (scheme == nullptr || scheme->Converter == nullptr) continue;

                const Point2D real_point(
                    xyoff.X + (p.buffer_drawpoint.X - kCompositeOrigin.X),
                    xyoff.Y + (p.buffer_drawpoint.Y - kCompositeOrigin.Y));

                Submit_Voxel_Object(*p.voxeldata, p.frame, p.matrix, real_point, rect,
                                    *scheme->Converter, p.brightness, p.alpha, p.z_adjust,
                                    /*single_layer*/ -1,
                                    p.is_predator, p.predator_warp_pixels);
            } else {
                const PendingShapeDraw& p = rec.shape;
                if (p.shapefile == nullptr) continue;
                ConvertClass* convert = shape_convert_override != nullptr
                                      ? shape_convert_override
                                      : p.convert;
                if (convert == nullptr) continue;

                const Point2D real_point(
                    xyoff.X + (p.buffer_point.X - kCompositeOrigin.X),
                    xyoff.Y + (p.buffer_point.Y - kCompositeOrigin.Y) + kCompositeYBias);

                const ShapeFlags_Type replay_flags =
                    p.flags & ~SHAPE_WIN_REL;

                Draw_Shape(dst_surface, *convert, p.shapefile, p.shapenum,
                           real_point, rect, replay_flags,
                           /*remap*/ nullptr,
                           p.height_offset, p.zgrad, p.intensity,
                           p.z_shapefile, p.z_shapenum, p.z_off);
            }
        }
        g_pending_composite.clear();
        return;
    }

    /**
     *  Non-predator composite (Titan, Disruptor, HMLRS, etc). Defer to the
     *  scratch-RT composite path — runs in the GPU pass loop where the
     *  scene RT is bound, terrain depth is established, and we can do
     *  unified body+turret rendering with correct paint order.
     */
    DeferredComposite deferred;
    deferred.records                = g_pending_composite;
    deferred.xyoff                  = xyoff;
    deferred.rect                   = rect;
    deferred.shape_convert_override = shape_convert_override;
    g_deferred_composites.push_back(std::move(deferred));
    g_pending_composite.clear();
}


/**
 *  Internal: render one deferred unit composite. Bound to be called from
 *  `Composite_Process_Deferred` during the GPU pass loop, after the regular
 *  tile/sprite/voxel flushes for ObjectLayer.
 */
namespace
{
    void Render_Deferred_Composite(Vinifera::Gfx::GraphicsDevice& device,
                                    const DeferredComposite& deferred)
    {
        if (deferred.records.empty()) return;
        if (!Vinifera::Gfx::UnitScratch::Get().Is_Initialized()) return;

        /**
         *  Determine the unit's overall translucency. Vanilla applies the
         *  same visual-character alpha to every section (body, turret,
         *  barrel) so the alpha is uniform across records. Scan for the
         *  smallest alpha — voxel records hold it as a float; shape records
         *  encode it via SHAPE_TRANSLUCENT* bits.
         */
        float unit_alpha = 1.0f;
        for (const PendingComposite& rec : deferred.records) {
            if (rec.kind == CompositeKind::Voxel) {
                unit_alpha = std::min(rec.voxel.alpha, unit_alpha);
            } else {
                const float a = Alpha_From_Sprite_Flags(rec.shape.flags);
                unit_alpha = std::min(a, unit_alpha);
            }
        }

        /**
         *  Begin the scratch session. After this, the scratch RT+DSV are
         *  bound with a 256x256 viewport; subsequent immediate-render
         *  calls paint into the scratch.
         */
        if (!Vinifera::Gfx::UnitScratch::Get().Begin_Unit(device)) {
            return;
        }

        const Rect& rect = deferred.rect;
        const Point2D& xyoff = deferred.xyoff;
        ConvertClass* shape_convert_override = deferred.shape_convert_override;
        // The dst_surface arg threaded through GPU_Draw_Shape is only used
        // for clipping and the GpuSurface dynamic_cast check; the captured
        // composite already validated GpuSurface, so we can reuse the
        // CompositeSurface as our dummy "destination" — `Draw_Shape` takes
        // a Surface& and only inspects rect bounds.
        Surface& dst_surface_dummy = *CompositeSurface;

        bool first_record = true;
        for (const PendingComposite& rec : deferred.records) {
        /**
         *  Clear scratch depth before each record so subsequent records
         *  paint in pure submission order without per-section depth
         *  inversions. Within a single record's voxel sections, the
         *  unchanged scratch depth gives correct front-most-voxel-wins
         *  ordering.
         */
        if (!first_record) {
            Vinifera::Gfx::UnitScratch::Get().Clear_Depth(device);
        }
        first_record = false;
        switch (rec.kind) {
        case CompositeKind::Voxel: {
            const PendingVoxelDraw& p = rec.voxel;
            if (p.voxeldata == nullptr) break;

            ColorScheme* scheme = ColorSchemes[p.color_scheme];
            if (scheme == nullptr || scheme->Converter == nullptr) break;

            /**
             *  Translate the EightBitSurface-local buffer_drawpoint (centered
             *  at (80,80)) to scratch-local coords (centered at (128,128)).
             *  Each captured section keeps its relative offset to the unit
             *  centroid; voxel barrel above turret etc. stays correctly
             *  layered within the scratch.
             */
            const Point2D scratch_local(
                kUnitScratchOrigin.X + (p.buffer_drawpoint.X - kCompositeOrigin.X),
                kUnitScratchOrigin.Y + (p.buffer_drawpoint.Y - kCompositeOrigin.Y));

            Render_Voxel_Object_To_Scratch_Immediate(
                *p.voxeldata, p.frame, p.matrix, scratch_local, rect,
                *scheme->Converter, p.brightness, p.z_adjust);
            break;
        }
        case CompositeKind::Shape: {
            const PendingShapeDraw& p = rec.shape;
            if (p.shapefile == nullptr) break;

            ConvertClass* convert = shape_convert_override != nullptr
                                  ? shape_convert_override
                                  : p.convert;
            if (convert == nullptr) break;

            const Point2D scratch_local(
                kUnitScratchOrigin.X + (p.buffer_point.X - kCompositeOrigin.X),
                kUnitScratchOrigin.Y + (p.buffer_point.Y - kCompositeOrigin.Y) + kCompositeYBias);

            /**
             *  Strip SHAPE_WIN_REL (it was a no-op at capture time; re-
             *  applying with a non-zero window double-shifts). Also strip
             *  SHAPE_TRANSLUCENT* so the SHP renders OPAQUE into the
             *  scratch; the unit's overall alpha is applied once at
             *  composite-blit time by `End_Unit_Composite`. This is what
             *  makes the per-pixel blend equivalent across voxel and SHP
             *  parts of the unit.
             */
            const ShapeFlags_Type replay_flags = p.flags & ~(SHAPE_WIN_REL | SHAPE_TRANSLUCENT75);

            /**
             *  Build the SpriteDrawCmd without queuing — we render
             *  immediately to the scratch. The scratch viewport is the
             *  256x256 region the unit fits in, with no AlphaBuffer/DSV
             *  bound for the SHP. Using a fake "scratch window" rect
             *  reuses the SHAPE_WIN_REL-stripped flags; the window passed
             *  is just for clipping, not positioning (SHAPE_CENTER
             *  centers on `scratch_local` via fi->X/Y offsets).
             */
            const Rect scratch_window(0, 0, Vinifera::Gfx::kUnitScratchWidth,
                                            Vinifera::Gfx::kUnitScratchHeight);

            Vinifera::Gfx::SpriteDrawCmd built_cmd = {};
            if (!Vinifera::Gfx::GPU_Draw_Shape(dst_surface_dummy, *convert, p.shapefile, p.shapenum,
                                               scratch_local, scratch_window, replay_flags,
                                               p.height_offset, p.zgrad, p.intensity,
                                               p.z_shapefile, p.z_shapenum, p.z_off,
                                               /*predator_offset*/ 0,
                                               &built_cmd)) {
                break;
            }

            /**
             *  Force opaque per-section render to the scratch (alpha=1).
             *  Composite blit applies `unit_alpha` once at scene merge.
             */
            built_cmd.Tint[3] = 1.0f;

            /**
             *  Disable scene-side depth-test against scratch depth so
             *  the SHP always paints into the scratch in submission
             *  order (vanilla's per-section paint order). Scratch
             *  depth was cleared to 1.0; any z value works.
             */
            built_cmd.DisableDepth = true;
            built_cmd.WriteDepth   = false;

            Vinifera::Gfx::SpriteQueue::Get().Render_Sprite_Immediate(
                device, built_cmd,
                Vinifera::Gfx::kUnitScratchWidth,
                Vinifera::Gfx::kUnitScratchHeight);
            break;
        }
        }
    }

        /**
         *  Composite the scratch onto the scene RT at the unit's drawpoint
         *  with the determined unit alpha. End_Unit_Composite restores the
         *  saved RT/DSV/viewport and emits per-pixel SV_Depth so the unit's
         *  silhouette occludes correctly against terrain at every Y.
         */
        const Point2D scene_origin(xyoff.X - kUnitScratchOrigin.X,
                                   xyoff.Y - kUnitScratchOrigin.Y);
        Vinifera::Gfx::UnitScratch::Get().End_Unit_Composite(
            device, scene_origin, unit_alpha, /*scene_depth*/ 0.5f);
    }
}  // anonymous namespace


void Composite_Process_Deferred(Vinifera::Gfx::GraphicsDevice& device)
{
    if (g_deferred_composites.empty()) return;
    for (const DeferredComposite& d : g_deferred_composites) {
        Render_Deferred_Composite(device, d);
    }
    g_deferred_composites.clear();
}


/**
 *  Replacement classes — Patch_Jump targets. ABI-compatible with vanilla
 *  member functions (thiscall, matching args).
 */
class TechnoClassExt : public TechnoClass
{
public:
    void _Draw_Voxel(VoxelObject& voxeldata, unsigned int frame, int key, VoxelIndexClass* index,
                     Rect& rect, Point2D& point, Matrix3D& matrix, int brightness, int flags) const;
    void _Techno_Draw_Voxel_Shadow(VoxelObject const& voxeldata, int layer_index, int key,
                                    VoxelIndexClass* index, Rect const& cliprect,
                                    Point2D const& point, Matrix3D const& matrix,
                                    bool force_cache) const;
};


class BulletClassExt : public BulletClass
{
public:
    void _Draw_Voxel(VoxelObject const& voxeldata, Matrix3D const& transform, Point2D const& drawpoint,
                     Rect const& cliprect, int frame, int flags, int brightness) const;
};


class VoxelAnimClassExt : public VoxelAnimClass
{
public:
    void _Draw_It(Point2D& point, Rect& cliprect) const;
};


class UnitClassExt : public UnitClass
{
public:
    void _Unit_Blit_Voxel(Surface& surface, Point2D xyoff, Rect rect, int alpha) const;
};


void TechnoClassExt::_Draw_Voxel(VoxelObject& voxeldata, unsigned int frame, int /*key*/,
                                  VoxelIndexClass* /*index*/, Rect& rect, Point2D& point,
                                  Matrix3D& matrix, int brightness, int /*flags*/) const
{
    /**
     *  Translucency / predator / hidden flags from Visual_Character —
     *  same logic vanilla applies in its outer Draw_Voxel.
     */
    const VisualType visual = Visual_Character(false, nullptr);
    VisualFx vfx;
    if (!VisualFx_From_Visual(visual, vfx)) {
        return;
    }

    /**
     *  Predator offset is computed up front so it's stable across both the
     *  direct-submit path and the composite-defer path (composite captures
     *  it for replay later). Vanilla math, unmodified.
     */
    const int predator_warp = vfx.is_predator ? Get_Predator_Offset() : 0;
    const int final_brightness = Apparent_Brightness(brightness);

    if (House == nullptr) {
        return;
    }

    /**
     *  Bridge z-fudge for `IsTooBigToFitUnderBridge` units (Mammoth-class).
     *  Mirrors vanilla unit.cpp:2431-2440: when such a unit is either
     *  passing under a low bridge (`Is_Z_Fudge_Bridge && Get_Z_Fudge_Column == 0`)
     *  or docking into a Weapons Factory door, vanilla swaps the voxel
     *  render for a 32×32 placeholder sprite. We can't easily reproduce
     *  that placeholder on the GPU sprite path, so we skip the voxel
     *  render entirely — invisible-under-bridge is closer to correct
     *  than full-voxel-poking-through.
     */
    if (RTTI == RTTI_UNIT) {
        FootClass* foot = reinterpret_cast<FootClass*>(const_cast<TechnoClassExt*>(this));
        const UnitTypeClass* utype = reinterpret_cast<UnitClass const*>(this)->Class;
        if (utype != nullptr && utype->IsTooBigToFitUnderBridge) {
            bool fudge = false;
            if (foot->Is_Z_Fudge_Bridge() && foot->Get_Z_Fudge_Column() == 0) {
                fudge = true;
            } else if (foot->NavCom != nullptr) {
                TechnoClass* contact = foot->Contact_With_Whom();
                if (contact != nullptr
                    && contact->What_Am_I() == RTTI_BUILDING
                    && static_cast<BuildingClass*>(contact)->Class->IsWeaponsFactory) {
                    fudge = true;
                }
            }
            if (fudge) {
                return;
            }
        }
    }

    /**
     *  Sinking offset for ice-cracker units. Vanilla sets `IsSinking = true`
     *  when a heavy unit cracks ice, then `Calculate_Sinking_Offset` lazily
     *  populates `SinkingYOffset`. The render path then intersects the
     *  cliprect with a screen-Y ceiling so the bottom rows of the voxel
     *  get clipped — the unit appears to sink.
     *
     *  Vanilla's `region.Bounds.Height` arg to Calculate_Sinking_Offset is
     *  the CPU-rasterized voxel bbox height; we approximate with a 32 px
     *  constant. The facing fudge inside the function (-8 to -12 px) is on
     *  the same scale, so the visual error is small.
     */
    Rect effective_rect = rect;
    if (IsSinking) {
        if (SinkingYOffset == 0) {
            const_cast<TechnoClassExt*>(this)->Calculate_Sinking_Offset(32, point.Y);
        }
        if (SinkingYOffset > 0) {
            const Rect sink_clip(0, 0, TacticalRect.Width, SinkingYOffset - TacticalRect.Y);
            effective_rect = Intersect(rect, sink_clip);
        }
    }

    /**
     *  Detect turreted-unit composite mode. Vanilla swaps `LogicalSurface`
     *  to the 160x160 `EightBitSurface` scratch before calling Draw_Voxel
     *  for each section, then `Unit_Blit_Voxel` blits the composite to the
     *  real tactical surface at the unit's actual screen position. We can
     *  see this swap and defer submission until the blit fires with the
     *  real xdrawpoint.
     */
    if (LogicalSurface == EightBitSurface) {
        Composite_Push_Voxel(voxeldata, frame, matrix, point, effective_rect,
                             final_brightness, vfx.alpha, House->Scheme,
                             Get_Z_Adjustment(),
                             vfx.is_predator, predator_warp);
        return;
    }

    const int z_adjust = Get_Z_Adjustment();

    ConvertClass& converter = *ColorSchemes[House->Scheme]->Converter;
    Submit_Voxel_Object(voxeldata, frame, matrix, point, effective_rect, converter,
                       final_brightness, vfx.alpha, z_adjust,
                       /*single_layer*/ -1,
                       vfx.is_predator, predator_warp);
}


void TechnoClassExt::_Techno_Draw_Voxel_Shadow(VoxelObject const& voxeldata, int layer_index,
                                                int /*key*/, VoxelIndexClass* /*index*/,
                                                Rect const& cliprect, Point2D const& point,
                                                Matrix3D const& matrix, bool /*force_cache*/) const
{
    /**
     *  Early-out mirroring vanilla:
     *    if (Cloak != UNCLOAKED || (FlashCount/2)%2 == 1 || voxlib.Load_Failed())
     *      return;
     *  Cloak/Flash access on TechnoClass — verify field names in TSpp.
     */
    if (voxeldata.VoxelLibrary == nullptr || voxeldata.VoxelLibrary->Load_Failed()) {
        return;
    }

    if (House == nullptr) {
        return;
    }
    ConvertClass& converter = *ColorSchemes[House->Scheme]->Converter;

    Submit_Voxel_Shadow(voxeldata, layer_index, matrix, point, cliprect, converter);
}


void BulletClassExt::_Draw_Voxel(VoxelObject const& voxeldata, Matrix3D const& transform,
                                  Point2D const& drawpoint, Rect const& cliprect, int frame,
                                  int /*flags*/, int brightness) const
{
    if (Class == nullptr) return;
    ConvertClass& converter = *ColorSchemes[Class->Color]->Converter;
    Submit_Voxel_Object(voxeldata, static_cast<unsigned int>(frame), transform, drawpoint, cliprect,
                       converter, brightness, /*alpha*/ 1.0f, /*z_adjust*/ 0);
}


void VoxelAnimClassExt::_Draw_It(Point2D& point, Rect& cliprect) const
{
    if (IsInvisible) return;
    if (Class == nullptr || Class->Voxel.VoxelLibrary == nullptr) return;
    if (Class->Voxel.VoxelLibrary->Load_Failed()) return;

    /**
     *  Mirror vanilla's vanim.cpp:182-193 owner/Tiberium/ownerless branching:
     *    - House != NULL  → house scheme converter, cell brightness.
     *    - IsTiberium     → TiberiumDrawer, cell brightness.
     *    - Otherwise      → VoxelDrawer (we substitute neutral scheme 0),
     *                       brightness pinned to 1000 (vanilla quirk for
     *                       ownerless debris).
     */
    ConvertClass* converter_ptr = nullptr;
    int brightness = Map[Position].Brightness;
    if (House != nullptr) {
        ColorScheme* scheme = ColorSchemes[House->Scheme];
        if (scheme != nullptr) converter_ptr = scheme->Converter;
    } else if (Class->IsTiberium) {
        converter_ptr = TiberiumDrawer;
    } else {
        ColorScheme* scheme = ColorSchemes[0];
        if (scheme != nullptr) converter_ptr = scheme->Converter;
        brightness = 1000;
    }
    if (converter_ptr == nullptr) return;
    ConvertClass& converter = *converter_ptr;

    const int layer = Class->VoxelIndex;

    /**
     *  Shadow first, then object — mirrors vanilla's draw order so the
     *  shadow lands under the body.
     *
     *  Vanilla's `vanim.cpp:161,170` pulls `BounceClass::Get_Matrix()` for
     *  the per-frame rotation/bounce. TSpp now exposes that accessor; use
     *  it so tumbling debris carries the right orientation.
     */
    const Matrix3D anim_matrix = Bouncer.Get_Matrix();
    Submit_Voxel_Shadow(Class->Voxel, layer, anim_matrix, point, cliprect, converter);
    Submit_Voxel_Object(Class->Voxel, 0u, anim_matrix, point, cliprect,
                       converter, brightness, /*alpha*/ Class->IsTranslucent ? 0.5f : 1.0f,
                       /*z_adjust*/ 0, /*single_layer*/ layer);
}


void UnitClassExt::_Unit_Blit_Voxel(Surface& surface, Point2D xyoff, Rect rect, int /*alpha*/) const
{
    /**
     *  Drain the unified composite queue. The shape replay needs the unit's
     *  house-aware converter — vanilla draws body/turret with EightBitDrawer
     *  during composite and only applies house colors at blit time. Resolve
     *  from `House->Scheme` so each SHP record lands with the correct unit
     *  remap.
     */
    ConvertClass* unit_convert = nullptr;
    if (House != nullptr) {
        ColorScheme* scheme = ColorSchemes[House->Scheme];
        if (scheme != nullptr) {
            unit_convert = scheme->Converter;
        }
    }
    Composite_Replay(surface, xyoff, rect, unit_convert);
}


void Voxel_Blit_Hooks()
{
    Patch_Jump(0x006354E0, &TechnoClassExt::_Draw_Voxel);
    Patch_Jump(0x00635860, &TechnoClassExt::_Techno_Draw_Voxel_Shadow);
    Patch_Jump(0x004472C0, &BulletClassExt::_Draw_Voxel);
    Patch_Jump(0x0065E050, &VoxelAnimClassExt::_Draw_It);
    Patch_Jump(0x00651F50, &UnitClassExt::_Unit_Blit_Voxel);

    DEBUG_INFO("Voxel_Blit_Hooks: 4 voxel paths + Unit_Blit_Voxel no-op patched.\n");
}
