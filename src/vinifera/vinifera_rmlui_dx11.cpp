/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Direct3D 11 RenderInterface for RmlUi.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "vinifera_rmlui_dx11.h"

#include "debughandler.h"

#include <RmlUi/Core/Vertex.h>

#include <d3dcompiler.h>

#include <cstring>


namespace
{
    /**
     *  Per-draw constant buffer. The 4x4 ProjMtx is the orthographic
     *  projection from RmlUi's logical (video-resolution) space to clip space;
     *  Translation is applied to vertex positions before the projection.
     *
     *  Layout matches a `cbuffer { float4x4 ProjMtx; float2 Translation; };`
     *  with std HLSL packing rules, padded to 16-byte alignment.
     */
    struct DrawConstants
    {
        float ProjMtx[16];
        float Translation[2];
        float Padding[2];
    };

    static_assert(sizeof(DrawConstants) % 16 == 0, "DrawConstants must be 16-byte aligned.");

    struct CompiledGeometryDX11
    {
        ID3D11Buffer* VertexBuffer = nullptr;
        ID3D11Buffer* IndexBuffer  = nullptr;
        UINT          IndexCount   = 0;
    };

    struct TextureDX11
    {
        ID3D11Texture2D*          Texture = nullptr;
        ID3D11ShaderResourceView* SRV     = nullptr;
    };

    template<typename T>
    void Safe_Release(T*& obj)
    {
        if (obj) {
            obj->Release();
            obj = nullptr;
        }
    }

    bool Compile_HLSL(const char* source, size_t source_size, const char* entry, const char* target, ID3DBlob** out_blob)
    {
        ID3DBlob* error_blob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(source, source_size, "rmlui_dx11", nullptr, nullptr, entry, target, flags, 0, out_blob, &error_blob);
        if (FAILED(hr)) {
            if (error_blob != nullptr) {
                DEBUG_ERROR("RmlUi DX11: shader compile failed: %s\n", static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            } else {
                DEBUG_ERROR("RmlUi DX11: shader compile failed (HRESULT 0x%08X).\n", hr);
            }
            return false;
        }
        if (error_blob != nullptr) {
            error_blob->Release();
        }
        return true;
    }

    const char ShaderHLSL[] =
        "cbuffer DrawCB : register(b0) {\n"
        "    float4x4 ProjMtx;\n"
        "    float2   Translation;\n"
        "    float2   _pad;\n"
        "};\n"
        "struct VSIn  { float2 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
        "struct VSOut { float4 pos : SV_Position; float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
        "VSOut VSMain(VSIn i) {\n"
        "    VSOut o;\n"
        "    float2 p = i.pos + Translation;\n"
        "    o.pos = mul(ProjMtx, float4(p, 0, 1));\n"
        "    o.col = i.col;\n"
        "    o.uv  = i.uv;\n"
        "    return o;\n"
        "}\n"
        "Texture2D    Tex : register(t0);\n"
        "SamplerState Smp : register(s0);\n"
        "float4 PSMain(VSOut v) : SV_Target {\n"
        "    float4 t = Tex.Sample(Smp, v.uv);\n"
        "    return t * v.col;\n"
        "}\n";
}


ViniferaRmlUiDX11RenderInterface::ViniferaRmlUiDX11RenderInterface() = default;

ViniferaRmlUiDX11RenderInterface::~ViniferaRmlUiDX11RenderInterface()
{
    Shutdown();
}


bool ViniferaRmlUiDX11RenderInterface::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (device == nullptr || context == nullptr) {
        return false;
    }
    if (Device != nullptr) {
        return true;
    }

    Device = device;
    Context = context;
    Device->AddRef();
    Context->AddRef();

    if (!Create_Pipeline_Objects()) {
        Shutdown();
        return false;
    }

    if (!Create_White_Texture()) {
        Shutdown();
        return false;
    }

    return true;
}


void ViniferaRmlUiDX11RenderInterface::Shutdown()
{
    Safe_Release(WhiteSRV);
    Safe_Release(WhiteTex);
    Release_Pipeline_Objects();
    Safe_Release(Context);
    Safe_Release(Device);
}


