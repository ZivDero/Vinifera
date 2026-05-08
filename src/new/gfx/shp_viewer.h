/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2a sandbox: ImGui developer window that loads a SHP and
 *          renders it via the GPU sprite + palette-LUT pipeline. Used to
 *          validate the pipeline end-to-end before any in-game drawer is
 *          hooked.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <string>

#include "palette_lut.h"
#include "shp_asset.h"
#include "sprite_batch.h"
#include "sprite_effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    class ShpViewer
    {
    public:
        ShpViewer() = default;
        ~ShpViewer() = default;

        ShpViewer(const ShpViewer&) = delete;
        ShpViewer& operator=(const ShpViewer&) = delete;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Build the ImGui controls window. Called inside ImGui::NewFrame /
         *  ImGui::Render cycle.
         */
        void Build_UI();

        /**
         *  Render the chosen SHP frame to the back buffer using SpriteBatch +
         *  SpriteEffect. Call after the game's CPU-blitted surface has been
         *  drawn (so the SHP appears on top), and before End_Frame().
         */
        void Render(GraphicsDevice& device);

    private:
        bool Reload_Shp(GraphicsDevice& device);
        bool Reload_Palette(GraphicsDevice& device);
        void Apply_Tint();

        GraphicsDevice* DevicePtr = nullptr;

        ShpAsset      Shp;
        PaletteLUT    Palette;
        SpriteBatch   Batch;
        SpriteEffect  PalEffect;

        bool          Initialized = false;
        bool          ShpDirty = false;
        bool          PaletteDirty = false;
        bool          TintDirty = false;

        char          ShpPathBuf[260]      = "UNITTEM.SHP";
        char          PalettePathBuf[260]  = "UNITTEM.PAL";
        std::string   StatusLine;

        int           FrameIndex = 0;
        int           DrawX = 200;
        int           DrawY = 200;
        int           Scale = 4;

        int           TintR = 1000;
        int           TintG = 1000;
        int           TintB = 1000;

        bool          UseRemap     = false;
        bool          Darken       = false;
        bool          Translucent25 = false;
        bool          Translucent50 = false;
        bool          Translucent75 = false;

        /**
         *  Cached raw palette bytes (256 × 3) to allow re-tinting without a
         *  reread.
         */
        unsigned char PaletteRaw[256 * 3] = {};
        bool          PaletteValid = false;
    };


    /**
     *  Global ShpViewer instance, owned by sdl_functions.cpp lifecycle.
     */
    extern ShpViewer* g_ShpViewer;
}
