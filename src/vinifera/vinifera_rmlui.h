/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Main-window RmlUi integration.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <Windows.h>

struct SDL_Renderer;

namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace ViniferaRmlUi
{
    bool Initialize(HWND hwnd, SDL_Renderer* renderer);
    void Shutdown();

    bool Process_Window_Message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void Render();

    bool Is_Initialized();
    bool Is_Dialog_Open();

    Rml::Context* Get_Context();
    Rml::ElementDocument* Load_Document(const char* rml);
    void Close_Document();
}
