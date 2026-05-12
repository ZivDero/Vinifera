/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame screen-space distortion queue + flush.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "distortion_queue.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_array.h"
#include "scene_copy.h"
#include "shp_atlas.h"
#include "shp_cache.h"

#include <algorithm>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Distortion shader. Inputs match the standard SpriteVertex layout so
         *  the queue can drive draws through `SpriteBatch::Draw` (saving us
         *  from writing yet another quad batcher). The COLOR0 attribute is
         *  REPURPOSED:
         *    col.r = blend_ratio (0..1; 1 = full background, 0 = full sprite)
         *    col.g = warp_offset_pixels (signed; horizontal sample offset)
         *    col.b, col.a = unused
         *
         *  PS samples the SceneCopy texture at the rasterized pixel center +
         *  warp offset, then blends with the SHP's palette color via lerp.
         *  Transparent palette index (0) discards so the sprite's alpha mask
         *  is honored.
         */
        const char DistortionHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float2 AtlasSize;\n"
            "    float2 SceneSize;\n"
            "};\n"
            "\n"
            "struct VSIn {\n"
            "    float3 pos    : POSITION;\n"
            "    float2 uv     : TEXCOORD0;\n"
            "    float2 zuv    : TEXCOORD1;\n"   // unused
            "    float4 col    : COLOR0;\n"      // col.r = blend, col.g = warp_offset_px
            "    uint   layer  : TEXCOORD2;\n"
            "    uint   pflags : TEXCOORD3;\n"   // unused
            "};\n"
            "struct VSOut {\n"
            "    float4 pos       : SV_Position;\n"
            "    float2 uv        : TEXCOORD0;\n"
            "    nointerpolation float blend  : COLOR0;\n"
            "    nointerpolation float warp   : COLOR1;\n"
            "    nointerpolation uint  layer  : TEXCOORD2;\n"
            "};\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos    = float4(p.x, p.y, i.pos.z, 1);\n"
            "    o.uv     = i.uv;\n"
            "    o.blend  = i.col.r;\n"
            "    o.warp   = i.col.g;\n"
            "    o.layer  = i.layer;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<uint>          Atlas      : register(t0);\n"
            "Texture2DArray<float4>   PaletteArr : register(t1);\n"
            "Texture2D<float4>        SceneCopy  : register(t2);\n"
            "SamplerState             PointS     : register(s0);\n"
            "\n"
            "float4 PSMain(VSOut v) : SV_Target {\n"
            "    int2 px = int2(v.uv * AtlasSize);\n"
            "    uint idx = Atlas.Load(int3(px, 0));\n"
            "    if (idx == 0) discard;\n"
            "    float4 shp = PaletteArr.Load(int4((int)idx, 0, (int)v.layer, 0));\n"
            "    /**\n"
            "     * SV_Position.xy in the PS is the rasterized pixel center;\n"
            "     * divide by the scene RT size to get a [0,1] UV. The warp is\n"
            "     * a fixed horizontal offset in pixel units (matches vanilla's\n"
            "     * `dest[warp_offset]` integer-step displacement). Saturate so\n"
            "     * edges of the screen don't wrap into garbage.\n"
            "     */\n"
            "    float2 base_uv = v.pos.xy / SceneSize;\n"
            "    float2 warp_uv = saturate(base_uv + float2(v.warp / SceneSize.x, 0));\n"
            "    float4 bg = SceneCopy.Sample(PointS, warp_uv);\n"
            "    bg.a = 1.0;\n"
            "    /**\n"
            "     * lerp(shp, bg, blend) — blend=0.75 mirrors vanilla 75% (more\n"
            "     * background, less sprite); blend=0.25 mirrors vanilla 25%.\n"
            "     */\n"
            "    float4 c = lerp(shp, bg, v.blend);\n"
            "    c.a = 1.0;\n"
            "    return c;\n"
            "}\n";


        /**
         *  Matches `SpriteIL` from sprite_effect.cpp exactly so the same
         *  `SpriteBatch::Draw(..., tint, ...)` overload writes the right slots
         *  for our shader's VSIn.
         */
        const D3D11_INPUT_ELEMENT_DESC DistortionIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_UINT,           0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_UINT,           0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    bool DistortionEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                DistortionHLSL, sizeof(DistortionHLSL) - 1,
                "distortion",
                DistortionIL, _countof(DistortionIL),
                /* SpriteCB at b0 — float4x4 ProjMtx */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(Params);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("DistortionEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void DistortionEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void DistortionEffect::Set_Params(GraphicsDevice& device, const Params& params)
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


    DistortionQueue& DistortionQueue::Get()
    {
        static DistortionQueue instance;
        return instance;
    }


    bool DistortionQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        if (!Batch.Initialize(device, /*max_quads_per_batch*/ 256)) {
            return false;
        }
        if (!FxEffect.Initialize(device)) {
            Batch.Shutdown();
            return false;
        }
        Commands.reserve(64);
        Initialized = true;
        return true;
    }


    void DistortionQueue::Shutdown()
    {
        FxEffect.Shutdown();
        Batch.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void DistortionQueue::Submit(const DistortionDrawCmd& cmd)
    {
        if (!Initialized || cmd.Asset == nullptr || cmd.Palette == nullptr) {
            return;
        }
        Commands.push_back(cmd);
    }


    void DistortionQueue::Clear()
    {
        Commands.clear();
    }


    void DistortionQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || pass != RenderPass::PostEffects || Commands.empty()) {
            return;
        }

        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) {
            return;
        }

        /**
         *  Trigger (or reuse) the per-frame scene snapshot. VoxelQueue's
         *  predator path also calls this — first caller wins.
         */
        if (!SceneCopy::Get().Ensure_Copied(device)) {
            return;
        }
        const int scene_w = SceneCopy::Get().Get_Width();
        const int scene_h = SceneCopy::Get().Get_Height();
        if (scene_w <= 0 || scene_h <= 0) {
            return;
        }

        /**
         *  Sort by OutputTarget then by atlas page so we don't break batches
         *  unnecessarily. Currently distortion only writes Scene, but keep
         *  the structure forward-compatible with sidebar variants.
         */
        std::stable_sort(Commands.begin(), Commands.end(),
            [](const DistortionDrawCmd& a, const DistortionDrawCmd& b) {
                if (a.OutputTarget != b.OutputTarget) {
                    return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
                }
                return a.Asset->Atlas_Page() < b.Asset->Atlas_Page();
            });

        size_t i = 0;
        while (i < Commands.size()) {
            const GpuRenderTarget bucket_target = Commands[i].OutputTarget;
            if (bucket_target == GpuRenderTarget::None) {
                ++i;
                continue;
            }

            Bind_Render_Target(device, bucket_target);

            const bool is_sidebar = (bucket_target == GpuRenderTarget::Sidebar);
            const int target_w = is_sidebar ? device.Get_Sidebar_Target_Width()  : device.Get_Logical_Width();
            const int target_h = is_sidebar ? device.Get_Sidebar_Target_Height() : device.Get_Logical_Height();

            /**
             *  Group by atlas page within the bucket.
             */
            size_t j = i + 1;
            while (j < Commands.size()
                && Commands[j].OutputTarget == bucket_target
                && Commands[j].Asset->Atlas_Page() == Commands[i].Asset->Atlas_Page()) {
                ++j;
            }

            ShpAsset*  head_asset = Commands[i].Asset;
            Texture2D& page_tex   = ShpAtlas::Get().Get_Page(head_asset->Atlas_Page());

            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp, &FxEffect,
                        target_w, target_h, EDepthStencil::TestLessEqual_NoWrite);

            /**
             *  Bind shared SRVs: t0 atlas (SpriteBatch sets this from page_tex),
             *  t1 palette array (manual), t2 SceneCopy (manual).
             */
            ID3D11ShaderResourceView* pal_srv = PaletteArray::Get().Get_SRV();
            ctx->PSSetShaderResources(1, 1, &pal_srv);
            ID3D11ShaderResourceView* copy_srv = SceneCopy::Get().Get_SRV();
            ctx->PSSetShaderResources(2, 1, &copy_srv);

            DistortionEffect::Params fxp = {};
            fxp.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
            fxp.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
            fxp.SceneSize[0] = (float)scene_w;
            fxp.SceneSize[1] = (float)scene_h;
            FxEffect.Set_Params(device, fxp);

            for (size_t k = i; k < j; ++k) {
                const DistortionDrawCmd& c = Commands[k];
                const ShpFrameInfo* fi = c.Asset->Get_Frame(c.FrameIndex);
                if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
                    continue;
                }
                const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY,
                                     (float)fi->W,      (float)fi->H };
                /**
                 *  Pack distortion params into the tint slot: r = blend ratio,
                 *  g = warp offset in pixels. The shader's VS picks them out
                 *  by COLOR0.{r,g}.
                 */
                const float tint[4] = {
                    c.BlendRatio,
                    (float)c.WarpOffsetPixels,
                    0.0f,
                    1.0f
                };
                const uint32_t layer = (c.Palette != nullptr && c.Palette->Layer() >= 0)
                                     ? (uint32_t)c.Palette->Layer() : 0u;
                Batch.Draw(&page_tex, c.Dst, &src, tint,
                           c.DstZTop, c.DstZBottom,
                           /*z_uv*/ nullptr,
                           c.Clip.Is_Valid() ? &c.Clip : nullptr,
                           layer, /*flags*/ 0u);
            }

            Batch.End(device);
            i = j;
        }

        /**
         *  Unbind SceneCopy + palette SRVs so subsequent passes (next frame)
         *  start clean. SceneRT is still bound as RTV — leaving t2 bound to
         *  SceneCopy would be harmless but D3D may warn on the next frame
         *  when SceneRT is rebound as RTV.
         */
        ID3D11ShaderResourceView* null_srvs[3] = {};
        ctx->PSSetShaderResources(0, 3, null_srvs);
    }
}
