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
        case EBlend::DestMultiplyHalf:
            /**
             *  output.rgb = (src*0) + (dest * blend_factor.rgb).
             *  Caller must set blend_factor to (0.5, 0.5, 0.5, *) at
             *  OMSetBlendState time. Alpha pass-through preserves dest.a.
             */
            bd.RenderTarget[0].BlendEnable    = TRUE;
            bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].DestBlend      = D3D11_BLEND_BLEND_FACTOR;
            bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case EBlend::DualSourceBlend:
            /**
             *  result = src0 + src1 * dest. Shader emits src0 in SV_Target0
             *  and src1 in SV_Target1, and chooses the values per-pixel so a
             *  single batch can mix what would otherwise be Premultiplied
             *  and DestMultiplyHalf state. See sprite_effect.cpp.
             */
            bd.RenderTarget[0].BlendEnable    = TRUE;
            bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlend      = D3D11_BLEND_SRC1_COLOR;
            bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_SRC1_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case EBlend::MinSrcDest:
            /**
             *  output = min(src, dest). Idempotent darken — writing the same
             *  src color multiple times to a pixel produces the same result.
             *  Used by voxel shadow rendering: at cardinal facings multiple
             *  shadow columns project to the same screen pixel, and we want
             *  the pixel darkened ONCE not compounded.
             */
            bd.RenderTarget[0].BlendEnable    = TRUE;
            bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlend      = D3D11_BLEND_ONE;
            bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_MIN;
            bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_MIN;
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
        case EDepthStencil::WriteLessEqual:
            dsd.DepthEnable = TRUE;
            dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            dsd.StencilEnable = FALSE;
            break;
        case EDepthStencil::TestLessEqual_NoWrite:
            dsd.DepthEnable = TRUE;
            dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            dsd.StencilEnable = FALSE;
            break;
        case EDepthStencil::WriteLess:
            // Strict LESS comparison: equal-depth writes are rejected. Used for
            // voxel shadow dedup — multiple shadow voxels at the same pixel
            // share the same fixed depth value, so the first write wins and
            // subsequent are skipped (preventing compound darken at cardinal
            // facings).
            dsd.DepthEnable = TRUE;
            dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsd.DepthFunc = D3D11_COMPARISON_LESS;
            dsd.StencilEnable = FALSE;
            break;
        default: return nullptr;
        }

        Device->CreateDepthStencilState(&dsd, &DepthStencils[idx]);
        return DepthStencils[idx];
    }
}
