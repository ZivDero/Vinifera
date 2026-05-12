/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Low-level helpers shared by the Vinifera Gfx layer.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "gfx_utils.h"

#include "debughandler.h"


namespace Vinifera::Gfx
{
    bool Compile_HLSL(const char* source, size_t source_size, const char* source_name,
                      const char* entry, const char* target, ID3DBlob** out_blob)
    {
        ID3DBlob* error_blob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(source, source_size, source_name, nullptr, nullptr,
                                entry, target, flags, 0, out_blob, &error_blob);
        if (FAILED(hr)) {
            if (error_blob != nullptr) {
                DEBUG_ERROR("Gfx: %s shader compile failed: %s\n",
                    source_name ? source_name : "<unknown>",
                    static_cast<const char*>(error_blob->GetBufferPointer()));
                error_blob->Release();
            } else {
                DEBUG_ERROR("Gfx: %s shader compile failed (HRESULT 0x%08X).\n",
                    source_name ? source_name : "<unknown>", hr);
            }
            return false;
        }
        if (error_blob != nullptr) {
            error_blob->Release();
        }
        return true;
    }
}
