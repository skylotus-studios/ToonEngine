//============================================================================
//  tonemap.hlsl — HDR resolve: exposure + ACES tone map, HDR target -> back buffer.
//
//  The first post-process pass on the HDR pipeline that DiligentFX's effects
//  (bloom, SSAO, ...) will layer onto next. Drawn as a single full-screen
//  triangle (no vertex buffer).
//============================================================================
Texture2D    g_HDRColor;
SamplerState g_HDRColor_sampler;

// Screen-space ambient occlusion (DiligentFX SSAO). Stores *visibility* in .r
// (1 = fully open, 0 = fully occluded). When SSAO is off, a 1x1 white texture is
// bound so the composite below is a no-op.
Texture2D    g_AO;
SamplerState g_AO_sampler;

cbuffer PostConstants
{
    float g_Exposure;      // linear multiplier before tone mapping
    float g_ToneMap;       // 1 = ACES filmic, 0 = clamp only
    float g_OutputSRGB;    // 1 = encode sRGB here (back buffer is a non-sRGB UNORM)
    float g_SSAOStrength;  // 0 = AO ignored; 1 = full occlusion
};

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

// Full-screen triangle straight from the vertex id — covers the screen with one
// oversized triangle, no vertex/index buffer required.
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);   // (0,0) (2,0) (0,2)
    o.UV  = uv;
    o.Pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// Narkowicz ACES filmic approximation (operates in linear space, returns [0,1]).
float3 ACESFilm(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 LinearToSRGB(float3 c)
{
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return lerp(lo, hi, step(0.0031308, c));
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float3 hdr = g_HDRColor.Sample(g_HDRColor_sampler, i.UV).rgb;

    // Darken occluded areas (contact shadows). Applied in linear HDR, before
    // exposure + tone mapping, so it reads as lost ambient light. GTAO visibility is
    // physically restrained (subtle on convex shapes); square it so contact shadows
    // read with the stylized look, leaving fully-open areas (1.0) untouched.
    float ao = g_AO.Sample(g_AO_sampler, i.UV).r;
    hdr *= lerp(1.0, ao * ao, g_SSAOStrength);

    hdr *= g_Exposure;

    float3 col = g_ToneMap > 0.5 ? ACESFilm(hdr) : saturate(hdr);

    // If the back buffer is an sRGB format the hardware encodes for us; if it's a
    // plain UNORM, encode here so mid-tones aren't crushed.
    if (g_OutputSRGB > 0.5)
        col = LinearToSRGB(col);

    return float4(col, 1.0);
}
