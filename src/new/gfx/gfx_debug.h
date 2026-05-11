/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Gfx debug ImGui surfaces (toolbar + perf / z / alpha windows).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once


namespace Vinifera::Gfx::Gfx_Debug
{
    /**
     *  Draws every gfx-debug ImGui surface (the TacticalRect-anchored toolbar
     *  and the Perf / Z buffer / Alpha buffer windows). Caller gates on
     *  `Vinifera_GfxDebug`; sub-window visibility is owned internally.
     */
    void Draw_Debug_UI();
}
