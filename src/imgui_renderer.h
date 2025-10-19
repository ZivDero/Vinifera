#pragma once
#include "imgui.h"
#include <d3d.h>
#include <ddraw.h>

struct ImGui_ImplD3D2_Data {
    LPDIRECT3D2 d3d = nullptr;
    LPDIRECT3DDEVICE2 dev = nullptr;
    LPDIRECTDRAW2 dd = nullptr;
    LPDIRECTDRAWSURFACE rtSurface = nullptr;
    LPDIRECT3DVIEWPORT2 viewport = nullptr;
    LPDIRECTDRAWSURFACE fontSurf = nullptr;
    LPDIRECT3DTEXTURE2 fontTex = nullptr;
    int fb_w = 0, fb_h = 0;
};

bool ImGui_ImplD3D2_Init();
void ImGui_ImplD3D2_Shutdown();
void ImGui_ImplD3D2_NewFrame();
bool ImGui_ImplD3D2_CreateFontsTexture();
void ImGui_ImplD3D2_DestroyFontsTexture();
void ImGui_ImplD3D2_RenderDrawData(ImDrawData* draw_data);
