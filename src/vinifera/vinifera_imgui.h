/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Main-window Dear ImGui integration.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <SDL3/SDL_gpu.h>
#include <windows.h>

namespace ViniferaImGui
{
    /**
     *  Initializes the main-window ImGui context and the SDL_GPU renderer
     *  backend. The colour-target format must match the swapchain format
     *  ImGui will draw into.
     */
    bool Initialize(HWND hwnd, SDL_GPUDevice* device, SDL_GPUTextureFormat color_target_format);

    /**
     *  Shuts down the main-window ImGui context and backends.
     */
    void Shutdown();

    /**
     *  Forwards a Win32 message to ImGui and returns whether ImGui consumed it.
     */
    bool Process_Window_Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    /**
     *  Builds the ImGui frame and uploads its draw lists onto the supplied
     *  command buffer. Must be called BEFORE the render pass that ImGui will
     *  draw into is started, as required by imgui_impl_sdlgpu3.
     */
    void Prepare(SDL_GPUCommandBuffer* command_buffer);

    /**
     *  Issues ImGui draw commands inside an already-started render pass that
     *  targets the swapchain. Must be called between Prepare() and the end
     *  of the same render pass.
     */
    void Render(SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* render_pass);

    /**
     *  Returns whether the main-window ImGui context is initialized.
     */
    bool Is_Initialized();
}
