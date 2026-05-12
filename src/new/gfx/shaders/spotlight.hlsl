cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer SpotLightCB : register(b1)
{
    float4 Misc;   // x=EffectiveRadius, y=QuadTopLeft.x, z=QuadTopLeft.y, w=SceneW
    float4 Geom;   // x=SceneH, y=UniformMask (-1 => concentric falloff), zw=unused
};

struct VSIn  { float2 pos : POSITION; };
struct VSOut { float4 pos : SV_Position; float2 scene_xy : TEXCOORD0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    // Depth slightly less than terrain at the same screen-Y so
    // the spotlight beats terrain. Spotlight isn't z-tested in
    // vanilla (the CPU blitter writes unconditionally inside
    // its 256x128 area), so this is just to keep ordering sane
    // against other PostEffects content.
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
    int scene_w = (int)Misc.w;
    int scene_h = (int)Geom.x;
    int2 px = int2(v.scene_xy);

    // Quad-local coords (256 wide x 128 tall, top-left at Misc.yz).
    float2 quad_local = v.scene_xy - Misc.yz;
    float  dx = quad_local.x - 128.0;
    float  dy = 2.0 * (quad_local.y - 64.0);   // 2:1 vertical compensation
    float  dist = sqrt(dx * dx + dy * dy);

    float R = Misc.x;
    float uniform_mask = Geom.y;               // -1 => concentric falloff
    float mask;
    if (uniform_mask >= 0.0) {
        // Uniform-disc case (BuildingLight extra surfaces). Vanilla's
        // One_Time draws a single filled circle of constant colour --
        // a flat bright disc, not a gradient.
        if (dist > R) discard;
        mask = uniform_mask;
    } else {
        // Concentric-ring case (warhead spotlights). Vanilla draws
        // nested filled circles with brightness 0, 2, 6, ..., 4i-2.
        // Channel quantization in 16-bit RGB565 made boosts below ~6%
        // imperceptible -- the faint outer rings effectively vanished.
        // Our 32-bit pipeline shows them as a soft halo, so drop the
        // weak half to keep only the visibly-bright core.
        float steps = floor((R - dist) * 0.5);
        if (steps <= 0.0) discard;
        mask = 4.0 * steps - 2.0;
        float peak = 2.0 * R - 2.0;
        if (mask < peak * 0.5) discard;
    }
    mask = min(mask, 255.0);

    int2 sample_px = clamp(px, int2(0, 0), int2(scene_w - 1, scene_h - 1));
    float4 bg = SceneCopy.Load(int3(sample_px, 0));

    float boost = mask / 256.0;
    float3 out_rgb = saturate(bg.rgb + bg.rgb * boost);
    return float4(out_rgb, 1.0);
}
