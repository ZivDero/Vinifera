/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU mirror of the voxel light-remap table (VPL).
 *
 *          The VPL (`Voxel_PaletteLookup[light][color]`) remaps a (Lambertian
 *          light index, palette index) pair to a lit palette index. It is
 *          game-wide and static after startup, so the GPU texture is uploaded
 *          once on first flush and reused for the rest of the run.
 *
 *          Two-step GPU voxel shading:
 *              lit_idx = VoxelLightRemapTex[light][color_idx]
 *              rgba    = PaletteLUT[lit_idx]
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    class VoxelLightRemapTexture
    {
    public:
        static constexpr int Width  = 256;   // palette index axis
        static constexpr int Height = 32;    // light axis (MAX_PALETTE_LOOKUP_ENTRIES)
        static constexpr int Bytes  = Width * Height;

        VoxelLightRemapTexture() = default;
        ~VoxelLightRemapTexture() = default;

        VoxelLightRemapTexture(const VoxelLightRemapTexture&) = delete;
        VoxelLightRemapTexture& operator=(const VoxelLightRemapTexture&) = delete;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Upload vanilla's `Voxel_PaletteLookup` into the GPU texture on
         *  the first call; no-op thereafter. The VPL is a one-shot static
         *  load, so a single upload per game session is all we need.
         */
        bool Ensure_Uploaded();

        Texture2D& Get_Texture() { return Tex; }

    private:
        Texture2D Tex;
        bool      Uploaded = false;
    };
}
