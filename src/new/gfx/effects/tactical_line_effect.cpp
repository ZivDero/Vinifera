/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Depth- and alpha-aware tactical line Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tactical_line_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC TacticalLineIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT,    0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

    }


    bool TacticalLineEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device, "TACTICAL_LINE",
                                TacticalLineIL, _countof(TacticalLineIL),
                                /* SpriteCB at b0 — float4x4 ProjMtx */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(TacticalLineEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("TacticalLineEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void TacticalLineEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void TacticalLineEffect::Bind_Sources(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srvs[2] = {
            device.Get_Depth_SRV(),
            device.Get_Alpha_SRV(),
        };
        ctx->PSSetShaderResources(0, 2, srvs);
    }


    void TacticalLineEffect::Set_Params(GraphicsDevice& device, const TacticalLineEffectParams& params)
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
