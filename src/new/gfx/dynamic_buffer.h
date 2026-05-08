/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  D3D11_USAGE_DYNAMIC vertex/index buffer that grows on demand.
 *
 *          Begin(count) returns a writable T* of `count` elements via
 *          Map(WRITE_DISCARD). End() unmaps and binds. If the requested count
 *          exceeds capacity, the buffer is recreated at 1.5x the requested
 *          size before the map. Suitable for per-frame upload of streamed
 *          geometry (sprite batches, ImGui draw lists, etc.).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>

#include "gfx_utils.h"


namespace Vinifera::Gfx
{
    template<typename T>
    class DynamicVertexBuffer
    {
    public:
        DynamicVertexBuffer() = default;
        ~DynamicVertexBuffer() { Shutdown(); }

        DynamicVertexBuffer(const DynamicVertexBuffer&) = delete;
        DynamicVertexBuffer& operator=(const DynamicVertexBuffer&) = delete;

        void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, int initial_capacity = 1024)
        {
            Device = device;
            Context = context;
            Capacity = 0;
            Ensure_Capacity(initial_capacity);
        }

        void Shutdown()
        {
            Safe_Release(Buffer);
            Capacity = 0;
            Device = nullptr;
            Context = nullptr;
        }

        ID3D11Buffer* Get() const { return Buffer; }
        int Get_Capacity() const { return Capacity; }

        /**
         *  Returns a writable pointer to `count` elements. The caller must call
         *  End() before Begin() is called again or the buffer is bound.
         */
        T* Begin(int count)
        {
            if (Device == nullptr || count <= 0) {
                return nullptr;
            }
            Ensure_Capacity(count);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return nullptr;
            }
            return static_cast<T*>(mapped.pData);
        }

        void End()
        {
            if (Buffer != nullptr) {
                Context->Unmap(Buffer, 0);
            }
        }

    private:
        void Ensure_Capacity(int required)
        {
            if (required <= Capacity && Buffer != nullptr) {
                return;
            }
            Safe_Release(Buffer);
            Capacity = (required * 3) / 2;
            if (Capacity < 64) Capacity = 64;

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth      = (UINT)(Capacity * sizeof(T));
            desc.Usage          = D3D11_USAGE_DYNAMIC;
            desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Device->CreateBuffer(&desc, nullptr, &Buffer);
        }

        ID3D11Device*        Device = nullptr;
        ID3D11DeviceContext* Context = nullptr;
        ID3D11Buffer*        Buffer = nullptr;
        int                  Capacity = 0;
    };


    /**
     *  16-bit index buffer specialization. ImGui wants 16-bit; sprite batches
     *  too (since each batch is small).
     */
    class DynamicIndexBuffer
    {
    public:
        DynamicIndexBuffer() = default;
        ~DynamicIndexBuffer() { Shutdown(); }

        DynamicIndexBuffer(const DynamicIndexBuffer&) = delete;
        DynamicIndexBuffer& operator=(const DynamicIndexBuffer&) = delete;

        void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, int initial_capacity = 1024)
        {
            Device = device;
            Context = context;
            Capacity = 0;
            Ensure_Capacity(initial_capacity);
        }

        void Shutdown()
        {
            Safe_Release(Buffer);
            Capacity = 0;
            Device = nullptr;
            Context = nullptr;
        }

        ID3D11Buffer* Get() const { return Buffer; }
        int Get_Capacity() const { return Capacity; }

        unsigned short* Begin(int count)
        {
            if (Device == nullptr || count <= 0) {
                return nullptr;
            }
            Ensure_Capacity(count);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(Context->Map(Buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return nullptr;
            }
            return static_cast<unsigned short*>(mapped.pData);
        }

        void End()
        {
            if (Buffer != nullptr) {
                Context->Unmap(Buffer, 0);
            }
        }

    private:
        void Ensure_Capacity(int required)
        {
            if (required <= Capacity && Buffer != nullptr) {
                return;
            }
            Safe_Release(Buffer);
            Capacity = (required * 3) / 2;
            if (Capacity < 256) Capacity = 256;

            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth      = (UINT)(Capacity * sizeof(unsigned short));
            desc.Usage          = D3D11_USAGE_DYNAMIC;
            desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Device->CreateBuffer(&desc, nullptr, &Buffer);
        }

        ID3D11Device*        Device = nullptr;
        ID3D11DeviceContext* Context = nullptr;
        ID3D11Buffer*        Buffer = nullptr;
        int                  Capacity = 0;
    };
}
