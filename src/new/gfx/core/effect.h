/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Bundle of compiled VS + PS + InputLayout, plus an optional set of
 *          named constant-buffer slots for parameter upload.
 *
 *          Lighter than MonoGame's Effect (no parameter reflection, no
 *          technique/pass machinery) but enough to factor out shader-bundle
 *          construction into one object that gets bound via Apply().
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>


namespace Vinifera::Gfx
{
    class GraphicsDevice;

    class Effect
    {
    public:
        Effect() = default;
        ~Effect();

        Effect(const Effect&) = delete;
        Effect& operator=(const Effect&) = delete;

        /**
         *  Compile from a single HLSL source string with VS entry "VSMain" and
         *  PS entry "PSMain". Profiles default to vs_4_0/ps_4_0; pass
         *  ps_5_0/vs_5_0 for shader features that need it (UAV writes from
         *  pixel shader, etc.).
         */
        bool Initialize(GraphicsDevice& device, const char* hlsl_source, size_t source_size,
                        const char* debug_name,
                        const D3D11_INPUT_ELEMENT_DESC* input_elements, UINT input_element_count,
                        size_t constant_buffer_size = 0,
                        const char* vs_profile = "vs_4_0",
                        const char* ps_profile = "ps_4_0");

        void Shutdown();

        /**
         *  Bind VS, PS and InputLayout. Caller is responsible for binding
         *  vertex/index buffers, primitive topology, and any custom CB/SRVs.
         */
        void Apply(GraphicsDevice& device);

        /**
         *  Update and bind the effect's optional constant buffer at slot b0
         *  (both VS and PS). `data` must be `constant_buffer_size` bytes.
         */
        void Set_Constants(GraphicsDevice& device, const void* data);

        ID3D11VertexShader* Get_VS() const { return VS; }
        ID3D11PixelShader*  Get_PS() const { return PS; }
        ID3D11InputLayout*  Get_Input_Layout() const { return InputLayout; }
        ID3D11Buffer*       Get_Constant_Buffer() const { return ConstantBuffer; }

    private:
        ID3D11VertexShader* VS = nullptr;
        ID3D11PixelShader*  PS = nullptr;
        ID3D11InputLayout*  InputLayout = nullptr;
        ID3D11Buffer*       ConstantBuffer = nullptr;
        size_t              ConstantBufferSize = 0;
    };
}
