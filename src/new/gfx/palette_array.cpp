/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Texture2DArray of paletted-sprite LUTs.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "palette_array.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    PaletteArray& PaletteArray::Get()
    {
        static PaletteArray instance;
        return instance;
    }


    bool PaletteArray::Initialize(GraphicsDevice& device, int initial_capacity)
    {
        if (Is_Initialized()) {
            return true;
        }
        Device = &device;
        if (initial_capacity < 1) initial_capacity = 1;
        if (!Create_Array(initial_capacity)) {
            Device = nullptr;
            return false;
        }
        DEBUG_INFO("PaletteArray: ready (256 x 1 x %d RGBA8).\n", Capacity_);
        return true;
    }


    void PaletteArray::Shutdown()
    {
        Destroy_Array();
        StagedLayers.clear();
        Capacity_ = 0;
        Count = 0;
        Device = nullptr;
    }


    void PaletteArray::Reset()
    {
        /**
         *  Drop all layers but keep the array texture allocated at the
         *  current capacity — re-uploads will re-fill it. Cheap.
         */
        Count = 0;
        StagedLayers.clear();
    }


    bool PaletteArray::Create_Array(int capacity)
    {
        Destroy_Array();
        if (Device == nullptr) return false;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width          = 256;
        desc.Height         = 1;
        desc.MipLevels      = 1;
        desc.ArraySize      = (UINT)capacity;
        desc.Format         = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage          = D3D11_USAGE_DEFAULT;
        desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(Device->Get_Device()->CreateTexture2D(&desc, nullptr, &Texture))) {
            DEBUG_ERROR("PaletteArray: CreateTexture2D failed (capacity=%d).\n", capacity);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        srv_desc.Texture2DArray.MostDetailedMip = 0;
        srv_desc.Texture2DArray.MipLevels       = 1;
        srv_desc.Texture2DArray.FirstArraySlice = 0;
        srv_desc.Texture2DArray.ArraySize       = (UINT)capacity;

        if (FAILED(Device->Get_Device()->CreateShaderResourceView(Texture, &srv_desc, &SRV))) {
            DEBUG_ERROR("PaletteArray: CreateShaderResourceView failed.\n");
            Safe_Release(Texture);
            return false;
        }

        Capacity_ = capacity;
        return true;
    }


    void PaletteArray::Destroy_Array()
    {
        Safe_Release(SRV);
        Safe_Release(Texture);
        Capacity_ = 0;
    }


    bool PaletteArray::Grow_To(int new_capacity)
    {
        if (new_capacity <= Capacity_) return true;
        if (Device == nullptr) return false;

        /**
         *  Keep staged copies aside, re-create at new capacity, then
         *  re-upload every existing layer's bytes into the new resource.
         */
        const int old_count = Count;
        DEBUG_INFO("PaletteArray: growing %d -> %d layers (re-uploading %d existing).\n",
            Capacity_, new_capacity, old_count);

        if (!Create_Array(new_capacity)) {
            return false;
        }
        ID3D11DeviceContext* ctx = Device->Get_Context();
        for (int i = 0; i < old_count; ++i) {
            const std::vector<uint8_t>& bytes = StagedLayers[i];
            D3D11_BOX box = { 0, 0, 0, 256, 1, 1 };
            ctx->UpdateSubresource(Texture, (UINT)D3D11CalcSubresource(0, (UINT)i, 1),
                                   &box, bytes.data(), 256 * 4, 256 * 4);
        }
        return true;
    }


    int PaletteArray::Allocate_And_Upload(const uint8_t* rgba_256x4)
    {
        if (rgba_256x4 == nullptr || !Is_Initialized()) {
            return -1;
        }
        if (Count >= Capacity_) {
            if (!Grow_To(Capacity_ * 2)) {
                return -1;
            }
        }

        const int layer = Count++;
        StagedLayers.emplace_back(rgba_256x4, rgba_256x4 + 256 * 4);

        ID3D11DeviceContext* ctx = Device->Get_Context();
        D3D11_BOX box = { 0, 0, 0, 256, 1, 1 };
        ctx->UpdateSubresource(Texture, (UINT)D3D11CalcSubresource(0, (UINT)layer, 1),
                               &box, rgba_256x4, 256 * 4, 256 * 4);
        return layer;
    }


    bool PaletteArray::Update_Layer(int layer, const uint8_t* rgba_256x4)
    {
        if (rgba_256x4 == nullptr || layer < 0 || layer >= Count) {
            return false;
        }
        std::vector<uint8_t>& bytes = StagedLayers[layer];
        bytes.assign(rgba_256x4, rgba_256x4 + 256 * 4);

        ID3D11DeviceContext* ctx = Device->Get_Context();
        D3D11_BOX box = { 0, 0, 0, 256, 1, 1 };
        ctx->UpdateSubresource(Texture, (UINT)D3D11CalcSubresource(0, (UINT)layer, 1),
                               &box, rgba_256x4, 256 * 4, 256 * 4);
        return true;
    }
}
