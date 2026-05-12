/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Shared mega-atlas for all isometric tilesets.
 *
 *          Every IsoTileAsset allocates regions inside this single R8_UINT
 *          texture pair instead of owning its own atlas. Result: all tile
 *          draws can share one color SRV and one Z SRV, so TileQueue::Flush
 *          groups by palette only — the per-(asset,palette) fragmentation
 *          collapses into a handful of batches.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    class IsoTileAtlas
    {
    public:
        static IsoTileAtlas& Get();

        bool Initialize(GraphicsDevice& device, int width = 8192, int height = 8192);
        void Shutdown();

        /**
         *  Drop all allocations and start packing from (0, 0) again. The
         *  underlying GPU texture stays alive — its contents are stale but
         *  will be overwritten as IsoTileAssets re-upload. Called from
         *  IsoTileCache::Clear() on video-mode reset.
         */
        void Reset();

        /**
         *  Reserve a (w x h) region; on success, returns true and fills
         *  out_x/out_y with the assigned position. Simple shelf packing —
         *  rows grow downward, atlas grows along Y as needed (capped at
         *  initial height). Returns false if the atlas is full.
         */
        bool Allocate_Region(int w, int h, int& out_x, int& out_y);

        /**
         *  Upload pixel data into a previously-allocated region.
         */
        bool Upload_Region(int x, int y, int w, int h,
                           const uint8_t* pixels, int pitch_bytes);
        bool Upload_Z_Region(int x, int y, int w, int h,
                             const uint8_t* pixels, int pitch_bytes);

        Texture2D& Get_Texture() { return Atlas; }
        const Texture2D& Get_Texture() const { return Atlas; }
        Texture2D& Get_Z_Texture() { return ZAtlas; }
        const Texture2D& Get_Z_Texture() const { return ZAtlas; }

        bool Is_Initialized() const { return Atlas.Get_SRV() != nullptr && ZAtlas.Get_SRV() != nullptr; }

        /* Telemetry. */
        int  Cursor_X() const { return CursorX; }
        int  Cursor_Y() const { return CursorY; }
        int  Row_Height() const { return RowH; }
        long long Used_Pixels() const;
        long long Total_Pixels() const;

    private:
        IsoTileAtlas() = default;

        Texture2D Atlas;
        Texture2D ZAtlas;
        int CursorX = 0;
        int CursorY = 0;
        int RowH    = 0;
    };
}
