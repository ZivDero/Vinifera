/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Voxel point-list effect.
 *
 *          Renders one screen pixel per VXL voxel via a vertex stream of
 *          `VoxelVertex` (x, y, z, color_idx, normal_idx) drawn as POINTLIST.
 *          The vertex shader applies the per-section transform from `b1` to
 *          land the voxel at its screen position and depth. The pixel shader
 *          looks up the normal vector from a structured buffer (`NormalsBuf`,
 *          all four vanilla normal tables concatenated), computes a Lambertian
 *          shade index, looks the (shade, color) pair up in the VPL
 *          light-remap texture (R8_UINT 256x32) for a lit palette index,
 *          and then converts that to RGBA via the unit's house-aware
 *          `PaletteLUT` (RGBA8 256x1, built from `ColorScheme::Converter`).
 *
 *          Bind layout:
 *            t0 — palette LUT       (RGBA8, 256x1, house-aware)
 *            t1 — VPL light remap   (R8_UINT, 256x32, game-wide)
 *            t2 — normals buffer    (StructuredBuffer<float3>)
 *            s0 — point-clamp sampler
 *            b0 — SpriteCB ProjMtx
 *            b1 — VoxelEffectParams (transform + lighting + tint)
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>

#include "effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class PaletteLUT;
    class Texture2D;


    /**
     *  Per-section CB. Mirrors vanilla's TransformMatrix[0..3] from
     *  VoxelLibrary::Render_Object. Rows are float4 so we can pack a depth
     *  scale + lighting into the W components instead of separate registers.
     *
     *  Layout (HLSL packing):
     *      T0 = (screen_x_origin, screen_y_origin, depth_origin,    _pad)
     *      T1 = (screen_dx_per_x, screen_dy_per_x, depth_dx_per_x,  _pad)
     *      T2 = (screen_dx_per_y, screen_dy_per_y, depth_dy_per_y,  _pad)
     *      T3 = (screen_dx_per_z, screen_dy_per_z, depth_dz_per_z,  _pad)
     *      LightDir         = (lx, ly, lz, normal_table_base)
     *      Tint             = (r, g, b, a)
     *      Misc             = (depth_baseline, depth_scale,
     *                          shadow_dark_amount, flags)
     *      Predator         = (warp_offset_px, blend_ratio,
     *                          scene_w, scene_h)
     *                          — only sampled by VoxelDistortionEffect
     */
    struct VoxelEffectParams
    {
        float T0[4];
        float T1[4];
        float T2[4];
        float T3[4];
        float LightDir[4];
        float Tint[4];
        float Misc[4];
        float Predator[4];
    };


    enum VoxelEffectFlag : uint32_t
    {
        VEF_NONE      = 0,
        VEF_SHADOW    = 1u << 0,    // pixel shader emits dark-gray for ground shadow
    };


    class VoxelEffect : public Effect
    {
    public:
        VoxelEffect() = default;
        ~VoxelEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        /**
         *  Bind the house-aware palette LUT (t0) and the game-wide VPL
         *  light-remap texture (t1) for the next draw.
         */
        void Bind_Palette(GraphicsDevice& device, PaletteLUT& palette);
        void Bind_Light_Remap(GraphicsDevice& device, Texture2D& light_remap_tex);

        /**
         *  Update + bind the per-section CB at b1.
         */
        void Set_Params(GraphicsDevice& device, const VoxelEffectParams& params);

        /**
         *  Bind the normals SRV (t2). Done once after Initialize; only call
         *  again if the device is rebuilt.
         */
        void Bind_Normals(GraphicsDevice& device);

        /**
         *  Exposes the normals SRV so VoxelDistortionEffect can bind the same
         *  game-wide normals buffer without owning its own copy.
         */
        ID3D11ShaderResourceView* Get_Normals_SRV() const { return NormalsSRV; }

    private:
        bool Build_Normals_Buffer(GraphicsDevice& device);
        void Release_Normals_Buffer();

        ID3D11Buffer*             ParamsCB = nullptr;
        ID3D11Buffer*             NormalsBuf = nullptr;
        ID3D11ShaderResourceView* NormalsSRV = nullptr;
    };


    /**
     *  Predator/cloak variant of the voxel point-list effect. Same VS layout
     *  and CB shape as `VoxelEffect`; the PS additionally samples a SceneCopy
     *  texture at the rasterized pixel + warp offset and lerps with the
     *  shaded palette color. Bound at slot t3 by the caller.
     *
     *  Used by `VoxelQueue` in the PostEffects pass for `VISUAL_RIPPLE`
     *  (stealth-tank chassis); the equivalent of `BlitTransLucent*ZReadWarp`
     *  from vanilla but on the GPU.
     */
    class VoxelDistortionEffect : public Effect
    {
    public:
        VoxelDistortionEffect() = default;
        ~VoxelDistortionEffect() = default;

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Bind_Palette(GraphicsDevice& device, PaletteLUT& palette);
        void Bind_Light_Remap(GraphicsDevice& device, Texture2D& light_remap_tex);
        void Bind_Scene_Copy(GraphicsDevice& device, ID3D11ShaderResourceView* scene_copy_srv);
        void Bind_Normals(GraphicsDevice& device, ID3D11ShaderResourceView* normals_srv);

        void Set_Params(GraphicsDevice& device, const VoxelEffectParams& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };
}
