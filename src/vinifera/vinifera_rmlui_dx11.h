/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Direct3D 11 RenderInterface for RmlUi.
 *
 *          Modeled on RmlUi's official RmlUi_Renderer_DX12 backend, downstepped
 *          to plain D3D11 idioms (immediate context, dynamic buffers, no
 *          descriptor heaps). Implements the required RenderInterface methods
 *          to match feature parity with the previous SDL_Renderer-based
 *          interface.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <d3d11.h>


class ViniferaRmlUiDX11RenderInterface final : public Rml::RenderInterface
{
public:
    ViniferaRmlUiDX11RenderInterface();
    ~ViniferaRmlUiDX11RenderInterface() override;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    /**
     *  Configure per-frame viewport and scaling. `logical_width/height` is the
     *  RmlUi context's coordinate space (the game's video resolution).
     *  `xscale/yscale` map logical pixels to back-buffer pixels.
     *  `backbuffer_width/height` is the current swap-chain back buffer size.
     */
    void Begin_Frame(int logical_width, int logical_height,
                     float xscale, float yscale,
                     int backbuffer_width, int backbuffer_height);

    /**
     *  Disables scissor and unbinds the SRV slot used by RmlUi.
     */
    void End_Frame();

    /**
     *  Required RenderInterface methods.
     */
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture_handle) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    bool Create_Pipeline_Objects();
    void Release_Pipeline_Objects();

    bool Create_White_Texture();

    void Apply_Scissor_State();

    ID3D11Device*               Device = nullptr;
    ID3D11DeviceContext*        Context = nullptr;

    ID3D11VertexShader*         VertexShader = nullptr;
    ID3D11PixelShader*          PixelShader = nullptr;
    ID3D11InputLayout*          InputLayout = nullptr;
    ID3D11Buffer*               ConstantBuffer = nullptr;
    ID3D11BlendState*           BlendPremultiplied = nullptr;
    ID3D11RasterizerState*      RasterScissor = nullptr;
    ID3D11RasterizerState*      RasterNoScissor = nullptr;
    ID3D11DepthStencilState*    DepthStencilDisabled = nullptr;
    ID3D11SamplerState*         Sampler = nullptr;

    /**
     *  1x1 white texture used as a fallback when RenderGeometry is called with
     *  texture handle 0 (RmlUi's "untextured" case). Avoids needing a second
     *  pixel shader.
     */
    ID3D11Texture2D*            WhiteTex = nullptr;
    ID3D11ShaderResourceView*   WhiteSRV = nullptr;

    bool                        ScissorEnabled = false;
    Rml::Rectanglei             ScissorRect = {};
    float                       XScale = 1.0f;
    float                       YScale = 1.0f;
    int                         LogicalWidth = 0;
    int                         LogicalHeight = 0;
    int                         BackbufferWidth = 0;
    int                         BackbufferHeight = 0;
};
