/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  GPU mirror of the theatre's voxel light-remap table (VPL).
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_light_remap.h"

#include "graphics_device.h"
#include "voxel.hh"
#include "voxelglobals.h"


namespace Vinifera::Gfx
{
    bool VoxelLightRemapTexture::Initialize(GraphicsDevice& device)
    {
        return Tex.Initialize(device, Width, Height, DXGI_FORMAT_R8_UINT, D3D11_USAGE_DEFAULT);
    }


    void VoxelLightRemapTexture::Shutdown()
    {
        Tex.Shutdown();
        Uploaded = false;
    }


    bool VoxelLightRemapTexture::Ensure_Uploaded()
    {
        if (Uploaded) {
            return true;
        }
        /**
         *  `Voxel_PaletteLookup` is laid out `[light][color]` row-major,
         *  exactly the order the texture expects (Y = light, X = color).
         *  Copy straight in.
         */
        if (!Tex.Set_Sub_Data(0, 0, Width, Height,
                              &Voxel_PaletteLookup[0][0], Width)) {
            return false;
        }
        Uploaded = true;
        return true;
    }
}
