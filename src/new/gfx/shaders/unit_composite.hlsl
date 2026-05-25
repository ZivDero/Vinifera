cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float  Alpha;
    int    Ssaa;       // SSAA factor used for the t1 RT (only sampled when PassCount > 1)
    int    PassCount;  // 1 = NoSSAA only; 2 = blend NoSSAA + SSAA 50/50
    float  _Pad;
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

Texture2D<float4> ScratchNoSSAA : register(t0);
Texture2D<float4> ScratchSSAA   : register(t1);
SamplerState      PointS        : register(s0);

struct PSOut {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

/**
 *  Dual-pass composite resolve. Both passes render the SAME unit into
 *  two separate scratch RTs:
 *    t0 — NoSSAA RT (256², POINTLIST voxels, 1 source texel per dst pixel)
 *    t1 — SSAA   RT (512², splatted voxels, 2×2 source block per dst pixel)
 *  When PassCount == 1 (SmoothVoxels=off) only t0 was rendered; the PS
 *  pass-throughs it. When PassCount == 2 the PS samples both and emits
 *  a 50/50 average — gives "half-strength AA" where the SSAA softens
 *  the unit but the crisp POINTLIST version keeps the pixel-art look
 *  visible underneath.
 *
 *  Pixel handling at the silhouette: each pass has its own coverage,
 *  averaging the premultiplied alphas means a dst pixel covered by only
 *  one pass renders at ~50% alpha. That's the intended "blended" look
 *  the dual-pass approach asks for — if you want pixel-aligned crisp
 *  silhouettes, use SmoothVoxels=off (PassCount=1).
 */

float4 Resolve_NoSSAA(float2 uv)
{
    /**
     *  1-tap point sample at the dst pixel's source texel. Voxel POINTLIST
     *  pixels in the 256² RT map 1:1 to dst pixels, so this is exactly
     *  what the unit was rasterized to.
     */
    uint w, h;
    ScratchNoSSAA.GetDimensions(w, h);
    int2 px = int2(floor(uv * float2(w, h)));
    float4 c = ScratchNoSSAA.Load(int3(px, 0));
    /**
     *  Binarize alpha — premultiplied transparent pixels carry (0,0,0,0)
     *  and opaque ones (color, 1.0). Returning the pre-multiplied tuple
     *  keeps the blend math downstream consistent.
     */
    return c;
}

float4 Resolve_SSAA(float2 uv)
{
    /**
     *  4-tap SSAA resolve with majority-opaque rule. Same logic as the
     *  pre-dual-pass shader, just isolated as a helper so the dual-pass
     *  blend can call it cleanly. Samples 4 corners of an Ssaa×Ssaa
     *  block from the SSAA RT — for Ssaa=2 the 4 taps cover a 2×2 source
     *  block; the majority rule keeps silhouettes pixel-aligned while
     *  smoothing interior color transitions.
     */
    uint w, h;
    ScratchSSAA.GetDimensions(w, h);
    float2 src_px = uv * float2(w, h);
    int2   base   = int2(floor(src_px - 0.5));
    int    stride = Ssaa - 1;

    float4 s0 = ScratchSSAA.Load(int3(base + int2(0,      0     ), 0));
    float4 s1 = ScratchSSAA.Load(int3(base + int2(stride, 0     ), 0));
    float4 s2 = ScratchSSAA.Load(int3(base + int2(0,      stride), 0));
    float4 s3 = ScratchSSAA.Load(int3(base + int2(stride, stride), 0));

    float a0 = s0.a > 0.5 ? 1.0 : 0.0;
    float a1 = s1.a > 0.5 ? 1.0 : 0.0;
    float a2 = s2.a > 0.5 ? 1.0 : 0.0;
    float a3 = s3.a > 0.5 ? 1.0 : 0.0;
    float opaque_count = a0 + a1 + a2 + a3;

    // Majority threshold (≥2 of 4) for silhouette. Sub-threshold pixels
    // return premultiplied transparent; the dual-pass blend caller
    // decides what to do when only one pass has coverage at a given
    // pixel. Single return point — FXC's flow analyzer flags helper
    // functions with multiple returns as "potentially uninitialized" in
    // some configurations, so collapse to one tuple.
    float pass_ok = opaque_count >= 2.0 ? 1.0 : 0.0;
    float safe_count = max(opaque_count, 1.0);   // avoid /0 when pass_ok==0
    float3 rgb_sum = s0.rgb * a0 + s1.rgb * a1 + s2.rgb * a2 + s3.rgb * a3;
    float3 rgb     = (rgb_sum / safe_count) * pass_ok;
    return float4(rgb, pass_ok);
}

PSOut PSMain(VSOut v)
{
    float4 crisp = Resolve_NoSSAA(v.uv);

    float4 blended;
    if (PassCount > 1) {
        /**
         *  Coverage-weighted blend. Each pass contributes proportional
         *  to its own coverage, and the output alpha is the *union* of
         *  both — so a pixel reached by only one pass renders fully
         *  opaque (no half-alpha halo around the silhouette where one
         *  pass extended past the other), and pixels reached by both
         *  get a true 50/50 color average. Both inputs are premultiplied
         *  (rgb already scaled by their own alpha), so summing the rgbs
         *  and dividing by the total alpha gives the weighted-average
         *  color directly without re-premultiplying.
         */
        float4 smooth = Resolve_SSAA(v.uv);
        float  total_a = crisp.a + smooth.a;
        if (total_a <= 0.0) discard;
        float3 mixed_rgb = (crisp.rgb + smooth.rgb) / total_a;
        // Output alpha = "either pass covers this pixel". Saturate to
        // 1 so a same-coverage interior pixel doesn't double up.
        float  union_a = saturate(total_a);
        blended = float4(mixed_rgb * union_a, union_a);
    } else {
        blended = crisp;
    }

    // Discard fully-transparent pixels so the scene depth test only
    // sees the unit's actual footprint.
    if (blended.a <= 0.0) discard;

    PSOut o;
    /**
     *  Per-unit translucency. The blend math kept premultiplication, so
     *  scaling the whole tuple by `Alpha` re-premultiplies for the
     *  scene's `EBlend::Premultiplied` blend (`src + (1 - src.a)*dst`).
     */
    o.color = blended * Alpha;
    /**
     *  Per-unit constant depth from the caller (drawpoint-Y anchored to
     *  match `Tile_Base_Depth_From_Visual_Y`).
     */
    o.depth = v.pos.z;
    return o;
}
