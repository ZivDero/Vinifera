cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer FontCB : register(b1)
{
    float2 AtlasSize;
    uint2  _pad0;
    uint4  Remap[4];   // 16 palette indices, packed 4 per uint4
};

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float2 zuv : TEXCOORD1; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos = float4(p.x, p.y, i.pos.z, 1);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}

Texture2D<uint>   Atlas   : register(t0);
Texture2D<float4> Palette : register(t1);

float4 PSMain(VSOut v) : SV_Target
{
    int2 px = int2(v.uv * AtlasSize);
    uint idx = Atlas.Load(int3(px, 0));
    /* WWFont glyph pixel values are 0..15. Apply the per-batch
       remap before palette lookup. Remap[0] (i.e. index 0) is
       transparent if it remaps to 0; we discard. */
    uint remapped = Remap[idx >> 2][idx & 3];
    if (remapped == 0) discard;
    float4 c = Palette.Load(int3((int)remapped, 0, 0));
    c.rgb *= v.col.rgb;
    c.a   *= v.col.a;
    c.rgb *= c.a;   /* premultiply for EBlend::Premultiplied */
    return c;
}
