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
#include "vinifera_globals.h"

#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
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
}

/**
 *  Initializes the main-window ImGui context and the SDL_GPU renderer backend.
 *
 *  @author: ZivDero
 */
bool ViniferaImGui::Initialize(HWND hwnd, SDL_GPUDevice* device, SDL_GPUTextureFormat color_target_format)
{
    if (IsInitialized) {
        return true;
    }

    if (hwnd == nullptr || device == nullptr) {
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

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = device;
    init_info.ColorTargetFormat = color_target_format;
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&init_info)) {
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

    ImGui_ImplSDLGPU3_Shutdown();
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
 *  Builds the ImGui frame and uploads its draw lists onto the supplied
 *  command buffer. Must run BEFORE the render pass that ImGui will draw
 *  into; this is a hard requirement of imgui_impl_sdlgpu3.
 *
 *  @author: ZivDero
 */
void ViniferaImGui::Prepare(SDL_GPUCommandBuffer* command_buffer)
{
    if (!IsInitialized || command_buffer == nullptr) {
        return;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

#ifndef NDEBUG
    if (Vinifera_AudioDebug) {
        AudioManager.Draw_Debug_UI();
    }
#endif

    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), command_buffer);
}

/**
 *  Issues ImGui draw commands inside the supplied render pass. Must be
 *  called between Prepare() and the end of the same render pass.
 *
 *  @author: ZivDero
 */
void ViniferaImGui::Render(SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* render_pass)
{
    if (!IsInitialized || command_buffer == nullptr || render_pass == nullptr) {
        return;
    }

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) {
        return;
    }

    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
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
