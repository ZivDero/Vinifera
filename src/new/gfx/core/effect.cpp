/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Compiled VS+PS+IL bundle. Loads bytecode from RCDATA resources
 *          baked into Vinifera.dll at build time (see
 *          cmake/modules/CompileShaders.cmake and generated/shaders.rc).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"

#include <windows.h>

#include <cstdio>
#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Look up the Vinifera.dll module handle. `GetModuleHandle(nullptr)`
         *  would return the host EXE (TS.EXE), which doesn't carry our
         *  resources — use the from-address variant against a symbol that
         *  lives in this DLL.
         */
        HMODULE Get_Self_Module()
        {
            HMODULE mod = nullptr;
            GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
              | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&Get_Self_Module),
                &mod);
            return mod;
        }


        bool Load_Shader_Resource(const char* resource_name,
                                  const void*& out_data, DWORD& out_size)
        {
            out_data = nullptr;
            out_size = 0;

            HMODULE mod = Get_Self_Module();
            if (mod == nullptr) {
                DEBUG_ERROR("Effect: GetModuleHandleEx failed (resource %s).\n", resource_name);
                return false;
            }
            HRSRC h_rsrc = FindResourceA(mod, resource_name, MAKEINTRESOURCEA(RT_RCDATA));
            if (h_rsrc == nullptr) {
                DEBUG_ERROR("Effect: FindResource(%s) failed.\n", resource_name);
                return false;
            }
            HGLOBAL h_glob = LoadResource(mod, h_rsrc);
            if (h_glob == nullptr) {
                DEBUG_ERROR("Effect: LoadResource(%s) failed.\n", resource_name);
                return false;
            }
            out_data = LockResource(h_glob);
            out_size = SizeofResource(mod, h_rsrc);
            if (out_data == nullptr || out_size == 0) {
                DEBUG_ERROR("Effect: LockResource/SizeofResource(%s) returned empty.\n", resource_name);
                return false;
            }
            return true;
        }
    }


    Effect::~Effect()
    {
        Shutdown();
    }


    bool Effect::Initialize(GraphicsDevice& device, const char* shader_name,
                            const D3D11_INPUT_ELEMENT_DESC* input_elements, UINT input_element_count,
                            size_t constant_buffer_size)
    {
        ID3D11Device* d3d_device = device.Get_Device();
        if (d3d_device == nullptr || shader_name == nullptr) {
            return false;
        }

        char vs_resource[64];
        char ps_resource[64];
        std::snprintf(vs_resource, sizeof(vs_resource), "%s_VS", shader_name);
        std::snprintf(ps_resource, sizeof(ps_resource), "%s_PS", shader_name);

        const void* vs_bytes = nullptr;
        DWORD       vs_size  = 0;
        if (!Load_Shader_Resource(vs_resource, vs_bytes, vs_size)) {
            return false;
        }

        const void* ps_bytes = nullptr;
        DWORD       ps_size  = 0;
        if (!Load_Shader_Resource(ps_resource, ps_bytes, ps_size)) {
            return false;
        }

        if (FAILED(d3d_device->CreateVertexShader(vs_bytes, vs_size, nullptr, &VS))) {
            DEBUG_ERROR("Effect: CreateVertexShader(%s) failed.\n", shader_name);
            return false;
        }
        if (FAILED(d3d_device->CreatePixelShader(ps_bytes, ps_size, nullptr, &PS))) {
            DEBUG_ERROR("Effect: CreatePixelShader(%s) failed.\n", shader_name);
            return false;
        }

        if (input_elements != nullptr && input_element_count > 0) {
            if (FAILED(d3d_device->CreateInputLayout(input_elements, input_element_count,
                                                     vs_bytes, vs_size, &InputLayout))) {
                DEBUG_ERROR("Effect: CreateInputLayout(%s) failed.\n", shader_name);
                return false;
            }
        }

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
