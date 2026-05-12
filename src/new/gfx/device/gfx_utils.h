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
}
