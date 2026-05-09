/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 3 palette-LUT tile Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tile_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "palette_lut.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC TileIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        const char TileShaderHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float2 AtlasSize;\n"
            "    float2 _pad;\n"
            "};\n"
            "struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos = float4(p.x, p.y, i.pos.z, 1);\n"
            "    o.uv  = i.uv;\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n"
            "Texture2D<uint>   Atlas   : register(t0);\n"
            "Texture2D<float4> Palette : register(t1);\n"
            "float4 PSMain(VSOut v) : SV_Target {\n"
            "    int2 px = int2(v.uv * AtlasSize);\n"
            "    uint idx = Atlas.Load(int3(px, 0));\n"
            "    if (idx == 0) discard;\n"
            "    float4 c = Palette.Load(int3((int)idx, 0, 0));\n"
            "    c.rgb *= v.col.rgb;\n"
            "    /* Tiles are opaque; alpha not used downstream, but write 1 to be safe. */\n"
            "    c.a = 1.0;\n"
            "    return c;\n"
            "}\n";
    }


    bool TileEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                TileShaderHLSL, sizeof(TileShaderHLSL) - 1,
                "tile_palette",
                TileIL, _countof(TileIL),
                /* SpriteCB at b0 — float4x4 ProjMtx, 64 bytes */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(TileEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("TileEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void TileEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void TileEffect::Bind_Palette(GraphicsDevice& device, PaletteLUT& palette)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srv = palette.Get_Palette_Texture().Get_SRV();
        ctx->PSSetShaderResources(1, 1, &srv);
    }


    void TileEffect::Set_Params(GraphicsDevice& device, const TileEffectParams& params)
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
