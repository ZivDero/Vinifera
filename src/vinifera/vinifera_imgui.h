/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Main-window Dear ImGui integration.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <windows.h>


struct ID3D11Device;
struct ID3D11DeviceContext;


namespace ViniferaImGui
{
    /**
     *  Initializes the main-window ImGui context and backends.
     */
    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);

    /**
     *  Shuts down the main-window ImGui context and backends.
     */
    void Shutdown();

    /**
     *  Forwards a Win32 message to ImGui and returns whether ImGui consumed it.
     */
    bool Process_Window_Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    /**
     *  Renders the main-window ImGui frame through the active D3D11 device.
     */
    void Render();

    /**
     *  Returns whether the main-window ImGui context is initialized.
     */
    bool Is_Initialized();
}
