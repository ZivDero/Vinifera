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

#include "bullet.h"
#include "bullettype.h"
#include "colorscheme.h"
#include "convert.h"
#include "debughandler.h"
#include "drawshape.h"
#include "graphics_device.h"
#include "gpu_surface.h"
#include "gpu_surface_target.h"
#include "hooker.h"
#include "house.h"
#include "matrix3d.h"
#include "motionlib.h"
#include "objecttype.h"
#include "render_pass.h"
#include "shp_cache.h"
#include "sprite_batch.h"
#include "techno.h"
#include "tibsun_globals.h"
#include "unit.h"
#include "voxel.hh"
#include "voxel_asset.h"
#include "voxel_effect.h"
#include "voxel_queue.h"
#include "voxelanim.h"
#include "voxelanimtype.h"
#include "voxelinit.h"
#include "voxellib.h"
#include "voxelobj.h"

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
     *  Capture buffer for turreted-unit composite mode. Vanilla's
     *  `Unit_Draw_Voxel` swaps `LogicalSurface` to a 160x160 `EightBitSurface`
     *  and calls `Draw_Voxel(...)` with `drawpoint=(80,80)+offset` for each
     *  section (body / turret / barrel). After all sections are drawn,
     *  `Unit_Blit_Voxel` blits the composited scratch to the real tactical
     *  surface at the unit's actual screen position `xdrawpoint`.
     *
     *  When we detect we're being called in this composite mode, we capture
     *  the section's args instead of submitting immediately. `Unit_Blit_Voxel`
     *  then replays the captured queue with the screen drawpoint adjusted by
     *  `xdrawpoint + (captured.drawpoint - (80, 80))`.
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
    static std::vector<PendingVoxelDraw> g_pending_voxels;
    static const Point2D kCompositeOrigin(80, 80);


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
        constexpr float kVoxelYBias = 17.0f;
        out.T0[0] = (float)point.X + c0.X;
        out.T0[1] = (float)point.Y + c0.Y + kVoxelYBias;
        out.T0[2] = c0.Z - center.Z;
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
        const float kVoxelZScale = 1.0e-5f;
        float max_y_below_drawpoint = 0.0f;
        float max_abs_voxel_z       = 0.0f;
        for (int i = 0; i < VOXEL_BOUNDS_MAX; ++i) {
            Vector3 v = final_mtx * mesh.Bounds[i];
            const float screen_y_offset = -v.Y + kVoxelYBias;   // post Y-flip; +ve = below drawpoint
            if (screen_y_offset > max_y_below_drawpoint) max_y_below_drawpoint = screen_y_offset;
            const float dz = std::fabs(v.Z - center.Z);
            if (dz > max_abs_voxel_z) max_abs_voxel_z = dz;
        }
        const float kObjectEps_section = max_y_below_drawpoint * kPixelToDepth
                                       + max_abs_voxel_z * kVoxelZScale
                                       + 1.0e-4f;

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
     *  Detect turreted-unit composite mode. Vanilla swaps `LogicalSurface`
     *  to the 160x160 `EightBitSurface` scratch before calling Draw_Voxel
     *  for each section, then `Unit_Blit_Voxel` blits the composite to the
     *  real tactical surface at the unit's actual screen position. We can
     *  see this swap and defer submission until the blit fires with the
     *  real xdrawpoint.
     */
    if (LogicalSurface == EightBitSurface) {
        PendingVoxelDraw p;
        p.voxeldata        = &voxeldata;
        p.matrix           = matrix;
        p.buffer_drawpoint = point;
        p.cliprect         = rect;
        p.frame            = frame;
        p.brightness       = final_brightness;
        p.alpha            = visual_alpha;
        p.color_scheme     = House->Scheme;
        p.z_adjust         = const_cast<TechnoClassExt*>(this)->Get_Z_Adjustment();
        g_pending_voxels.push_back(p);
        return;
    }

    const int z_adjust = const_cast<TechnoClassExt*>(this)->Get_Z_Adjustment();

    ConvertClass& converter = *ColorSchemes[House->Scheme]->Converter;
    Submit_Voxel_Object(voxeldata, frame, matrix, point, rect, converter,
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
     *  We skip vanilla's fog/under-bridge position adjustments; the
     *  `point` arg already comes from those calculations upstream.
     *  Color scheme selection: prefer owner's scheme; fall back to a
     *  neutral (scheme 0) palette for ownerless / Tiberium voxels.
     */
    ColorScheme* scheme = (House != nullptr) ? ColorSchemes[House->Scheme] : ColorSchemes[0];
    if (scheme == nullptr || scheme->Converter == nullptr) return;
    ConvertClass& converter = *scheme->Converter;

    const int layer = Class->VoxelIndex;
    const int brightness = (House != nullptr) ? 1000 : 1000;   // TODO: cell brightness

    /**
     *  Shadow first, then object — mirrors vanilla's draw order so the
     *  shadow lands under the body.
     *
     *  TODO: vanilla pulls `BounceClass::Get_Matrix()` for the per-frame
     *  rotation/bounce; that accessor isn't in TSpp. For now use identity,
     *  so VoxelAnim renders at the screen point but without the bounce
     *  rotation. Visual regression on tumbling debris until we wire it up.
     */
    Matrix3D anim_matrix;
    anim_matrix.Make_Identity();
    Submit_Voxel_Shadow(Class->Voxel, layer, anim_matrix, point, cliprect, converter);
    Submit_Voxel_Object(Class->Voxel, 0u, anim_matrix, point, cliprect,
                       converter, brightness, /*alpha*/ Class->IsTranslucent ? 0.5f : 1.0f,
                       /*z_adjust*/ 0, /*single_layer*/ layer);
}


void UnitClassExt::_Unit_Blit_Voxel(Surface& surface, Point2D xyoff, Rect rect, int /*alpha*/) const
{
    /**
     *  Replay captured per-section draws with the unit's actual screen
     *  position. Each captured `buffer_drawpoint` is in the 160x160
     *  EightBitSurface's coord space (centered at (80, 80)); the real
     *  screen position for the section is `xyoff + (buffer_dp - (80,80))`.
     */
    GpuSurface* gpu_dest = dynamic_cast<GpuSurface*>(&surface);
    if (gpu_dest == nullptr) {
        // Non-GpuSurface destination — would need to fall back to vanilla
        // CPU compositing, but that's out of scope (cameos etc.). Just
        // drop the captures.
        g_pending_voxels.clear();
        return;
    }

    if (g_pending_voxels.empty()) {
        return;
    }

    for (const PendingVoxelDraw& p : g_pending_voxels) {
        if (p.voxeldata == nullptr) continue;

        Point2D real_point(xyoff.X + (p.buffer_drawpoint.X - kCompositeOrigin.X),
                           xyoff.Y + (p.buffer_drawpoint.Y - kCompositeOrigin.Y));

        // Clip rect in the captured composite mode is the 160x160 scratch
        // rect; the real clip is the unit's tactical cliprect — but at the
        // capture site we don't have that. Pass the supplied `rect` here
        // (the unit's true clip from Unit_Blit_Voxel's args).
        ColorScheme* scheme = ColorSchemes[p.color_scheme];
        if (scheme == nullptr || scheme->Converter == nullptr) continue;

        Submit_Voxel_Object(*p.voxeldata, p.frame, p.matrix, real_point, rect,
                           *scheme->Converter, p.brightness, p.alpha, p.z_adjust);
    }
    g_pending_voxels.clear();
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
