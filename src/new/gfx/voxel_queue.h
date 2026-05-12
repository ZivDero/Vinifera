/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame voxel draw queue + flush.
 *
 *          Each `VoxelDrawCmd` references an immutable per-section vertex
 *          buffer from `VoxelAssetCache` plus a unit-specific `PaletteLUT`
 *          (from `PaletteCache`, built from the unit's `ColorScheme`
 *          converter — house colors baked in there). The flush path
 *          bucket-sorts commands by output target, then issues per-cmd:
 *              1. Update VoxelEffectParams CB (transform + lighting + tint)
 *              2. Bind palette LUT (t0) + light remap (t1) + normals (t2)
 *              3. Set viewport / rasterizer / depth / blend
 *              4. IASetVertexBuffers(mesh.VB) + Draw(POINTLIST)
 *
 *          The light-remap texture (VPL) is uploaded once on first flush
 *          from the global `Voxel_PaletteLookup`. The VPL is a single
 *          game-wide static load (`voxels.vpl` at startup); it does not
 *          change per-theatre or per-unit, so no refresh is needed.
 *
 *          One vertex = one rasterized pixel. Object pass uses standard
 *          alpha blend + LessEqual depth. Shadow pass reuses a separate
 *          per-section VB (one vertex per occupied (x,y) column) with a
 *          ground-projection transform and DestMultiplyHalf blend.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "gpu_surface_target.h"
#include "render_pass.h"
#include "sprite_batch.h"
#include "states.h"
#include "voxel_effect.h"
#include "voxel_light_remap.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;
    struct VoxelSectionMesh;


    struct VoxelDrawCmd
    {
        const VoxelSectionMesh* Mesh = nullptr;
        VoxelEffectParams       Params = {};

        /**
         *  House-aware palette built from the unit's `ColorScheme::Converter`
         *  via `PaletteCache::Get_Or_Build(device, converter)`. The converter
         *  already substitutes house colors into the "remap range" slots, so
         *  this LUT alone covers all house-color recoloring. The light
         *  remap (VPL) is game-wide and lives in `VoxelQueue::LightRemap`.
         */
        PaletteLUT*             Palette = nullptr;

        bool                    IsShadow = false;
        bool                    WriteDepth = false;
        bool                    DisableDepth = false;
        RectF                   Clip = {};
        RenderPass              Pass = RenderPass::ObjectLayer;
        GpuRenderTarget         OutputTarget = GpuRenderTarget::Scene;

        /**
         *  Predator/cloak (VISUAL_RIPPLE). When true the cmd is routed via
         *  `VoxelDistortionEffect` in the PostEffects pass; the PS samples
         *  SceneCopy at `(SV_Position + (WarpPixels, 0))` and blends with
         *  the shaded palette color by `BlendRatio`. Mutually exclusive
         *  with `IsShadow`.
         */
        bool                    IsPredator         = false;
        int                     PredatorWarpPixels = 0;
        float                   PredatorBlendRatio = 0.5f;

        /**
         *  Composite-unit grouping. `-1` (the default) means the cmd is a
         *  standalone draw and goes through the fast batched path. Any
         *  other value means the cmd is one section of a multi-section /
         *  translucent unit; all sections of the unit share the same ID.
         *  `VoxelQueue::Flush_Pass` collects same-ID cmds and renders them
         *  to a shared scratch RT (see `unit_scratch.h`) which is then
         *  composited to the scene as a single quad — fixing the per-
         *  pixel blend compounding that hits translucent voxels.
         */
        int                     UnitGroupID = -1;
    };


    /**
     *  Per-unit metadata for composite groups. Built at submit time when
     *  `Submit_Voxel_Object` allocates a UnitGroupID; consumed at flush
     *  time by the composite path.
     */
    struct VoxelUnitGroup
    {
        Point2D Drawpoint   = { 0, 0 };
        float   Alpha       = 1.0f;
        float   SceneDepth  = 0.5f;
        RectF   Clip        = {};
    };


    class VoxelQueue
    {
    public:
        static VoxelQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const VoxelDrawCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);
        void Clear();

        /**
         *  Reserve a new UnitGroupID and register the unit's metadata.
         *  Returns the allocated ID for stamping into VoxelDrawCmd. Caller
         *  is responsible for submitting the cmds; the group's metadata
         *  (drawpoint, alpha, depth) is used by the composite flush path.
         */
        int Allocate_Unit_Group(const VoxelUnitGroup& group);

        /**
         *  Render one voxel cmd synchronously against the currently bound
         *  render target. Used by the composite-replay path (turreted units
         *  composed via the EightBitSurface intercept) which needs to render
         *  voxel and SHP parts to the unit-scratch in submission order, not
         *  via the deferred per-queue flush.
         */
        void Render_Cmd_Immediate(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                                  int target_w, int target_h, bool is_sidebar);

        VoxelEffect&            Effect()    { return EffectInstance; }
        VoxelLightRemapTexture&  LightRemap() { return LightRemapInstance; }

    private:
        VoxelQueue() = default;

        void Issue_Cmd(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                       int target_w, int target_h, bool is_sidebar);
        void Issue_Predator_Cmd(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                                int target_w, int target_h);

        /**
         *  Render `count` cmds of one composite unit group into the shared
         *  scratch RT, then composite the scratch into the scene at the
         *  group's drawpoint with the group's alpha. Used by Flush_Pass.
         */
        void Flush_Composite_Group(GraphicsDevice& device,
                                   const VoxelDrawCmd* const* cmds,
                                   size_t count,
                                   const VoxelUnitGroup& group);

        bool Has_Predator_Commands(RenderPass pass) const;

        VoxelEffect               EffectInstance;
        VoxelDistortionEffect      DistortionEffectInstance;
        VoxelLightRemapTexture     LightRemapInstance;
        std::vector<VoxelDrawCmd> Commands;
        std::vector<VoxelUnitGroup> UnitGroups;
        bool                      Initialized = false;
    };
}
