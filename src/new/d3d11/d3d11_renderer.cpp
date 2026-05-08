/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Native Direct3D 11 renderer.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "d3d11_renderer.h"

#include "debughandler.h"
#include "rect.h"

#include <d3dcompiler.h>
#include <dxgi1_5.h>


D3D11Renderer* D3DRenderer = nullptr;


namespace
{
    /**
     *  Fullscreen-triangle present pipeline. The VS emits a single triangle
     *  that covers NDC; the PS samples the bound texture.
     */
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

    template<typename T>
    void Safe_Release(T*& obj)
    {
        if (obj) {
            obj->Release();
            obj = nullptr;
        }
    }

    bool Compile_Shader(const char* entry, const char* target, ID3DBlob** out_blob)
    {
        ID3DBlob* error_blob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(PresentShaderHLSL, sizeof(PresentShaderHLSL) - 1, "present_quad",
            nullptr, nullptr, entry, target, flags, 0, out_blob, &error_blob);
        if (FAILED(hr)) {
            if (error_blob != nullptr) {
                DEBUG_ERROR("D3D11Renderer: shader compile failed: %s\n",
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            } else {
                DEBUG_ERROR("D3D11Renderer: shader compile failed (HRESULT 0x%08X).\n", hr);
            }
            return false;
        }
        if (error_blob != nullptr) {
            error_blob->Release();
        }
        return true;
    }
}


D3D11Renderer::~D3D11Renderer()
{
    Shutdown();
}


bool D3D11Renderer::Initialize(HWND hwnd, int backbuffer_width, int backbuffer_height, bool vsync)
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

    if (!Create_Surface_Pipeline()) {
        Shutdown();
        return false;
    }

    DEBUG_INFO("D3D11Renderer initialized (%dx%d, vsync=%d).\n", backbuffer_width, backbuffer_height, VSync ? 1 : 0);
    return true;
}


void D3D11Renderer::Shutdown()
{
    Release_Surface_Texture();
    Release_Surface_Pipeline();
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
    SurfaceWidth = 0;
    SurfaceHeight = 0;
    WindowHandle = nullptr;
}


bool D3D11Renderer::Create_Device()
{
    UINT flags = 0;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    /**
     *  Don't pass D3D11_CREATE_DEVICE_DEBUG by default — it requires the
     *  Windows SDK debug layer to be installed. Developers can enable it
     *  manually if needed.
     */
#endif

    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
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

    /**
     *  Get the DXGI factory through the device's adapter so we use the same
     *  factory the device was created on.
     */
    IDXGIDevice* dxgi_device = nullptr;
    hr = Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: QI IDXGIDevice failed.\n");
        return false;
    }

    IDXGIAdapter* adapter = nullptr;
    hr = dxgi_device->GetAdapter(&adapter);
    dxgi_device->Release();
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: GetAdapter failed.\n");
        return false;
    }

    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&DxgiFactory));
    adapter->Release();
    if (FAILED(hr) || DxgiFactory == nullptr) {
        DEBUG_ERROR("D3D11Renderer: GetParent(IDXGIFactory2) failed.\n");
        return false;
    }

    /**
     *  Probe for tearing support (Win10+). Used so VSync-off presents on
     *  borderless flip-discard swap chains can avoid the DWM cap.
     */
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


bool D3D11Renderer::Create_Swap_Chain(HWND hwnd, int width, int height)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    HRESULT hr = DxgiFactory->CreateSwapChainForHwnd(
        Device, hwnd, &desc, nullptr, nullptr, &SwapChain);
    if (FAILED(hr)) {
        DEBUG_ERROR("CreateSwapChainForHwnd failed (HRESULT 0x%08X).\n", hr);
        return false;
    }

    /**
     *  Disable DXGI's Alt+Enter — SDL/the game owns fullscreen toggling.
     */
    DxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);

    return true;
}


bool D3D11Renderer::Create_Backbuffer_RTV()
{
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: GetBuffer failed.\n");
        return false;
    }

    hr = Device->CreateRenderTargetView(back_buffer, nullptr, &BackbufferRTV);
    back_buffer->Release();
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: CreateRenderTargetView failed.\n");
        return false;
    }

    return true;
}


void D3D11Renderer::Release_Backbuffer_RTV()
{
    Safe_Release(BackbufferRTV);
}


bool D3D11Renderer::Create_Surface_Pipeline()
{
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    if (!Compile_Shader("VSMain", "vs_4_0", &vs_blob)) {
        return false;
    }
    if (!Compile_Shader("PSMain", "ps_4_0", &ps_blob)) {
        vs_blob->Release();
        return false;
    }

    HRESULT hr = Device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &PresentVS);
    vs_blob->Release();
    if (FAILED(hr)) {
        ps_blob->Release();
        DEBUG_ERROR("D3D11Renderer: CreateVertexShader failed.\n");
        return false;
    }

    hr = Device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &PresentPS);
    ps_blob->Release();
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: CreatePixelShader failed.\n");
        return false;
    }

    D3D11_SAMPLER_DESC samp = {};
    samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp.MinLOD = 0.0f;
    samp.MaxLOD = D3D11_FLOAT32_MAX;

    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(Device->CreateSamplerState(&samp, &SamplerLinear))) {
        return false;
    }
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(Device->CreateSamplerState(&samp, &SamplerPoint))) {
        return false;
    }

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(Device->CreateBlendState(&bd, &BlendOpaque))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = FALSE;
    if (FAILED(Device->CreateRasterizerState(&rd, &RasterNoCull))) {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = FALSE;
    dsd.StencilEnable = FALSE;
    if (FAILED(Device->CreateDepthStencilState(&dsd, &DepthDisabled))) {
        return false;
    }

    return true;
}


