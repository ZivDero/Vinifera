/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Scene-RT snapshot for post-effects.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "scene_copy.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "render_target_2d.h"

#include <d3d11.h>


namespace Vinifera::Gfx
{
    SceneCopy& SceneCopy::Get()
    {
        static SceneCopy instance;
        return instance;
    }


    bool SceneCopy::Initialize(GraphicsDevice& /*device*/)
    {
        if (Initialized) {
            return true;
        }
        CopiedThisFrame = false;
        Initialized = true;
        return true;
    }


    void SceneCopy::Shutdown()
    {
        Release_Target();
        CopiedThisFrame = false;
        Initialized = false;
    }


    void SceneCopy::Begin_Frame()
    {
        CopiedThisFrame = false;
    }


    ID3D11ShaderResourceView* SceneCopy::Get_SRV() const
    {
        return Target != nullptr ? Target->Get_SRV() : nullptr;
    }


    bool SceneCopy::Ensure_Target(GraphicsDevice& device, int width, int height)
    {
        if (Target != nullptr && Width == width && Height == height) {
            return true;
        }
        Release_Target();
        Target = new RenderTarget2D();
        if (Target == nullptr) {
            return false;
        }
        if (!Target->Initialize(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM)) {
            DEBUG_ERROR("SceneCopy: target creation failed (%dx%d).\n", width, height);
            Release_Target();
            return false;
        }
        Width = width;
        Height = height;
        return true;
    }


    void SceneCopy::Release_Target()
    {
        if (Target != nullptr) {
            delete Target;
            Target = nullptr;
        }
        Width = 0;
        Height = 0;
    }


    bool SceneCopy::Ensure_Copied(GraphicsDevice& device)
    {
        if (!Initialized) {
            return false;
        }
        if (CopiedThisFrame) {
            return true;
        }

        ID3D11DeviceContext* ctx = device.Get_Context();
        ID3D11ShaderResourceView* scene_srv = device.Get_Scene_SRV();
        if (ctx == nullptr || scene_srv == nullptr) {
            return false;
        }

        const int scene_w = device.Get_Scene_Target_Width();
        const int scene_h = device.Get_Scene_Target_Height();
        if (scene_w <= 0 || scene_h <= 0) {
            return false;
        }
        if (!Ensure_Target(device, scene_w, scene_h)) {
            return false;
        }

        /**
         *  CopyResource doesn't disturb the active RTV / depth bindings.
         *  Caller's render state continues unchanged after this.
         */
        ID3D11Resource* scene_tex = nullptr;
        scene_srv->GetResource(&scene_tex);
        if (scene_tex == nullptr) {
            return false;
        }
        ctx->CopyResource(Target->Get_Texture(), scene_tex);
        scene_tex->Release();

        CopiedThisFrame = true;
        return true;
    }
}
