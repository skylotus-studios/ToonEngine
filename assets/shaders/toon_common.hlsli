//============================================================================
//  toon_common.hlsli — shared bindings for the toon fill + outline passes.
//
//  The cbuffer layout here MUST match toon::ShaderConstants in
//  core/renderer.cpp (same field order, 16-byte rows). Matrices are declared
//  row_major so Diligent's row-major float4x4 uploads verbatim (no transpose),
//  and vectors are transformed row-vector style: mul(v, M).
//============================================================================
cbuffer Constants
{
    row_major float4x4 g_WorldViewProj; // object -> clip
    row_major float4x4 g_World;         // object -> world (for normals; uniform scale assumed)

    float4 g_LightDir;   // xyz: normalized direction TO the light;  w unused
    float4 g_BaseColor;  // rgb: material albedo;                    w unused
    float4 g_Outline;    // rgb: outline color;                      w: object-space extrude width
    float4 g_Params;     // x: number of shading bands; y: ambient floor; zw unused
};

struct VSInput
{
    float3 Pos          : ATTRIB0;
    float3 Normal       : ATTRIB1;  // shading normal (may be faceted)
    float3 SmoothNormal : ATTRIB2;  // averaged normal for the outline hull
};

struct PSInput
{
    float4 Pos         : SV_POSITION;
    float3 WorldNormal : TEXCOORD0;
};

// The scene pass writes two targets (MRT): the shaded color, and a world-space
// normal G-buffer that DiligentFX's SSAO reads. Both toon passes (fill + outline)
// output this struct so their PSO render-target counts match the bound targets.
struct PSOutput
{
    float4 Color  : SV_Target0;  // HDR scene color
    float4 Normal : SV_Target1;  // world-space normal in [-1,1] (xyz); w unused
};