void D3D11Renderer::Release_Surface_Pipeline()
{
    Safe_Release(DepthDisabled);
    Safe_Release(RasterNoCull);
    Safe_Release(BlendOpaque);
    Safe_Release(SamplerPoint);
    Safe_Release(SamplerLinear);
    Safe_Release(PresentPS);
    Safe_Release(PresentVS);
}


void D3D11Renderer::Release_Surface_Texture()
{
    Safe_Release(SurfaceSRV);
    Safe_Release(SurfaceTex);
    SurfaceWidth = 0;
    SurfaceHeight = 0;
}


bool D3D11Renderer::Resize_Backbuffer(int width, int height)
{
    if (SwapChain == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (width == BackbufferWidth && height == BackbufferHeight) {
        return true;
    }

    Context->OMSetRenderTargets(0, nullptr, nullptr);
    Release_Backbuffer_RTV();

    UINT flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: ResizeBuffers failed (HRESULT 0x%08X).\n", hr);
        return false;
    }

    BackbufferWidth = width;
    BackbufferHeight = height;

    return Create_Backbuffer_RTV();
}


bool D3D11Renderer::Set_Surface_Format(int width, int height)
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

    HRESULT hr = Device->CreateTexture2D(&td, nullptr, &SurfaceTex);
    if (FAILED(hr)) {
        DEBUG_ERROR("D3D11Renderer: surface CreateTexture2D failed (HRESULT 0x%08X).\n", hr);
        return false;
    }

    hr = Device->CreateShaderResourceView(SurfaceTex, nullptr, &SurfaceSRV);
    if (FAILED(hr)) {
        Release_Surface_Texture();
        DEBUG_ERROR("D3D11Renderer: surface CreateShaderResourceView failed.\n");
        return false;
    }

    SurfaceWidth = width;
    SurfaceHeight = height;
    return true;
}


bool D3D11Renderer::Upload_Surface(const void* pixels, int pitch_bytes)
{
    if (SurfaceTex == nullptr || pixels == nullptr) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = Context->Map(SurfaceTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        return false;
    }

    const int row_bytes = SurfaceWidth * 2;
    const unsigned char* src = static_cast<const unsigned char*>(pixels);
    unsigned char* dst = static_cast<unsigned char*>(mapped.pData);
    if (mapped.RowPitch == static_cast<UINT>(pitch_bytes) && pitch_bytes == row_bytes) {
        memcpy(dst, src, static_cast<size_t>(pitch_bytes) * SurfaceHeight);
    } else {
        for (int y = 0; y < SurfaceHeight; ++y) {
            memcpy(dst + y * mapped.RowPitch, src + y * pitch_bytes, row_bytes);
        }
    }

    Context->Unmap(SurfaceTex, 0);
    return true;
}


void D3D11Renderer::Begin_Frame()
{
    if (BackbufferRTV == nullptr) {
        return;
    }

    const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    Context->OMSetRenderTargets(1, &BackbufferRTV, nullptr);
    Context->ClearRenderTargetView(BackbufferRTV, clear_color);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(BackbufferWidth);
    vp.Height = static_cast<float>(BackbufferHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &vp);
}


void D3D11Renderer::Bind_Backbuffer()
{
    if (BackbufferRTV == nullptr) {
        return;
    }
    Context->OMSetRenderTargets(1, &BackbufferRTV, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(BackbufferWidth);
    vp.Height = static_cast<float>(BackbufferHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &vp);
}


void D3D11Renderer::Draw_Surface(const Rect& dst_rect, SDL_ScaleMode scale_mode)
{
    if (SurfaceSRV == nullptr || PresentVS == nullptr) {
        return;
    }

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = static_cast<float>(dst_rect.X);
    vp.TopLeftY = static_cast<float>(dst_rect.Y);
    vp.Width    = static_cast<float>(dst_rect.Width);
    vp.Height   = static_cast<float>(dst_rect.Height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &vp);

    Context->IASetInputLayout(nullptr);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->VSSetShader(PresentVS, nullptr, 0);
    Context->PSSetShader(PresentPS, nullptr, 0);

    ID3D11SamplerState* sampler = (scale_mode == SDL_SCALEMODE_LINEAR) ? SamplerLinear : SamplerPoint;
    Context->PSSetSamplers(0, 1, &sampler);
    Context->PSSetShaderResources(0, 1, &SurfaceSRV);

    const float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Context->OMSetBlendState(BlendOpaque, blend_factor, 0xFFFFFFFF);
    Context->OMSetDepthStencilState(DepthDisabled, 0);
    Context->RSSetState(RasterNoCull);

    Context->Draw(3, 0);

    /**
     *  Unbind the SRV so subsequent rendering paths can rebind any state
     *  without DX11 warning about a bound RT/SRV conflict.
     */
    ID3D11ShaderResourceView* null_srv = nullptr;
    Context->PSSetShaderResources(0, 1, &null_srv);
}


void D3D11Renderer::End_Frame()
{
    if (SwapChain == nullptr) {
        return;
    }

    UINT sync_interval = VSync ? 1 : 0;
    UINT flags = (!VSync && TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    SwapChain->Present(sync_interval, flags);
}
