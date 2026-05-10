/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 4 Phase 4.2 shroud / fog alpha-write Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shroud_fog_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Reuse SpriteBatch's vertex layout (Pos, UV, ZUV, Tint). ZUV/Tint go
         *  unused by the shroud/fog pixel shader; we still declare them so the
         *  IL matches the shared SpriteVertex stride and we can drive the
         *  effect from `SpriteBatch::Draw`.
         */
        const D3D11_INPUT_ELEMENT_DESC ShroudFogIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };


        /**
         *  Vanilla shroud/fog formulas, ported verbatim from
         *  `CellClass::Draw_Shroud_Or_Fog_Shape` (0x00454E60) and
         *  `CellClass::Draw_Fog_Shape` (0x00455130). The cross-quad write
         *  ordering is whatever D3D's draw-order serialization gives us,
         *  matching vanilla's per-cell sequence one-to-one.
         */
        const char ShroudFogHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer EffectCB : register(b1) {\n"
            "    float2 AtlasSize;\n"
            "    uint   Mode;       /* 0 = ShroudOverwrite, 1 = FogAdditive */\n"
            "    uint   _pad;\n"
            "};\n"
            "\n"
            "Texture2D<uint>          Atlas    : register(t0);\n"
            "RWTexture2D<unorm float> AlphaUAV : register(u0);\n"
            "\n"
            "struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float2 zuv : TEXCOORD1; float4 col : COLOR0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos = float4(p.x, p.y, 0, 1);\n"
            "    o.uv  = i.uv;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "void PSMain(VSOut v) {\n"
            "    int2 px = int2(v.uv * AtlasSize);\n"
            "    uint shape = Atlas.Load(int3(px, 0));\n"
            "    int2 dst = int2(v.pos.xy);\n"
            "    if (Mode == 0) {\n"
            "        /* ShroudOverwrite — vanilla 0x00454E60. */\n"
            "        if (shape == 0xFE) discard;\n"
            "        AlphaUAV[dst] = (float)shape / 255.0;\n"
            "    } else {\n"
            "        /* FogAdditive — vanilla 0x00455130. */\n"
            "        if (shape > 0x7F) discard;\n"
            "        float old_byte = AlphaUAV[dst] * 255.0;\n"
            "        float new_byte;\n"
            "        if (abs(old_byte - 127.0) < 0.5) {\n"
            "            new_byte = (float)shape;\n"
            "        } else {\n"
            "            new_byte = max(0.0, old_byte + (float)shape - 127.0);\n"
            "        }\n"
            "        AlphaUAV[dst] = new_byte / 255.0;\n"
            "    }\n"
            "}\n";
    }


    bool ShroudFogEffect::Initialize(GraphicsDevice& device)
    {
        /**
         *  PS_5_0 required for `RWTexture2D` writes from a pixel shader. The
         *  device is created at FL 11.0+ so this is always available.
         */
        if (!Effect::Initialize(device,
                ShroudFogHLSL, sizeof(ShroudFogHLSL) - 1,
                "shroud_fog",
                ShroudFogIL, _countof(ShroudFogIL),
                /* SpriteCB at b0 */ 64,
                /* vs_profile */ "vs_5_0",
                /* ps_profile */ "ps_5_0")) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(ShroudFogEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("ShroudFogEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void ShroudFogEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void ShroudFogEffect::Set_Params(GraphicsDevice& device, const ShroudFogEffectParams& params)
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
