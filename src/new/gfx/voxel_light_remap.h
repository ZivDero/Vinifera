/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU mirror of the theatre's voxel light-remap table (VPL).
 *
 *          Vanilla loads the active theatre's VPL (e.g. `unittem.vpl`,
 *          `unitsno.vpl`, `uniturb.vpl`) into the global
 *          `Voxel_PaletteLookup[light][color]` — a single 32x256 byte
 *          table that remaps a (Lambertian light index, source palette
 *          index) pair to a lit palette index in the theatre's palette.
 *          The same table covers every voxel in the theatre; it is NOT
 *          per-unit and NOT per-house.
 *
 *          House-color recoloring (the "remap range" slots that flip to
 *          red / blue / yellow / etc.) is handled separately at sample
 *          time by the unit's `ColorScheme::Converter`, which the existing
 *          `PaletteCache::Get_Or_Build` already turns into a `PaletteLUT`
 *          with the correct colors baked in. So the GPU pipeline has two
 *          steps after lighting:
 *
 *              lit_idx = VoxelLightRemapTex[light][color_idx]   (this file)
 *              rgba    = PaletteLUT[lit_idx]                    (PaletteCache)
 *
 *          `voxels.vpl` is loaded once at game startup (`Init_Voxel_Palette`)
 *          and never changes after that — it's not per-theatre and not
 *          per-unit. We mirror that lifecycle on the GPU side: the
 *          texture is uploaded once on the first flush after vanilla has
 *          populated the global, then left alone for the rest of the run.
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
