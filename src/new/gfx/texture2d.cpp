/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  RAII wrapper around ID3D11Texture2D + ID3D11ShaderResourceView.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "texture2d.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"

#include <cstring>


namespace Vinifera::Gfx
{
    Texture2D::~Texture2D()
    {
        Shutdown();
    }


    bool Texture2D::Initialize(GraphicsDevice& device, int width, int height, DXGI_FORMAT format,
                               D3D11_USAGE usage, const void* initial_data, int initial_pitch_bytes,
                               UINT bind_flags)
    {
        if (Texture != nullptr) {
            return true;
        }
        if (width <= 0 || height <= 0) {
            return false;
        }
        ID3D11Device* d3d_device = device.Get_Device();
        if (d3d_device == nullptr) {
            return false;
        }

        ParentDevice = d3d_device;
        ParentContext = device.Get_Context();
        TextureWidth = width;
        TextureHeight = height;
        TextureFormat = format;
        TextureUsage = usage;

        if (!Create_Resource(d3d_device, bind_flags, initial_data, initial_pitch_bytes)) {
            Shutdown();
            return false;
        }
        return true;
    }


    bool Texture2D::Create_Resource(ID3D11Device* device, UINT bind_flags,
                                    const void* initial_data, int initial_pitch)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = TextureWidth;
        td.Height = TextureHeight;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = TextureFormat;
        td.SampleDesc.Count = 1;
        td.Usage = TextureUsage;
        td.BindFlags = bind_flags;
        td.CPUAccessFlags = (TextureUsage == D3D11_USAGE_DYNAMIC) ? D3D11_CPU_ACCESS_WRITE : 0;

        D3D11_SUBRESOURCE_DATA init = {};
        const D3D11_SUBRESOURCE_DATA* init_ptr = nullptr;
        if (initial_data != nullptr && initial_pitch > 0) {
            init.pSysMem = initial_data;
            init.SysMemPitch = (UINT)initial_pitch;
            init_ptr = &init;
        }

        if (FAILED(device->CreateTexture2D(&td, init_ptr, &Texture))) {
            DEBUG_ERROR("Gfx::Texture2D: CreateTexture2D failed (%dx%d, fmt=%d).\n",
                TextureWidth, TextureHeight, (int)TextureFormat);
            return false;
        }

        if (bind_flags & D3D11_BIND_SHADER_RESOURCE) {
            if (FAILED(device->CreateShaderResourceView(Texture, nullptr, &SRV))) {
                DEBUG_ERROR("Gfx::Texture2D: CreateShaderResourceView failed.\n");
                return false;
            }
        }
        return true;
    }


    void Texture2D::Shutdown()
    {
        Safe_Release(SRV);
        Safe_Release(Texture);
        ParentDevice = nullptr;
        ParentContext = nullptr;
        TextureWidth = 0;
        TextureHeight = 0;
        TextureFormat = DXGI_FORMAT_UNKNOWN;
    }


    bool Texture2D::Set_Data(const void* pixels, int pitch_bytes)
    {
        if (Texture == nullptr || ParentContext == nullptr || pixels == nullptr) {
            return false;
        }

        if (TextureUsage == D3D11_USAGE_DYNAMIC) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(ParentContext->Map(Texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return false;
            }
            const unsigned char* src = static_cast<const unsigned char*>(pixels);
            unsigned char* dst = static_cast<unsigned char*>(mapped.pData);
            const int copy_pitch = pitch_bytes < (int)mapped.RowPitch ? pitch_bytes : (int)mapped.RowPitch;
            if ((UINT)pitch_bytes == mapped.RowPitch) {
                memcpy(dst, src, (size_t)pitch_bytes * TextureHeight);
            } else {
                for (int y = 0; y < TextureHeight; ++y) {
                    memcpy(dst + y * mapped.RowPitch, src + y * pitch_bytes, copy_pitch);
                }
            }
            ParentContext->Unmap(Texture, 0);
            return true;
        }

        ParentContext->UpdateSubresource(Texture, 0, nullptr, pixels, (UINT)pitch_bytes, 0);
        return true;
    }
}
