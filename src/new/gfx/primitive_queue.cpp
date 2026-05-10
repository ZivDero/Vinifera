/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame solid-color tactical primitive queue.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "primitive_queue.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "perf_monitor.h"

#include <cmath>
#include <cstring>


namespace Vinifera::Gfx
{
    namespace
    {
        const char PrimitiveShaderHLSL[] =
            "cbuffer PrimitiveCB : register(b0) { float4x4 ProjMtx; };\n"
            "struct VSIn  { float2 pos : POSITION; float4 col : COLOR0; };\n"
            "struct VSOut { float4 pos : SV_Position; float4 col : COLOR0; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    o.pos = mul(ProjMtx, float4(i.pos.xy, 0.0f, 1.0f));\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n"
            "float4 PSMain(VSOut v) : SV_Target { return v.col; }\n";

        const D3D11_INPUT_ELEMENT_DESC PrimitiveIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    PrimitiveQueue& PrimitiveQueue::Get()
    {
        static PrimitiveQueue instance;
        return instance;
    }


    bool PrimitiveQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }

        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(), 4096);
        if (!Create_Effect(device)) {
            VertexBuffer.Shutdown();
            return false;
        }

        Commands.reserve(1024);
        Initialized = true;
        return true;
    }


    bool PrimitiveQueue::Create_Effect(GraphicsDevice& device)
    {
        return PrimitiveEffect.Initialize(
            device,
            PrimitiveShaderHLSL, sizeof(PrimitiveShaderHLSL) - 1,
            "primitive_solid",
            PrimitiveIL, _countof(PrimitiveIL),
            sizeof(PrimitiveCB));
    }


    void PrimitiveQueue::Shutdown()
    {
        PrimitiveEffect.Shutdown();
        VertexBuffer.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void PrimitiveQueue::Submit(const PrimitiveDrawCmd& cmd)
    {
        if (!Initialized) {
            return;
        }
        if (cmd.Kind == PrimitiveKind::SolidRect && !cmd.Rect.Is_Valid()) {
            return;
        }
        Commands.push_back(cmd);
        PerfMonitor::Get().Note_Primitive_Submit();
    }


    void PrimitiveQueue::Clear()
    {
        Commands.clear();
    }


    void PrimitiveQueue::Emit_Rect(std::vector<PrimitiveVertex>& vertices, const RectF& rect, const float color[4])
    {
        if (!rect.Is_Valid()) {
            return;
        }

        const float x0 = rect.X;
        const float y0 = rect.Y;
        const float x1 = rect.X + rect.W;
        const float y1 = rect.Y + rect.H;

        const PrimitiveVertex v0 = { { x0, y0 }, { color[0], color[1], color[2], color[3] } };
        const PrimitiveVertex v1 = { { x1, y0 }, { color[0], color[1], color[2], color[3] } };
        const PrimitiveVertex v2 = { { x1, y1 }, { color[0], color[1], color[2], color[3] } };
        const PrimitiveVertex v3 = { { x0, y1 }, { color[0], color[1], color[2], color[3] } };

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v3);
    }


    void PrimitiveQueue::Emit_Line(std::vector<PrimitiveVertex>& vertices, const PrimitiveDrawCmd& cmd)
    {
        const float dx = cmd.X1 - cmd.X0;
        const float dy = cmd.Y1 - cmd.Y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0001f) {
            RectF rect = {};
            rect.X = cmd.X0;
            rect.Y = cmd.Y0;
            rect.W = cmd.Thickness;
            rect.H = cmd.Thickness;
            Emit_Rect(vertices, rect, cmd.Color);
            return;
        }

        const float half = cmd.Thickness * 0.5f;
        const float nx = -dy / len * half;
        const float ny =  dx / len * half;

        const PrimitiveVertex v0 = { { cmd.X0 + nx, cmd.Y0 + ny }, { cmd.Color[0], cmd.Color[1], cmd.Color[2], cmd.Color[3] } };
        const PrimitiveVertex v1 = { { cmd.X1 + nx, cmd.Y1 + ny }, { cmd.Color[0], cmd.Color[1], cmd.Color[2], cmd.Color[3] } };
        const PrimitiveVertex v2 = { { cmd.X1 - nx, cmd.Y1 - ny }, { cmd.Color[0], cmd.Color[1], cmd.Color[2], cmd.Color[3] } };
        const PrimitiveVertex v3 = { { cmd.X0 - nx, cmd.Y0 - ny }, { cmd.Color[0], cmd.Color[1], cmd.Color[2], cmd.Color[3] } };

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v0);
        vertices.push_back(v2);
        vertices.push_back(v3);
    }


    void PrimitiveQueue::Draw_Group(GraphicsDevice& device, const std::vector<PrimitiveDrawCmd>& commands, size_t begin, size_t end)
    {
        if (begin >= end) {
            return;
        }

        std::vector<PrimitiveVertex> vertices;
        vertices.reserve((end - begin) * 6);

        for (size_t i = begin; i < end; ++i) {
            const PrimitiveDrawCmd& cmd = commands[i];
            if (cmd.Kind == PrimitiveKind::SolidRect) {
                Emit_Rect(vertices, cmd.Rect, cmd.Color);
            } else {
                Emit_Line(vertices, cmd);
            }
        }

        if (vertices.empty()) {
            return;
        }

        ID3D11DeviceContext* ctx = device.Get_Context();
        const int bb_w = device.Get_Backbuffer_Width();
        const int bb_h = device.Get_Backbuffer_Height();

        const float L = 0.0f;
        const float R = (float)bb_w;
        const float T = 0.0f;
        const float B = (float)bb_h;
        PrimitiveCB cb = {};
        cb.ProjMtx[0]  = 2.0f / (R - L);
        cb.ProjMtx[5]  = 2.0f / (T - B);
        cb.ProjMtx[10] = 1.0f;
        cb.ProjMtx[12] = (R + L) / (L - R);
        cb.ProjMtx[13] = (T + B) / (B - T);
        cb.ProjMtx[15] = 1.0f;
        PrimitiveEffect.Set_Constants(device, &cb);

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)bb_w;
        vp.Height = (float)bb_h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));
        ctx->OMSetDepthStencilState(device.States().Get(EDepthStencil::None), 0);

        const float blend_factor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(device.States().Get(commands[begin].Blend), blend_factor, 0xFFFFFFFFu);

        PrimitiveEffect.Apply(device);

        PrimitiveVertex* dst = VertexBuffer.Begin((int)vertices.size());
        if (dst == nullptr) {
            return;
        }
        memcpy(dst, vertices.data(), vertices.size() * sizeof(PrimitiveVertex));
        VertexBuffer.End();

        ID3D11Buffer* vb = VertexBuffer.Get();
        UINT stride = sizeof(PrimitiveVertex);
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw((UINT)vertices.size(), 0);
        PerfMonitor::Get().Note_Primitive_Draw_Call();
    }


    void PrimitiveQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        std::vector<PrimitiveDrawCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const PrimitiveDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass) {
                pass_commands.push_back(cmd);
            }
        }
        if (pass_commands.empty()) {
            return;
        }

        device.Bind_Scene_Target();

        size_t begin = 0;
        while (begin < pass_commands.size()) {
            size_t end = begin + 1;
            while (end < pass_commands.size() && pass_commands[end].Blend == pass_commands[begin].Blend) {
                ++end;
            }
            Draw_Group(device, pass_commands, begin, end);
            begin = end;
        }
    }
}
