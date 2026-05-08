/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Texture2D + ID3D11RenderTargetView.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "render_target_2d.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"


namespace Vinifera::Gfx
{
    RenderTarget2D::~RenderTarget2D()
    {
        Shutdown();
    }


    bool RenderTarget2D::Initialize(GraphicsDevice& device, int width, int height, DXGI_FORMAT format)
    {
        if (!Texture2D::Initialize(device, width, height, format,
                                   D3D11_USAGE_DEFAULT, nullptr, 0,
                                   D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) {
            return false;
        }
        if (FAILED(ParentDevice->CreateRenderTargetView(Texture, nullptr, &RTV))) {
            DEBUG_ERROR("Gfx::RenderTarget2D: CreateRenderTargetView failed.\n");
            Shutdown();
            return false;
        }
        return true;
    }


    void RenderTarget2D::Shutdown()
    {
        Safe_Release(RTV);
        Texture2D::Shutdown();
    }


    void RenderTarget2D::Clear(const float color[4])
    {
        if (ParentContext != nullptr && RTV != nullptr) {
            ParentContext->ClearRenderTargetView(RTV, color);
        }
    }
}
