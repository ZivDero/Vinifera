/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lazy-cached pipeline-state presets.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "states.h"

#include "gfx_utils.h"


namespace Vinifera::Gfx
{
    StateCache::~StateCache()
    {
        Shutdown();
    }


    void StateCache::Initialize(ID3D11Device* device)
    {
        Device = device;
    }


    void StateCache::Shutdown()
    {
        for (auto& p : Blends)        Safe_Release(p);
        for (auto& p : Samplers)      Safe_Release(p);
        for (auto& p : Rasterizers)   Safe_Release(p);
        for (auto& p : DepthStencils) Safe_Release(p);
        Device = nullptr;
    }


    ID3D11BlendState* StateCache::Get(EBlend blend)
    {
        const size_t idx = (size_t)blend;
        if (Blends[idx] != nullptr) {
            return Blends[idx];
        }
        if (Device == nullptr) {
            return nullptr;
        }

        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        switch (blend) {
        case EBlend::Opaque:
            bd.RenderTarget[0].BlendEnable = FALSE;
            break;
        case EBlend::AlphaBlend:
            bd.RenderTarget[0].BlendEnable    = TRUE;
            bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case EBlend::Premultiplied:
            bd.RenderTarget[0].BlendEnable    = TRUE;
            bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case EBlend::Additive:
            bd.RenderTarget[0].BlendEnable    = TRUE;
            bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend      = D3D11_BLEND_ONE;
            bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        default:
            return nullptr;
        }

        Device->CreateBlendState(&bd, &Blends[idx]);
        return Blends[idx];
    }


    ID3D11SamplerState* StateCache::Get(ESampler sampler)
    {
        const size_t idx = (size_t)sampler;
        if (Samplers[idx] != nullptr) {
            return Samplers[idx];
        }
        if (Device == nullptr) {
            return nullptr;
        }

        D3D11_SAMPLER_DESC sd = {};
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = D3D11_FLOAT32_MAX;

        switch (sampler) {
        case ESampler::LinearClamp: sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; break;
        case ESampler::PointClamp:  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;  break;
        default: return nullptr;
        }

        Device->CreateSamplerState(&sd, &Samplers[idx]);
        return Samplers[idx];
    }


    ID3D11RasterizerState* StateCache::Get(ERasterizer rasterizer)
    {
        const size_t idx = (size_t)rasterizer;
        if (Rasterizers[idx] != nullptr) {
            return Rasterizers[idx];
        }
        if (Device == nullptr) {
            return nullptr;
        }

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;

        switch (rasterizer) {
        case ERasterizer::CullNone:        rd.ScissorEnable = FALSE; break;
        case ERasterizer::CullNoneScissor: rd.ScissorEnable = TRUE;  break;
        default: return nullptr;
        }

        Device->CreateRasterizerState(&rd, &Rasterizers[idx]);
        return Rasterizers[idx];
    }


    ID3D11DepthStencilState* StateCache::Get(EDepthStencil depth_stencil)
    {
        const size_t idx = (size_t)depth_stencil;
        if (DepthStencils[idx] != nullptr) {
            return DepthStencils[idx];
        }
        if (Device == nullptr) {
            return nullptr;
        }

        D3D11_DEPTH_STENCIL_DESC dsd = {};
        switch (depth_stencil) {
        case EDepthStencil::None:
            dsd.DepthEnable = FALSE;
            dsd.StencilEnable = FALSE;
            break;
        default: return nullptr;
        }

        Device->CreateDepthStencilState(&dsd, &DepthStencils[idx]);
        return DepthStencils[idx];
    }
}
