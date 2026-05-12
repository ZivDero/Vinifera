/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for SpotLightClass GPU rendering.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "spotlight_queue.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "gpu_surface_target.h"
#include "render_pass.h"
#include "scene_copy.h"
#include "states.h"

#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  SpotLight shader. VS projects scene-pixel vertices through the
         *  standard pixel→NDC matrix. PS computes the radial mask intensity
         *  analytically in the quad's local coords and applies vanilla's
         *  per-pixel multiplicative brighten.
         *
         *  Vanilla's mask construction (One_Time):
         *    surf = 256x256, Fill(0)
         *    R = 2*index + 1, color = -2
         *    while irad > 0:
         *      Draw_Circle outline at radius `irad`, color `max(0, color)`
         *      irad -= 2, color += 4
         *    blit 2:1 vertical-compressed to 256x128 final mask
         *
         *  Net pixel value at distance d from center (in 1:1 source space):
         *    mask = max(0, 2*(R - d) - 2) clamped to [0, 255]
         *  After 2:1 compression: source y = 2 * dst_y, so for a 256x128 PS
         *  pixel (xd, yd) the effective distance is
         *    d = sqrt((xd - 128)^2 + (2*(yd - 64))^2)
         *
         *  Brighten math (vanilla): `out.rgb = sat(bg.rgb + bg.rgb * mask/256)`.
         *  Applied per channel via the PS, with the dest read coming from
         *  `SceneCopy` (the per-frame snapshot).
         */
        const char SpotLightShaderHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer SpotLightCB : register(b1) {\n"
            "    float4 Misc;   // x=EffectiveRadius, y=QuadTopLeft.x, z=QuadTopLeft.y, w=SceneW\n"
            "    float4 Geom;   // x=SceneH, y=UniformMask (-1 => concentric falloff), zw=unused\n"
            "};\n"
            "\n"
            "struct VSIn  { float2 pos : POSITION; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 scene_xy : TEXCOORD0; };\n"
            "\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    // Depth slightly less than terrain at the same screen-Y so\n"
            "    // the spotlight beats terrain. Spotlight isn't z-tested in\n"
            "    // vanilla (the CPU blitter writes unconditionally inside\n"
            "    // its 256x128 area), so this is just to keep ordering sane\n"
            "    // against other PostEffects content.\n"
            "    const float kPixelToDepth = 1.0 / 16000.0;\n"
            "    float z = clamp(1.0 - i.pos.y * kPixelToDepth - 1.0e-4, 1.0e-4, 0.9999);\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0.0, 1.0));\n"
            "    o.pos = float4(p.x, p.y, z, 1.0);\n"
            "    o.scene_xy = i.pos.xy;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<float4> SceneCopy : register(t0);\n"
            "\n"
            "float4 PSMain(VSOut v) : SV_Target {\n"
            "    int scene_w = (int)Misc.w;\n"
            "    int scene_h = (int)Geom.x;\n"
            "    int2 px = int2(v.scene_xy);\n"
            "\n"
            "    // Quad-local coords (256 wide x 128 tall, top-left at Misc.yz).\n"
            "    float2 quad_local = v.scene_xy - Misc.yz;\n"
            "    float  dx = quad_local.x - 128.0;\n"
            "    float  dy = 2.0 * (quad_local.y - 64.0);   // 2:1 vertical compensation\n"
            "    float  dist = sqrt(dx * dx + dy * dy);\n"
            "\n"
            "    float R = Misc.x;\n"
            "    float uniform_mask = Geom.y;               // -1 => concentric falloff\n"
            "    float mask;\n"
            "    if (uniform_mask >= 0.0) {\n"
            "        // Uniform-disc case (BuildingLight extra surfaces). Vanilla's\n"
            "        // One_Time draws a single filled circle of constant colour --\n"
            "        // a flat bright disc, not a gradient.\n"
            "        if (dist > R) discard;\n"
            "        mask = uniform_mask;\n"
            "    } else {\n"
            "        // Concentric-ring case (warhead spotlights). Vanilla draws\n"
            "        // nested filled circles with brightness 0, 2, 6, ..., 4i-2.\n"
            "        // Channel quantization in 16-bit RGB565 made boosts below ~6%\n"
            "        // imperceptible -- the faint outer rings effectively vanished.\n"
            "        // Our 32-bit pipeline shows them as a soft halo, so drop the\n"
            "        // weak half to keep only the visibly-bright core.\n"
            "        float steps = floor((R - dist) * 0.5);\n"
            "        if (steps <= 0.0) discard;\n"
            "        mask = 4.0 * steps - 2.0;\n"
            "        float peak = 2.0 * R - 2.0;\n"
            "        if (mask < peak * 0.5) discard;\n"
            "    }\n"
            "    mask = min(mask, 255.0);\n"
            "\n"
            "    int2 sample_px = clamp(px, int2(0, 0), int2(scene_w - 1, scene_h - 1));\n"
            "    float4 bg = SceneCopy.Load(int3(sample_px, 0));\n"
            "\n"
            "    float boost = mask / 256.0;\n"
            "    float3 out_rgb = saturate(bg.rgb + bg.rgb * boost);\n"
            "    return float4(out_rgb, 1.0);\n"
            "}\n";


        const D3D11_INPUT_ELEMENT_DESC SpotLightIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    bool SpotLightEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                SpotLightShaderHLSL, sizeof(SpotLightShaderHLSL) - 1,
                "spotlight",
                SpotLightIL, _countof(SpotLightIL),
                /* SpriteCB at b0 — float4x4 ProjMtx, 64 bytes */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(CB);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &PerLightCB))) {
            DEBUG_ERROR("SpotLightEffect: PerLightCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void SpotLightEffect::Shutdown()
    {
        Safe_Release(PerLightCB);
        Effect::Shutdown();
    }


    void SpotLightEffect::Set_Per_Light(GraphicsDevice& device, const CB& data)
    {
        if (PerLightCB == nullptr) return;
        ID3D11DeviceContext* ctx = device.Get_Context();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(ctx->Map(PerLightCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return;
        }
        memcpy(mapped.pData, &data, sizeof(data));
        ctx->Unmap(PerLightCB, 0);
        ctx->VSSetConstantBuffers(1, 1, &PerLightCB);
        ctx->PSSetConstantBuffers(1, 1, &PerLightCB);
    }


    SpotLightQueue& SpotLightQueue::Get()
    {
        static SpotLightQueue instance;
        return instance;
    }


    bool SpotLightQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(),
                                /*initial_capacity*/ 6 * 16);   // 6 verts per quad, room for 16
        if (!Fx.Initialize(device)) {
            VertexBuffer.Shutdown();
            return false;
        }
        Commands.reserve(16);
        Initialized = true;
        return true;
    }


    void SpotLightQueue::Shutdown()
    {
        Fx.Shutdown();
        VertexBuffer.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void SpotLightQueue::Submit(const SpotLightDrawCmd& cmd)
    {
        if (!Initialized) return;
        Commands.push_back(cmd);
    }


    void SpotLightQueue::Clear()
    {
        Commands.clear();
    }


    void SpotLightQueue::Issue_Cmd(GraphicsDevice& device, const SpotLightDrawCmd& cmd,
                                   int scene_w, int scene_h)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        /**
         *  Build a 256x128 quad centered on cmd.Center. Two triangles, 6
         *  vertices in triangle-list order (TL, TR, BR, TL, BR, BL).
         */
        const float left   = (float)(cmd.Center.X - 128);
        const float top    = (float)(cmd.Center.Y - 64);
        const float right  = left + 256.0f;
        const float bottom = top  + 128.0f;

        Vertex* dst = VertexBuffer.Begin(6);
        if (dst == nullptr) return;
        dst[0].Pos[0] = left;  dst[0].Pos[1] = top;
        dst[1].Pos[0] = right; dst[1].Pos[1] = top;
        dst[2].Pos[0] = right; dst[2].Pos[1] = bottom;
        dst[3].Pos[0] = left;  dst[3].Pos[1] = top;
        dst[4].Pos[0] = right; dst[4].Pos[1] = bottom;
        dst[5].Pos[0] = left;  dst[5].Pos[1] = bottom;
        VertexBuffer.End();

        SpotLightEffect::CB cb = {};
        cb.Misc[0] = cmd.EffectiveRadius;
        cb.Misc[1] = left;
        cb.Misc[2] = top;
        cb.Misc[3] = (float)scene_w;
        cb.Geom[0] = (float)scene_h;
        cb.Geom[1] = cmd.UniformMask;
        Fx.Set_Per_Light(device, cb);

        const float L = 0.0f;
        const float R = (float)scene_w;
        const float T = 0.0f;
        const float B = (float)scene_h;
        struct ProjCB { float Mtx[16]; } pcb = {};
        pcb.Mtx[0]  = 2.0f / (R - L);
        pcb.Mtx[5]  = 2.0f / (T - B);
        pcb.Mtx[10] = 1.0f;
        pcb.Mtx[12] = (R + L) / (L - R);
        pcb.Mtx[13] = (T + B) / (B - T);
        pcb.Mtx[15] = 1.0f;
        Fx.Set_Constants(device, &pcb);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)scene_w;
        vp.Height   = (float)scene_h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));

        /**
         *  Vanilla doesn't depth-test the spotlight — the CPU blitter
         *  writes unconditionally within the 256x128 area. Using
         *  `TestLessEqual_NoWrite` keeps the spotlight in front of
         *  terrain at the same Y but it doesn't disturb the depth buffer.
         */
        ctx->OMSetDepthStencilState(device.States().Get(EDepthStencil::TestLessEqual_NoWrite), 0);

        const float blend_factor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(device.States().Get(EBlend::Opaque), blend_factor, 0xFFFFFFFFu);

        Fx.Apply(device);

        ID3D11ShaderResourceView* scene_srv = SceneCopy::Get().Get_SRV();
        ctx->PSSetShaderResources(0, 1, &scene_srv);

        ID3D11Buffer* vb = VertexBuffer.Get();
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(6, 0);
    }


    void SpotLightQueue::Flush_Pass(GraphicsDevice& device, int pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }
        if (pass != (int)RenderPass::PostEffects) {
            return;
        }

        if (!SceneCopy::Get().Ensure_Copied(device)) {
            return;
        }

        Bind_Render_Target(device, GpuRenderTarget::Scene);

        const int scene_w = device.Get_Logical_Width();
        const int scene_h = device.Get_Logical_Height();
        if (scene_w <= 0 || scene_h <= 0) {
            return;
        }

        for (const SpotLightDrawCmd& cmd : Commands) {
            Issue_Cmd(device, cmd, scene_w, scene_h);
        }

        ID3D11ShaderResourceView* null_srv = nullptr;
        device.Get_Context()->PSSetShaderResources(0, 1, &null_srv);
    }
}
