/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Main-window Dear ImGui integration.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "vinifera_imgui.h"

#include "audio_manager.h"
#include "debughandler.h"
#include "effect.h"
#include "graphics_device.h"
#include "perf_monitor.h"
#include "render_target_2d.h"
#include "shp_viewer.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"

#include <algorithm>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

/**
 *  Forward declare message handler from imgui_impl_win32.cpp.
 */
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    static bool IsInitialized = false;

    static constexpr float ZDepthScreenYRange = 16000.0f;

    static float Visible_Screen_Depth_Min(int fallback_height)
    {
        const int logical_height = (VideoHeight > 0) ? VideoHeight : fallback_height;
        const float min_depth = 1.0f - ((float)logical_height / ZDepthScreenYRange);

        return std::max(0.0f, std::min(1.0f, min_depth));
    }

    static bool Is_Mouse_Position_Message(UINT message)
    {
        switch (message) {
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_NCMOUSEMOVE:
        case WM_NCMOUSELEAVE:
            return true;
        default:
            return false;
        }
    }

    static bool Is_Mouse_Button_Message(UINT message)
    {
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            return true;
        default:
            return false;
        }
    }

    static bool Is_Mouse_Wheel_Message(UINT message)
    {
        switch (message) {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return true;
        default:
            return false;
        }
    }

    static bool Is_Keyboard_Message(UINT message)
    {
        switch (message) {
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
            return true;
        default:
            return false;
        }
    }

    static void Build_Z_Buffer_Window()
    {
        if (!Vinifera_ZBufferWindow || Vinifera::Gfx::Device == nullptr) {
            return;
        }

        struct ZDebugParams
        {
            float MinDepth;
            float MaxDepth;
            float Invert;
            float Pad;
        };

        static const char ZDebugHLSL[] =
            "cbuffer ZDebugCB : register(b0) {\n"
            "    float MinDepth;\n"
            "    float MaxDepth;\n"
            "    float Invert;\n"
            "    float Pad;\n"
            "};\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSOut VSMain(uint id : SV_VertexID) {\n"
            "    VSOut o;\n"
            "    float2 uv = float2((id << 1) & 2, id & 2);\n"
            "    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
            "    o.uv = uv;\n"
            "    return o;\n"
            "}\n"
            "Texture2D<float> DepthTex : register(t0);\n"
            "SamplerState Smp : register(s0);\n"
            "float4 PSMain(VSOut v) : SV_Target {\n"
            "    float depth = DepthTex.SampleLevel(Smp, v.uv, 0);\n"
            "    float denom = max(MaxDepth - MinDepth, 0.000001);\n"
            "    float value = saturate((depth - MinDepth) / denom);\n"
            "    if (Invert > 0.5) value = 1.0 - value;\n"
            "    return float4(value, value, value, 1.0);\n"
            "}\n";

        static Vinifera::Gfx::RenderTarget2D z_preview;
        static Vinifera::Gfx::Effect z_effect;
        static ID3D11Device* resource_device = nullptr;
        static int preview_w = 0;
        static int preview_h = 0;
        static bool range_initialized = false;
        static float min_depth = 0.95f;
        static float max_depth = 1.0f;
        static bool invert = true;

        Vinifera::Gfx::GraphicsDevice& device = *Vinifera::Gfx::Device;
        ID3D11Device* d3d_device = device.Get_Device();
        ID3D11DeviceContext* ctx = device.Get_Context();
        ID3D11ShaderResourceView* depth_srv = device.Get_Depth_SRV();
        const int bb_w = device.Get_Backbuffer_Width();
        const int bb_h = device.Get_Backbuffer_Height();

        if (!range_initialized && bb_h > 0) {
            min_depth = Visible_Screen_Depth_Min(bb_h);
            max_depth = 1.0f;
            range_initialized = true;
        }

        if (resource_device != nullptr && resource_device != d3d_device) {
            z_effect.Shutdown();
            z_preview.Shutdown();
            resource_device = nullptr;
            preview_w = preview_h = 0;
        }

        ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Vinifera - Z Buffer", &Vinifera_ZBufferWindow)) {
            if (depth_srv == nullptr || bb_w <= 0 || bb_h <= 0 || ctx == nullptr) {
                ImGui::TextUnformatted("No depth buffer SRV.");
            } else {
                static float scale = 1.0f;

                ImGui::Checkbox("Invert", &invert);
                ImGui::SameLine();
                ImGui::SliderFloat("Scale", &scale, 0.1f, 4.0f, "%.2f");
                if (ImGui::Button("Visible Band")) {
                    min_depth = Visible_Screen_Depth_Min(bb_h);
                    max_depth = 1.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Full Range")) {
                    min_depth = 0.0f;
                    max_depth = 1.0f;
                }
                ImGui::SliderFloat("Min", &min_depth, 0.0f, 1.0f, "%.6f");
                ImGui::SliderFloat("Max", &max_depth, 0.0f, 1.0f, "%.6f");
                if (min_depth > max_depth) {
                    std::swap(min_depth, max_depth);
                }
                ImGui::Text("Backbuffer: %d x %d", bb_w, bb_h);

                if (resource_device == nullptr) {
                    resource_device = d3d_device;
                    if (!z_effect.Initialize(device, ZDebugHLSL, sizeof(ZDebugHLSL) - 1,
                                             "z_buffer_debug", nullptr, 0, sizeof(ZDebugParams))) {
                        resource_device = nullptr;
                    }
                }
                if ((preview_w != bb_w || preview_h != bb_h) && resource_device != nullptr) {
                    z_preview.Shutdown();
                    if (z_preview.Initialize(device, bb_w, bb_h, DXGI_FORMAT_R8G8B8A8_UNORM)) {
                        preview_w = bb_w;
                        preview_h = bb_h;
                    } else {
                        preview_w = preview_h = 0;
                    }
                }

                if (resource_device != nullptr && z_preview.Get_SRV() != nullptr) {
                    device.Set_Render_Target(&z_preview);

                    ID3D11SamplerState* sampler = device.States().Get(Vinifera::Gfx::ESampler::PointClamp);
                    ctx->PSSetSamplers(0, 1, &sampler);
                    ctx->PSSetShaderResources(0, 1, &depth_srv);
                    ctx->OMSetBlendState(device.States().Get(Vinifera::Gfx::EBlend::Opaque), nullptr, 0xFFFFFFFFu);
                    ctx->OMSetDepthStencilState(device.States().Get(Vinifera::Gfx::EDepthStencil::None), 0);
                    ctx->RSSetState(device.States().Get(Vinifera::Gfx::ERasterizer::CullNone));
                    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                    ZDebugParams params = {};
                    params.MinDepth = min_depth;
                    params.MaxDepth = max_depth;
                    params.Invert = invert ? 1.0f : 0.0f;
                    z_effect.Set_Constants(device, &params);
                    z_effect.Apply(device);
                    ctx->Draw(3, 0);

                    ID3D11ShaderResourceView* null_srv = nullptr;
                    ctx->PSSetShaderResources(0, 1, &null_srv);
                }

                ImVec2 avail = ImGui::GetContentRegionAvail();
                if (avail.x < 1.0f) avail.x = 1.0f;
                if (avail.y < 1.0f) avail.y = 1.0f;

                const float aspect = (float)bb_w / (float)bb_h;
                ImVec2 size(avail.x * scale, (avail.x / aspect) * scale);
                if (size.y > avail.y * scale) {
                    size.y = avail.y * scale;
                    size.x = size.y * aspect;
                }

                const ImVec2 uv0(0.0f, 0.0f);
                const ImVec2 uv1(1.0f, 1.0f);
                const ImVec4 border(1.0f, 1.0f, 1.0f, 0.25f);
                ImGui::Image((ImTextureID)z_preview.Get_SRV(), size, uv0, uv1,
                             ImVec4(1.0f, 1.0f, 1.0f, 1.0f), border);
            }
        }
        ImGui::End();
    }
}

