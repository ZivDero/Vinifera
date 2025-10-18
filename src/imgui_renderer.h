#pragma once

struct ImDrawData;
class Surface;

void ImGuiDSurface_CreateFontsTexture();
void ImGuiDSurface_DestroyFontsTexture();
void ImGuiDSurface_Render(ImDrawData* draw_data, Surface* surface);