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
#include "palette_array.h"
#include "palette_lut.h"
#include "texture2d.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC SpriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_UINT,           0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_UINT,           0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        /**
         *  Dual-source blend emits two PS outputs per pixel:
         *    SV_Target0 = src0, SV_Target1 = src1
         *  and the blend hardware computes result = src0 + src1 * dest.
         *
         *  Premultiplied path: src0 = c.rgb*c.a, src1 = (1-c.a)
         *      → result = c.rgb*c.a + (1-c.a)*dest         (standard premul)
         *
         *  DARKEN path:        src0 = 0, src1 = 0.5
         *      → result = 0 + 0.5*dest                     (vanilla SHAPE_DARKEN)
         *
         *  The shader picks per pixel based on the per-vertex `flags` input.
         */
        const char PaletteSpriteHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float2 AtlasSize;\n"
            "    float2 ZShapeAtlasSize;\n"
            "    float  ZShapeDepthScale;\n"
            "    uint   Flags;\n"
            "    uint2  _pad1;\n"
            "};\n"
            "static const uint SEF_DARKEN           = 0x02;\n"
            "static const uint SEF_USE_ZSHAPE       = 0x20;\n"
            "static const uint SEF_NO_ALPHA_BUFFER  = 0x40;\n"
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
            "    float4 pos    : SV_Position;\n"
            "    float2 uv     : TEXCOORD0;\n"
            "    float2 zuv    : TEXCOORD1;\n"
            "    float4 col    : COLOR0;\n"
            "    nointerpolation uint layer  : TEXCOORD2;\n"
            "    nointerpolation uint pflags : TEXCOORD3;\n"
            "};\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos    = float4(p.x, p.y, i.pos.z, 1);\n"
            "    o.uv     = i.uv;\n"
            "    o.zuv    = i.zuv;\n"
            "    o.col    = i.col;\n"
            "    o.layer  = i.layer;\n"
            "    o.pflags = i.pflags;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<uint>          Atlas      : register(t0);\n"
            "Texture2DArray<float4>   PaletteArr : register(t1);\n"
            "Texture2D<uint>          ZShape     : register(t3);\n"
            "Texture2D<float>         AlphaTex   : register(t4);\n"
            "\n"
            "struct PSOut {\n"
            "    float4 color  : SV_Target0;\n"
            "    float4 factor : SV_Target1;\n"
            "    float  depth  : SV_Depth;\n"
            "};\n"
            "PSOut PSMain(VSOut v) {\n"
            "    PSOut o;\n"
            "    o.depth = v.pos.z;\n"
            "    int2 px = int2(v.uv * AtlasSize);\n"
            "    uint idx = Atlas.Load(int3(px, 0));\n"
            "    if (idx == 0) discard;\n"
            "    if (v.pflags & SEF_USE_ZSHAPE) {\n"
            "        float2 zpf = v.zuv * ZShapeAtlasSize;\n"
            "        if (zpf.x >= 0.0 && zpf.y >= 0.0 && zpf.x < ZShapeAtlasSize.x && zpf.y < ZShapeAtlasSize.y) {\n"
            "            uint zraw = ZShape.Load(int3(int2(zpf), 0)) & 0xFF;\n"
            "            int zsigned = (zraw >= 128) ? (int)zraw - 256 : (int)zraw;\n"
            "            o.depth = saturate(o.depth - (float)zsigned * ZShapeDepthScale);\n"
            "        }\n"
            "    }\n"
            "    /**\n"
            "     * SHAPE_DARKEN: shape acts as a mask. Dual-source blend with\n"
            "     * src0 = 0 and src1 = 0.5 multiplies the destination by 0.5,\n"
            "     * bit-identical to the old DestMultiplyHalf state.\n"
            "     */\n"
            "    if (v.pflags & SEF_DARKEN) {\n"
            "        o.color  = float4(0, 0, 0, 0);\n"
            "        o.factor = float4(0.5, 0.5, 0.5, 1);\n"
            "        return o;\n"
            "    }\n"
            "    float4 c = PaletteArr.Load(int4((int)idx, 0, (int)v.layer, 0));\n"
            "    c.rgb *= v.col.rgb;\n"
            "    c.a   *= v.col.a;\n"
            "    /**\n"
            "     * Alpha-buffer modulation. Same formula as the tile shader:\n"
            "     * 127 = neutral, 254 ~= 2x overbright, 0 = full dark.\n"
            "     */\n"
            "    if (!(Flags & SEF_NO_ALPHA_BUFFER)) {\n"
            "        float alpha_byte = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;\n"
            "        c.rgb *= alpha_byte / 127.0;\n"
            "    }\n"
            "    c.rgb *= c.a;     /* premultiply */\n"
            "    o.color  = c;\n"
            "    o.factor = float4(1.0 - c.a, 1.0 - c.a, 1.0 - c.a, 1.0 - c.a);\n"
            "    return o;\n"
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


    void SpriteEffect::Bind_Palette_Array(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srv = PaletteArray::Get().Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
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
