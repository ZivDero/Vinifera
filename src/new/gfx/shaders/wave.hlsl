/**
 *  Wave shader. VS projects scene-pixel vertices through the standard
 *  pixel→NDC matrix, with depth derived from screen-Y so the polygon
 *  occludes correctly against terrain. PS branches on `Misc.x`:
 *
 *    Kind 0 (Sonic):
 *      - Compute the pixel's perpendicular distance from the beam's
 *        centre line (`radius`).
 *      - `amp = sin((SonicEC + radius) * 2*pi/500)` mirrors vanilla's
 *        500-entry SineTable phase.
 *      - Bucket amplitude into 0/1/2/3 cells of perpendicular offset
 *        (vanilla's `__colorints_188` does the same in discrete steps,
 *        but the continuous floor() here is visually equivalent).
 *      - Sample SceneCopy at the warp offset.
 *      - Boost G and B channels by `mult = (110 + amp*104)/256`
 *        (matches vanilla's IntensityTable[amp] = 110..214 range).
 *
 *    Kind 1 (Laser):
 *      - Sample SceneCopy at the current pixel (no warp).
 *      - Boost R channel by `Misc.z / 256` (precomputed from LaserEC).
 */

cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer WaveCB : register(b1)
{
    float4 Misc;   // x=kind, y=SonicEC, z=LaserMult, w=SceneW
    float4 Geom;   // x=SceneH, yzw=unused
    float4 Start;  // xy=RadiusRef (sonic ripple origin), zw=unused
    float4 Beam;   // xy=PerpDirPixels, zw=unused
};

struct VSIn  { float2 pos : POSITION; };
struct VSOut { float4 pos : SV_Position; float2 scene_xy : TEXCOORD0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    // Depth from screen-Y, same convention as every other queue:
    // closer to the bottom of the screen = closer to camera =
    // smaller depth value. eps keeps the wave slightly in front
    // of any terrain pixel at the same Y so the beam paints over
    // the ground it crosses.
    const float kPixelToDepth = 1.0 / 16000.0;
    float z = clamp(1.0 - i.pos.y * kPixelToDepth - 1.0e-4, 1.0e-4, 0.9999);
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0.0, 1.0));
    o.pos = float4(p.x, p.y, z, 1.0);
    o.scene_xy = i.pos.xy;
    return o;
}

Texture2D<float4> SceneCopy : register(t0);

float4 PSMain(VSOut v) : SV_Target
{
    int kind = (int)Misc.x;
    int scene_w = (int)Misc.w;
    int scene_h = (int)Geom.x;
    int2 px = int2(v.scene_xy);

    if (kind == 0) {
        // Sonic: Euclidean distance from `RadiusRef` (the wave's
        // source point in scene-RT pixels). Pixels at equal
        // distance share the sine phase, producing concentric-
        // ring ripples radiating from the source. Vanilla's
        // RadiusTable does the same (sqrt(dx*dx + dy*dy)).
        float radius = length(v.scene_xy - Start.xy);

        // Vanilla's SineTable[i] = sin(i * 0.125) * 12 + 0.49, so the
        // effective frequency is `i * 0.125` (period ~50 in i units).
        // amp scaled into [0, 12] to match vanilla's integer index
        // into the WaveIntensityTable.
        float sine_v = sin((Misc.y + radius) * 0.125);
        float amp = abs(sine_v) * 12.0;             // 0..12

        // Vanilla's WaveIntensityTable bucketing: amp 0..1 -> 0 cells,
        // 2..6 -> 1 cell, 7..9 -> 2 cells, 10..12 -> 3 cells. Mapped
        // here with two `step()` comparisons (cheap branch-free).
        int    amp_i = (int)amp;
        float  magnitude = (amp_i <= 1) ? 0.0
                         : (amp_i <= 6) ? 1.0
                         : (amp_i <= 9) ? 2.0 : 3.0;

        int2 offset = int2(Beam.xy * magnitude);
        int2 sample_px = clamp(px + offset, int2(0, 0),
                                int2(scene_w - 1, scene_h - 1));
        float4 bg = SceneCopy.Load(int3(sample_px, 0));

        // Vanilla's IntensityTable[i] = 110 + i*8; mult = / 256 to put
        // the boost in float [0..0.805] range. G/B saturated, R unchanged.
        float mult = (110.0 + amp * 8.0) / 256.0;
        float3 c;
        c.r = bg.r;
        c.g = saturate(bg.g + bg.g * mult);
        c.b = saturate(bg.b + bg.b * mult);
        return float4(c, 1.0);
    } else {
        // Laser: pure red boost, no warp.
        float4 bg = SceneCopy.Load(int3(px, 0));
        float mult = Misc.z / 256.0;
        float3 c;
        c.r = saturate(bg.r + bg.r * mult);
        c.g = bg.g;
        c.b = bg.b;
        return float4(c, 1.0);
    }
}
