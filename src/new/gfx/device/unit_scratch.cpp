/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-unit scratch render target for composite-unit rendering.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "unit_scratch.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "rect.h"
#include "render_target_2d.h"
#include "states.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Composite blit shader. Samples the scratch RT (premultiplied RGBA
         *  output from the voxel/SHP pipeline that drew INTO it) and scales
         *  by a unit-level alpha. Outputs the result with premultiplied alpha
         *  so the caller's `EBlend::Premultiplied` blend state produces the
         *  correct `scratch*unit_alpha + (1 - scratch.a*unit_alpha)*scene`.
         */
        const char UnitCompositeHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float Alpha;\n"
            "    float3 _Pad;\n"
            "};\n"
            "\n"
            "struct VSIn {\n"
            "    float3 pos    : POSITION;\n"
            "    float2 uv     : TEXCOORD0;\n"
            "    float2 zuv    : TEXCOORD1;\n"
            "    float4 col    : COLOR0;\n"
            "    uint   layer  : TEXCOORD2;\n"
            "    uint   pflags : TEXCOORD3;\n"
            "};\n"
            "struct VSOut {\n"
            "    float4 pos : SV_Position;\n"
            "    float2 uv  : TEXCOORD0;\n"
            "};\n"
            "\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos = float4(p.x, p.y, i.pos.z, 1);\n"
            "    o.uv  = i.uv;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<float4> Scratch : register(t0);\n"
            "SamplerState      PointS  : register(s0);\n"
            "\n"
            "struct PSOut {\n"
            "    float4 color : SV_Target;\n"
            "    float  depth : SV_Depth;\n"
            "};\n"
            "\n"
            "PSOut PSMain(VSOut v) {\n"
            "    float4 c = Scratch.Sample(PointS, v.uv);\n"
            "    // Discard fully-transparent scratch pixels so we don't trip\n"
            "    // the depth test on empty unit area outside the unit's actual\n"
            "    // voxel footprint.\n"
            "    if (c.a <= 0.0) discard;\n"
            "    PSOut o;\n"
            "    // Scratch is already premultiplied. Scale by unit alpha so\n"
            "    // the final scene blend is `(scratch*unit_a) + (1 - scratch.a*unit_a)*scene`.\n"
            "    o.color = c * Alpha;\n"
            "    // Per-pixel SV_Depth: emit a depth value that just barely beats\n"
            "    // terrain at THIS pixel's screen-Y. Terrain depth at pixel Y is\n"
            "    // `1 - Y * kPixelToDepth` (1/16000); subtract\n"
            "    // a small eps so composite consistently wins LessEqual against\n"
            "    // terrain across the whole 256x256 unit footprint. Without this,\n"
            "    // a single per-unit depth value misses on roughly half the unit\n"
            "    // (where terrain happens to be closer than our chosen baseline).\n"
            "    const float kPixelToDepth = 1.0 / 16000.0;\n"
            "    o.depth = clamp(1.0 - v.pos.y * kPixelToDepth - 1.0e-4, 1.0e-4, 0.9999);\n"
            "    return o;\n"
            "}\n";


        /**
         *  Matches the SpriteIL layout from sprite_effect.cpp so SpriteBatch
         *  can drive this effect with its standard `Draw(...)` overloads.
         */
        const D3D11_INPUT_ELEMENT_DESC UnitCompositeIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_UINT,           0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_UINT,           0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    bool UnitCompositeEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                UnitCompositeHLSL, sizeof(UnitCompositeHLSL) - 1,
                "unit_composite",
                UnitCompositeIL, _countof(UnitCompositeIL),
                /* SpriteCB at b0 */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(Params);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("UnitCompositeEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void UnitCompositeEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void UnitCompositeEffect::Set_Params(GraphicsDevice& device, const Params& params)
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


    UnitScratch& UnitScratch::Get()
    {
        static UnitScratch instance;
        return instance;
    }


    bool UnitScratch::Initialize(GraphicsDevice& device)
    {
        if (Initialized) return true;

        if (!Ensure_Targets(device)) {
            return false;
        }
        if (!CompositeBatch.Initialize(device, /*max_quads_per_batch*/ 4)) {
            Release_Targets();
            return false;
        }
        if (!CompositeFx.Initialize(device)) {
            CompositeBatch.Shutdown();
            Release_Targets();
            return false;
        }
        Initialized = true;
        return true;
    }


    void UnitScratch::Shutdown()
    {
        CompositeFx.Shutdown();
        CompositeBatch.Shutdown();
        Release_Targets();
        Safe_Release(SavedRTV);
        Safe_Release(SavedDSV);
        SavedVPCount = 0;
        Initialized = false;
    }


    bool UnitScratch::Ensure_Targets(GraphicsDevice& device)
    {
        if (ScratchRT != nullptr && DepthDSV != nullptr) return true;
        Release_Targets();

        ScratchRT = new RenderTarget2D();
        if (ScratchRT == nullptr) return false;
        if (!ScratchRT->Initialize(device, kUnitScratchWidth, kUnitScratchHeight,
                                   DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("UnitScratch: color RT init failed.\n");
            Release_Targets();
            return false;
        }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width      = kUnitScratchWidth;
        td.Height     = kUnitScratchHeight;
        td.MipLevels  = 1;
        td.ArraySize  = 1;
        td.Format     = DXGI_FORMAT_D32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage      = D3D11_USAGE_DEFAULT;
        td.BindFlags  = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(device.Get_Device()->CreateTexture2D(&td, nullptr, &DepthTex))) {
            DEBUG_ERROR("UnitScratch: depth tex creation failed.\n");
            Release_Targets();
            return false;
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
        dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(device.Get_Device()->CreateDepthStencilView(DepthTex, &dsvd, &DepthDSV))) {
            DEBUG_ERROR("UnitScratch: DSV creation failed.\n");
            Release_Targets();
            return false;
        }
        return true;
    }


    void UnitScratch::Release_Targets()
    {
        Safe_Release(DepthDSV);
        Safe_Release(DepthTex);
        if (ScratchRT != nullptr) {
            delete ScratchRT;
            ScratchRT = nullptr;
        }
    }


    ID3D11ShaderResourceView* UnitScratch::Get_SRV() const
    {
        return ScratchRT != nullptr ? ScratchRT->Get_SRV() : nullptr;
    }


    void UnitScratch::Clear_Depth(GraphicsDevice& device)
    {
        if (!Initialized || DepthDSV == nullptr) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;
        ctx->ClearDepthStencilView(DepthDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
    }


    bool UnitScratch::Begin_Unit(GraphicsDevice& device)
    {
        if (!Initialized) return false;
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return false;

        /**
         *  Save current RTV/DSV/viewport so End_Unit_Composite can restore
         *  them after the composite blit. OMGetRenderTargets AddRefs both
         *  views; we Release in End_Unit_Composite.
         */
        Safe_Release(SavedRTV);
        Safe_Release(SavedDSV);
        ctx->OMGetRenderTargets(1, &SavedRTV, &SavedDSV);
        SavedVPCount = 1;
        ctx->RSGetViewports(&SavedVPCount, &SavedVP);

        /**
         *  Bind scratch RT/DSV. Clear color to fully transparent (0,0,0,0)
         *  and depth to 1.0 (far). Subsequent draws into the scratch will
         *  fill the unit's pixels with the correct premultiplied color.
         */
        ID3D11RenderTargetView* rtv = ScratchRT->Get_RTV();
        ctx->OMSetRenderTargets(1, &rtv, DepthDSV);
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ctx->ClearRenderTargetView(rtv, clear_color);
        ctx->ClearDepthStencilView(DepthDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)kUnitScratchWidth;
        vp.Height   = (float)kUnitScratchHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        return true;
    }


    void UnitScratch::End_Unit_Composite(GraphicsDevice& device,
                                          Point2D scene_origin,
                                          float alpha,
                                          float scene_depth)
    {
        if (!Initialized) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        /**
         *  Restore the caller's RT/DSV bindings + viewport before issuing
         *  the composite quad. The quad targets whatever RT was active
         *  before `Begin_Unit` (typically Scene), at the original viewport.
         */
        ctx->OMSetRenderTargets(1, &SavedRTV, SavedDSV);
        if (SavedVPCount > 0) {
            ctx->RSSetViewports(SavedVPCount, &SavedVP);
        }
        Safe_Release(SavedRTV);
        Safe_Release(SavedDSV);
        SavedVPCount = 0;

        if (alpha <= 0.0f) {
            return;
        }

        /**
         *  Composite blit: full scratch quad → scene RT at scene_origin.
         *  SpriteBatch handles the projection-matrix CB at b0; our
         *  effect CB at b1 carries the unit alpha.
         */
        const int scene_w = device.Get_Logical_Width();
        const int scene_h = device.Get_Logical_Height();

        CompositeBatch.Begin(device, EBlend::Premultiplied, ESampler::PointClamp, &CompositeFx,
                             scene_w, scene_h, EDepthStencil::TestLessEqual_NoWrite);

        UnitCompositeEffect::Params p = {};
        p.Alpha = alpha;
        CompositeFx.Set_Params(device, p);

        const RectF dst {
            (float)scene_origin.X,
            (float)scene_origin.Y,
            (float)kUnitScratchWidth,
            (float)kUnitScratchHeight
        };
        const RectF src { 0.0f, 0.0f, (float)kUnitScratchWidth, (float)kUnitScratchHeight };
        const float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        CompositeBatch.Draw(static_cast<Texture2D*>(ScratchRT), dst, &src, tint,
                            scene_depth, scene_depth,
                            /*z_uv*/ nullptr,
                            /*clip*/ nullptr,
                            /*layer*/ 0u, /*flags*/ 0u);
        CompositeBatch.End(device);

        /**
         *  Unbind the scratch SRV at slot 0 so subsequent passes that bind
         *  the same slot don't see a stale view.
         */
        ID3D11ShaderResourceView* null_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &null_srv);
    }
}