bool ViniferaRmlUiDX11RenderInterface::Create_Pipeline_Objects()
{
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    if (!Compile_HLSL(ShaderHLSL, sizeof(ShaderHLSL) - 1, "VSMain", "vs_4_0", &vs_blob)) {
        return false;
    }
    if (!Compile_HLSL(ShaderHLSL, sizeof(ShaderHLSL) - 1, "PSMain", "ps_4_0", &ps_blob)) {
        vs_blob->Release();
        return false;
    }

    HRESULT hr = Device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &VertexShader);
    if (FAILED(hr)) { vs_blob->Release(); ps_blob->Release(); return false; }

    hr = Device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &PixelShader);
    if (FAILED(hr)) { vs_blob->Release(); ps_blob->Release(); return false; }

    /**
     *  Rml::Vertex is { Vector2f position; Colourb colour; Vector2f tex_coord; }
     *  -> position (R32G32_FLOAT) at offset 0
     *  -> colour   (R8G8B8A8_UNORM) at offset 8
     *  -> tex_coord (R32G32_FLOAT) at offset 12
     *  Total stride: 20 bytes.
     */
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = Device->CreateInputLayout(layout, _countof(layout), vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &InputLayout);
    vs_blob->Release();
    ps_blob->Release();
    if (FAILED(hr)) {
        return false;
    }

    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.ByteWidth = sizeof(DrawConstants);
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(Device->CreateBuffer(&cb_desc, nullptr, &ConstantBuffer))) {
        return false;
    }

    /**
     *  Premultiplied-alpha blend, mirroring SDL_BLENDFACTOR_ONE / ONE_MINUS_SRC_ALPHA.
     */
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable           = TRUE;
    bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(Device->CreateBlendState(&bd, &BlendPremultiplied))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    if (FAILED(Device->CreateRasterizerState(&rd, &RasterScissor))) {
        return false;
    }
    rd.ScissorEnable = FALSE;
    if (FAILED(Device->CreateRasterizerState(&rd, &RasterNoScissor))) {
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = FALSE;
    dsd.StencilEnable = FALSE;
    if (FAILED(Device->CreateDepthStencilState(&dsd, &DepthStencilDisabled))) {
        return false;
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(Device->CreateSamplerState(&sd, &Sampler))) {
        return false;
    }

    return true;
}


void ViniferaRmlUiDX11RenderInterface::Release_Pipeline_Objects()
{
    Safe_Release(Sampler);
    Safe_Release(DepthStencilDisabled);
    Safe_Release(RasterNoScissor);
    Safe_Release(RasterScissor);
    Safe_Release(BlendPremultiplied);
    Safe_Release(ConstantBuffer);
    Safe_Release(InputLayout);
    Safe_Release(PixelShader);
    Safe_Release(VertexShader);
}


