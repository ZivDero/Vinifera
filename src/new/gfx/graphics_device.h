/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  D3D11 device + DXGI swap chain + present-quad pipeline.
 *
 *          GraphicsDevice owns the immediate context, the swap chain, the
 *          back-buffer RTV, the cached state-preset objects, and the present
 *          quad used to upload the game's CPU surface to the back buffer each
 *          frame.
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
#include "states.h"


namespace Vinifera::Gfx
{
    class Texture2D;
    class RenderTarget2D;

    class GraphicsDevice
    {
    public:
        GraphicsDevice() = default;
        ~GraphicsDevice();

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;

        bool Initialize(HWND hwnd, int backbuffer_width, int backbuffer_height, bool vsync);
        void Shutdown();

        bool Resize_Backbuffer(int width, int height);
        void Set_VSync(bool enable) { VSync = enable; }
        bool Get_VSync() const { return VSync; }

        bool Set_Surface_Format(int width, int height);
        bool Upload_Surface(const void* pixels, int pitch_bytes);

        void Begin_Frame();
        void Draw_Surface(const Rect& dst_rect, SDL_ScaleMode scale_mode);
        void End_Frame();

        /**
         *  Bind the back buffer (or a custom RT) as the current render target
         *  with a viewport covering the full target. nullptr -> back buffer.
         */
        void Set_Render_Target(RenderTarget2D* target);
        void Bind_Backbuffer() { Set_Render_Target(nullptr); }

        ID3D11Device*           Get_Device() const { return Device; }
        ID3D11DeviceContext*    Get_Context() const { return Context; }
        IDXGISwapChain1*        Get_Swap_Chain() const { return SwapChain; }
        ID3D11RenderTargetView* Get_Backbuffer_RTV() const { return BackbufferRTV; }
        ID3D11DepthStencilView* Get_Depth_DSV() const { return DepthDSV; }
        ID3D11ShaderResourceView* Get_Depth_SRV() const { return DepthSRV; }
        ID3D11RenderTargetView*    Get_Alpha_RTV() const { return AlphaRTV; }
        ID3D11ShaderResourceView*  Get_Alpha_SRV() const { return AlphaSRV; }
        ID3D11UnorderedAccessView* Get_Alpha_UAV() const { return AlphaUAV; }
        int                     Get_Backbuffer_Width() const { return BackbufferWidth; }
        int                     Get_Backbuffer_Height() const { return BackbufferHeight; }

        StateCache&             States() { return StateCacheInstance; }

        /**
         *  Bind the backbuffer without a DSV. Useful for debug/UI passes that
         *  sample the backbuffer depth texture as an SRV.
         */
        void Bind_Backbuffer_Color_Only();

    private:
        bool Create_Device();
        bool Create_Swap_Chain(HWND hwnd, int width, int height);
        bool Create_Backbuffer_RTV();
        void Release_Backbuffer_RTV();

        bool Create_Depth_Buffer(int width, int height);
        void Release_Depth_Buffer();

        bool Create_Alpha_Buffer(int width, int height);
        void Release_Alpha_Buffer();

        bool Create_Present_Pipeline();
        void Release_Present_Pipeline();

        void Release_Surface_Texture();

        HWND                     WindowHandle = nullptr;

        ID3D11Device*            Device = nullptr;
        ID3D11DeviceContext*     Context = nullptr;
        IDXGIFactory2*           DxgiFactory = nullptr;
        IDXGISwapChain1*         SwapChain = nullptr;
        ID3D11RenderTargetView*  BackbufferRTV = nullptr;
        ID3D11Texture2D*         DepthTex = nullptr;
        ID3D11DepthStencilView*  DepthDSV = nullptr;
        ID3D11ShaderResourceView*DepthSRV = nullptr;

        /**
         *  Alpha buffer mirrors vanilla's `AlphaBuffer` (a 16-bit-per-pixel
         *  surface seeded to mid-gray each frame and modulated by alpha
         *  lights / shroud). We use R8_UNORM since the upper byte of vanilla's
         *  buffer was effectively unused intensity scaling. Bound as RTV +
         *  SRV for the read/clear paths and as UAV for the alpha-light write
         *  pass that implements `BrightnessTable[shape][old]` per pixel.
         */
        ID3D11Texture2D*           AlphaTex = nullptr;
        ID3D11RenderTargetView*    AlphaRTV = nullptr;
        ID3D11ShaderResourceView*  AlphaSRV = nullptr;
        ID3D11UnorderedAccessView* AlphaUAV = nullptr;

        int                      BackbufferWidth = 0;
        int                      BackbufferHeight = 0;
        bool                     VSync = false;
        bool                     TearingSupported = false;

        ID3D11VertexShader*      PresentVS = nullptr;
        ID3D11PixelShader*       PresentPS = nullptr;

        ID3D11Texture2D*         SurfaceTex = nullptr;
        ID3D11ShaderResourceView*SurfaceSRV = nullptr;
        int                      SurfaceWidth = 0;
        int                      SurfaceHeight = 0;

        StateCache               StateCacheInstance;
    };


    /**
     *  Global GraphicsDevice instance, lifetime managed by sdl_functions.cpp.
     *  Null until SDL_Set_Video_Mode initializes it.
     */
    extern GraphicsDevice* Device;
}
