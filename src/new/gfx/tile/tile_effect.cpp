/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Palette-LUT tile Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tile_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_lut.h"
#include "tibsun_globals.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC TileIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

    }


    bool TileEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device, "TILE",
                                TileIL, _countof(TileIL),
                                /* SpriteCB at b0 — float4x4 ProjMtx, 64 bytes */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(TileEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("TileEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }

        /**
         *  Upload vanilla's `_default_mask` (256 bools, all true on a stock
         *  build) as a 256x1 R8_UNORM texture for the shader's `is_tint`
         *  branch. The mask is a compile-time constant in vanilla — no need
         *  to refresh per-frame.
         */
        uint8_t mask_bytes[256];
        for (int i = 0; i < 256; ++i) {
            mask_bytes[i] = DefaultTintMask[i] ? 255 : 0;
        }
        if (!TintMaskTex.Initialize(device, 256, 1, DXGI_FORMAT_R8_UNORM,
                                    D3D11_USAGE_DEFAULT, mask_bytes, 256)) {
            DEBUG_ERROR("TileEffect: TintMaskTex creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void TileEffect::Shutdown()
    {
        TintMaskTex.Shutdown();
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void TileEffect::Bind_Palette(GraphicsDevice& device, PaletteLUT& palette)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srv = palette.Get_Palette_Texture().Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
    }


    void TileEffect::Bind_Tint_Mask(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srv = TintMaskTex.Get_SRV();
        ctx->PSSetShaderResources(4, 1, &srv);
    }


    void TileEffect::Set_Params(GraphicsDevice& device, const TileEffectParams& params)
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