bool ViniferaRmlUiDX11RenderInterface::Create_White_Texture()
{
    const unsigned char pixels[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 1;
    td.Height = 1;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = pixels;
    init.SysMemPitch = 4;

    if (FAILED(Device->CreateTexture2D(&td, &init, &WhiteTex))) {
        return false;
    }
    if (FAILED(Device->CreateShaderResourceView(WhiteTex, nullptr, &WhiteSRV))) {
        return false;
    }
    return true;
}


void ViniferaRmlUiDX11RenderInterface::Begin_Frame(int logical_width, int logical_height,
                                                   float xscale, float yscale,
                                                   int backbuffer_width, int backbuffer_height)
{
    LogicalWidth = logical_width > 0 ? logical_width : 1;
    LogicalHeight = logical_height > 0 ? logical_height : 1;
    XScale = xscale;
    YScale = yscale;
    BackbufferWidth = backbuffer_width;
    BackbufferHeight = backbuffer_height;

    /**
     *  RmlUi's coordinate space is logical (video resolution). The back buffer
     *  is window-sized; the rasterizer scales clip-space [-1,1] to that
     *  buffer, so an ortho projection from logical space gives us automatic
     *  scaling for "free". Scissor rects, on the other hand, are in
     *  back-buffer pixels, so SetScissorRegion multiplies by xscale/yscale.
     */
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(BackbufferWidth);
    vp.Height = static_cast<float>(BackbufferHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    Context->RSSetViewports(1, &vp);

    /**
     *  Force scissor off at the start of the frame; RmlUi will toggle it.
     */
    ScissorEnabled = false;
    ScissorRect = {};

    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->IASetInputLayout(InputLayout);
    Context->VSSetShader(VertexShader, nullptr, 0);
    Context->PSSetShader(PixelShader, nullptr, 0);
    Context->VSSetConstantBuffers(0, 1, &ConstantBuffer);
    Context->PSSetSamplers(0, 1, &Sampler);

    const float blend_factor[4] = { 0, 0, 0, 0 };
    Context->OMSetBlendState(BlendPremultiplied, blend_factor, 0xFFFFFFFF);
    Context->OMSetDepthStencilState(DepthStencilDisabled, 0);

    Apply_Scissor_State();
}


void ViniferaRmlUiDX11RenderInterface::End_Frame()
{
    if (Context == nullptr) {
        return;
    }

    /**
     *  Disable scissor so subsequent passes that share the immediate context
     *  don't inherit our state.
     */
    ScissorEnabled = false;
    Context->RSSetState(RasterNoScissor);

    ID3D11ShaderResourceView* null_srv = nullptr;
    Context->PSSetShaderResources(0, 1, &null_srv);
}


Rml::CompiledGeometryHandle ViniferaRmlUiDX11RenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    if (Device == nullptr || vertices.empty() || indices.empty()) {
        return 0;
    }

    CompiledGeometryDX11* geom = new CompiledGeometryDX11;
    geom->IndexCount = static_cast<UINT>(indices.size());

    D3D11_BUFFER_DESC vb_desc = {};
    vb_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Rml::Vertex));
    vb_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vb_data = {};
    vb_data.pSysMem = vertices.data();
    if (FAILED(Device->CreateBuffer(&vb_desc, &vb_data, &geom->VertexBuffer))) {
        delete geom;
        return 0;
    }

    D3D11_BUFFER_DESC ib_desc = {};
    ib_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(int));
    ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ib_data = {};
    ib_data.pSysMem = indices.data();
    if (FAILED(Device->CreateBuffer(&ib_desc, &ib_data, &geom->IndexBuffer))) {
        Safe_Release(geom->VertexBuffer);
        delete geom;
        return 0;
    }

    return reinterpret_cast<Rml::CompiledGeometryHandle>(geom);
}


