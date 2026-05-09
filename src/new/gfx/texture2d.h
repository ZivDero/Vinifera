/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  RAII wrapper around ID3D11Texture2D + ID3D11ShaderResourceView.
 *
 *          Designed for 2D content: paletted sprite atlases, palette LUTs, the
 *          game-surface streaming texture, and ImGui-style font atlases.
 *          Supports both D3D11_USAGE_DEFAULT (UpdateSubresource) and
 *          D3D11_USAGE_DYNAMIC (Map/Unmap) upload paths.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>


namespace Vinifera::Gfx
{
    class GraphicsDevice;

    class Texture2D
    {
    public:
        Texture2D() = default;
        virtual ~Texture2D();

        Texture2D(const Texture2D&) = delete;
        Texture2D& operator=(const Texture2D&) = delete;

        bool Initialize(GraphicsDevice& device,
                        int width, int height,
                        DXGI_FORMAT format,
                        D3D11_USAGE usage = D3D11_USAGE_DEFAULT,
                        const void* initial_data = nullptr,
                        int initial_pitch_bytes = 0,
                        UINT bind_flags = D3D11_BIND_SHADER_RESOURCE);

        void Shutdown();

        /**
         *  Upload pixel data. For DEFAULT usage, uses UpdateSubresource. For
         *  DYNAMIC usage, uses Map(WRITE_DISCARD). `pitch_bytes` is the row
         *  stride of the source buffer.
         */
        bool Set_Data(const void* pixels, int pitch_bytes);

        /**
         *  Upload pixel data into a sub-rectangle of the texture. DEFAULT
         *  usage only. Used by atlas packers that fill the texture in many
         *  small uploads. (x, y, w, h) is in the destination texture's
         *  pixel space.
         */
        bool Set_Sub_Data(int x, int y, int w, int h, const void* pixels, int pitch_bytes);

        ID3D11Texture2D*           Get_Texture() const { return Texture; }
        ID3D11ShaderResourceView*  Get_SRV() const { return SRV; }
        int                        Width() const { return TextureWidth; }
        int                        Height() const { return TextureHeight; }
        DXGI_FORMAT                Format() const { return TextureFormat; }
        D3D11_USAGE                Usage() const { return TextureUsage; }

    protected:
        /**
         *  Allocate the underlying texture (override in RenderTarget2D to add
         *  D3D11_BIND_RENDER_TARGET). Default creates an SRV-only texture.
         */
        bool Create_Resource(ID3D11Device* device, UINT bind_flags, const void* initial_data, int initial_pitch);

        ID3D11Device*              ParentDevice = nullptr;
        ID3D11DeviceContext*       ParentContext = nullptr;
        ID3D11Texture2D*           Texture = nullptr;
        ID3D11ShaderResourceView*  SRV = nullptr;
        int                        TextureWidth = 0;
        int                        TextureHeight = 0;
        DXGI_FORMAT                TextureFormat = DXGI_FORMAT_UNKNOWN;
        D3D11_USAGE                TextureUsage = D3D11_USAGE_DEFAULT;
    };
}
