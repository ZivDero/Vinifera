/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Gfx debug ImGui surfaces (toolbar + perf / z / alpha windows).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "gfx_debug.h"

#include "effect.h"
#include "graphics_device.h"
#include "perf_monitor.h"
#include "render_target_2d.h"
#include "sdl_functions.h"
#include "tibsun_globals.h"
#include "iso_tile_atlas.h"
#include "shp_atlas.h"
#include "vinifera_globals.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>


namespace Vinifera::Gfx::Gfx_Debug
{
    namespace
    {
        /**
         *  Sub-window visibility. Owned here; the only externally visible knob
         *  is `Vinifera_GfxDebug`.
         */
        static bool ShowPerf        = true;
        static bool ShowZBuffer     = true;
        static bool ShowAlphaBuffer = false;

        static constexpr float ZDepthScreenYRange = 16000.0f;


        static float Visible_Screen_Depth_Min(int fallback_height)
        {
            const int logical_height = (VideoHeight > 0) ? VideoHeight : fallback_height;
            const float min_depth = 1.0f - ((float)logical_height / ZDepthScreenYRange);

            return std::max(0.0f, std::min(1.0f, min_depth));
        }


        static void Toggle_Button(const char* label, bool* state)
        {
            const bool active = (state != nullptr) && *state;
            if (active) {
                const ImVec4 accent       = ImVec4(0.30f, 0.55f, 0.85f, 1.0f);
                const ImVec4 accent_hover = ImVec4(0.40f, 0.65f, 0.95f, 1.0f);
                const ImVec4 accent_press = ImVec4(0.25f, 0.45f, 0.75f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        accent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_hover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accent_press);
            }
            if (ImGui::Button(label) && state != nullptr) {
                *state = !*state;
            }
            if (active) {
                ImGui::PopStyleColor(3);
            }
        }


        static void Build_Debug_Toolbar()
        {
            /**
             *  Anchor at TacticalRect's top-left, converted from game/logical
             *  pixels to physical window pixels (ImGui's coordinate space).
             *  When SDL_Should_Scale() is false, both scales return 1.0.
             */
            const float pos_x = (float)TacticalRect.X / SDL_XScale();
            const float pos_y = (float)TacticalRect.Y / SDL_YScale();
            ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);

            ImGui::Begin("##ViniferaDebugToolbar", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize   |
                ImGuiWindowFlags_NoMove     |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize);

            Toggle_Button("Perf",         &ShowPerf);
            ImGui::SameLine();
            Toggle_Button("Z Buffer",     &ShowZBuffer);
            ImGui::SameLine();
            Toggle_Button("Alpha Buffer", &ShowAlphaBuffer);

            ImGui::End();
        }


