/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lightweight per-frame GPU performance counters (queue throughput,
 *          cache sizes, wall-clock time via QPC).
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
        /**
         *  SpriteQueue batch-break taxonomy. Each state transition between
         *  adjacent commands is classified by the *first* field that differs
         *  (in the same order state_eq checks). Sum equals SpriteBatches
         *  minus the number of bucket starts.
         */
        int  SpriteBreakBucket   = 0;     // OutputTarget transition (rare)
        int  SpriteBreakPage     = 0;     // SHP atlas page differs
        int  SpriteBreakZPage    = 0;     // Z-shape atlas page differs
        int  SpriteBreakPalette  = 0;     // PaletteLUT* differs
        int  SpriteBreakFlags    = 0;     // EffectFlags differ (DARKEN, NO_ALPHA_BUFFER)
        int  SpriteBreakDepth    = 0;     // WriteDepth/DisableDepth differ
        int  TileCmds         = 0;
        int  TileBatches      = 0;
        int  TileDrawCalls    = 0;
        int  PrimitiveCmds    = 0;
        int  PrimitiveDrawCalls = 0;
        int  FontCmds         = 0;
        int  FontBatches      = 0;
        int  FontDrawCalls    = 0;
        int  VoxelCompositeCmds      = 0;
        int  VoxelCompositeDrawCalls = 0;
        int  TacticalLineCmds      = 0;
        int  TacticalLineDrawCalls = 0;
        int  SidebarComposites = 0;
        int  AlphaLights      = 0;     // alpha-light shapes submitted this frame
        int  ShroudFog        = 0;     // shroud/fog cells submitted this frame
        int  ShroudFogDraws   = 0;     // shroud/fog DrawIndexed calls this frame

        /* Cache size snapshot (refreshed each frame). */
        int  ShpCacheSize     = 0;
        int  IsoTileCacheSize     = 0;
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

        void Begin_Frame();  // reset per-frame counters and snapshot the start time
        void End_Frame();    // stop the timer; commit elapsed time to the sliding window

        /* Counter helpers used by the queues. */
        void Note_Sprite_Submit()     { ++Stats.SpriteCmds; }
        void Note_Sprite_Batch()      { ++Stats.SpriteBatches; }
        void Note_Sprite_Draw_Call()  { ++Stats.SpriteDrawCalls; }
        void Note_Sprite_Break_Bucket()  { ++Stats.SpriteBreakBucket; }
        void Note_Sprite_Break_Page()    { ++Stats.SpriteBreakPage; }
        void Note_Sprite_Break_ZPage()   { ++Stats.SpriteBreakZPage; }
        void Note_Sprite_Break_Palette() { ++Stats.SpriteBreakPalette; }
        void Note_Sprite_Break_Flags()   { ++Stats.SpriteBreakFlags; }
        void Note_Sprite_Break_Depth()   { ++Stats.SpriteBreakDepth; }
        void Note_Tile_Submit()       { ++Stats.TileCmds; }
        void Note_Tile_Batch()        { ++Stats.TileBatches; }
        void Note_Tile_Draw_Call()    { ++Stats.TileDrawCalls; }
        void Note_Primitive_Submit()  { ++Stats.PrimitiveCmds; }
        void Note_Primitive_Draw_Call() { ++Stats.PrimitiveDrawCalls; }
        void Note_Font_Submit()       { ++Stats.FontCmds; }
        void Note_Font_Batch()        { ++Stats.FontBatches; }
        void Note_Font_Draw_Call()    { ++Stats.FontDrawCalls; }
        void Note_Voxel_Composite_Submit()    { ++Stats.VoxelCompositeCmds; }
        void Note_Voxel_Composite_Draw_Call() { ++Stats.VoxelCompositeDrawCalls; }
        void Note_Tactical_Line_Submit()    { ++Stats.TacticalLineCmds; }
        void Note_Tactical_Line_Draw_Call() { ++Stats.TacticalLineDrawCalls; }
        void Note_Sidebar_Composite()  { ++Stats.SidebarComposites; }
        void Set_Alpha_Lights(int n)  { Stats.AlphaLights = n; }
        void Set_Shroud_Fog(int n)    { Stats.ShroudFog = n; }
        void Set_Shroud_Fog_Draws(int n) { Stats.ShroudFogDraws = n; }

        /* Cache snapshots — called by Begin_Frame; queues update separately. */
        void Set_Cache_Sizes(int shp, int iso_tile, int pal);

        const PerfStats& Get_Stats() const { return Stats; }

        /**
         *  Recent per-frame elapsed times (ms) used by the perf graph.
         *  Returns the ring-buffer storage; `count` is how many entries are
         *  populated (up to kWindowSize), `head` is the index where the *next*
         *  sample will be written (so the oldest sample is at `head` when the
         *  ring is full).
         */
        const double* Recent_Frame_Ms_Data() const { return RecentFrameMs.data(); }
        int           Recent_Frame_Ms_Count() const { return WindowFilled; }
        int           Recent_Frame_Ms_Head() const { return WindowIdx; }
        int           Recent_Frame_Ms_Capacity() const { return kWindowSize; }

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