/**
 *  Initializes the main-window ImGui context and backends.
 *
 *  @author: ZivDero
 */
bool ViniferaImGui::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (IsInitialized) {
        return true;
    }

    if (hwnd == nullptr || device == nullptr || context == nullptr) {
        return false;
    }

    ImGui_ImplWin32_EnableDpiAwareness();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd)) {
        ImGui::DestroyContext();
        return false;
    }

    if (!ImGui_ImplDX11_Init(device, context)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    IsInitialized = true;

    return true;
}

/**
 *  Shuts down the main-window ImGui context and backends.
 *
 *  @author: ZivDero
 */
void ViniferaImGui::Shutdown()
{
    if (!IsInitialized) {
        return;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    IsInitialized = false;
}

/**
 *  Forwards a Win32 message to ImGui and returns whether ImGui consumed it.
 *
 *  @author: ZivDero
 */
bool ViniferaImGui::Process_Window_Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (!IsInitialized) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();

    /*
    **  The Win32 backend mutates global mouse capture on button messages.
    **  Only feed those messages when ImGui already wants mouse input; mouse
    **  movement is still always fed so hover state can become true.
    */
    if (Is_Mouse_Position_Message(msg)) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        return false;
    }

    if (Is_Mouse_Button_Message(msg) || Is_Mouse_Wheel_Message(msg)) {
        if (!io.WantCaptureMouse) {
            return false;
        }

        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        return true;
    }

    if (Is_Keyboard_Message(msg)) {
        if (!io.WantCaptureKeyboard) {
            return false;
        }

        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
        return true;
    }

    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);

    return false;
}

/**
 *  Renders the main-window ImGui frame through the active D3D11 device.
 *
 *  @author: ZivDero
 */
void ViniferaImGui::Render()
{
    if (!IsInitialized) {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

#ifndef NDEBUG
    if (Vinifera_AudioDebug) {
        AudioManager.Draw_Debug_UI();
    }
#endif

    if (Vinifera::Gfx::g_ShpViewer != nullptr) {
        Vinifera::Gfx::g_ShpViewer->Build_UI();
    }

    Vinifera::Gfx::PerfMonitor::Get().Build_UI();
    Build_Z_Buffer_Window();

    ImGui::Render();
    if (Vinifera::Gfx::Device != nullptr) {
        Vinifera::Gfx::Device->Bind_Backbuffer_Color_Only();
    }
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

/**
 *  Returns whether the main-window ImGui context is initialized.
 *
 *  @author: ZivDero
 */
bool ViniferaImGui::Is_Initialized()
{
    return IsInitialized;
}
