/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Shared Texture2DArray holding all paletted-sprite LUTs.
 *
 *          The SpriteEffect (and any other effect that wants to switch palette
 *          per-pixel) samples this array with a layer index supplied per
 *          vertex. Eliminates the per-batch palette-bind state change.
 *
 *          Layout: 256 × 1 × N RGBA8. Each layer is one 256-entry palette LUT,
 *          alpha = 0 for index 0, 255 otherwise (same encoding PaletteLUT
 *          produces).
 *
 *          Growth: lazy. Pre-allocate `initial_capacity` layers; on overflow
 *          re-create at 2× capacity and re-upload existing layers from a
 *          staged CPU copy.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>
#include <vector>


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    class PaletteArray
    {
    public:
        static PaletteArray& Get();

        bool Initialize(GraphicsDevice& device, int initial_capacity = 16);
        void Shutdown();

        /**
         *  Drop all layers and the array texture itself. Called from
         *  PaletteCache::Clear on video-mode reset / theater swap.
         */
        void Reset();

        /**
         *  Allocate a new layer, upload 256 × 4 RGBA8 bytes, return the
         *  layer index. Grows the underlying array if the current capacity
         *  is exhausted. Returns -1 on failure.
         */
        int Allocate_And_Upload(const uint8_t* rgba_256x4);

        /**
         *  Re-upload an existing layer's pixel data in-place. Used by
         *  `PaletteLUT::Update_Palette` when the same `PaletteLUT` is
         *  re-driven with new bytes (rare; PaletteCache is keyed on stable
         *  vanilla pointers).
         */
        bool Update_Layer(int layer, const uint8_t* rgba_256x4);

        ID3D11ShaderResourceView* Get_SRV() const { return SRV; }
        ID3D11Texture2D*          Get_Texture() const { return Texture; }

        int Layer_Count() const { return Count; }
        int Capacity()    const { return Capacity_; }

        bool Is_Initialized() const { return Device != nullptr; }

    private:
        PaletteArray() = default;

        bool Create_Array(int capacity);
        void Destroy_Array();
        bool Grow_To(int new_capacity);

        GraphicsDevice* Device = nullptr;
        ID3D11Texture2D*          Texture = nullptr;
        ID3D11ShaderResourceView* SRV     = nullptr;
        int Capacity_ = 0;
        int Count     = 0;

        /**
         *  CPU copy of every layer's bytes, kept so we can re-upload after a
         *  capacity grow (which destroys & re-creates the GPU resource).
         *  256 × 4 bytes per layer = 1 KB each — at 64 layers that's 64 KB,
         *  negligible.
         */
        std::vector<std::vector<uint8_t>> StagedLayers;
    };
}
