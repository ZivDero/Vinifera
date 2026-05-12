/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lightweight per-frame performance counters.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "perf_monitor.h"


namespace Vinifera::Gfx
{
    PerfMonitor& PerfMonitor::Get()
    {
        static PerfMonitor instance;
        return instance;
    }


    PerfMonitor::PerfMonitor()
    {
        QueryPerformanceFrequency(&QpcFreq);
    }


    void PerfMonitor::Begin_Frame()
    {
        QueryPerformanceCounter(&FrameStart);
        Stats.SpriteCmds = 0;
        Stats.SpriteBatches = 0;
        Stats.SpriteDrawCalls = 0;
        Stats.SpriteBreakBucket = 0;
        Stats.SpriteBreakPage = 0;
        Stats.SpriteBreakZPage = 0;
        Stats.SpriteBreakPalette = 0;
        Stats.SpriteBreakFlags = 0;
        Stats.SpriteBreakDepth = 0;
        Stats.TileCmds = 0;
        Stats.TileBatches = 0;
        Stats.TileDrawCalls = 0;
        Stats.PrimitiveCmds = 0;
        Stats.PrimitiveDrawCalls = 0;
        Stats.FontCmds = 0;
        Stats.FontBatches = 0;
        Stats.FontDrawCalls = 0;
        Stats.VoxelCompositeCmds = 0;
        Stats.VoxelCompositeDrawCalls = 0;
        Stats.TacticalLineCmds = 0;
        Stats.TacticalLineDrawCalls = 0;
        Stats.SidebarComposites = 0;
    }


    void PerfMonitor::End_Frame()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double freq = (double)QpcFreq.QuadPart;
        const double elapsed_ms = freq > 0.0
            ? (double)(now.QuadPart - FrameStart.QuadPart) * 1000.0 / freq
            : 0.0;

        Stats.LastFrameMs = elapsed_ms;
        RecentFrameMs[WindowIdx] = elapsed_ms;
        WindowIdx = (WindowIdx + 1) % kWindowSize;
        if (WindowFilled < kWindowSize) ++WindowFilled;

        double sum = 0.0;
        double mn = elapsed_ms;
        double mx = elapsed_ms;
        for (int i = 0; i < WindowFilled; ++i) {
            const double v = RecentFrameMs[i];
            sum += v;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        Stats.AvgFrameMs = WindowFilled > 0 ? sum / (double)WindowFilled : elapsed_ms;
        Stats.MinFrameMs = mn;
        Stats.MaxFrameMs = mx;
    }


    void PerfMonitor::Set_Cache_Sizes(int shp, int iso_tile, int pal)
    {
        Stats.ShpCacheSize = shp;
        Stats.IsoTileCacheSize = iso_tile;
        Stats.PaletteCacheSize = pal;
    }


}
