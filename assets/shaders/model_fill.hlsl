//============================================================================
//  model_fill.hlsl — cel fill for loaded glTF models (textured).
//
//  Same banded look as toon_fill, but for the DiligentTools glTF loader's vertex
//  layout (position / normal / UV — no smooth normal) and with an albedo texture.
//  Shares the constant buffer, the CelShade ramp, the motion helper, and the MRT
//  PSOutput with toon_common.hlsli. (Models get no inverted-hull outline yet.)
//============================================================================
#include "toon_common.hlsli"

// Base-color (albedo) texture from the glTF material. The DiligentTools loader stores
// every texture as a 2D ARRAY (one layer each in the non-atlas path), so bind it as an
// array and sample slice 0. Combined-sampler style: the PSO supplies the sampler as an
// immutable sampler named "g_Albedo". A 1x1 white 2D-array is bound for untextured prims.
Texture2DArray g_Albedo;
SamplerState   g_Albedo_sampler;

// The loader packs POSITION/NORMAL/TEXCOORD_0 into one interleaved buffer; the PSO's
// input layout is built from the same VertexAttributeDesc array, so ATTRIB0/1/2 line
// up here (see Renderer::CreateModelPipeline / LoadModel).
struct ModelVSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV     : ATTRIB2;
};

struct ModelPSInput
{
    float4 Pos         : SV_POSITION;
    float3 WorldNormal : TEXCOORD0;
    float2 UV          : TEXCOORD1;
    float4 CurrClip    : TEXCOORD2;  // clip pos this frame (motion vectors)
    float4 PrevClip    : TEXCOORD3;  // clip pos last frame
};

ModelPSInput VSMain(ModelVSInput vin)
{
    ModelPSInput o;
    o.Pos         = mul(float4(vin.Pos, 1.0), g_WorldViewProj);
    o.WorldNormal = mul(float4(vin.Normal, 0.0), g_NormalMatrix).xyz;  // inverse-transpose (non-uniform scale)
    o.UV          = vin.UV;
    o.CurrClip    = o.Pos;
    o.PrevClip    = mul(float4(vin.Pos, 1.0), g_PrevWorldViewProj);
    return o;
}

PSOutput PSMain(ModelPSInput pin)
{
    float3 N      = normalize(pin.WorldNormal);
    // Albedo x base-color factor (g_BaseColor carries the material factor * app tint).
    // Slice 0: each glTF texture is its own 1-layer array in the non-atlas path.
    float3 albedo = g_Albedo.Sample(g_Albedo_sampler, float3(pin.UV, 0.0)).rgb * g_BaseColor.rgb;

    PSOutput o;
    // g_LightColor tints/scales the whole ramp (including the ambient floor) by the light's
    // color * intensity -- see toon_fill.hlsl.
    o.Color  = float4(CelShade(albedo, N, g_LightDir.xyz, g_Params.x, g_Params.y) * g_LightColor.rgb, 1.0);
    o.Normal = float4(N, g_Params.z);   // world normal (SSAO) + roughness in w (SSR)
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
