/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Texture2D + ID3D11RenderTargetView so the texture can be drawn into.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include "texture2d.h"


namespace Vinifera::Gfx
{
    class RenderTarget2D : public Texture2D
    {
    public:
        RenderTarget2D() = default;
        ~RenderTarget2D() override;

        bool Initialize(GraphicsDevice& device,
                        int width, int height,
                        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

        void Shutdown();

        ID3D11RenderTargetView* Get_RTV() const { return RTV; }

        /**
         *  Clear this render target to the given RGBA color. The caller must
         *  bind it via GraphicsDevice::Set_Render_Target first.
         */
        void Clear(const float color[4]);

    private:
        ID3D11RenderTargetView* RTV = nullptr;
    };
}
