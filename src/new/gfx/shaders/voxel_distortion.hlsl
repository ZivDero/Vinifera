cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float4 T0;
    float4 T1;
    float4 T2;
    float4 T3;
    float4 LightDir;
    float4 Tint;
    float4 Misc;
    float4 Predator;     // x = warp_offset_px, y = blend_ratio,
                         // z = scene_w,         w = scene_h
};

Texture2D<float4>            Palette       : register(t0);
Texture2D<uint>              LightRemapTex : register(t1);
StructuredBuffer<float3>     Normals       : register(t2);
Texture2D<float4>            SceneCopy     : register(t3);
Texture2D<float>             AlphaTex      : register(t4);
static const uint VEF_NO_ALPHA_BUFFER = 0x02;

struct VSIn  {
    uint4 pos       : POSITION;
    uint4 normal_w  : NORMALIDX;
};
struct VSOut {
    float4 pos        : SV_Position;
    nointerpolation uint  color_idx  : COLOR0;
    nointerpolation uint  normal_idx : COLOR1;
    nointerpolation float voxel_z    : VOXELZ;
    nointerpolation float unit_y     : UNITY;
};

VSOut VSMain(VSIn i)
{
    float vx = (float)i.pos.x;
    float vy = (float)i.pos.y;
    float vz = (float)i.pos.z;
    float screen_x = T0.x + vx * T1.x + vy * T2.x + vz * T3.x;
    float screen_y = T0.y + vx * T1.y + vy * T2.y + vz * T3.y;
    float voxel_z  = T0.z + vx * T1.z + vy * T2.z + vz * T3.z;
    float4 clip = mul(ProjMtx, float4(screen_x, screen_y, 0.0, 1.0));
    VSOut o;
    o.pos        = float4(clip.x, clip.y, 0.5, 1.0);
    o.color_idx  = i.pos.w;
    o.normal_idx = i.normal_w.x;
    o.voxel_z    = voxel_z;
    o.unit_y     = T0.w;
    return o;
}

struct PSOut {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

PSOut PSMain(VSOut v)
{
    // Identical shading to VoxelEffect — normal lookup, Lambert,
    // VPL shade, palette LUT. Drop the shadow branch (predator
    // and shadow are disjoint).
    const float kNeutralShade = 16.0;
    int   table_base = (int)LightDir.w;
    float3 n = Normals.Load(table_base + (int)v.normal_idx);
    float  diffuse = saturate(dot(n, LightDir.xyz));
    float  shade_f = diffuse * kNeutralShade * Tint.r;
    int    shade = (int)shade_f;
    if (shade > 31) shade = 31;
    if (shade < 0)  shade = 0;
    uint shaded_idx = LightRemapTex.Load(int3((int)v.color_idx, shade, 0));
    if (shaded_idx == 0) discard;
    float4 rgba = Palette.Load(int3((int)shaded_idx, 0, 0));

    // SceneCopy sample at rasterized pixel + horizontal warp.
    // Integer Load matches vanilla's pointer-offset displacement.
    int scene_w = (int)Predator.z;
    int scene_h = (int)Predator.w;
    int2 sample_px = int2(v.pos.x, v.pos.y) + int2((int)Predator.x, 0);
    sample_px.x = clamp(sample_px.x, 0, scene_w - 1);
    sample_px.y = clamp(sample_px.y, 0, scene_h - 1);
    float4 bg = SceneCopy.Load(int3(sample_px, 0));

    // Modulate the unit color BEFORE the predator blend so `bg` keeps
    // its already-shrouded look. AlphaTex factor matches voxel.hlsl.
    uint flags = (uint)Misc.w;
    if (!(flags & VEF_NO_ALPHA_BUFFER)) {
        float alpha_byte = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;
        rgba.rgb *= alpha_byte / 127.0;
    }

    // lerp(voxel, scene, blend) — same math as the SHP distortion
    // path. Tint.a (visual-character translucency) is ignored: the
    // predator blend replaces it entirely.
    float3 mixed = lerp(rgba.rgb, bg.rgb, Predator.y);

    PSOut o;
    o.color = float4(mixed, 1.0);
    const float kVoxelZScale = 1e-4;
    float kObjectEps = Misc.z;
    float base = 1.0 - v.unit_y * Misc.y;
    o.depth = base - kObjectEps - v.voxel_z * kVoxelZScale + Misc.x * Misc.y;
    o.depth = clamp(o.depth, 0.0001, 0.9999);
    return o;
}
