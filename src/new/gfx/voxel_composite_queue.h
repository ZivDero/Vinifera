/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame voxel composite queue + flush.
 *
 *          Backing for the GPU port of vanilla's `Bit_Blit` / `Blit_Block` /
 *          `RLE_Blit` calls that feed voxel pixels (rendered by vanilla's CPU
 *          voxel rasterizer into `VoxelSurface` / `EightBitSurface`) onto the
 *          tactical `GpuSurface`. Each cmd carries the source CPU pixel
 *          pointer + sub-rect dims, an optional per-pixel z buffer pointer
 *          (for `VoxelZSurface`-sourced cmds), the unit's `PaletteLUT`, the
 *          destination rect, effect-flag bitmask (translucency / darken),
 *          tint, and depth state.
 *
 *          Flush_Pass uploads each cmd's pixels to a persistent 256x256
 *          R8_UINT atlas via `Texture2D::Set_Sub_Data`, then submits one
 *          quad through `SpriteBatch` using the shared `SpriteEffect`
 *          shader. Begin/End is per-cmd (the shared atlas is overwritten
 *          per cmd), so there's no inter-cmd batching in this MVP.
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
#include "sprite_effect.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;


    struct VoxelCompositeCmd
    {
        std::vector<uint8_t> ColorData;          // packed W×H palette-index bytes (copied at submit time)
        std::vector<uint8_t> ZData;              // same layout; empty = no per-pixel z
        int             SourceW = 0;             // sub-rect width (pixels)
        int             SourceH = 0;             // sub-rect height (pixels)
        PaletteLUT*     Palette = nullptr;
        RectF           Dst = {};                // dst rect in logical/backbuffer coords
        RectF           Clip = {};               // scissor rect; invalid = full target
        uint32_t        EffectFlags = 0;         // SEF_* (typically 0 for voxels; translucency lives in Tint[3])
        float           Tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float           DepthBaseline = 0.0f;    // [0,1] flat depth; baseline applied to all pixels
        bool            WriteDepth = false;
        bool            DisableDepth = false;
        RenderPass      Pass = RenderPass::ObjectLayer;
        GpuRenderTarget OutputTarget = GpuRenderTarget::Scene;
    };


    class VoxelCompositeQueue
    {
    public:
        static VoxelCompositeQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const VoxelCompositeCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);
        void Clear();

    private:
        VoxelCompositeQueue() = default;

        SpriteBatch                    Batch;
        SpriteEffect                   Effect;
        Texture2D                      ColorAtlas;     // R8_UINT palette indices
        Texture2D                      ZAtlas;         // R8_UINT per-pixel z bytes
        std::vector<VoxelCompositeCmd> Commands;
        bool                           Initialized = false;
    };
}
