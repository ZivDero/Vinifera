/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette-LUT sprite Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sprite_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_array.h"
#include "palette_lut.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC SpriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_UINT,           0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_UINT,           0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

    }


    bool SpriteEffect::Initialize(GraphicsDevice& device)
    {
        /**
         *  b0 holds SpriteBatch's ProjMtx (float4x4 = 64 bytes); b1 holds the
         *  per-effect SpriteEffectParams. Effect::Initialize creates the b0
         *  CB; SpriteBatch::End writes ProjMtx into it via Set_Constants.
         */
        if (!Effect::Initialize(device, "PALETTE_SPRITE",
                                SpriteIL, _countof(SpriteIL),
                                /* SpriteCB at b0 — float4x4 ProjMtx */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(SpriteEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("SpriteEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void SpriteEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void SpriteEffect::Bind_Palette_Array(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srv = PaletteArray::Get().Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
    }


    void SpriteEffect::Set_Params(GraphicsDevice& device, const SpriteEffectParams& params)
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
