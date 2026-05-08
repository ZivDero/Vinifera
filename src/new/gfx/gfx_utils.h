/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Low-level helpers shared by the Vinifera Gfx layer.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>


namespace Vinifera::Gfx
{
    template<typename T>
    inline void Safe_Release(T*& obj)
    {
        if (obj) {
            obj->Release();
            obj = nullptr;
        }
    }

    /**
     *  Compile an HLSL source string with D3DCompile. Logs and returns false on
     *  failure. The caller owns the returned blob.
     */
    bool Compile_HLSL(const char* source, size_t source_size, const char* source_name,
                      const char* entry, const char* target, ID3DBlob** out_blob);
}
