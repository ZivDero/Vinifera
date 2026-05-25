cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float  Alpha;
    int    Ssaa;       // active SSAA stride: 1 = pass-through (1-tap), 2 = 2x2 resolve
    float2 _Pad;
};

struct VSIn {
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD0;
    float2 zuv    : TEXCOORD1;
    float4 col    : COLOR0;
    uint   layer  : TEXCOORD2;
    uint   pflags : TEXCOORD3;
};
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos = float4(p.x, p.y, i.pos.z, 1);
    o.uv  = i.uv;
    return o;
}

Texture2D<float4> Scratch : register(t0);
SamplerState      PointS  : register(s0);

struct PSOut {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

/**
 *  4-tap SSAA resolve with crisp-silhouette + interior-AA logic. The
 *  scratch is allocated at logical × kUnitScratchMaxSSAA and the active
 *  region for the current unit is logical × Ssaa (where `Ssaa` is the
 *  runtime CB uniform, 1 or 2 depending on `[AudioVisual] SmoothVoxels=`).
 *  Each dst pixel maps to an Ssaa×Ssaa source-texel block; doing the
 *  resolve ourselves instead of letting a bilinear sampler box-average
 *  everything keeps the silhouette pixel-aligned while smoothing the
 *  unit's interior.
 *
 *  Per dst pixel (with Ssaa = 2):
 *    - count of opaque source samples in the 2×2 block decides coverage
 *    - <2 opaque → discard (transparent; sharpens the outer silhouette)
 *    - ≥2 opaque → output fully opaque, color = avg of opaque-only samples
 *  The boundary jumps from <2 to ≥2 within one dst pixel so the
 *  silhouette stays crisp instead of fading through ~25%-alpha halo.
 *  Inside the unit (all 4 opaque) the average smooths VPL-ramp banding
 *  and softens transitions between adjacent voxels of different colors.
 *  Composite-replay SHPs whose one source texel covers a 2×2 scratch
 *  block contribute 4 identical samples, so SHP interiors and edges
 *  also stay crisp.
 *
 *  With Ssaa = 1 (SmoothVoxels=off, vanilla look) the stride collapses
 *  to 0, all 4 taps land on the same source texel, opaque_count is 0 or
 *  4, and the path reduces to a 1-tap pass-through that emits whatever
 *  was rasterized — no AA, no SSAA cost, voxel POINTLIST pixels render
 *  one-for-one into the scene.
 */
PSOut PSMain(VSOut v)
{
    uint w, h;
    Scratch.GetDimensions(w, h);
    float2 src_px = v.uv * float2(w, h);
    int2   base   = int2(floor(src_px - 0.5));
    int    stride = Ssaa - 1;   // 0 → all taps collapse; 1 → 2×2 footprint

    float4 s0 = Scratch.Load(int3(base + int2(0,      0     ), 0));
    float4 s1 = Scratch.Load(int3(base + int2(stride, 0     ), 0));
    float4 s2 = Scratch.Load(int3(base + int2(0,      stride), 0));
    float4 s3 = Scratch.Load(int3(base + int2(stride, stride), 0));

    float a0 = s0.a > 0.5 ? 1.0 : 0.0;
    float a1 = s1.a > 0.5 ? 1.0 : 0.0;
    float a2 = s2.a > 0.5 ? 1.0 : 0.0;
    float a3 = s3.a > 0.5 ? 1.0 : 0.0;
    float opaque_count = a0 + a1 + a2 + a3;

    // Coverage threshold: majority of taps for SSAA=2 (≥2 of 4);
    // any-tap for SSAA=1 (≥1 of 4, since all 4 are the same sample
    // so opaque_count is 0 or 4 — 0.5 catches the latter). Discarding
    // (instead of writing alpha=0) also keeps the scene depth test
    // from seeing pixels that aren't really the unit's footprint.
    const float kCoverThreshold = (Ssaa > 1) ? 2.0 : 0.5;
    if (opaque_count < kCoverThreshold) discard;

    // Scratch is premultiplied; opaque samples have rgb == final color,
    // transparent samples have rgb == 0. Masking by per-sample a and
    // dividing by opaque_count gives the actual color average across
    // contributing samples — no transparent-black pollution at the
    // silhouette pixels.
    float3 rgb_sum = s0.rgb * a0 + s1.rgb * a1 + s2.rgb * a2 + s3.rgb * a3;
    float3 rgb     = rgb_sum / opaque_count;

    PSOut o;
    // Per-unit translucency. Re-premultiplies for the scene's
    // EBlend::Premultiplied blend (`src + (1 - src.a)*dst`).
    o.color = float4(rgb * Alpha, Alpha);
    // Per-unit constant depth from `Render_Deferred_Composite`, anchored
    // at the unit's drawpoint Y to match `Tile_Base_Depth_From_Visual_Y`.
    o.depth = v.pos.z;
    return o;
}
