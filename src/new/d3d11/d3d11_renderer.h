/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Native Direct3D 11 renderer that owns the swap chain, the game
 *          surface present quad, and exposes the device/context to ImGui and
 *          RmlUi backends.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <SDL3/SDL_surface.h>

#include "rect.h"


class Surface;


class D3D11Renderer
{
public:
    D3D11Renderer() = default;
    ~D3D11Renderer();

    D3D11Renderer(const D3D11Renderer&) = delete;
    D3D11Renderer& operator=(const D3D11Renderer&) = delete;

    bool Initialize(HWND hwnd, int backbuffer_width, int backbuffer_height, bool vsync);
    void Shutdown();

    bool Resize_Backbuffer(int width, int height);
    void Set_VSync(bool enable) { VSync = enable; }
    bool Get_VSync() const { return VSync; }

    /**
     *  Creates (or recreates) the dynamic streaming texture used to upload the
     *  game's RGB565 surface each frame.
     */
    bool Set_Surface_Format(int width, int height);

    /**
     *  Maps the streaming texture and copies in `pixels` (assumed to be
     *  RGB565 with `pitch_bytes` row stride).
     */
    bool Upload_Surface(const void* pixels, int pitch_bytes);

    /**
     *  Begins the frame: binds the back buffer as the render target, clears
     *  it, and sets a default viewport covering the entire back buffer.
     */
    void Begin_Frame();

    /**
     *  Draws the previously uploaded surface to `dst_rect` (in back-buffer
     *  pixel space) using either linear or point sampling.
     */
    void Draw_Surface(const Rect& dst_rect, SDL_ScaleMode scale_mode);

    /**
     *  Presents the back buffer to the window.
     */
    void End_Frame();

    /**
     *  Restores the back buffer as the current render target with a viewport
     *  that covers the whole back buffer. Called after RmlUi/ImGui finish if
     *  they rebound any state.
     */
    void Bind_Backbuffer();

    ID3D11Device*               Get_Device() const { return Device; }
    ID3D11DeviceContext*        Get_Context() const { return Context; }
    IDXGISwapChain1*            Get_Swap_Chain() const { return SwapChain; }
    ID3D11RenderTargetView*     Get_Backbuffer_RTV() const { return BackbufferRTV; }
    int                         Get_Backbuffer_Width() const { return BackbufferWidth; }
    int                         Get_Backbuffer_Height() const { return BackbufferHeight; }

private:
    bool Create_Device();
    bool Create_Swap_Chain(HWND hwnd, int width, int height);
    bool Create_Backbuffer_RTV();
    void Release_Backbuffer_RTV();

    bool Create_Surface_Pipeline();
    void Release_Surface_Pipeline();

    void Release_Surface_Texture();

private:
    HWND                        WindowHandle = nullptr;

    ID3D11Device*               Device = nullptr;
    ID3D11DeviceContext*        Context = nullptr;
    IDXGIFactory2*              DxgiFactory = nullptr;
    IDXGISwapChain1*            SwapChain = nullptr;
    ID3D11RenderTargetView*     BackbufferRTV = nullptr;

    int                         BackbufferWidth = 0;
    int                         BackbufferHeight = 0;
    bool                        VSync = false;
    bool                        TearingSupported = false;

    /**
     *  Present-quad pipeline objects. The VS produces a fullscreen triangle
     *  from SV_VertexID; the PS samples the surface texture. The viewport is
     *  set to the destination rectangle each draw, so no vertex/constant
     *  buffer is needed.
     */
    ID3D11VertexShader*         PresentVS = nullptr;
    ID3D11PixelShader*          PresentPS = nullptr;
    ID3D11SamplerState*         SamplerLinear = nullptr;
    ID3D11SamplerState*         SamplerPoint = nullptr;
    ID3D11BlendState*           BlendOpaque = nullptr;
    ID3D11RasterizerState*      RasterNoCull = nullptr;
    ID3D11DepthStencilState*    DepthDisabled = nullptr;

    /**
     *  Game-surface streaming texture (RGB565).
     */
    ID3D11Texture2D*            SurfaceTex = nullptr;
    ID3D11ShaderResourceView*   SurfaceSRV = nullptr;
    int                         SurfaceWidth = 0;
    int                         SurfaceHeight = 0;
};


/**
 *  Global pointer to the renderer. Lifetime is managed by sdl_functions.cpp.
 */
extern D3D11Renderer* D3DRenderer;
