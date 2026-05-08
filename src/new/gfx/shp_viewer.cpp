/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 2a sandbox SHP viewer.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shp_viewer.h"

#include "ccfile.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "vinifera_globals.h"

#include <imgui.h>


namespace Vinifera::Gfx
{
    ShpViewer* g_ShpViewer = nullptr;


    bool ShpViewer::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        DevicePtr = &device;
        if (!Palette.Initialize(device)) {
            DEBUG_ERROR("ShpViewer: palette init failed.\n");
            return false;
        }
        if (!Batch.Initialize(device, /*max_quads*/ 64)) {
            Palette.Shutdown();
            return false;
        }
        if (!PalEffect.Initialize(device)) {
            Batch.Shutdown();
            Palette.Shutdown();
            return false;
        }
        Initialized = true;
        ShpDirty = true;
        PaletteDirty = true;
        return true;
    }


    void ShpViewer::Shutdown()
    {
        PalEffect.Shutdown();
        Batch.Shutdown();
        Palette.Shutdown();
        Shp.Unload();
        Initialized = false;
        DevicePtr = nullptr;
    }


    bool ShpViewer::Reload_Shp(GraphicsDevice& device)
    {
        Shp.Unload();
        if (!Shp.Load(device, ShpPathBuf)) {
            StatusLine = std::string("Failed to load ") + ShpPathBuf;
            return false;
        }
        StatusLine = std::string("Loaded ") + ShpPathBuf
                   + " (" + std::to_string(Shp.Frame_Count()) + " frames)";
        if (FrameIndex >= Shp.Frame_Count()) FrameIndex = 0;
        return true;
    }


    bool ShpViewer::Reload_Palette(GraphicsDevice& device)
    {
        (void)device;
        CCFileClass file(PalettePathBuf);
        if (!file.Is_Available() || !file.Open(FILE_ACCESS_READ)) {
            StatusLine = std::string("Palette not found: ") + PalettePathBuf;
            PaletteValid = false;
            return false;
        }
        const long got = file.Read(PaletteRaw, sizeof(PaletteRaw));
        file.Close();
        if (got != sizeof(PaletteRaw)) {
            StatusLine = std::string("Palette truncated: ") + PalettePathBuf;
            PaletteValid = false;
            return false;
        }
        PaletteValid = true;
        Palette.Update_Palette(PaletteRaw, /*six_bit*/ true, TintR, TintG, TintB);
        StatusLine = std::string("Palette loaded: ") + PalettePathBuf;
        return true;
    }


    void ShpViewer::Apply_Tint()
    {
        if (!PaletteValid) return;
        Palette.Update_Palette(PaletteRaw, /*six_bit*/ true, TintR, TintG, TintB);
    }


    void ShpViewer::Build_UI()
    {
        if (!Initialized || !Vinifera_ShpViewer) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Vinifera - SHP Viewer", &Vinifera_ShpViewer)) {

            ImGui::TextUnformatted("Asset:");
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##shp_path", ShpPathBuf, sizeof(ShpPathBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                ShpDirty = true;
            }
            ImGui::PopItemWidth();
            if (ImGui::Button("Reload SHP")) ShpDirty = true;

            ImGui::Separator();
            ImGui::TextUnformatted("Palette:");
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##pal_path", PalettePathBuf, sizeof(PalettePathBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                PaletteDirty = true;
            }
            ImGui::PopItemWidth();
            if (ImGui::Button("Reload Palette")) PaletteDirty = true;

            ImGui::Separator();
            const int max_frame = Shp.Is_Loaded() ? Shp.Frame_Count() - 1 : 0;
            ImGui::SliderInt("Frame", &FrameIndex, 0, max_frame > 0 ? max_frame : 0);
            ImGui::SliderInt("X", &DrawX, 0, 4000);
            ImGui::SliderInt("Y", &DrawY, 0, 4000);
            ImGui::SliderInt("Scale", &Scale, 1, 16);

            ImGui::Separator();
            ImGui::TextUnformatted("Tint (1000 = 100%)");
            if (ImGui::SliderInt("R", &TintR, 0, 2000)) TintDirty = true;
            if (ImGui::SliderInt("G", &TintG, 0, 2000)) TintDirty = true;
            if (ImGui::SliderInt("B", &TintB, 0, 2000)) TintDirty = true;

            ImGui::Separator();
            ImGui::TextUnformatted("Flags:");
            ImGui::Checkbox("Use House Remap", &UseRemap);
            ImGui::Checkbox("Darken",          &Darken);
            ImGui::Checkbox("Translucent 25%", &Translucent25);
            ImGui::SameLine();
            ImGui::Checkbox("50%",             &Translucent50);
            ImGui::SameLine();
            ImGui::Checkbox("75%",             &Translucent75);

            ImGui::Separator();
            if (!StatusLine.empty()) {
                ImGui::TextWrapped("%s", StatusLine.c_str());
            }
            if (Shp.Is_Loaded()) {
                ImGui::Text("Logical %dx%d, Atlas %dx%d",
                    Shp.Logical_Width(), Shp.Logical_Height(),
                    Shp.Get_Atlas().Width(), Shp.Get_Atlas().Height());
                if (auto* fi = Shp.Get_Frame(FrameIndex)) {
                    ImGui::Text("Frame %d: pos=(%d,%d) size=(%d,%d) %s%s",
                        FrameIndex, fi->X, fi->Y, fi->W, fi->H,
                        fi->RLE ? "[RLE]" : "[raw]",
                        fi->Transparent ? " [trans]" : "");
                }
            }
        }
        ImGui::End();
    }


    void ShpViewer::Render(GraphicsDevice& device)
    {
        if (!Initialized || !Vinifera_ShpViewer) {
            return;
        }

        if (ShpDirty) {
            Reload_Shp(device);
            ShpDirty = false;
        }
        if (PaletteDirty) {
            Reload_Palette(device);
            PaletteDirty = false;
            TintDirty = false;
        }
        if (TintDirty) {
            Apply_Tint();
            TintDirty = false;
        }

        if (!Shp.Is_Loaded()) {
            return;
        }
        const ShpFrameInfo* fi = Shp.Get_Frame(FrameIndex);
        if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
            return;
        }

        /**
         *  Build per-draw effect parameters.
         */
        SpriteEffectParams params = {};
        params.AtlasSize[0] = (float)Shp.Get_Atlas().Width();
        params.AtlasSize[1] = (float)Shp.Get_Atlas().Height();
        params.Flags = 0;
        if (UseRemap)       params.Flags |= SEF_USE_REMAP;
        if (Darken)         params.Flags |= SEF_DARKEN;
        if (Translucent25)  params.Flags |= SEF_TRANSLUCENT25;
        if (Translucent50)  params.Flags |= SEF_TRANSLUCENT50;
        if (Translucent75)  params.Flags |= SEF_TRANSLUCENT75;

        /**
         *  Bind the back buffer (Draw_Surface and ImGui both touch the
         *  viewport / RTV state earlier in the frame) before drawing.
         */
        device.Bind_Backbuffer();

        Batch.Begin(device, EBlend::Premultiplied, ESampler::PointClamp, &PalEffect,
                    device.Get_Backbuffer_Width(), device.Get_Backbuffer_Height());

        RectF dst = {
            (float)(DrawX + fi->X * Scale),
            (float)(DrawY + fi->Y * Scale),
            (float)(fi->W * Scale),
            (float)(fi->H * Scale),
        };
        RectF src = { (float)fi->AtlasX, (float)fi->AtlasY, (float)fi->W, (float)fi->H };
        Batch.Draw(&Shp.Get_Atlas(), dst, &src, 0xFFFFFFFFu);

        /**
         *  Bind palette + remap and per-effect params before flushing.
         */
        PalEffect.Bind_Palette(device, Palette);
        PalEffect.Set_Params(device, params);

        Batch.End(device);

        /**
         *  Unbind to keep subsequent passes clean.
         */
        ID3D11ShaderResourceView* null_srvs[3] = {};
        device.Get_Context()->PSSetShaderResources(0, 3, null_srvs);
    }
}
