/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  D3D11 device + DXGI swap chain + present-quad pipeline.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "graphics_device.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "render_target_2d.h"

#include <dxgi1_5.h>


namespace Vinifera::Gfx
{
    GraphicsDevice* Device = nullptr;


    namespace
    {
        const char PresentShaderHLSL[] =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSOut VSMain(uint id : SV_VertexID) {\n"
            "    VSOut o;\n"
            "    float2 uv = float2((id << 1) & 2, id & 2);\n"
            "    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
            "    o.uv = uv;\n"
            "    return o;\n"
            "}\n"
            "Texture2D Tex : register(t0);\n"
            "SamplerState Smp : register(s0);\n"
            "float4 PSMain(VSOut v) : SV_Target { return Tex.Sample(Smp, v.uv); }\n";
    }


    GraphicsDevice::~GraphicsDevice()
    {
        Shutdown();
    }


    bool GraphicsDevice::Initialize(HWND hwnd, int backbuffer_width, int backbuffer_height, bool vsync)
    {
        if (Device != nullptr) {
            return true;
        }

        WindowHandle = hwnd;
        VSync = vsync;
        BackbufferWidth = backbuffer_width;
        BackbufferHeight = backbuffer_height;

        if (!Create_Device()) {
            Shutdown();
            return false;
        }
        if (!Create_Swap_Chain(hwnd, backbuffer_width, backbuffer_height)) {
            Shutdown();
            return false;
        }
        if (!Create_Backbuffer_RTV()) {
            Shutdown();
            return false;
        }

        /**
         *  SceneTarget / DepthBuffer / AlphaBuffer are sized to vanilla's
         *  logical render resolution, not the backbuffer. They're created on
         *  the first `Set_Logical_Resolution` call (driven from
         *  `SDL_Set_Video_Mode` once vanilla knows its video mode dims).
         *  Bind_Scene_Target falls back to Bind_Backbuffer when SceneTarget
         *  is null, so pre-video-mode frames still present cleanly.
         */
        StateCacheInstance.Initialize(Device);

        if (!Create_Present_Pipeline()) {
            Shutdown();
            return false;
        }

        DEBUG_INFO("Gfx::GraphicsDevice initialized (%dx%d, vsync=%d).\n",
            backbuffer_width, backbuffer_height, VSync ? 1 : 0);
        return true;
    }


    void GraphicsDevice::Shutdown()
    {
        Release_Sidebar_Surface_Texture();
        Release_Surface_Texture();
        Release_Present_Pipeline();
        StateCacheInstance.Shutdown();
        Release_Alpha_Buffer();
        Release_Sidebar_Target();
        Release_Scene_Target();
        Release_Depth_Buffer();
        Release_Backbuffer_RTV();
        Safe_Release(SwapChain);
        Safe_Release(DxgiFactory);
        if (Context != nullptr) {
            Context->ClearState();
            Context->Flush();
        }
        Safe_Release(Context);
        Safe_Release(Device);
        BackbufferWidth = 0;
        BackbufferHeight = 0;
        LogicalWidth = 0;
        LogicalHeight = 0;
        SurfaceWidth = 0;
        SurfaceHeight = 0;
        SidebarSurfaceWidth = 0;
        SidebarSurfaceHeight = 0;
        WindowHandle = nullptr;
    }


    bool GraphicsDevice::Create_Device()
    {
        UINT flags = 0;
#ifndef NDEBUG
        flags |= D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#endif

        const D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        };

        D3D_FEATURE_LEVEL obtained_level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            feature_levels, _countof(feature_levels), D3D11_SDK_VERSION,
            &Device, &obtained_level, &Context);

        if (FAILED(hr)) {
            DEBUG_ERROR("D3D11CreateDevice failed (HRESULT 0x%08X).\n", hr);
            return false;
        }

        DEBUG_INFO("D3D11 feature level: 0x%X\n", obtained_level);

