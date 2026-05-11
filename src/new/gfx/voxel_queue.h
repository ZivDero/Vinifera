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

        VoxelEffect&            Effect()    { return EffectInstance; }
        VoxelLightRemapTexture&  LightRemap() { return LightRemapInstance; }

    private:
        VoxelQueue() = default;

        void Issue_Cmd(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                       int target_w, int target_h, bool is_sidebar);

        VoxelEffect               EffectInstance;
        VoxelLightRemapTexture     LightRemapInstance;
        std::vector<VoxelDrawCmd> Commands;
        bool                      Initialized = false;
    };
}
