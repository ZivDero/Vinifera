/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette-LUT font Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "font_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_lut.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC FontIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

    }


    bool FontEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device, "FONT",
                                FontIL, _countof(FontIL),
                                /* SpriteCB at b0 — float4x4 ProjMtx */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(FontEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("FontEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void FontEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void FontEffect::Bind_Palette(GraphicsDevice& device, PaletteLUT& palette)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srv = palette.Get_Palette_Texture().Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
    }


    void FontEffect::Set_Params(GraphicsDevice& device, const FontEffectParams& params)
    {
        if (ParamsCB == nullptr) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(ParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        memcpy(mapped.pData, &params, sizeof(params));
        ctx->Unmap(ParamsCB, 0);

        ctx->VSSetConstantBuffers(1, 1, &ParamsCB);
        ctx->PSSetConstantBuffers(1, 1, &ParamsCB);
    }
}
