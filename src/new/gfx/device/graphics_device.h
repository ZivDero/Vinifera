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

    enum class DepthBinding
    {
        None,
        SharedDepth,
    };

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

        /**
         *  Size the scene-side render targets (SceneRT, depth buffer, alpha
         *  buffer) to (width, height). This is vanilla's logical render
         *  resolution — `VideoWidth × VideoHeight` — *not* the backbuffer.
         *  Decoupling the two means a 4K display draws sprites at logical
         *  res and the present-time `SceneRT → Backbuffer` blit handles the
         *  upscale, saving most of the pixel-shader fillrate. Idempotent.
         */
        bool Set_Logical_Resolution(int width, int height);
        int  Get_Logical_Width() const  { return LogicalWidth; }
        int  Get_Logical_Height() const { return LogicalHeight; }

        bool Set_Surface_Format(int width, int height);
        bool Upload_Surface(const void* pixels, int pitch_bytes, int width, int height);
        bool Ensure_Sidebar_Target_Size(int width, int height);

        /**
         *  CPU→GPU bridge for the radar minimap. `Upload_Radar_Surface`
         *  maps `RadarSurface`'s 565 pixels into `RadarTex`;
         *  `Draw_Radar_To_Sidebar` composites that texture onto whichever
         *  RT is currently bound (the sidebar render loop binds `SidebarRT`
         *  before calling it).
         */
        bool Upload_Radar_Surface(const void* pixels, int pitch_bytes, int width, int height);
        void Draw_Radar_To_Sidebar(const Rect& dst_rect);

        /**
         *  CPU→GPU bridge for sidebar (ingame) movies. Same pattern as the
         *  radar pair. The render loop binds `SidebarRT` and passes the
         *  VQHandle's `StretchRect` (movie native size at the radar
         *  drawing origin — matches what vanilla `Movie_Queue_Ingame`
         *  would have placed) as the destination.
         */
        bool Upload_Sidebar_Movie_Surface(const void* pixels, int pitch_bytes, int width, int height);
        void Draw_Sidebar_Movie(const Rect& dst_rect);

        /**
         *  OwnerDraw overlay (window-res dialog content). Uploaded into
         *  a dedicated texture and drawn 1:1 onto the backbuffer after
         *  the upscaled VisibleSurface so dialogs stay sharp.
         */
        bool Upload_OwnerDraw_Surface(const void* pixels, int pitch_bytes, int width, int height);
        void Draw_OwnerDraw_Overlay(const Rect& dst_rect);

        void Begin_Frame();
        void Draw_Texture(ID3D11ShaderResourceView* srv, const Rect& dst_rect, SDL_ScaleMode scale_mode, EBlend blend = EBlend::Opaque);
        void Draw_Surface(const Rect& dst_rect, SDL_ScaleMode scale_mode);
        void End_Frame();

        /**
         *  Bind the back buffer (or a custom RT) as the current render target
         *  with a viewport covering the full target. nullptr -> back buffer.
         */
        void Set_Render_Target(RenderTarget2D* target, DepthBinding depth = DepthBinding::None);
        void Bind_Backbuffer() { Set_Render_Target(nullptr, DepthBinding::SharedDepth); }
        void Bind_Scene_Target();
        void Bind_Sidebar_Target();
        void Clear_Sidebar_Target();

        ID3D11Device*           Get_Device() const { return Device; }
        ID3D11DeviceContext*    Get_Context() const { return Context; }
        IDXGISwapChain1*        Get_Swap_Chain() const { return SwapChain; }
        ID3D11RenderTargetView* Get_Backbuffer_RTV() const { return BackbufferRTV; }
        ID3D11DepthStencilView* Get_Depth_DSV() const { return DepthDSV; }
        ID3D11ShaderResourceView* Get_Depth_SRV() const { return DepthSRV; }
        ID3D11RenderTargetView*    Get_Alpha_RTV() const { return AlphaRTV; }
        ID3D11ShaderResourceView*  Get_Alpha_SRV() const { return AlphaSRV; }
        ID3D11UnorderedAccessView* Get_Alpha_UAV() const { return AlphaUAV; }
        ID3D11ShaderResourceView*  Get_Scene_SRV() const;
        ID3D11ShaderResourceView*  Get_Sidebar_Target_SRV() const;
        int                     Get_Backbuffer_Width() const { return BackbufferWidth; }
        int                     Get_Backbuffer_Height() const { return BackbufferHeight; }
        int                     Get_Sidebar_Target_Width() const;
        int                     Get_Sidebar_Target_Height() const;
        int                     Get_Scene_Target_Width() const;
        int                     Get_Scene_Target_Height() const;

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

        bool Create_Scene_Target(int width, int height);
        void Release_Scene_Target();

        bool Create_Sidebar_Target(int width, int height);
        void Release_Sidebar_Target();

        bool Create_Alpha_Buffer(int width, int height);
        void Release_Alpha_Buffer();

        bool Create_Present_Pipeline();
        void Release_Present_Pipeline();

        void Release_Surface_Texture();
        void Release_Radar_Texture();
        void Release_Movie_Texture();
        void Release_OwnerDraw_Texture();

        HWND                     WindowHandle = nullptr;

        ID3D11Device*            Device = nullptr;
        ID3D11DeviceContext*     Context = nullptr;
        IDXGIFactory2*           DxgiFactory = nullptr;
        IDXGISwapChain1*         SwapChain = nullptr;
        ID3D11RenderTargetView*  BackbufferRTV = nullptr;
        ID3D11Texture2D*         DepthTex = nullptr;
        ID3D11DepthStencilView*  DepthDSV = nullptr;
        ID3D11ShaderResourceView*DepthSRV = nullptr;
        RenderTarget2D*          SceneTarget = nullptr;
        RenderTarget2D*          SidebarTarget = nullptr;

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
        int                      LogicalWidth = 0;
        int                      LogicalHeight = 0;
        bool                     VSync = false;
        bool                     TearingSupported = false;

        ID3D11VertexShader*      PresentVS = nullptr;
        ID3D11PixelShader*       PresentPS = nullptr;

        ID3D11Texture2D*         SurfaceTex = nullptr;
        ID3D11ShaderResourceView*SurfaceSRV = nullptr;
        int                      SurfaceWidth = 0;
        int                      SurfaceHeight = 0;

        ID3D11Texture2D*         RadarTex = nullptr;
        ID3D11ShaderResourceView*RadarSRV = nullptr;
        int                      RadarTexWidth = 0;
        int                      RadarTexHeight = 0;

        ID3D11Texture2D*         MovieTex = nullptr;
        ID3D11ShaderResourceView*MovieSRV = nullptr;
        int                      MovieTexWidth = 0;
        int                      MovieTexHeight = 0;

        ID3D11Texture2D*         OwnerDrawTex = nullptr;
        ID3D11ShaderResourceView*OwnerDrawSRV = nullptr;
        int                      OwnerDrawTexWidth = 0;
        int                      OwnerDrawTexHeight = 0;

        StateCache               StateCacheInstance;
    };


    /**
     *  Global GraphicsDevice instance, lifetime managed by sdl_functions.cpp.
     *  Null until SDL_Set_Video_Mode initializes it.
     */
    extern GraphicsDevice* Device;
}
