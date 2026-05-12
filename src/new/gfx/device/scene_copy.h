/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Scene-RT snapshot for post-effects that need to sample the rendered
 *          scene (predator distortion, Sonic/Laser refraction). `Ensure_Copied`
 *          performs at most one `CopyResource` per frame; subsequent calls are no-ops.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>

struct ID3D11ShaderResourceView;


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class RenderTarget2D;


    class SceneCopy
    {
    public:
        static SceneCopy& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        /**
         *  Idempotent within a frame. Lazily (re)creates the backing texture
         *  to match the current scene-RT size, then `CopyResource`s the live
         *  scene into it. Returns true if the copy is now valid this frame.
         */
        bool Ensure_Copied(GraphicsDevice& device);

        void Begin_Frame();  // reset the per-frame guard so the next frame triggers a fresh copy

        ID3D11ShaderResourceView* Get_SRV() const;
        int Get_Width() const  { return Width; }
        int Get_Height() const { return Height; }

    private:
        SceneCopy() = default;

        bool Ensure_Target(GraphicsDevice& device, int width, int height);
        void Release_Target();

        RenderTarget2D* Target  = nullptr;
        int             Width   = 0;
        int             Height  = 0;
        bool            CopiedThisFrame = false;
        bool            Initialized     = false;
    };
}
