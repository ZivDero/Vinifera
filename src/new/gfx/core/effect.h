/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Bundle of compiled VS + PS + InputLayout, plus an optional set of
 *          named constant-buffer slots for parameter upload.
 *
 *          No parameter reflection or technique/pass machinery — enough to
 *          factor out shader-bundle construction into one bindable object.
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
         *  Load pre-compiled bytecode embedded in the DLL as RCDATA resources.
         *  `shader_name` is the unqualified manifest name (uppercase by
         *  convention, e.g. "WAVE"); the helper looks up `<NAME>_VS` and
         *  `<NAME>_PS` via `FindResource`. Profiles are baked into the
         *  bytecode at build time — no runtime D3DCompile.
         */
        bool Initialize(GraphicsDevice& device, const char* shader_name,
                        const D3D11_INPUT_ELEMENT_DESC* input_elements, UINT input_element_count,
                        size_t constant_buffer_size = 0);

        void Shutdown();

        void Apply(GraphicsDevice& device);  // bind VS, PS and InputLayout

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
