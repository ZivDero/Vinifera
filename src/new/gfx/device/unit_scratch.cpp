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
        if (!Effect::Initialize(device, "UNIT_COMPOSITE",
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

        ID3D11Device* d3d = device.Get_Device();
        if (d3d == nullptr) {
            return false;
        }

        /**
         *  SSAA color target — RTV + SRV via RenderTarget2D, single-sample
         *  at physical resolution (logical × kUnitScratchSSAA). Voxels
         *  render here at the oversampled resolution; the composite blit
         *  reads this SRV through a LinearClamp sampler so 2×2 scratch
         *  texels downsample-average into one scene pixel.
         */
        ScratchRT = new RenderTarget2D();
        if (ScratchRT == nullptr) {
            Release_Targets();
            return false;
        }
        if (!ScratchRT->Initialize(device,
                                   kUnitScratchPhysicalWidth,
                                   kUnitScratchPhysicalHeight,
                                   DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("UnitScratch: scratch RT init failed.\n");
            Release_Targets();
            return false;
        }

        /**
         *  Depth at physical resolution to match the color RT. Single-
         *  sample D32_FLOAT, BIND_DEPTH_STENCIL only — depth is never
         *  sampled or resolved.
         */
        D3D11_TEXTURE2D_DESC td = {};
        td.Width      = kUnitScratchPhysicalWidth;
        td.Height     = kUnitScratchPhysicalHeight;
        td.MipLevels  = 1;
        td.ArraySize  = 1;
        td.Format     = DXGI_FORMAT_D32_FLOAT;
        td.SampleDesc.Count   = 1;
        td.SampleDesc.Quality = 0;
        td.Usage      = D3D11_USAGE_DEFAULT;
        td.BindFlags  = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(d3d->CreateTexture2D(&td, nullptr, &DepthTex))) {
            DEBUG_ERROR("UnitScratch: depth tex creation failed.\n");
            Release_Targets();
            return false;
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
        dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(d3d->CreateDepthStencilView(DepthTex, &dsvd, &DepthDSV))) {
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
         *  Bind the SSAA scratch RT/DSV at physical resolution. Clear color
         *  to fully transparent (0,0,0,0) and depth to 1.0 (far). Voxel
         *  cmds rendered into this RT use viewport-and-T-scaled coordinates
         *  (see VoxelQueue::Flush_Composite_Group) so the unit's logical
         *  footprint fills the full physical viewport — the composite blit
         *  then linear-downsamples it back to logical size in scene space.
         */
        ID3D11RenderTargetView* rtv = ScratchRT->Get_RTV();
        ctx->OMSetRenderTargets(1, &rtv, DepthDSV);
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ctx->ClearRenderTargetView(rtv, clear_color);
        ctx->ClearDepthStencilView(DepthDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)kUnitScratchPhysicalWidth;
        vp.Height   = (float)kUnitScratchPhysicalHeight;
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
         *  Composite blit: full SSAA scratch quad → scene RT at scene_origin
         *  at logical size. The composite PS does its own 4-tap downsample
         *  (see unit_composite.hlsl) — explicit Load() of the 2×2 scratch
         *  block per dst pixel + a majority-opaque rule that averages
         *  interior colors but keeps the silhouette pixel-aligned. The
         *  sampler is unused (Load bypasses it) but we bind PointClamp
         *  to keep the sampler state predictable and to match the shader's
         *  point-sample intent.
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
        const RectF src {
            0.0f, 0.0f,
            (float)kUnitScratchPhysicalWidth,
            (float)kUnitScratchPhysicalHeight
        };
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
