/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Registry mapping legacy Surface instances to GPU target metadata.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "surface_target_registry.h"

#include "tibsun_globals.h"

#include <algorithm>


namespace Vinifera::Gfx
{
    SurfaceTargetRegistry& SurfaceTargetRegistry::Get()
    {
        static SurfaceTargetRegistry instance;
        return instance;
    }


    void SurfaceTargetRegistry::Clear()
    {
        Targets.clear();
    }


    void SurfaceTargetRegistry::Bind(const GpuSurfaceTargetDesc& desc)
    {
        if (desc.SurfacePtr == nullptr) {
            return;
        }

        Unbind(desc.SurfacePtr);
        Targets.emplace_back(desc);
    }


    void SurfaceTargetRegistry::Unbind(Surface* surface)
    {
        Targets.erase(
            std::remove_if(
                Targets.begin(),
                Targets.end(),
                [surface](const GpuSurfaceTarget& target) {
                    return target.Get_Surface() == surface;
                }),
            Targets.end());
    }


    GpuSurfaceTarget* SurfaceTargetRegistry::Find(Surface* surface)
    {
        if (surface == nullptr) {
            return nullptr;
        }

        for (GpuSurfaceTarget& target : Targets) {
            if (target.Get_Surface() == surface) {
                return &target;
            }
        }
        return nullptr;
    }


    const GpuSurfaceTarget* SurfaceTargetRegistry::Find(Surface* surface) const
    {
        return const_cast<SurfaceTargetRegistry*>(this)->Find(surface);
    }
}