        static void Build_Perf_Window()
        {
            if (!ShowPerf) {
                return;
            }

            const PerfStats& Stats = PerfMonitor::Get().Get_Stats();

            ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Vinifera - GPU Perf", &ShowPerf)) {

                const double fps_avg = Stats.AvgFrameMs > 0.0 ? 1000.0 / Stats.AvgFrameMs : 0.0;
                const double fps_max = Stats.MinFrameMs > 0.0 ? 1000.0 / Stats.MinFrameMs : 0.0;
                const double fps_min = Stats.MaxFrameMs > 0.0 ? 1000.0 / Stats.MaxFrameMs : 0.0;

                ImGui::Text("Frame  : %6.2f ms (last)   %6.1f fps", Stats.LastFrameMs,
                    Stats.LastFrameMs > 0.0 ? 1000.0 / Stats.LastFrameMs : 0.0);
                ImGui::Text("Avg/60 : %6.2f ms (%6.1f fps)", Stats.AvgFrameMs, fps_avg);
                ImGui::Text("Range  : %6.2f .. %6.2f ms (%6.1f .. %6.1f fps)",
                    Stats.MinFrameMs, Stats.MaxFrameMs, fps_min, fps_max);

                /**
                 *  Frame-time graph. Unroll the ring buffer into a contiguous
                 *  scratch array so PlotLines reads chronologically (left =
                 *  oldest, right = newest).
                 */
                const PerfMonitor& monitor = PerfMonitor::Get();
                const int filled = monitor.Recent_Frame_Ms_Count();
                if (filled > 0) {
                    const int capacity = monitor.Recent_Frame_Ms_Capacity();
                    const int head     = monitor.Recent_Frame_Ms_Head();
                    const double* src  = monitor.Recent_Frame_Ms_Data();
                    const int start    = (filled < capacity) ? 0 : head;

                    float plot[256];
                    const int count = std::min(filled, (int)(sizeof(plot) / sizeof(plot[0])));
                    for (int i = 0; i < count; ++i) {
                        plot[i] = (float)src[(start + i) % capacity];
                    }
                    char overlay[32];
                    snprintf(overlay, sizeof(overlay), "%.2f ms", Stats.LastFrameMs);
                    const float plot_max = (float)Stats.MaxFrameMs * 1.1f;
                    ImGui::PlotLines("##frame_ms", plot, count, 0, overlay,
                                     0.0f, plot_max > 1.0f ? plot_max : 16.0f,
                                     ImVec2(0.0f, 60.0f));
                }

                ImGui::Separator();
                ImGui::TextUnformatted("SpriteQueue:");
                ImGui::Text("  cmds   : %d", Stats.SpriteCmds);
                ImGui::Text("  batches: %d", Stats.SpriteBatches);
                ImGui::Text("  draws  : %d", Stats.SpriteDrawCalls);
                /**
                 *  Per-frame batch-break taxonomy. The largest column tells us
                 *  which state field is the load-bearing batch-breaker, and
                 *  therefore where the next optimization should go.
                 */
                const int total_breaks = Stats.SpriteBreakBucket
                                       + Stats.SpriteBreakPage
                                       + Stats.SpriteBreakZPage
                                       + Stats.SpriteBreakPalette
                                       + Stats.SpriteBreakFlags
                                       + Stats.SpriteBreakDepth;
                ImGui::Text("  breaks : %d (page=%d zpage=%d pal=%d flags=%d depth=%d bucket=%d)",
                    total_breaks,
                    Stats.SpriteBreakPage,
                    Stats.SpriteBreakZPage,
                    Stats.SpriteBreakPalette,
                    Stats.SpriteBreakFlags,
                    Stats.SpriteBreakDepth,
                    Stats.SpriteBreakBucket);

                ImGui::Separator();
                ImGui::TextUnformatted("TileQueue:");
                ImGui::Text("  cmds   : %d", Stats.TileCmds);
                ImGui::Text("  batches: %d", Stats.TileBatches);
                ImGui::Text("  draws  : %d", Stats.TileDrawCalls);

                ImGui::Separator();
                ImGui::TextUnformatted("FontQueue:");
                ImGui::Text("  cmds   : %d", Stats.FontCmds);
                ImGui::Text("  batches: %d", Stats.FontBatches);
                ImGui::Text("  draws  : %d", Stats.FontDrawCalls);

                ImGui::Separator();
                ImGui::TextUnformatted("VoxelComposite:");
                ImGui::Text("  cmds   : %d", Stats.VoxelCompositeCmds);
                ImGui::Text("  draws  : %d", Stats.VoxelCompositeDrawCalls);

                ImGui::Separator();
                ImGui::TextUnformatted("TacticalLines:");
                ImGui::Text("  cmds   : %d", Stats.TacticalLineCmds);
                ImGui::Text("  draws  : %d", Stats.TacticalLineDrawCalls);

                ImGui::Separator();
                ImGui::TextUnformatted("PrimitiveQueue:");
                ImGui::Text("  cmds   : %d", Stats.PrimitiveCmds);
                ImGui::Text("  draws  : %d", Stats.PrimitiveDrawCalls);

                ImGui::Separator();
                ImGui::TextUnformatted("SidebarRT:");
                ImGui::Text("  composites: %d", Stats.SidebarComposites);

                ImGui::Separator();
                ImGui::TextUnformatted("AlphaLights:");
                ImGui::Text("  shapes : %d submitted", Stats.AlphaLights);

                ImGui::Separator();
                ImGui::TextUnformatted("ShroudFog:");
                ImGui::Text("  cells  : %d submitted", Stats.ShroudFog);
                ImGui::Text("  draws  : %d", Stats.ShroudFogDraws);

                ImGui::Separator();
                ImGui::TextUnformatted("Caches:");
                ImGui::Text("  ShpCache    : %d entries", Stats.ShpCacheSize);
                ImGui::Text("  IsoTileCache    : %d entries", Stats.IsoTileCacheSize);
                ImGui::Text("  PaletteCache: %d entries", Stats.PaletteCacheSize);

                ImGui::Separator();
                ImGui::TextUnformatted("IsoTileAtlas:");
                const IsoTileAtlas& atlas = IsoTileAtlas::Get();
                const long long used = atlas.Used_Pixels();
                const long long total = atlas.Total_Pixels();
                const double pct = total > 0 ? 100.0 * (double)used / (double)total : 0.0;
                ImGui::Text("  size : %dx%d (%.1f MB R8)",
                    atlas.Get_Texture().Width(), atlas.Get_Texture().Height(),
                    (double)total / (1024.0 * 1024.0));
                ImGui::Text("  fill : %.2f%% (%lld / %lld px)", pct, used, total);
                ImGui::Text("  pack : cursor=(%d, %d), row_h=%d",
                    atlas.Cursor_X(), atlas.Cursor_Y(), atlas.Row_Height());

                ImGui::Separator();
                ImGui::TextUnformatted("ShpAtlas:");
                const ShpAtlas& shp_atlas = ShpAtlas::Get();
                const long long shp_used = shp_atlas.Used_Pixels();
                const long long shp_total = shp_atlas.Total_Pixels();
                const double shp_pct = shp_total > 0 ? 100.0 * (double)shp_used / (double)shp_total : 0.0;
                ImGui::Text("  pages: %d x (%dx%d) = %.1f MB R8",
                    shp_atlas.Page_Count(),
                    shp_atlas.Page_Width(), shp_atlas.Page_Height(),
                    (double)shp_total / (1024.0 * 1024.0));
                ImGui::Text("  fill : %.2f%% (%lld / %lld px)", shp_pct, shp_used, shp_total);

                if (Vinifera::Gfx::Device != nullptr) {
                    ImGui::Separator();
                    ImGui::TextUnformatted("Targets:");
                    ImGui::Text("  Backbuffer : %d x %d",
                        Vinifera::Gfx::Device->Get_Backbuffer_Width(),
                        Vinifera::Gfx::Device->Get_Backbuffer_Height());
                    ImGui::Text("  SceneRT    : %d x %d",
                        Vinifera::Gfx::Device->Get_Scene_Target_Width(),
                        Vinifera::Gfx::Device->Get_Scene_Target_Height());
                    ImGui::Text("  SidebarRT  : %d x %d",
                        Vinifera::Gfx::Device->Get_Sidebar_Target_Width(),
                        Vinifera::Gfx::Device->Get_Sidebar_Target_Height());
                }
            }
            ImGui::End();
        }


        static void Build_Z_Buffer_Window()
        {
            if (!ShowZBuffer || Vinifera::Gfx::Device == nullptr) {
                return;
            }

            struct ZDebugParams
            {
                float MinDepth;
                float MaxDepth;
                float Invert;
                float Pad;
            };

            static const char ZDebugHLSL[] =
                "cbuffer ZDebugCB : register(b0) {\n"
                "    float MinDepth;\n"
                "    float MaxDepth;\n"
                "    float Invert;\n"
                "    float Pad;\n"
                "};\n"
                "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
                "VSOut VSMain(uint id : SV_VertexID) {\n"
                "    VSOut o;\n"
                "    float2 uv = float2((id << 1) & 2, id & 2);\n"
                "    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
                "    o.uv = uv;\n"
                "    return o;\n"
                "}\n"
                "Texture2D<float> DepthTex : register(t0);\n"
                "SamplerState Smp : register(s0);\n"
                "float4 PSMain(VSOut v) : SV_Target {\n"
                "    float depth = DepthTex.SampleLevel(Smp, v.uv, 0);\n"
                "    float denom = max(MaxDepth - MinDepth, 0.000001);\n"
                "    float value = saturate((depth - MinDepth) / denom);\n"
                "    if (Invert > 0.5) value = 1.0 - value;\n"
                "    return float4(value, value, value, 1.0);\n"
                "}\n";

            static Vinifera::Gfx::RenderTarget2D z_preview;
            static Vinifera::Gfx::Effect z_effect;
            static ID3D11Device* resource_device = nullptr;
            static int preview_w = 0;
            static int preview_h = 0;
            static bool range_initialized = false;
            static float min_depth = 0.95f;
            static float max_depth = 1.0f;
            static bool invert = true;

            Vinifera::Gfx::GraphicsDevice& device = *Vinifera::Gfx::Device;
            ID3D11Device* d3d_device = device.Get_Device();
            ID3D11DeviceContext* ctx = device.Get_Context();
            ID3D11ShaderResourceView* depth_srv = device.Get_Depth_SRV();
            const int bb_w = device.Get_Backbuffer_Width();
            const int bb_h = device.Get_Backbuffer_Height();

            if (!range_initialized && bb_h > 0) {
                min_depth = Visible_Screen_Depth_Min(bb_h);
                max_depth = 1.0f;
                range_initialized = true;
            }

            if (resource_device != nullptr && resource_device != d3d_device) {
                z_effect.Shutdown();
                z_preview.Shutdown();
                resource_device = nullptr;
                preview_w = preview_h = 0;
            }

            ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Vinifera - Z Buffer", &ShowZBuffer)) {
                if (depth_srv == nullptr || bb_w <= 0 || bb_h <= 0 || ctx == nullptr) {
                    ImGui::TextUnformatted("No depth buffer SRV.");
                } else {
                    static float scale = 1.0f;

                    ImGui::Checkbox("Invert", &invert);
                    ImGui::SameLine();
                    ImGui::SliderFloat("Scale", &scale, 0.1f, 4.0f, "%.2f");
                    if (ImGui::Button("Visible Band")) {
                        min_depth = Visible_Screen_Depth_Min(bb_h);
                        max_depth = 1.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Full Range")) {
                        min_depth = 0.0f;
                        max_depth = 1.0f;
                    }
                    ImGui::SliderFloat("Min", &min_depth, 0.0f, 1.0f, "%.6f");
                    ImGui::SliderFloat("Max", &max_depth, 0.0f, 1.0f, "%.6f");
                    if (min_depth > max_depth) {
                        std::swap(min_depth, max_depth);
                    }
                    ImGui::Text("Backbuffer: %d x %d", bb_w, bb_h);

                    if (resource_device == nullptr) {
                        resource_device = d3d_device;
                        if (!z_effect.Initialize(device, ZDebugHLSL, sizeof(ZDebugHLSL) - 1,
                                                 "z_buffer_debug", nullptr, 0, sizeof(ZDebugParams))) {
                            resource_device = nullptr;
                        }
                    }
                    if ((preview_w != bb_w || preview_h != bb_h) && resource_device != nullptr) {
                        z_preview.Shutdown();
                        if (z_preview.Initialize(device, bb_w, bb_h, DXGI_FORMAT_R8G8B8A8_UNORM)) {
                            preview_w = bb_w;
                            preview_h = bb_h;
                        } else {
                            preview_w = preview_h = 0;
                        }
                    }

                    if (resource_device != nullptr && z_preview.Get_SRV() != nullptr) {
                        device.Set_Render_Target(&z_preview);

                        ID3D11SamplerState* sampler = device.States().Get(Vinifera::Gfx::ESampler::PointClamp);
                        ctx->PSSetSamplers(0, 1, &sampler);
                        ctx->PSSetShaderResources(0, 1, &depth_srv);
                        ctx->OMSetBlendState(device.States().Get(Vinifera::Gfx::EBlend::Opaque), nullptr, 0xFFFFFFFFu);
                        ctx->OMSetDepthStencilState(device.States().Get(Vinifera::Gfx::EDepthStencil::None), 0);
                        ctx->RSSetState(device.States().Get(Vinifera::Gfx::ERasterizer::CullNone));
                        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                        ZDebugParams params = {};
                        params.MinDepth = min_depth;
                        params.MaxDepth = max_depth;
                        params.Invert = invert ? 1.0f : 0.0f;
                        z_effect.Set_Constants(device, &params);
                        z_effect.Apply(device);
                        ctx->Draw(3, 0);

                        ID3D11ShaderResourceView* null_srv = nullptr;
                        ctx->PSSetShaderResources(0, 1, &null_srv);
                    }

                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    if (avail.x < 1.0f) avail.x = 1.0f;
                    if (avail.y < 1.0f) avail.y = 1.0f;

                    const float aspect = (float)bb_w / (float)bb_h;
                    ImVec2 size(avail.x * scale, (avail.x / aspect) * scale);
                    if (size.y > avail.y * scale) {
                        size.y = avail.y * scale;
                        size.x = size.y * aspect;
                    }

                    const ImVec2 uv0(0.0f, 0.0f);
                    const ImVec2 uv1(1.0f, 1.0f);
                    const ImVec4 border(1.0f, 1.0f, 1.0f, 0.25f);
                    ImGui::Image((ImTextureID)z_preview.Get_SRV(), size, uv0, uv1,
                                 ImVec4(1.0f, 1.0f, 1.0f, 1.0f), border);
                }
            }
            ImGui::End();
        }


        static void Build_Alpha_Buffer_Window()
        {
            if (!ShowAlphaBuffer || Vinifera::Gfx::Device == nullptr) {
                return;
            }

            struct AlphaDebugParams
            {
                float Invert;
                float Pad0;
                float Pad1;
                float Pad2;
            };

            static const char AlphaDebugHLSL[] =
                "cbuffer AlphaDebugCB : register(b0) {\n"
                "    float Invert;\n"
                "    float Pad0;\n"
                "    float Pad1;\n"
                "    float Pad2;\n"
                "};\n"
                "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
                "VSOut VSMain(uint id : SV_VertexID) {\n"
                "    VSOut o;\n"
                "    float2 uv = float2((id << 1) & 2, id & 2);\n"
                "    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
                "    o.uv = uv;\n"
                "    return o;\n"
                "}\n"
                "Texture2D<float> AlphaTex : register(t0);\n"
                "SamplerState Smp : register(s0);\n"
                "float4 PSMain(VSOut v) : SV_Target {\n"
                "    float a = AlphaTex.SampleLevel(Smp, v.uv, 0);\n"
                "    if (Invert > 0.5) a = 1.0 - a;\n"
                "    return float4(a, a, a, 1.0);\n"
                "}\n";

            static Vinifera::Gfx::RenderTarget2D a_preview;
            static Vinifera::Gfx::Effect a_effect;
            static ID3D11Device* resource_device = nullptr;
            static int preview_w = 0;
            static int preview_h = 0;
            static bool invert = false;

            Vinifera::Gfx::GraphicsDevice& device = *Vinifera::Gfx::Device;
            ID3D11Device* d3d_device = device.Get_Device();
            ID3D11DeviceContext* ctx = device.Get_Context();
            ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
            const int bb_w = device.Get_Backbuffer_Width();
            const int bb_h = device.Get_Backbuffer_Height();

            if (resource_device != nullptr && resource_device != d3d_device) {
                a_effect.Shutdown();
                a_preview.Shutdown();
                resource_device = nullptr;
                preview_w = preview_h = 0;
            }

            ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Vinifera - Alpha Buffer", &ShowAlphaBuffer)) {
                if (alpha_srv == nullptr || bb_w <= 0 || bb_h <= 0 || ctx == nullptr) {
                    ImGui::TextUnformatted("No alpha buffer SRV.");
                } else {
                    static float scale = 1.0f;

                    ImGui::Checkbox("Invert", &invert);
                    ImGui::SameLine();
                    ImGui::SliderFloat("Scale", &scale, 0.1f, 4.0f, "%.2f");
                    ImGui::TextUnformatted("R8_UNORM. Cleared per-frame to 127/255;");
                    ImGui::TextUnformatted("modulated by alpha-light / shroud writes.");
                    ImGui::Text("Backbuffer: %d x %d", bb_w, bb_h);

                    if (resource_device == nullptr) {
                        resource_device = d3d_device;
                        if (!a_effect.Initialize(device, AlphaDebugHLSL, sizeof(AlphaDebugHLSL) - 1,
                                                 "alpha_buffer_debug", nullptr, 0, sizeof(AlphaDebugParams))) {
                            resource_device = nullptr;
                        }
                    }
                    if ((preview_w != bb_w || preview_h != bb_h) && resource_device != nullptr) {
                        a_preview.Shutdown();
                        if (a_preview.Initialize(device, bb_w, bb_h, DXGI_FORMAT_R8G8B8A8_UNORM)) {
                            preview_w = bb_w;
                            preview_h = bb_h;
                        } else {
                            preview_w = preview_h = 0;
                        }
                    }

                    if (resource_device != nullptr && a_preview.Get_SRV() != nullptr) {
                        device.Set_Render_Target(&a_preview);

                        ID3D11SamplerState* sampler = device.States().Get(Vinifera::Gfx::ESampler::PointClamp);
                        ctx->PSSetSamplers(0, 1, &sampler);
                        ctx->PSSetShaderResources(0, 1, &alpha_srv);
                        ctx->OMSetBlendState(device.States().Get(Vinifera::Gfx::EBlend::Opaque), nullptr, 0xFFFFFFFFu);
                        ctx->OMSetDepthStencilState(device.States().Get(Vinifera::Gfx::EDepthStencil::None), 0);
                        ctx->RSSetState(device.States().Get(Vinifera::Gfx::ERasterizer::CullNone));
                        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                        AlphaDebugParams params = {};
                        params.Invert = invert ? 1.0f : 0.0f;
                        a_effect.Set_Constants(device, &params);
                        a_effect.Apply(device);
                        ctx->Draw(3, 0);

                        ID3D11ShaderResourceView* null_srv = nullptr;
                        ctx->PSSetShaderResources(0, 1, &null_srv);
                    }

                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    if (avail.x < 1.0f) avail.x = 1.0f;
                    if (avail.y < 1.0f) avail.y = 1.0f;

                    const float aspect = (float)bb_w / (float)bb_h;
                    ImVec2 size(avail.x * scale, (avail.x / aspect) * scale);
                    if (size.y > avail.y * scale) {
                        size.y = avail.y * scale;
                        size.x = size.y * aspect;
                    }

                    const ImVec2 uv0(0.0f, 0.0f);
                    const ImVec2 uv1(1.0f, 1.0f);
                    const ImVec4 border(1.0f, 1.0f, 1.0f, 0.25f);
                    ImGui::Image((ImTextureID)a_preview.Get_SRV(), size, uv0, uv1,
                                 ImVec4(1.0f, 1.0f, 1.0f, 1.0f), border);
                }
            }
            ImGui::End();
        }
    }


    void Draw_Debug_UI()
    {
        Build_Debug_Toolbar();
        Build_Perf_Window();
        Build_Z_Buffer_Window();
        Build_Alpha_Buffer_Window();
    }
}
