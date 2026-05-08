/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Compiled VS+PS+IL bundle.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    Effect::~Effect()
    {
        Shutdown();
    }


    bool Effect::Initialize(GraphicsDevice& device, const char* hlsl_source, size_t source_size,
                            const char* debug_name,
                            const D3D11_INPUT_ELEMENT_DESC* input_elements, UINT input_element_count,
                            size_t constant_buffer_size)
    {
        ID3D11Device* d3d_device = device.Get_Device();
        if (d3d_device == nullptr || hlsl_source == nullptr || source_size == 0) {
            return false;
        }

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        if (!Compile_HLSL(hlsl_source, source_size, debug_name, "VSMain", "vs_4_0", &vs_blob)) {
            return false;
        }
        if (!Compile_HLSL(hlsl_source, source_size, debug_name, "PSMain", "ps_4_0", &ps_blob)) {
            vs_blob->Release();
            return false;
        }

        if (FAILED(d3d_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &VS))) {
            vs_blob->Release(); ps_blob->Release();
            return false;
        }
        if (FAILED(d3d_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &PS))) {
            vs_blob->Release(); ps_blob->Release();
            return false;
        }

        if (input_elements != nullptr && input_element_count > 0) {
            if (FAILED(d3d_device->CreateInputLayout(input_elements, input_element_count,
                                                     vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
                                                     &InputLayout))) {
                vs_blob->Release(); ps_blob->Release();
                return false;
            }
        }
        vs_blob->Release();
        ps_blob->Release();

        if (constant_buffer_size > 0) {
            const size_t aligned = (constant_buffer_size + 15) & ~size_t(15);
            D3D11_BUFFER_DESC cb_desc = {};
            cb_desc.ByteWidth      = (UINT)aligned;
            cb_desc.Usage          = D3D11_USAGE_DYNAMIC;
            cb_desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(d3d_device->CreateBuffer(&cb_desc, nullptr, &ConstantBuffer))) {
                Shutdown();
                return false;
            }
            ConstantBufferSize = aligned;
        }
        return true;
    }


    void Effect::Shutdown()
    {
        Safe_Release(ConstantBuffer);
        Safe_Release(InputLayout);
        Safe_Release(PS);
        Safe_Release(VS);
        ConstantBufferSize = 0;
    }


    void Effect::Apply(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) {
            return;
        }
        ctx->IASetInputLayout(InputLayout);
        ctx->VSSetShader(VS, nullptr, 0);
        ctx->PSSetShader(PS, nullptr, 0);
        if (ConstantBuffer != nullptr) {
            ctx->VSSetConstantBuffers(0, 1, &ConstantBuffer);
            ctx->PSSetConstantBuffers(0, 1, &ConstantBuffer);
        }
    }


    void Effect::Set_Constants(GraphicsDevice& device, const void* data)
    {
        if (ConstantBuffer == nullptr || data == nullptr) {
            return;
        }
        ID3D11DeviceContext* ctx = device.Get_Context();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        memcpy(mapped.pData, data, ConstantBufferSize);
        ctx->Unmap(ConstantBuffer, 0);
    }
}
