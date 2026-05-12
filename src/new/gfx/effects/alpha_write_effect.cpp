/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Alpha-write Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "alpha_write_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Reuse SpriteBatch's vertex layout (Pos, UV, ZUV, Tint). ZUV/Tint go
         *  unused by the alpha-write pixel shader; we still declare them so
         *  the IL matches the shared SpriteVertex stride and we can drive the
         *  effect from `SpriteBatch::Draw`.
         */
        const D3D11_INPUT_ELEMENT_DESC AlphaWriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };


    }


    bool AlphaWriteEffect::Initialize(GraphicsDevice& device)
    {
        /**
         *  PS_5_0 required for `RWTexture2D` writes from a pixel shader. The
         *  device is created at FL 11.0+ so this is always available. The
         *  profile is baked into the .cso at build time via SHADER_MANIFEST.
         */
        if (!Effect::Initialize(device, "ALPHA_WRITE",
                                AlphaWriteIL, _countof(AlphaWriteIL),
                                /* SpriteCB at b0 */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(AlphaWriteEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("AlphaWriteEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void AlphaWriteEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void AlphaWriteEffect::Set_Params(GraphicsDevice& device, const AlphaWriteEffectParams& params)
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
