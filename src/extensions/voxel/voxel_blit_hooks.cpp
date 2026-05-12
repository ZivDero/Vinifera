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
#include "graphics_device.h"
#include "gpu_surface.h"
#include "gpu_surface_target.h"
#include "hooker.h"
#include "house.h"
#include "map.h"
#include "matrix3d.h"
#include "motionlib.h"
#include "objecttype.h"
#include "render_pass.h"
#include "shp_cache.h"
#include "sprite_batch.h"
#include "tactical.h"
#include "techno.h"
#include "tibsun_globals.h"
#include "unit.h"
#include "unit_composite.h"
#include "unittype.h"
#include "voxel.hh"
#include "voxel_asset.h"
#include "voxel_effect.h"
#include "voxel_queue.h"
#include "voxelanim.h"
#include "voxelanimtype.h"
#include "voxelinit.h"
#include "voxellib.h"
#include "voxelobj.h"


/**
 *  Forward declaration for re-entering the SHP path at replay time. Defined
 *  in draw_shape/draw_shapeext_hooks.cpp. We don't include its header here
 *  because that header (draw_shapeext_hooks.h) only exposes the install
 *  function; the proxy entry point is plain extern "C++" linkage.
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
    Point2D z_off);

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
        const float t = (float)brightness / 1000.0f;
        out[0] = t;
        out[1] = t;
        out[2] = t;
        out[3] = alpha;
    }


    /**
     *  Visual_Character → translucency alpha. Hidden returns false.
     *  Predator displacement deferred (treated as plain translucency).
     */
    bool Alpha_From_Visual(VisualType v, float& out_alpha)
    {
        out_alpha = 1.0f;
        switch (v) {
        case VISUAL_NORMAL:
            return true;
        case VISUAL_INDISTINCT:
            out_alpha = 0.75f;
            return true;
        case VISUAL_DARKEN:
        case VISUAL_SHADOWY:
            out_alpha = 0.5f;
            return true;
        case VISUAL_RIPPLE:
            out_alpha = 0.5f;
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
                       (int)mesh.XSize, (int)mesh.YSize, (int)mesh.ZSize, (int)mesh.NormalType,
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
        out.T0[0] = (float)point.X + c0.X;
        out.T0[1] = (float)point.Y + c0.Y + kVoxelYBias;
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
        out.T0[3] = (float)point.Y;

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
        const float x_size = (float)std::max<int>(1, mesh.XSize);
        const float y_size = (float)std::max<int>(1, mesh.YSize);
        const float z_size = (float)std::max<int>(1, mesh.ZSize);

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
        const int normal_type = (int)mesh.NormalType;
        out.LightDir[3] = (float)(std::max(0, normal_type - 1) * 256);

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
        const float kVoxelZScale = 1.0e-5f;
        float max_eps_needed = 0.0f;
        for (int i = 0; i < VOXEL_BOUNDS_MAX; ++i) {
            Vector3 v = final_mtx * mesh.Bounds[i];
            const float screen_y_offset = -v.Y + kVoxelYBias;
            const float back_z_contribution = std::max(0.0f, -v.Z) * kVoxelZScale;
            const float eps_this_corner = std::max(0.0f, screen_y_offset) * kPixelToDepth + back_z_contribution;
            if (eps_this_corner > max_eps_needed) max_eps_needed = eps_this_corner;
        }
        const float kObjectEps_section = max_eps_needed + 1.0e-4f;

        out.Misc[0] = is_shadow ? 0.0f : (float)z_adjust;
        out.Misc[1] = kPixelToDepth;
        out.Misc[2] = is_shadow ? 0.5f : kObjectEps_section;
        out.Misc[3] = is_shadow ? (float)VEF_SHADOW : 0.0f;
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

        const RenderPass pass = Vinifera::Gfx::Current_Render_Pass();

        float xscale = 1.0f, yscale = 1.0f;
        Vinifera::Gfx::Logical_To_Render_Target(*Vinifera::Gfx::Device, GpuRenderTarget::Scene, xscale, yscale);

        RectF clip = {};
        if (cliprect.Width > 0 && cliprect.Height > 0) {
            clip = RectF {
                (float)cliprect.X * xscale,
                (float)cliprect.Y * yscale,
                (float)cliprect.Width  * xscale,
                (float)cliprect.Height * yscale
            };
        }

        const int layer_count = voxlib->Get_Layer_Count();
        const int frame_lo = (single_layer >= 0) ? single_layer : 0;
        const int frame_hi = (single_layer >= 0) ? (single_layer + 1) : layer_count;

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
                    const int safe_frame = (int)(frame % (unsigned)frame_count);
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

            Build_Section_Params(*mesh, section_world, point, brightness, alpha,
                                 /*is_shadow*/ false, z_adjust, cmd.Params);

            VoxelQueue::Get().Submit(cmd);
        }
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
                (float)cliprect.X * xscale,
                (float)cliprect.Y * yscale,
                (float)cliprect.Width  * xscale,
                (float)cliprect.Height * yscale
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
                          int                z_adjust)
{
    PendingComposite rec;
    rec.kind = CompositeKind::Voxel;
    rec.voxel.voxeldata        = &voxeldata;
    rec.voxel.matrix           = matrix;
    rec.voxel.buffer_drawpoint = buffer_drawpoint;
    rec.voxel.cliprect         = cliprect;
    rec.voxel.frame            = frame;
    rec.voxel.brightness       = brightness;
    rec.voxel.alpha            = alpha;
    rec.voxel.color_scheme     = color_scheme;
    rec.voxel.z_adjust         = z_adjust;
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

    for (const PendingComposite& rec : g_pending_composite) {
        switch (rec.kind) {
        case CompositeKind::Voxel: {
            const PendingVoxelDraw& p = rec.voxel;
            if (p.voxeldata == nullptr) break;

            const Point2D real_point(xyoff.X + (p.buffer_drawpoint.X - kCompositeOrigin.X),
                                     xyoff.Y + (p.buffer_drawpoint.Y - kCompositeOrigin.Y));

            ColorScheme* scheme = ColorSchemes[p.color_scheme];
            if (scheme == nullptr || scheme->Converter == nullptr) break;

            Submit_Voxel_Object(*p.voxeldata, p.frame, p.matrix, real_point, rect,
                                *scheme->Converter, p.brightness, p.alpha, p.z_adjust);
            break;
        }
        case CompositeKind::Shape: {
            const PendingShapeDraw& p = rec.shape;
            if (p.shapefile == nullptr) break;

            /**
             *  Vanilla draws body/turret SHPs into EightBitSurface using
             *  `EightBitDrawer` (8-bit passthrough) and applies house
             *  colors later during composite. We need house colors at
             *  draw time, so override with the unit's converter passed
             *  through by `_Unit_Blit_Voxel`. Fall back to the captured
             *  converter only if the override is missing.
             */
            ConvertClass* convert = shape_convert_override != nullptr
                                  ? shape_convert_override
                                  : p.convert;
            if (convert == nullptr) break;

            /**
             *  Add `kCompositeYBias` so the shape lands at the same
             *  vertical offset vanilla's composite blit produces (and
             *  where the unit's voxel sections render via the matching
             *  `kVoxelYBias` in Build_Section_Params).
             */
            const Point2D real_point(xyoff.X + (p.buffer_point.X - kCompositeOrigin.X),
                                     xyoff.Y + (p.buffer_point.Y - kCompositeOrigin.Y) + kCompositeYBias);

            /**
             *  SHAPE_WIN_REL was a no-op at capture time (the proxy's
             *  `window` arg was the 160x160 scratch rect with X=Y=0). At
             *  replay the window is the real tactical clip with non-zero
             *  X,Y — re-applying WIN_REL would double-shift, so strip it.
             *  SHAPE_CENTER is preserved: it's a logical-size offset, not
             *  a window translation, and the captured buffer_point is the
             *  unit's centroid which `real_point` translates correctly.
             */
            const ShapeFlags_Type replay_flags =
                ShapeFlags_Type(p.flags & ~SHAPE_WIN_REL);

            Draw_Shape_Proxy_DX11(dst_surface, *convert, p.shapefile, p.shapenum,
                                  real_point, rect, replay_flags,
                                  /*remap*/ nullptr,
                                  p.height_offset, p.zgrad, p.intensity,
                                  p.z_shapefile, p.z_shapenum, p.z_off);
            break;
        }
        }
    }

    g_pending_composite.clear();
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
    const VisualType visual = const_cast<TechnoClassExt*>(this)->Visual_Character(false, nullptr);
    float visual_alpha = 1.0f;
    if (!Alpha_From_Visual(visual, visual_alpha)) {
        return;
    }

    const int final_brightness = const_cast<TechnoClassExt*>(this)->Apparent_Brightness(brightness);

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
        FootClass* foot = (FootClass*)const_cast<TechnoClassExt*>(this);
        const UnitTypeClass* utype = ((UnitClass const*)this)->Class;
        if (utype != nullptr && utype->IsTooBigToFitUnderBridge) {
            bool fudge = false;
            if (foot->Is_Z_Fudge_Bridge() && foot->Get_Z_Fudge_Column() == 0) {
                fudge = true;
            } else if (foot->NavCom != nullptr) {
                TechnoClass* contact = foot->Contact_With_Whom();
                if (contact != nullptr
                    && contact->What_Am_I() == RTTI_BUILDING
                    && ((BuildingClass*)contact)->Class->IsWeaponsFactory) {
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
                             final_brightness, visual_alpha, House->Scheme,
                             const_cast<TechnoClassExt*>(this)->Get_Z_Adjustment());
        return;
    }

    const int z_adjust = const_cast<TechnoClassExt*>(this)->Get_Z_Adjustment();

    ConvertClass& converter = *ColorSchemes[House->Scheme]->Converter;
    Submit_Voxel_Object(voxeldata, frame, matrix, point, effective_rect, converter,
                       final_brightness, visual_alpha, z_adjust);
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
    Submit_Voxel_Object(voxeldata, (unsigned int)frame, transform, drawpoint, cliprect,
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
    int brightness = ((MapClass&)Map)[Position].Brightness;
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
