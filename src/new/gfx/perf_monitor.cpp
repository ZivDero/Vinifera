/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lightweight per-frame performance counters + ImGui window.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "perf_monitor.h"

#include "tmp_atlas.h"
#include "vinifera_globals.h"

#include <algorithm>
#include <imgui.h>


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
        Stats.TileCmds = 0;
        Stats.TileBatches = 0;
        Stats.TileDrawCalls = 0;
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


    void PerfMonitor::Set_Cache_Sizes(int shp, int tmp, int pal)
    {
        Stats.ShpCacheSize = shp;
        Stats.TmpCacheSize = tmp;
        Stats.PaletteCacheSize = pal;
    }


    void PerfMonitor::Build_UI()
    {
        if (!Vinifera_PerfWindow) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Vinifera - GPU Perf", &Vinifera_PerfWindow)) {

            const double fps_avg = Stats.AvgFrameMs > 0.0 ? 1000.0 / Stats.AvgFrameMs : 0.0;
            const double fps_max = Stats.MinFrameMs > 0.0 ? 1000.0 / Stats.MinFrameMs : 0.0;
            const double fps_min = Stats.MaxFrameMs > 0.0 ? 1000.0 / Stats.MaxFrameMs : 0.0;

            ImGui::Text("Frame  : %6.2f ms (last)   %6.1f fps", Stats.LastFrameMs,
                Stats.LastFrameMs > 0.0 ? 1000.0 / Stats.LastFrameMs : 0.0);
            ImGui::Text("Avg/60 : %6.2f ms (%6.1f fps)", Stats.AvgFrameMs, fps_avg);
            ImGui::Text("Range  : %6.2f .. %6.2f ms (%6.1f .. %6.1f fps)",
                Stats.MinFrameMs, Stats.MaxFrameMs, fps_min, fps_max);

            ImGui::Separator();
            ImGui::TextUnformatted("SpriteQueue:");
            ImGui::Text("  cmds   : %d", Stats.SpriteCmds);
            ImGui::Text("  batches: %d", Stats.SpriteBatches);
            ImGui::Text("  draws  : %d", Stats.SpriteDrawCalls);

            ImGui::Separator();
            ImGui::TextUnformatted("TileQueue:");
            ImGui::Text("  cmds   : %d", Stats.TileCmds);
            ImGui::Text("  batches: %d", Stats.TileBatches);
            ImGui::Text("  draws  : %d", Stats.TileDrawCalls);

            ImGui::Separator();
            ImGui::TextUnformatted("Caches:");
            ImGui::Text("  ShpCache    : %d entries", Stats.ShpCacheSize);
            ImGui::Text("  TmpCache    : %d entries", Stats.TmpCacheSize);
            ImGui::Text("  PaletteCache: %d entries", Stats.PaletteCacheSize);

            ImGui::Separator();
            ImGui::TextUnformatted("TmpAtlas:");
            const TmpAtlas& atlas = TmpAtlas::Get();
            const long long used = atlas.Used_Pixels();
            const long long total = atlas.Total_Pixels();
            const double pct = total > 0 ? 100.0 * (double)used / (double)total : 0.0;
            ImGui::Text("  size : %dx%d (%.1f MB R8)",
                atlas.Get_Texture().Width(), atlas.Get_Texture().Height(),
                (double)total / (1024.0 * 1024.0));
            ImGui::Text("  fill : %.2f%% (%lld / %lld px)", pct, used, total);
            ImGui::Text("  pack : cursor=(%d, %d), row_h=%d",
                atlas.Cursor_X(), atlas.Cursor_Y(), atlas.Row_Height());
        }
        ImGui::End();
    }
}
