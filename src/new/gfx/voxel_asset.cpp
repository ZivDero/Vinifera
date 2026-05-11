/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-VXL section vertex buffers for GPU voxel rasterization.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_asset.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "voxel.hh"
#include "voxellib.h"

#include <climits>
#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Decode one column's RLE bytes, emitting one VoxelVertex per
         *  occupied Z slot. Mirrors vanilla's `Draw_Voxel_Regular_Normals`
         *  inner loop — `delta` skips empty Z slots, `run_length` voxels
         *  each carry (color, normal), then a trailing backward-run byte
         *  (which we ignore on the forward decode). See voxlib.cpp:923 in
         *  the source drop for the canonical implementation.
         */
        void Decode_Column(const unsigned char* ptr, int x, int y, int z_size,
                           std::vector<VoxelVertex>& out)
        {
            int z = 0;
            int remaining = z_size;
            while (remaining > 0) {
                const unsigned int skip = *ptr++;
                z += (int)skip;
                remaining -= (int)skip;

                const unsigned int run = *ptr++;
                if (run > 0) {
                    for (unsigned int i = 0; i < run && remaining > 0; ++i) {
                        const uint8_t color_index  = *ptr++;
                        const uint8_t normal_index = *ptr++;
                        VoxelVertex v = {};
                        v.X = (uint8_t)x;
                        v.Y = (uint8_t)y;
                        v.Z = (uint8_t)z;
                        v.ColorIndex  = color_index;
                        v.NormalIndex = normal_index;
                        out.push_back(v);
                        ++z;
                        --remaining;
                    }
                }
                /* trailing backward-run byte; consumed but unused */
                ++ptr;
            }
        }


        ID3D11Buffer* Create_Immutable_VB(ID3D11Device* d3d, const void* data, size_t bytes, const char* dbg)
        {
            if (d3d == nullptr || data == nullptr || bytes == 0) {
                return nullptr;
            }
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = (UINT)bytes;
            desc.Usage     = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = data;
            ID3D11Buffer* vb = nullptr;
            if (FAILED(d3d->CreateBuffer(&desc, &init, &vb))) {
                DEBUG_ERROR("VoxelAsset: failed to create immutable VB (%s, %zu bytes).\n", dbg, bytes);
                return nullptr;
            }
            return vb;
        }
    }


    VoxelAsset::~VoxelAsset()
    {
        Shutdown();
    }


    void VoxelAsset::Shutdown()
    {
        for (VoxelSectionMesh& s : Sections) {
            Safe_Release(s.VertexBuffer);
            Safe_Release(s.ShadowVertexBuffer);
        }
        Sections.clear();
        LayerCount = 0;
        LayerInfoCount = 0;
        Loaded = false;
    }


    bool VoxelAsset::Build(GraphicsDevice& device, VoxelLibraryClass* library)
    {
        if (Loaded) {
            return true;
        }
        if (library == nullptr || library->Load_Failed()) {
            return false;
        }

        LayerCount     = library->Get_Layer_Count();
        LayerInfoCount = 1;  // we only build (layer, info=0) per layer
        if (LayerCount <= 0) {
            return false;
        }

        Sections.resize((size_t)LayerCount);

        /**
         *  Vanilla's `Get_Layer_Info(layer, info)` resolves to
         *  `LayerInfos[LayerHeaders[layer].InfoIndex + info]` with no
         *  bounds check. Each layer typically has exactly one info slot
         *  starting at `InfoIndex`; iterating beyond that walks past
         *  valid data. We use info=0 only — matches what the renderer
         *  invokes at draw time.
         */
        for (int layer = 0; layer < LayerCount; ++layer) {
            Build_Section(device, library, layer, 0);
        }

        Loaded = true;
        return true;
    }


    bool VoxelAsset::Build_Section(GraphicsDevice& device, VoxelLibraryClass* library, int layer, int info)
    {
        VoxelLibraryClass::LayerInfoStruct* li = library->Get_Layer_Info(layer, info);
        if (li == nullptr) {
            return false;
        }
        VoxelSectionMesh& mesh = Sections[(size_t)layer];
        mesh.XSize      = li->XSize;
        mesh.YSize      = li->YSize;
        mesh.ZSize      = li->ZSize;
        mesh.NormalType = li->NormalType;
        for (int i = 0; i < VOXEL_BOUNDS_MAX; ++i) {
            mesh.Bounds[i] = li->Bounds[i];
        }

        const int x_size = li->XSize;
        const int y_size = li->YSize;
        const int z_size = li->ZSize;
        if (x_size <= 0 || y_size <= 0 || z_size <= 0) {
            return true;
        }
        if (li->StartOffset == nullptr || li->DataOffset == nullptr) {
            return true;
        }

        const unsigned int* column_offsets = reinterpret_cast<const unsigned int*>(li->StartOffset);
        const unsigned int* end_offsets    = reinterpret_cast<const unsigned int*>(li->EndOffset);

        std::vector<VoxelVertex> verts;
        verts.reserve((size_t)x_size * (size_t)y_size * 4);

        std::vector<VoxelVertex> shadow_verts;
        shadow_verts.reserve((size_t)x_size * (size_t)y_size);

        for (int y = 0; y < y_size; ++y) {
            for (int x = 0; x < x_size; ++x) {
                const int idx = y * x_size + x;
                const unsigned int data_offset = column_offsets[idx];
                if (data_offset != UINT_MAX) {
                    Decode_Column(li->DataOffset + data_offset, x, y, z_size, verts);
                }
                /**
                 *  Shadow column footprint: one vertex per (x, y) column
                 *  that has any solid voxels. Vanilla's `Render_Shadow`
                 *  uses `EndOffset` as a presence mask the same way.
                 */
                if (end_offsets != nullptr && end_offsets[idx] != UINT_MAX) {
                    VoxelVertex sv = {};
                    sv.X = (uint8_t)x;
                    sv.Y = (uint8_t)y;
                    sv.Z = 0;
                    sv.ColorIndex = 0;
                    sv.NormalIndex = 0;
                    shadow_verts.push_back(sv);
                }
            }
        }

        ID3D11Device* d3d = device.Get_Device();
        if (!verts.empty()) {
            mesh.VertexBuffer = Create_Immutable_VB(d3d, verts.data(), verts.size() * sizeof(VoxelVertex), "voxel_vb");
            mesh.VertexCount  = (uint32_t)verts.size();
        }
        if (!shadow_verts.empty()) {
            mesh.ShadowVertexBuffer = Create_Immutable_VB(d3d, shadow_verts.data(), shadow_verts.size() * sizeof(VoxelVertex), "voxel_shadow_vb");
            mesh.ShadowVertexCount  = (uint32_t)shadow_verts.size();
        }
        return true;
    }


    const VoxelSectionMesh* VoxelAsset::Get_Section(int layer, int info) const
    {
        if (layer < 0 || layer >= LayerCount || info != 0) {
            return nullptr;
        }
        return &Sections[(size_t)layer];
    }


    VoxelAssetCache& VoxelAssetCache::Get()
    {
        static VoxelAssetCache instance;
        return instance;
    }


    VoxelAsset* VoxelAssetCache::Get_Or_Build(GraphicsDevice& device, VoxelLibraryClass* library)
    {
        if (library == nullptr) {
            return nullptr;
        }
        auto it = Cache.find(library);
        if (it != Cache.end()) {
            return it->second;
        }
        VoxelAsset* asset = new VoxelAsset();
        if (!asset->Build(device, library)) {
            delete asset;
            return nullptr;
        }
        Cache[library] = asset;
        return asset;
    }


    void VoxelAssetCache::Shutdown()
    {
        for (auto& kv : Cache) {
            delete kv.second;
        }
        Cache.clear();
    }
}
