/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Tactical render pass tags for queued DX11 draws.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>


namespace Vinifera::Gfx
{
    enum class RenderPass : uint8_t
    {
        ObjectsNearShroud = 0,
        Shroud,
        TerrainCells,
        FoggedObjects,
        Overlays,
        TerrainObjects,
        CellShadows,
        Buildings,
        PreObjectUi,
        ObjectLayer,
        PostEffects,
        UiOverlay,
        Count
    };

    inline RenderPass g_CurrentRenderPass = RenderPass::TerrainCells;

    inline RenderPass Current_Render_Pass()
    {
        return g_CurrentRenderPass;
    }

    inline void Set_Current_Render_Pass(RenderPass pass)
    {
        g_CurrentRenderPass = pass;
    }

    inline void Reset_Current_Render_Pass()
    {
        g_CurrentRenderPass = RenderPass::TerrainCells;
    }

    inline bool Is_Cell_Shadow_Pass(RenderPass pass)
    {
        return pass == RenderPass::CellShadows;
    }
}
