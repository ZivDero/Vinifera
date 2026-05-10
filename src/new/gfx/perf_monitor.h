/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lightweight per-frame performance counters + ImGui window.
 *
 *          Counts SpriteQueue / TileQueue throughput (commands submitted,
 *          batches issued, DrawIndexed calls), tracks asset-cache sizes,
 *          and measures per-frame wall-clock time via QPC. The ImGui window
 *          is gated on `Vinifera_PerfWindow` and only renders when the user
 *          opens it; counters always run with negligible cost.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <windows.h>


namespace Vinifera::Gfx
{
    struct PerfStats
    {
        /* Per-frame counters (reset by Begin_Frame). */
        int  SpriteCmds       = 0;
        int  SpriteBatches    = 0;
        int  SpriteDrawCalls  = 0;
        int  TileCmds         = 0;
        int  TileBatches      = 0;
        int  TileDrawCalls    = 0;
        int  AlphaLights      = 0;     // alpha-light shapes submitted this frame

        /* Cache size snapshot (refreshed each frame). */
        int  ShpCacheSize     = 0;
        int  TmpCacheSize     = 0;
        int  PaletteCacheSize = 0;

        /* Frame timing in milliseconds. */
        double LastFrameMs = 0.0;
        double AvgFrameMs  = 0.0;     // mean of the last N frames
        double MinFrameMs  = 0.0;     // min over the window
        double MaxFrameMs  = 0.0;     // max over the window
    };


    class PerfMonitor
    {
    public:
        static PerfMonitor& Get();

        /**
         *  Reset per-frame counters and snapshot the start time. Call near
         *  the top of SDL_Update_Screen.
         */
        void Begin_Frame();

        /**
         *  Stop the timer; commit the elapsed time to the sliding window.
         *  Call at the end of SDL_Update_Screen (after End_Frame Present).
         */
        void End_Frame();

        /* Counter helpers used by the queues. */
        void Note_Sprite_Submit()     { ++Stats.SpriteCmds; }
        void Note_Sprite_Batch()      { ++Stats.SpriteBatches; }
        void Note_Sprite_Draw_Call()  { ++Stats.SpriteDrawCalls; }
        void Note_Tile_Submit()       { ++Stats.TileCmds; }
        void Note_Tile_Batch()        { ++Stats.TileBatches; }
        void Note_Tile_Draw_Call()    { ++Stats.TileDrawCalls; }
        void Set_Alpha_Lights(int n)  { Stats.AlphaLights = n; }

        /* Cache snapshots — called by Begin_Frame; queues update separately. */
        void Set_Cache_Sizes(int shp, int tmp, int pal);

        /**
         *  Build the ImGui window. No-op when Vinifera_PerfWindow is false.
         */
        void Build_UI();

        const PerfStats& Get_Stats() const { return Stats; }

    private:
        PerfMonitor();

        PerfStats   Stats;
        LARGE_INTEGER QpcFreq = {};
        LARGE_INTEGER FrameStart = {};

        static constexpr int kWindowSize = 60;
        std::array<double, kWindowSize> RecentFrameMs = {};
        int    WindowIdx = 0;
        int    WindowFilled = 0;
    };
}
