/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2a palette-LUT sprite Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sprite_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_lut.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC SpriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        const char PaletteSpriteHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float2 AtlasSize;\n"
            "    float2 _pad0;\n"
            "    uint   Flags;\n"
            "    uint3  _pad1;\n"
            "};\n"
            "static const uint SEF_USE_REMAP     = 0x01;\n"
            "static const uint SEF_DARKEN        = 0x02;\n"
            "static const uint SEF_TRANSLUCENT25 = 0x04;\n"
            "static const uint SEF_TRANSLUCENT50 = 0x08;\n"
            "static const uint SEF_TRANSLUCENT75 = 0x10;\n"
            "\n"
            "struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    /* ProjMtx maps (x,y) -> NDC; pos.z passes straight through to clip space. */\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos = float4(p.x, p.y, i.pos.z, 1);\n"
            "    o.uv  = i.uv;\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<uint>      Atlas    : register(t0);\n"
            "Texture2D<float4>    Palette  : register(t1);\n"
            "Texture2D<uint>      Remap    : register(t2);\n"
            "\n"
            "float4 PSMain(VSOut v) : SV_Target {\n"
            "    int2 px = int2(v.uv * AtlasSize);\n"
            "    uint idx = Atlas.Load(int3(px, 0));\n"
            "    if (idx == 0) discard;\n"
            "    /**\n"
            "     * SHAPE_DARKEN uses the shape only as a hit-test mask; the\n"
            "     * blend state (EBlend::DestMultiplyHalf) is what actually\n"
            "     * halves the destination. Source color is multiplied by\n"
            "     * ZERO at blend time, so emit anything non-discarded here.\n"
            "     */\n"
            "    if (Flags & SEF_DARKEN) {\n"
            "        return float4(0, 0, 0, 1);\n"
            "    }\n"
            "    if ((Flags & SEF_USE_REMAP) && idx >= 16 && idx < 32) {\n"
            "        idx = Remap.Load(int3((int)idx - 16, 0, 0));\n"
            "    }\n"
            "    float4 c = Palette.Load(int3((int)idx, 0, 0));\n"
            "    c.rgb *= v.col.rgb;\n"
            "    c.a   *= v.col.a;\n"
            "    if (Flags & SEF_TRANSLUCENT25) c.a *= 0.75;\n"
            "    if (Flags & SEF_TRANSLUCENT50) c.a *= 0.5;\n"
            "    if (Flags & SEF_TRANSLUCENT75) c.a *= 0.25;\n"
            "    c.rgb *= c.a;     /* premultiply for the EBlend::Premultiplied output */\n"
            "    return c;\n"
            "}\n";
    }


    bool SpriteEffect::Initialize(GraphicsDevice& device)
    {
        /**
         *  b0 holds SpriteBatch's ProjMtx (float4x4 = 64 bytes); b1 holds the
         *  per-effect SpriteEffectParams. Effect::Initialize creates the b0
         *  CB; SpriteBatch::End writes ProjMtx into it via Set_Constants.
         */
        if (!Effect::Initialize(device,
                PaletteSpriteHLSL, sizeof(PaletteSpriteHLSL) - 1,
                "sprite_palette",
                SpriteIL, _countof(SpriteIL),
                /* SpriteCB at b0 — float4x4 ProjMtx */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(SpriteEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("SpriteEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void SpriteEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void SpriteEffect::Bind_Palette(GraphicsDevice& device, PaletteLUT& palette)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srvs[2] = {
            palette.Get_Palette_Texture().Get_SRV(),
            palette.Get_Remap_Texture().Get_SRV(),
        };
        ctx->PSSetShaderResources(1, 2, srvs);
    }


    void SpriteEffect::Set_Params(GraphicsDevice& device, const SpriteEffectParams& params)
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
