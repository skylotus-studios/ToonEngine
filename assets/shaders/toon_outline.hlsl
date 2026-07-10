//============================================================================
//  toon_outline.hlsl — inverted-hull outline pass.
//
//  Each vertex is pushed out along its normal (object space); the outline PSO
//  culls FRONT faces, so only the enlarged back-facing shell survives. Drawn
//  before the fill, it shows as a solid-color rim around the silhouette.
//============================================================================
#include "toon_common.hlsli"

PSInput VSMain(VSInput vin)
{
    PSInput o;
    // Extrude along the SMOOTH normal so hard edges (e.g. a cube's corners) stay
    // closed — the per-face normal would split shared verts apart and gap.
    float3 inflated = vin.Pos + vin.SmoothNormal * g_Outline.w;  // object-space extrude
    o.Pos           = mul(float4(inflated, 1.0), g_WorldViewProj);
    o.WorldNormal   = mul(float4(vin.Normal, 0.0), g_World).xyz;  // world space, for the G-buffer
    return o;
}

PSOutput PSMain(PSInput pin)
{
    // The rim is mostly overwritten by the fill; where it survives (the silhouette)
    // still needs a sane G-buffer normal so SSAO doesn't read garbage there.
    PSOutput o;
    o.Color  = float4(g_Outline.rgb, 1.0);
    o.Normal = float4(normalize(pin.WorldNormal), 0.0);
    return o;
}
