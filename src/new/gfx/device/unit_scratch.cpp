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
#include "extension_globals.h"  // RuleExtension — read for active-SSAA gate
#include "gfx_utils.h"
#include "graphics_device.h"
#include "rect.h"
#include "render_target_2d.h"
#include "rulesext.h"
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


    namespace
    {
        /**
         *  Build one of the two scratch depth textures. Same format,
         *  different size per pass. Factored out so Ensure_Targets stays
         *  readable.
         */
        bool Create_Scratch_Depth(ID3D11Device* d3d, int w, int h,
                                  ID3D11Texture2D*& out_tex,
                                  ID3D11DepthStencilView*& out_dsv)
        {
            D3D11_TEXTURE2D_DESC td = {};
            td.Width      = w;
            td.Height     = h;
            td.MipLevels  = 1;
            td.ArraySize  = 1;
            td.Format     = DXGI_FORMAT_D32_FLOAT;
            td.SampleDesc.Count   = 1;
            td.SampleDesc.Quality = 0;
            td.Usage      = D3D11_USAGE_DEFAULT;
            td.BindFlags  = D3D11_BIND_DEPTH_STENCIL;
            if (FAILED(d3d->CreateTexture2D(&td, nullptr, &out_tex))) {
                return false;
            }

            D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
            dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
            dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            if (FAILED(d3d->CreateDepthStencilView(out_tex, &dsvd, &out_dsv))) {
                return false;
            }
            return true;
        }
    }


    bool UnitScratch::Ensure_Targets(GraphicsDevice& device)
    {
        if (ScratchNoSSAA != nullptr && ScratchSSAA != nullptr
            && DepthDSVNoSSAA != nullptr && DepthDSVSSAA != nullptr) return true;
        Release_Targets();

        ID3D11Device* d3d = device.Get_Device();
        if (d3d == nullptr) {
            return false;
        }

        /**
         *  NoSSAA color + depth at 256² (logical). Rendered every unit
         *  (single source for SmoothVoxels=off, crisp half of the blend
         *  when on). RTV+SRV via RenderTarget2D so the composite PS
         *  samples it directly.
         */
        ScratchNoSSAA = new RenderTarget2D();
        if (ScratchNoSSAA == nullptr
            || !ScratchNoSSAA->Initialize(device,
                                          kUnitScratchWidth,
                                          kUnitScratchHeight,
                                          DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("UnitScratch: NoSSAA scratch RT init failed.\n");
            Release_Targets();
            return false;
        }
        if (!Create_Scratch_Depth(d3d, kUnitScratchWidth, kUnitScratchHeight,
                                  DepthTexNoSSAA, DepthDSVNoSSAA)) {
            DEBUG_ERROR("UnitScratch: NoSSAA depth init failed.\n");
            Release_Targets();
            return false;
        }

        /**
         *  SSAA color + depth at backing (logical × kUnitScratchMaxSSAA,
         *  currently 512²). Rendered only on dual-pass units. Contributes
         *  the smooth 4-tap half of the composite blend.
         */
        ScratchSSAA = new RenderTarget2D();
        if (ScratchSSAA == nullptr
            || !ScratchSSAA->Initialize(device,
                                        kUnitScratchSSAAWidth,
                                        kUnitScratchSSAAHeight,
                                        DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("UnitScratch: SSAA scratch RT init failed.\n");
            Release_Targets();
            return false;
        }
        if (!Create_Scratch_Depth(d3d, kUnitScratchSSAAWidth, kUnitScratchSSAAHeight,
                                  DepthTexSSAA, DepthDSVSSAA)) {
            DEBUG_ERROR("UnitScratch: SSAA depth init failed.\n");
            Release_Targets();
            return false;
        }
        return true;
    }


    void UnitScratch::Release_Targets()
    {
        Safe_Release(DepthDSVSSAA);
        Safe_Release(DepthTexSSAA);
        if (ScratchSSAA != nullptr) {
            delete ScratchSSAA;
            ScratchSSAA = nullptr;
        }
        Safe_Release(DepthDSVNoSSAA);
        Safe_Release(DepthTexNoSSAA);
        if (ScratchNoSSAA != nullptr) {
            delete ScratchNoSSAA;
            ScratchNoSSAA = nullptr;
        }
    }


    int UnitScratch::Get_Pass_Count() const
    {
        return (RuleExtension != nullptr && RuleExtension->IsSmoothVoxels) ? 2 : 1;
    }


    void UnitScratch::Clear_Depth(GraphicsDevice& device)
    {
        if (!Initialized) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        /**
         *  Pick the DSV for the currently-active pass. Records-in-replay
         *  call this between captured records; in dual-pass mode each
         *  pass walks the same record list independently, so the right
         *  DSV depends on which pass we're currently in.
         */
        ID3D11DepthStencilView* dsv = (ActivePassIndex == 1) ? DepthDSVSSAA : DepthDSVNoSSAA;
        if (dsv == nullptr) return;
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    }


    bool UnitScratch::Begin_Unit_Pass(GraphicsDevice& device, int pass)
    {
        if (!Initialized) return false;
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return false;

        if (pass == 0) {
            /**
             *  Save current RTV/DSV/viewport so End_Unit_Composite can
             *  restore them after the composite blit. OMGetRenderTargets
             *  AddRefs both views; we Release in End_Unit_Composite.
             *  Capture only on the first pass — subsequent passes within
             *  the same unit just swap the scratch binding.
             */
            Safe_Release(SavedRTV);
            Safe_Release(SavedDSV);
            ctx->OMGetRenderTargets(1, &SavedRTV, &SavedDSV);
            SavedVPCount = 1;
            ctx->RSGetViewports(&SavedVPCount, &SavedVP);

            /**
             *  Latch the per-unit pass count from the rule so a live edit
             *  between passes can't desync End_Unit_Composite's blend.
             */
            ActivePassCount = Get_Pass_Count();
        }

        /**
         *  Pass 0 → NoSSAA RT (256², CurrentSsaa=1, POINTLIST voxels via
         *  the VEF_SPLAT mask in Issue_Cmd_To_Scratch).
         *  Pass 1 → SSAA RT (logical × kUnitScratchMaxSSAA, CurrentSsaa
         *  = max, splatted voxels). Only valid when ActivePassCount == 2.
         */
        ActivePassIndex = pass;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        if (pass == 0) {
            CurrentSsaa = 1;
            rtv = ScratchNoSSAA->Get_RTV();
            dsv = DepthDSVNoSSAA;
        } else {
            CurrentSsaa = kUnitScratchMaxSSAA;
            rtv = ScratchSSAA->Get_RTV();
            dsv = DepthDSVSSAA;
        }

        ctx->OMSetRenderTargets(1, &rtv, dsv);
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ctx->ClearRenderTargetView(rtv, clear_color);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)Get_Active_Width();
        vp.Height   = (float)Get_Active_Height();
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
         *  before `Begin_Unit_Pass(0)` (typically Scene), at the original
         *  viewport.
         */
        ctx->OMSetRenderTargets(1, &SavedRTV, SavedDSV);
        if (SavedVPCount > 0) {
            ctx->RSSetViewports(SavedVPCount, &SavedVP);
        }
        Safe_Release(SavedRTV);
        Safe_Release(SavedDSV);
        SavedVPCount = 0;

        const int pass_count = ActivePassCount;
        ActivePassIndex = -1;
        ActivePassCount = 0;
        CurrentSsaa     = kUnitScratchMaxSSAA;

        if (alpha <= 0.0f || pass_count == 0) {
            return;
        }

        /**
         *  Composite blit: full scratch → scene quad at logical size. The
         *  composite PS reads NoSSAA at t0 (1-tap point) and, if
         *  pass_count == 2, SSAA at t1 (4-tap majority-opaque resolve)
         *  and averages the two. SmoothVoxels=off short-circuits to just
         *  the t0 pass-through. PointClamp sampler is bound but unused
         *  (PS uses Load()).
         */
        const int scene_w = device.Get_Logical_Width();
        const int scene_h = device.Get_Logical_Height();

        CompositeBatch.Begin(device, EBlend::Premultiplied, ESampler::PointClamp, &CompositeFx,
                             scene_w, scene_h, EDepthStencil::TestLessEqual_NoWrite);

        UnitCompositeEffect::Params p = {};
        p.Alpha     = alpha;
        p.Ssaa      = kUnitScratchMaxSSAA;
        p.PassCount = pass_count;
        CompositeFx.Set_Params(device, p);

        /**
         *  Bind the SSAA RT at t1 manually (SpriteBatch::Draw only binds
         *  the texture at t0). When pass_count == 1 the shader ignores
         *  t1 entirely, so a stale binding is fine — but binding the
         *  current frame's SSAA RT regardless keeps state consistent and
         *  avoids leftover bindings from prior passes.
         */
        ID3D11ShaderResourceView* ssaa_srv = (ScratchSSAA != nullptr)
                                           ? ScratchSSAA->Get_SRV() : nullptr;
        ctx->PSSetShaderResources(1, 1, &ssaa_srv);

        const RectF dst {
            (float)scene_origin.X,
            (float)scene_origin.Y,
            (float)kUnitScratchWidth,
            (float)kUnitScratchHeight
        };
        /**
         *  Src is in pixels of the t0 texture (NoSSAA, 256²) → full quad.
         *  The shader computes the matching tap positions for t1 (SSAA)
         *  by multiplying the same UV by t1's size via GetDimensions, so
         *  no second src rect is needed.
         */
        const RectF src {
            0.0f, 0.0f,
            (float)kUnitScratchWidth,
            (float)kUnitScratchHeight
        };
        const float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        CompositeBatch.Draw(static_cast<Texture2D*>(ScratchNoSSAA), dst, &src, tint,
                            scene_depth, scene_depth,
                            /*z_uv*/ nullptr,
                            /*clip*/ nullptr,
                            /*layer*/ 0u, /*flags*/ 0u);
        CompositeBatch.End(device);

        /**
         *  Unbind both scratch SRVs (t0 + t1) so subsequent passes that
         *  bind the same slots don't see a stale view.
         */
        ID3D11ShaderResourceView* null_srvs[2] = { nullptr, nullptr };
        ctx->PSSetShaderResources(0, 2, null_srvs);
    }
}
