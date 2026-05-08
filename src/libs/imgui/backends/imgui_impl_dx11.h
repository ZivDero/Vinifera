// dear imgui: Renderer Backend for DirectX 11
// This needs to be used along with a Platform Backend (e.g. Win32).

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).

// This backend is a Vinifera-local reproduction of the upstream Dear ImGui
// DirectX 11 backend, kept compatible with the project's vendored imgui 1.92.x
// and the new ImTextureData lifecycle.

#pragma once
#include "imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE

struct ID3D11Device;
struct ID3D11DeviceContext;

IMGUI_IMPL_API bool     ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context);
IMGUI_IMPL_API void     ImGui_ImplDX11_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplDX11_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data);

// Use if you want to reset your rendering device without losing Dear ImGui state.
IMGUI_IMPL_API void     ImGui_ImplDX11_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplDX11_InvalidateDeviceObjects();

// (Advanced) Apply a single texture's pending status change. Normally handled
// automatically inside RenderDrawData(); exposed for staged rendering setups
// that want to drive texture updates at a specific point.
IMGUI_IMPL_API void     ImGui_ImplDX11_UpdateTexture(ImTextureData* tex);

#endif // #ifndef IMGUI_DISABLE