void ViniferaRmlUiDX11RenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture)
{
    if (Context == nullptr || handle == 0) {
        return;
    }
    CompiledGeometryDX11* geom = reinterpret_cast<CompiledGeometryDX11*>(handle);
    if (geom->VertexBuffer == nullptr || geom->IndexBuffer == nullptr || geom->IndexCount == 0) {
        return;
    }

    /**
     *  Update per-draw constants: ortho(0, logical_w, logical_h, 0) and translation.
     *  Note: HLSL is column-major by default; the matrix below is laid out as
     *  rows but uploaded as if columns, which matches DirectXMath's row-major
     *  convention via mul(ProjMtx, vec). We build it row-major and then
     *  transpose by swapping rows/columns when populating the array.
     */
    DrawConstants cb = {};
    const float L = 0.0f;
    const float R = static_cast<float>(LogicalWidth);
    const float T = 0.0f;
    const float B = static_cast<float>(LogicalHeight);

    /**
     *  Column-major float[16]:
     *    | 2/(R-L)   0          0   (R+L)/(L-R) |
     *    | 0         2/(T-B)    0   (T+B)/(B-T) |
     *    | 0         0          1   0           |
     *    | 0         0          0   1           |
     *  Stored column by column.
     */
    cb.ProjMtx[0]  = 2.0f / (R - L); cb.ProjMtx[1]  = 0.0f;           cb.ProjMtx[2]  = 0.0f; cb.ProjMtx[3]  = 0.0f;
    cb.ProjMtx[4]  = 0.0f;           cb.ProjMtx[5]  = 2.0f / (T - B); cb.ProjMtx[6]  = 0.0f; cb.ProjMtx[7]  = 0.0f;
    cb.ProjMtx[8]  = 0.0f;           cb.ProjMtx[9]  = 0.0f;           cb.ProjMtx[10] = 1.0f; cb.ProjMtx[11] = 0.0f;
    cb.ProjMtx[12] = (R + L) / (L - R);
    cb.ProjMtx[13] = (T + B) / (B - T);
    cb.ProjMtx[14] = 0.0f;
    cb.ProjMtx[15] = 1.0f;

    cb.Translation[0] = translation.x;
    cb.Translation[1] = translation.y;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(Context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    memcpy(mapped.pData, &cb, sizeof(cb));
    Context->Unmap(ConstantBuffer, 0);

    UINT stride = sizeof(Rml::Vertex);
    UINT offset = 0;
    Context->IASetVertexBuffers(0, 1, &geom->VertexBuffer, &stride, &offset);
    Context->IASetIndexBuffer(geom->IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

    ID3D11ShaderResourceView* srv = WhiteSRV;
    if (texture != 0) {
        srv = reinterpret_cast<TextureDX11*>(texture)->SRV;
    }
    Context->PSSetShaderResources(0, 1, &srv);

    Context->DrawIndexed(geom->IndexCount, 0, 0);
}


void ViniferaRmlUiDX11RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
    if (handle == 0) {
        return;
    }
    CompiledGeometryDX11* geom = reinterpret_cast<CompiledGeometryDX11*>(handle);
    Safe_Release(geom->VertexBuffer);
    Safe_Release(geom->IndexBuffer);
    delete geom;
}


Rml::TextureHandle ViniferaRmlUiDX11RenderInterface::LoadTexture(Rml::Vector2i&, const Rml::String&)
{
    /**
     *  Vinifera serves all RmlUi documents from in-memory; the image-loading
     *  path was never used by the SDL backend either. Mirror that behavior.
     */
    return 0;
}


Rml::TextureHandle ViniferaRmlUiDX11RenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
{
    if (Device == nullptr || source.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0) {
        return 0;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = source_dimensions.x;
    td.Height = source_dimensions.y;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = source.data();
    init.SysMemPitch = static_cast<UINT>(source_dimensions.x) * 4;

    TextureDX11* wrapper = new TextureDX11;
    if (FAILED(Device->CreateTexture2D(&td, &init, &wrapper->Texture)) || wrapper->Texture == nullptr) {
        delete wrapper;
        return 0;
    }

    if (FAILED(Device->CreateShaderResourceView(wrapper->Texture, nullptr, &wrapper->SRV)) || wrapper->SRV == nullptr) {
        Safe_Release(wrapper->Texture);
        delete wrapper;
        return 0;
    }

    return reinterpret_cast<Rml::TextureHandle>(wrapper);
}


void ViniferaRmlUiDX11RenderInterface::ReleaseTexture(Rml::TextureHandle texture_handle)
{
    if (texture_handle == 0) {
        return;
    }
    TextureDX11* wrapper = reinterpret_cast<TextureDX11*>(texture_handle);
    Safe_Release(wrapper->SRV);
    Safe_Release(wrapper->Texture);
    delete wrapper;
}


void ViniferaRmlUiDX11RenderInterface::EnableScissorRegion(bool enable)
{
    ScissorEnabled = enable;
    Apply_Scissor_State();
}


void ViniferaRmlUiDX11RenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
    ScissorRect = region;
    Apply_Scissor_State();
}


void ViniferaRmlUiDX11RenderInterface::Apply_Scissor_State()
{
    if (Context == nullptr) {
        return;
    }

    if (!ScissorEnabled) {
        Context->RSSetState(RasterNoScissor);
        return;
    }

    /**
     *  RmlUi gives us logical-space coordinates; convert to back-buffer pixels.
     */
    D3D11_RECT r = {};
    r.left   = static_cast<LONG>(ScissorRect.Left()   * XScale);
    r.top    = static_cast<LONG>(ScissorRect.Top()    * YScale);
    r.right  = static_cast<LONG>(ScissorRect.Right()  * XScale);
    r.bottom = static_cast<LONG>(ScissorRect.Bottom() * YScale);

    if (r.left < 0) r.left = 0;
    if (r.top < 0) r.top = 0;
    if (r.right > BackbufferWidth) r.right = BackbufferWidth;
    if (r.bottom > BackbufferHeight) r.bottom = BackbufferHeight;
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;

    Context->RSSetState(RasterScissor);
    Context->RSSetScissorRects(1, &r);
}
