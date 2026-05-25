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
    float4 LightDir;     // xyz = light, w = normal-table base offset
    float4 Tint;
    float4 Misc;         // x = z_adjust (pixels, object) or 0 (shadow);
                         // y = depth scale (1/16000);
                         // z = per-section kObjectEps (object) or shadow alpha;
                         // w = flags (VEF_SHADOW = 1, VEF_NO_ALPHA_BUFFER = 2)
};
static const uint VEF_SHADOW          = 0x01;
static const uint VEF_NO_ALPHA_BUFFER = 0x02;

Texture2D<float4>            Palette       : register(t0);
Texture2D<uint>              LightRemapTex : register(t1);
StructuredBuffer<float3>     Normals       : register(t2);
Texture2D<float>             AlphaTex      : register(t3);

struct VSIn  {
    uint4 pos       : POSITION;    // x, y, z, color_idx
    uint4 normal_w  : NORMALIDX;   // normal_idx, _, _, _
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

    // Per-section transform: screen = T0 + x*T1 + y*T2 + z*T3.
    float screen_x = T0.x + vx * T1.x + vy * T2.x + vz * T3.x;
    float screen_y = T0.y + vx * T1.y + vy * T2.y + vz * T3.y;
    float voxel_z  = T0.z + vx * T1.z + vy * T2.z + vz * T3.z;

    // Object voxels: depth = base - kObjectEps - voxel_z*kVZS +
    // z_adjust/16000. The z_adjust term carries vanilla's
    // Get_Z_Adjustment() (negative for elevated objects) so a
    // flying unit sorts at the ground cell beneath rather than
    // where its raised screen_y happens to land. A final min()
    // clamp guarantees every voxel sits ahead of terrain by
    // at least `kBackClamp` regardless of |voxel_z| magnitude.
    //
    // Shadow voxels: VS leaves SV_Position.z as the per-vertex
    // computed value but the PIXEL SHADER overrides depth via
    // SV_Depth, computing it from the fragment's pixel-center
    // Y. This makes shadow depth a function of which pixel was
    // hit, not which vertex hit it — so every shadow voxel
    // rasterized to the same pixel (across body/turret/barrel
    // sections and across iso-squashed adjacent voxels) gets
    // the SAME depth value. WriteLess then keeps only the
    // first write and DestMultiplyHalf can't compound-darken.
    // Depth is computed in the PIXEL shader (see PSMain). The
    // VS just produces SV_Position with a placeholder z; the
    // PS overrides via SV_Depth using SV_Position.y (the
    // fragment's pixel-center Y) so all voxels rasterizing
    // to the same pixel — across body/turret/barrel sections
    // and overlapping iso-projected voxels — get identical
    // base. voxel_z then alone decides front-to-back order.
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
    uint flags = (uint)Misc.w;
    if (flags & VEF_SHADOW) {
        // Shadow path: emit dark gray. Caller uses DestMultiplyHalf
        // blend so the scene RT darkens to ~50% under the shadow.
        // Depth from pixel-center Y (SV_Position.y in PS) so all
        // shadow voxels landing in this pixel — regardless of
        // which vertex/section produced them — share one depth
        // value; WriteLess+dedup then prevents compound darken.
        //
        // Terrain tile depth is anchored at the tile's *bottom* Y
        // (Tile_Base_Depth_From_Visual_Y, constant across the entire
        // diamond), so a shadow pixel can be up to a full tile height
        // (~30 px) ABOVE the tile_bottom_y that the cell wrote into
        // the depth buffer. With the old 5e-5 epsilon the shadow's
        // per-pixel-Y depth landed BEHIND the tile at every pixel
        // above the tile's bottom row — strict-LESS rejected it and
        // voxel shadows were completely invisible. Use ~32 px worth
        // of depth (32/16000 = 2e-3) so shadow.depth is always
        // strictly less than tile.depth at the same pixel regardless
        // of where in the diamond the fragment lands. Body voxel
        // sorting still wins because the per-section kObjectEps is
        // sized from the section's worst-case screen-Y + back-Z
        // contribution (typically > 2e-3 for any reasonable voxel
        // model) and body uses LessEqual against shadow's written
        // depth.
        const float kShadowEps = 2.0e-3;
        PSOut so;
        so.color = float4(0.5, 0.5, 0.5, Misc.z);
        so.depth = 1.0 - v.pos.y * Misc.y - kShadowEps;
        so.depth = clamp(so.depth, 0.0001, 0.9999);
        return so;
    }