        IDXGIDevice* dxgi_device = nullptr;
        if (FAILED(Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device)))) {
            return false;
        }
        IDXGIAdapter* adapter = nullptr;
        hr = dxgi_device->GetAdapter(&adapter);
        dxgi_device->Release();
        if (FAILED(hr)) {
            return false;
        }
        hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&DxgiFactory));
        adapter->Release();
        if (FAILED(hr) || DxgiFactory == nullptr) {
            return false;
        }

        IDXGIFactory5* factory5 = nullptr;
        if (SUCCEEDED(DxgiFactory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
            BOOL allow_tearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing)))) {
                TearingSupported = (allow_tearing == TRUE);
            }
            factory5->Release();
        }

        return true;
    }


    bool GraphicsDevice::Create_Swap_Chain(HWND hwnd, int width, int height)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        if (FAILED(DxgiFactory->CreateSwapChainForHwnd(Device, hwnd, &desc, nullptr, nullptr, &SwapChain))) {
            DEBUG_ERROR("CreateSwapChainForHwnd failed.\n");
            return false;
        }
        DxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
        return true;
    }


    bool GraphicsDevice::Create_Backbuffer_RTV()
    {
        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer)))) {
            return false;
        }
        HRESULT hr = Device->CreateRenderTargetView(back_buffer, nullptr, &BackbufferRTV);
        back_buffer->Release();
        return SUCCEEDED(hr);
    }


    void GraphicsDevice::Release_Backbuffer_RTV()
    {
        Safe_Release(BackbufferRTV);
    }


    bool GraphicsDevice::Create_Depth_Buffer(int width, int height)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R24G8_TYPELESS;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(Device->CreateTexture2D(&td, nullptr, &DepthTex))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: depth texture creation failed.\n");
            return false;
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Texture2D.MipSlice = 0;
        if (FAILED(Device->CreateDepthStencilView(DepthTex, &dsv_desc, &DepthDSV))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: DSV creation failed.\n");
            Release_Depth_Buffer();
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MostDetailedMip = 0;
        srv_desc.Texture2D.MipLevels = 1;
        if (FAILED(Device->CreateShaderResourceView(DepthTex, &srv_desc, &DepthSRV))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: depth SRV creation failed.\n");
            Release_Depth_Buffer();
            return false;
        }
        return true;
    }


    void GraphicsDevice::Release_Depth_Buffer()
    {
        Safe_Release(DepthSRV);
        Safe_Release(DepthDSV);
        Safe_Release(DepthTex);
    }


    bool GraphicsDevice::Create_Scene_Target(int width, int height)
    {
        Release_Scene_Target();

        SceneTarget = new RenderTarget2D();
        if (SceneTarget == nullptr) {
            return false;
        }
        if (!SceneTarget->Initialize(*this, width, height, DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("Gfx::GraphicsDevice: scene render target creation failed.\n");
            Release_Scene_Target();
            return false;
        }
        return true;
    }


    void GraphicsDevice::Release_Scene_Target()
    {
        if (SceneTarget != nullptr) {
            delete SceneTarget;
            SceneTarget = nullptr;
        }
    }


    bool GraphicsDevice::Create_Sidebar_Target(int width, int height)
    {
        Release_Sidebar_Target();

        SidebarTarget = new RenderTarget2D();
        if (SidebarTarget == nullptr) {
            return false;
        }
        if (!SidebarTarget->Initialize(*this, width, height, DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("Gfx::GraphicsDevice: sidebar render target creation failed.\n");
            Release_Sidebar_Target();
            return false;
        }
        return true;
    }


    void GraphicsDevice::Release_Sidebar_Target()
    {
        if (SidebarTarget != nullptr) {
            delete SidebarTarget;
            SidebarTarget = nullptr;
        }
    }


    bool GraphicsDevice::Create_Alpha_Buffer(int width, int height)
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET
                     | D3D11_BIND_SHADER_RESOURCE
                     | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(Device->CreateTexture2D(&td, nullptr, &AlphaTex))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: alpha texture creation failed.\n");
            return false;
        }

        if (FAILED(Device->CreateRenderTargetView(AlphaTex, nullptr, &AlphaRTV))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: alpha RTV creation failed.\n");
            Release_Alpha_Buffer();
            return false;
        }

        if (FAILED(Device->CreateShaderResourceView(AlphaTex, nullptr, &AlphaSRV))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: alpha SRV creation failed.\n");
            Release_Alpha_Buffer();
            return false;
        }

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_R8_UNORM;
        uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Texture2D.MipSlice = 0;
        if (FAILED(Device->CreateUnorderedAccessView(AlphaTex, &uav_desc, &AlphaUAV))) {
            DEBUG_ERROR("Gfx::GraphicsDevice: alpha UAV creation failed.\n");
            Release_Alpha_Buffer();
            return false;
        }
        return true;
    }


    void GraphicsDevice::Release_Alpha_Buffer()
    {
        Safe_Release(AlphaUAV);
        Safe_Release(AlphaSRV);
        Safe_Release(AlphaRTV);
        Safe_Release(AlphaTex);
    }


    bool GraphicsDevice::Create_Present_Pipeline()
    {
        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        if (!Compile_HLSL(PresentShaderHLSL, sizeof(PresentShaderHLSL) - 1, "present_quad", "VSMain", "vs_4_0", &vs_blob)) {
            return false;
        }
        if (!Compile_HLSL(PresentShaderHLSL, sizeof(PresentShaderHLSL) - 1, "present_quad", "PSMain", "ps_4_0", &ps_blob)) {
            vs_blob->Release();
            return false;
        }
        HRESULT hr = Device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &PresentVS);
        vs_blob->Release();
        if (FAILED(hr)) { ps_blob->Release(); return false; }

        hr = Device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &PresentPS);
        ps_blob->Release();
        return SUCCEEDED(hr);
    }


    void GraphicsDevice::Release_Present_Pipeline()
    {
        Safe_Release(PresentPS);
        Safe_Release(PresentVS);
    }


    void GraphicsDevice::Release_Surface_Texture()
    {
        Safe_Release(SurfaceSRV);
        Safe_Release(SurfaceTex);
        SurfaceWidth = 0;
        SurfaceHeight = 0;
    }


    void GraphicsDevice::Release_Sidebar_Surface_Texture()
    {
        Safe_Release(SidebarSurfaceSRV);
        Safe_Release(SidebarSurfaceTex);
        SidebarSurfaceWidth = 0;
        SidebarSurfaceHeight = 0;
    }


    bool GraphicsDevice::Resize_Backbuffer(int width, int height)
    {
        if (SwapChain == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        if (width == BackbufferWidth && height == BackbufferHeight) {
            return true;
        }

        /**
         *  Only the swap-chain backbuffer follows the window/display size.
         *  Scene-side render targets (SceneTarget, depth, alpha) track the
         *  logical render resolution and are managed by
         *  `Set_Logical_Resolution`. They're untouched here.
         */
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        Release_Backbuffer_RTV();

        UINT flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        if (FAILED(SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags))) {
            return false;
        }
        BackbufferWidth = width;
        BackbufferHeight = height;
        if (!Create_Backbuffer_RTV()) return false;
        return true;
    }


    bool GraphicsDevice::Set_Logical_Resolution(int width, int height)
    {
        if (Device == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        if (width == LogicalWidth && height == LogicalHeight
            && SceneTarget != nullptr && DepthTex != nullptr && AlphaTex != nullptr) {
            return true;
        }

        /**
         *  Detach any RTVs that might still reference the old DSV / SceneRT
         *  before releasing them.
         */
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        Release_Alpha_Buffer();
        Release_Scene_Target();
        Release_Depth_Buffer();

        if (!Create_Depth_Buffer(width, height)) {
            return false;
        }
        if (!Create_Scene_Target(width, height)) {
            Release_Depth_Buffer();
            return false;
        }
        if (!Create_Alpha_Buffer(width, height)) {
            Release_Scene_Target();
            Release_Depth_Buffer();
            return false;
        }

        LogicalWidth = width;
        LogicalHeight = height;
        DEBUG_INFO("Gfx::GraphicsDevice: logical render resolution set to %dx%d "
                   "(backbuffer %dx%d).\n",
            width, height, BackbufferWidth, BackbufferHeight);
        return true;
    }


    bool GraphicsDevice::Set_Surface_Format(int width, int height)
    {
        if (Device == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        if (SurfaceTex != nullptr && SurfaceWidth == width && SurfaceHeight == height) {
            return true;
        }
        Release_Surface_Texture();

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B5G6R5_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(Device->CreateTexture2D(&td, nullptr, &SurfaceTex))) {
            return false;
        }
        if (FAILED(Device->CreateShaderResourceView(SurfaceTex, nullptr, &SurfaceSRV))) {
            Release_Surface_Texture();
            return false;
        }
        SurfaceWidth = width;
        SurfaceHeight = height;
        return true;
    }


    bool GraphicsDevice::Set_Sidebar_Surface_Format(int width, int height)
    {
        if (Device == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        if (SidebarSurfaceTex != nullptr
            && SidebarSurfaceWidth == width
            && SidebarSurfaceHeight == height
            && SidebarTarget != nullptr
            && SidebarTarget->Width() == width
            && SidebarTarget->Height() == height) {
            return true;
        }

        Release_Sidebar_Surface_Texture();
        if (!Create_Sidebar_Target(width, height)) {
            return false;
        }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B5G6R5_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        if (FAILED(Device->CreateTexture2D(&td, nullptr, &SidebarSurfaceTex))) {
            Release_Sidebar_Target();
            return false;
        }
        if (FAILED(Device->CreateShaderResourceView(SidebarSurfaceTex, nullptr, &SidebarSurfaceSRV))) {
            Release_Sidebar_Surface_Texture();
            Release_Sidebar_Target();
            return false;
        }
        SidebarSurfaceWidth = width;
        SidebarSurfaceHeight = height;
        return true;
    }


    bool GraphicsDevice::Upload_Surface(const void* pixels, int pitch_bytes)
    {
        if (SurfaceTex == nullptr || pixels == nullptr) {
            return false;
        }
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(Context->Map(SurfaceTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return false;
        }
        const int row_bytes = SurfaceWidth * 2;
        const unsigned char* src = static_cast<const unsigned char*>(pixels);
        unsigned char* dst = static_cast<unsigned char*>(mapped.pData);
        if (mapped.RowPitch == (UINT)pitch_bytes && pitch_bytes == row_bytes) {
            memcpy(dst, src, (size_t)pitch_bytes * SurfaceHeight);
        } else {
            for (int y = 0; y < SurfaceHeight; ++y) {
                memcpy(dst + y * mapped.RowPitch, src + y * pitch_bytes, row_bytes);
            }
        }
        Context->Unmap(SurfaceTex, 0);
        return true;
    }


    bool GraphicsDevice::Upload_Sidebar_Surface(const void* pixels, int pitch_bytes)
    {
        if (SidebarSurfaceTex == nullptr || pixels == nullptr) {
            return false;
        }
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(Context->Map(SidebarSurfaceTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return false;
        }
        const int row_bytes = SidebarSurfaceWidth * 2;
        const unsigned char* src = static_cast<const unsigned char*>(pixels);
        unsigned char* dst = static_cast<unsigned char*>(mapped.pData);
        if (mapped.RowPitch == (UINT)pitch_bytes && pitch_bytes == row_bytes) {
            memcpy(dst, src, (size_t)pitch_bytes * SidebarSurfaceHeight);
        } else {
            for (int y = 0; y < SidebarSurfaceHeight; ++y) {
                memcpy(dst + y * mapped.RowPitch, src + y * pitch_bytes, row_bytes);
            }
        }
        Context->Unmap(SidebarSurfaceTex, 0);
        return true;
    }


    void GraphicsDevice::Begin_Frame()
    {
        if (BackbufferRTV == nullptr) {
            return;
        }
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        Context->OMSetRenderTargets(1, &BackbufferRTV, DepthDSV);
        Context->ClearRenderTargetView(BackbufferRTV, clear_color);
        if (SceneTarget != nullptr && SceneTarget->Get_RTV() != nullptr) {
            Context->ClearRenderTargetView(SceneTarget->Get_RTV(), clear_color);
        }
        if (SidebarTarget != nullptr && SidebarTarget->Get_RTV() != nullptr) {
            Context->ClearRenderTargetView(SidebarTarget->Get_RTV(), clear_color);
        }
        if (DepthDSV != nullptr) {
            Context->ClearDepthStencilView(DepthDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }
        if (AlphaRTV != nullptr) {
            /**
             *  Vanilla seeds AlphaBuffer to 127 each frame ("neutral mid-gray").
             *  Alpha-write shapes accumulate from there; alpha-sample shapes read
             *  this baseline when nothing has lit the pixel.
             */
            const float alpha_clear[4] = { 127.0f / 255.0f, 0.0f, 0.0f, 0.0f };
            Context->ClearRenderTargetView(AlphaRTV, alpha_clear);
        }

        D3D11_VIEWPORT vp = {};
        vp.Width  = (float)BackbufferWidth;
        vp.Height = (float)BackbufferHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        Context->RSSetViewports(1, &vp);
    }


    void GraphicsDevice::Set_Render_Target(RenderTarget2D* target, DepthBinding depth)
    {
        if (Context == nullptr) {
            return;
        }
        ID3D11RenderTargetView* rtv = (target != nullptr) ? target->Get_RTV() : BackbufferRTV;
        /**
         *  Some passes render color-only while SceneRT and the backbuffer share
         *  the frame depth buffer.
         */
        ID3D11DepthStencilView* dsv = (depth == DepthBinding::SharedDepth) ? DepthDSV : nullptr;
        Context->OMSetRenderTargets(1, &rtv, dsv);

        D3D11_VIEWPORT vp = {};
        if (target != nullptr) {
            vp.Width  = (float)target->Width();
            vp.Height = (float)target->Height();
        } else {
            vp.Width  = (float)BackbufferWidth;
            vp.Height = (float)BackbufferHeight;
        }
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        Context->RSSetViewports(1, &vp);
    }


    void GraphicsDevice::Bind_Backbuffer_Color_Only()
    {
        if (Context == nullptr || BackbufferRTV == nullptr) {
            return;
        }

        Context->OMSetRenderTargets(1, &BackbufferRTV, nullptr);

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)BackbufferWidth;
        vp.Height = (float)BackbufferHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        Context->RSSetViewports(1, &vp);
    }


    void GraphicsDevice::Bind_Scene_Target()
    {
        if (SceneTarget != nullptr) {
            Set_Render_Target(SceneTarget, DepthBinding::SharedDepth);
        } else {
            Bind_Backbuffer();
        }
    }


    void GraphicsDevice::Bind_Sidebar_Target()
    {
        if (SidebarTarget != nullptr) {
            Set_Render_Target(SidebarTarget, DepthBinding::None);
        } else {
            Bind_Scene_Target();
        }
    }


    void GraphicsDevice::Clear_Sidebar_Target()
    {
        if (SidebarTarget != nullptr) {
            const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            SidebarTarget->Clear(clear_color);
        }
    }


    ID3D11ShaderResourceView* GraphicsDevice::Get_Scene_SRV() const
    {
        return SceneTarget != nullptr ? SceneTarget->Get_SRV() : nullptr;
    }


    int GraphicsDevice::Get_Sidebar_Target_Width() const
    {
        return SidebarTarget != nullptr ? SidebarTarget->Width() : 0;
    }


    int GraphicsDevice::Get_Sidebar_Target_Height() const
    {
        return SidebarTarget != nullptr ? SidebarTarget->Height() : 0;
    }


    int GraphicsDevice::Get_Scene_Target_Width() const
    {
        return SceneTarget != nullptr ? SceneTarget->Width() : 0;
    }


    int GraphicsDevice::Get_Scene_Target_Height() const
    {
        return SceneTarget != nullptr ? SceneTarget->Height() : 0;
    }


    ID3D11ShaderResourceView* GraphicsDevice::Get_Sidebar_Target_SRV() const
    {
        return SidebarTarget != nullptr ? SidebarTarget->Get_SRV() : nullptr;
    }


    void GraphicsDevice::Draw_Texture(ID3D11ShaderResourceView* srv, const Rect& dst_rect, SDL_ScaleMode scale_mode, EBlend blend)
    {
        if (srv == nullptr || PresentVS == nullptr) {
            return;
        }

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = (float)dst_rect.X;
        vp.TopLeftY = (float)dst_rect.Y;
        vp.Width    = (float)dst_rect.Width;
        vp.Height   = (float)dst_rect.Height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        Context->RSSetViewports(1, &vp);

        Context->IASetInputLayout(nullptr);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->VSSetShader(PresentVS, nullptr, 0);
        Context->PSSetShader(PresentPS, nullptr, 0);

        ID3D11SamplerState* sampler =
            StateCacheInstance.Get(scale_mode == SDL_SCALEMODE_LINEAR ? ESampler::LinearClamp : ESampler::PointClamp);
        Context->PSSetSamplers(0, 1, &sampler);
        Context->PSSetShaderResources(0, 1, &srv);

        const float blend_factor[4] = { 0, 0, 0, 0 };
        Context->OMSetBlendState(StateCacheInstance.Get(blend), blend_factor, 0xFFFFFFFF);
        Context->OMSetDepthStencilState(StateCacheInstance.Get(EDepthStencil::None), 0);
        Context->RSSetState(StateCacheInstance.Get(ERasterizer::CullNone));

        Context->Draw(3, 0);

        ID3D11ShaderResourceView* null_srv = nullptr;
        Context->PSSetShaderResources(0, 1, &null_srv);
    }


    void GraphicsDevice::Draw_Surface(const Rect& dst_rect, SDL_ScaleMode scale_mode)
    {
        Draw_Texture(SurfaceSRV, dst_rect, scale_mode, EBlend::Opaque);
    }


    void GraphicsDevice::End_Frame()
    {
        if (SwapChain == nullptr) {
            return;
        }
        UINT sync_interval = VSync ? 1 : 0;
        UINT flags = (!VSync && TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        SwapChain->Present(sync_interval, flags);
    }
}
