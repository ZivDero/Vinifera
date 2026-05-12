/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Depth- and alpha-aware tactical line Effect.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "tactical_line_effect.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC TacticalLineIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT,    0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        const char TacticalLineHLSL[] =
            "cbuffer SpriteCB : register(b0) {\n"
            "    float4x4 ProjMtx;\n"
            "};\n"
            "cbuffer TacticalLineCB : register(b1) {\n"
            "    float4 ColorStart;\n"
            "    float4 ColorEnd;\n"
            "    float  ZStart;\n"
            "    float  ZEnd;\n"
            "    uint   Flags;\n"
            "    uint   _pad;\n"
            "};\n"
            "static const uint TLF_DEPTH_TEST     = 0x01;\n"
            "static const uint TLF_DEPTH_WRITE    = 0x02;\n"
            "static const uint TLF_ALPHA_MOD      = 0x04;\n"
            "static const uint TLF_ALPHA_TEST_BG  = 0x08;\n"
            "static const uint TLF_ALPHA_TEST_FG  = 0x10;\n"
            "static const uint TLF_GRADIENT       = 0x20;\n"
            "\n"
            "struct VSIn  { float2 pos : POSITION; float t : TEXCOORD0; };\n"
            "struct VSOut { float4 pos : SV_Position; float t : TEXCOORD0; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    o.pos = mul(ProjMtx, float4(i.pos, 0, 1));\n"
            "    o.t = i.t;\n"
            "    return o;\n"
            "}\n"
            "\n"
            "Texture2D<float> AlphaTex   : register(t1);\n"
            "\n"
            "/**\n"
            " * Hardware depth-test note: we DO NOT manually Load the scene\n"
            " * depth SRV here. The same depth resource is bound writable as\n"
            " * the active DSV (see GraphicsDevice::Bind_Scene_Target). D3D11\n"
            " * silently unbinds an SRV that aliases a bound writable DSV, so\n"
            " * any manual Load() in this shader reads 0 and discards every\n"
            " * pixel. Depth testing is done through SV_Depth + the bound\n"
            " * EDepthStencil state (TestLessEqual_NoWrite or WriteLessEqual)\n"
            " * — exactly equivalent to what the manual test would do.\n"
            " */\n"
            "struct PSOut { float4 color : SV_Target; float depth : SV_Depth; };\n"
            "PSOut PSMain(VSOut v) {\n"
            "    PSOut o;\n"
            "    float interp_z = lerp(ZStart, ZEnd, v.t);\n"
            "    o.depth = interp_z;\n"
            "    /* Alpha-buffer interactions all share one sample. */\n"
            "    uint alpha_mask = Flags & (TLF_ALPHA_MOD | TLF_ALPHA_TEST_BG | TLF_ALPHA_TEST_FG);\n"
            "    float alpha_byte = 127.0;\n"
            "    if (alpha_mask) {\n"
            "        alpha_byte = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;\n"
            "        if ((Flags & TLF_ALPHA_TEST_BG) && alpha_byte != 0.0) discard;\n"
            "        if ((Flags & TLF_ALPHA_TEST_FG) && alpha_byte == 0.0) discard;\n"
            "    }\n"
            "    float4 col = (Flags & TLF_GRADIENT) ? lerp(ColorStart, ColorEnd, v.t) : ColorStart;\n"
            "    if (Flags & TLF_ALPHA_MOD) {\n"
            "        col.rgb *= alpha_byte / 127.0;\n"
            "    }\n"
            "    col.rgb *= col.a;   /* premultiply for the bound blend state */\n"
            "    o.color = col;\n"
            "    return o;\n"
            "}\n";
    }


    bool TacticalLineEffect::Initialize(GraphicsDevice& device)
    {
        if (!Effect::Initialize(device,
                TacticalLineHLSL, sizeof(TacticalLineHLSL) - 1,
                "tactical_line",
                TacticalLineIL, _countof(TacticalLineIL),
                /* SpriteCB at b0 — float4x4 ProjMtx */ 64)) {
            return false;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeof(TacticalLineEffectParams);
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device.Get_Device()->CreateBuffer(&desc, nullptr, &ParamsCB))) {
            DEBUG_ERROR("TacticalLineEffect: ParamsCB creation failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void TacticalLineEffect::Shutdown()
    {
        Safe_Release(ParamsCB);
        Effect::Shutdown();
    }


    void TacticalLineEffect::Bind_Sources(GraphicsDevice& device)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) return;

        ID3D11ShaderResourceView* srvs[2] = {
            device.Get_Depth_SRV(),
            device.Get_Alpha_SRV(),
        };
        ctx->PSSetShaderResources(0, 2, srvs);
    }


    void TacticalLineEffect::Set_Params(GraphicsDevice& device, const TacticalLineEffectParams& params)
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
