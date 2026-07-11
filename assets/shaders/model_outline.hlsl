//============================================================================
//  model_outline.hlsl — inverted-hull outline for loaded glTF models.
//
//  Same idea as toon_outline, but for the loader's pos/normal/uv vertex, which has
//  no smooth normal — so we extrude along the shading normal. Smooth surfaces stay
//  closed; hard creases (split vertices with per-face normals) may gap slightly.
//  Drawn before model_fill (cull FRONT → the enlarged back-facing shell); the fill's
//  nearer depth then overwrites the shell everywhere but the silhouette rim.
//============================================================================
#include "toon_common.hlsli"

struct ModelVSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV     : ATTRIB2;   // unused here, but the input layout carries it
};

struct OutlinePSInput
{
    float4 Pos         : SV_POSITION;
    float3 WorldNormal : TEXCOORD0;
    float4 CurrClip    : TEXCOORD1;  // for motion vectors
    float4 PrevClip    : TEXCOORD2;
};

OutlinePSInput VSMain(ModelVSInput vin)
{
    OutlinePSInput o;
    // Extrude g_Outline.w *world-space* units along the true (inverse-transpose) world
    // normal, brought back into object space via world^-1 (the 3x3 transpose of
    // g_NormalMatrix) so the existing WVP path is reused — same trick as toon_outline,
    // but along Normal since models carry no smooth normal.
    float3   worldNormal = normalize(mul(float4(vin.Normal, 0.0), g_NormalMatrix).xyz);
    float3x3 worldInv    = transpose((float3x3)g_NormalMatrix);            // world^-1 (3x3)
    float3   inflated    = vin.Pos + mul(worldNormal, worldInv) * g_Outline.w;
    o.Pos         = mul(float4(inflated, 1.0), g_WorldViewProj);
    o.WorldNormal = mul(float4(vin.Normal, 0.0), g_NormalMatrix).xyz;
    o.CurrClip    = o.Pos;
    // Redo the extrude with the PREVIOUS frame's normal matrix, not this frame's -- see
    // toon_outline.hlsl's PrevClip for why (reusing `inflated` under-reports motion
    // during rotation, since the extrude direction is itself rotation-dependent).
    float3 prevWorldNormal = normalize(mul(float4(vin.Normal, 0.0), g_PrevNormalMatrix).xyz);
    float3x3 prevWorldInv  = transpose((float3x3)g_PrevNormalMatrix);
    float3 prevInflated    = vin.Pos + mul(prevWorldNormal, prevWorldInv) * g_Outline.w;
    o.PrevClip    = mul(float4(prevInflated, 1.0), g_PrevWorldViewProj);
    return o;
}

PSOutput PSMain(OutlinePSInput pin)
{
    PSOutput o;
    o.Color  = float4(g_Outline.rgb, 1.0);                       // solid outline color
    o.Normal = float4(normalize(pin.WorldNormal), g_Params.z);   // sane G-buffer normal at the rim
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
