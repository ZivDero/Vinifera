/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-VXL section vertex buffers for GPU voxel rasterization.
 *
 *          On first use of a `VoxelLibraryClass`, walk every section's RLE
 *          column data and emit one `VoxelVertex` per occupied voxel into an
 *          immutable D3D11 vertex buffer. A second buffer per section stores
 *          one vertex per occupied (x, y) column for the shadow pass.
 *
 *          Each section also caches `XSize/YSize/ZSize`, the `NormalType`
 *          (which selects one of vanilla's four normal tables in the pixel
 *          shader), and the eight `Bounds` corners needed to derive the
 *          per-section screen transform at draw time.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>
#include <unordered_map>
#include <vector>

#include "vector3.h"


class VoxelLibraryClass;


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    /**
     *  One vertex per occupied voxel. `X/Y/Z` are voxel-grid coordinates,
     *  `ColorIndex` is the VXL palette index, `NormalIndex` is the index
     *  into the normal table picked by the owning section's `NormalType`.
     *  Packed to 8 bytes so a 10k-voxel section is ~80 KB on the GPU.
     */
    struct VoxelVertex
    {
        uint8_t X;
        uint8_t Y;
        uint8_t Z;
        uint8_t ColorIndex;
        uint8_t NormalIndex;
        uint8_t Pad0;
        uint8_t Pad1;
        uint8_t Pad2;
    };


    /**
     *  Per-section data cached for a VoxelLibrary. The vertex buffer is
     *  immutable; the section's `Bounds`/sizes/`NormalType` are mirrored
     *  here so the queue doesn't need to re-resolve them at draw time.
     */
    struct VoxelSectionMesh
    {
        ID3D11Buffer*  VertexBuffer = nullptr;
        ID3D11Buffer*  ShadowVertexBuffer = nullptr;
        uint32_t       VertexCount = 0;
        uint32_t       ShadowVertexCount = 0;

        uint8_t        XSize = 0;
        uint8_t        YSize = 0;
        uint8_t        ZSize = 0;
        uint8_t        NormalType = 0;

        Vector3        Bounds[8] = {};
    };


    class VoxelAsset
    {
    public:
        VoxelAsset() = default;
        ~VoxelAsset();

        VoxelAsset(const VoxelAsset&) = delete;
        VoxelAsset& operator=(const VoxelAsset&) = delete;

        /**
         *  Decode every section of `library` (layer * info combos) into
         *  vertex buffers. Idempotent: returns true if already loaded.
         */
        bool Build(GraphicsDevice& device, VoxelLibraryClass* library);

        void Shutdown();

        bool Is_Loaded() const { return Loaded; }

        /**
         *  Get the mesh for (layer, info). Returns nullptr if the section is
         *  empty or out of range.
         */
        const VoxelSectionMesh* Get_Section(int layer, int info) const;

        int Layer_Count() const { return LayerCount; }
        int Layer_Info_Count() const { return LayerInfoCount; }

    private:
        bool Build_Section(GraphicsDevice& device, VoxelLibraryClass* library, int layer, int info);

        std::vector<VoxelSectionMesh> Sections;     // indexed by (layer * LayerInfoCount + info)
        int  LayerCount = 0;
        int  LayerInfoCount = 0;
        bool Loaded = false;
    };


    /**
     *  Global keyed cache mapping `VoxelLibraryClass*` → `VoxelAsset`. The
     *  VoxelLibrary itself is a CPU-side asset owned by vanilla, so we key
     *  by raw pointer (the pointer is stable for the lifetime of the
     *  library).
     */
    class VoxelAssetCache
    {
    public:
        static VoxelAssetCache& Get();

        VoxelAsset* Get_Or_Build(GraphicsDevice& device, VoxelLibraryClass* library);

        void Shutdown();

    private:
        VoxelAssetCache() = default;

        std::unordered_map<VoxelLibraryClass*, VoxelAsset*> Cache;
    };
}
