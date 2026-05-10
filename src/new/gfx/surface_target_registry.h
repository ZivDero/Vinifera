/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Registry mapping legacy Surface instances to GPU target metadata.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <vector>

#include "gpu_surface_target.h"


class Surface;


namespace Vinifera::Gfx
{
    class SurfaceTargetRegistry
    {
    public:
        static SurfaceTargetRegistry& Get();

        void Clear();
        void Bind(const GpuSurfaceTargetDesc& desc);
        void Unbind(Surface* surface);

        GpuSurfaceTarget* Find(Surface* surface);
        const GpuSurfaceTarget* Find(Surface* surface) const;

    private:
        SurfaceTargetRegistry() = default;

        std::vector<GpuSurfaceTarget> Targets;
    };
}