    // Lambertian shade. Vanilla maps diffuse to the VPL ramp via
    // VOXEL_PALETTE_LOOKUP_NEUTRAL (=16), and the VPL has 32 rows
    // total: 0..15 = darkened, 16 = neutral, 17..31 = overbright.
    // Cell brightness modulates the SHADE INDEX (not the output
    // color) — dark cells pull shade down, lit cells pull it up.
    // Tint.r carries brightness/1000 from the CPU side.
    const float kNeutralShade = 16.0;
    int   table_base = (int)LightDir.w;
    float3 n = Normals.Load(table_base + (int)v.normal_idx);
    float  diffuse = saturate(dot(n, LightDir.xyz));
    float  shade_f = diffuse * kNeutralShade * Tint.r;
    int    shade = (int)shade_f;
    if (shade > 31) shade = 31;
    if (shade < 0)  shade = 0;

    // VPL shade ramp: (shade, voxel color) -> theatre-palette index.
    uint shaded_idx = LightRemapTex.Load(int3((int)v.color_idx, shade, 0));
    if (shaded_idx == 0) discard;  // transparent palette entry

    // House-aware palette LUT (ColorScheme converter baked the
    // remap-range slots into the unit's faction colors already).
    // Brightness has already been applied via the shade index, so
    // we don't multiply rgba.rgb by Tint here — Tint.a still
    // carries visual-character translucency (VISUAL_DARKEN etc.).
    float4 rgba = Palette.Load(int3((int)shaded_idx, 0, 0));

    // AlphaTex holds the per-pixel "lighting byte" — 127 = neutral, 0 =
    // full shroud, ~254 = overbright cap. Same factor sprites and tiles
    // apply (see palette_sprite.hlsl).
    if (!(flags & VEF_NO_ALPHA_BUFFER)) {
        float alpha_byte = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;
        rgba.rgb *= alpha_byte / 127.0;
    }

    // Pre-multiply RGB by the final alpha. The queue binds an
    // EBlend::Premultiplied blend state which expects a pre-
    // multiplied source — without this the math collapses to
    // `src + (1-a)*dest` (unit full + partial terrain), making
    // translucent voxels look mostly opaque. Pre-multiplying
    // gives the correct `a*src + (1-a)*dest`. For opaque
    // voxels (a == 1) the multiply is a no-op.
    float a_final = rgba.a * Tint.a;
    PSOut o;
    o.color = float4(rgba.rgb * a_final, a_final);
    // Per-UNIT depth (NOT per-pixel). v.unit_y carries the
    // section's drawpoint Y from T0.w, identical across every
    // voxel of this section. Using drawpoint Y instead of the
    // fragment's screen_y means two units that overlap on the
    // same pixel sort by their drawpoint difference (which is
    // what we want: closer-to-camera drawpoint wins) rather
    // than by which voxel happens to have larger voxel_z. The
    // per-section kObjectEps is sized on the CPU to cover the
    // worst pixel below drawpoint, so voxels still stay in
    // front of terrain across the full sprite footprint.
    const float kVoxelZScale = 1e-4;
    float kObjectEps = Misc.z;
    float base = 1.0 - v.unit_y * Misc.y;
    o.depth = base - kObjectEps - v.voxel_z * kVoxelZScale + Misc.x * Misc.y;
    o.depth = clamp(o.depth, 0.0001, 0.9999);
    return o;
}
