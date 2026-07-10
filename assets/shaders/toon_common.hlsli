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
    row_major float4x4 g_WorldViewProj;     // object -> clip (this frame)
    row_major float4x4 g_World;             // object -> world (positions; outline extrude base)
    row_major float4x4 g_NormalMatrix;      // inverse-transpose of g_World. Its 3x3 transforms
                                            // normals correctly under NON-UNIFORM scale (plain
                                            // g_World would skew them); its 3x3 transpose is
                                            // world^-1, used to keep the outline width uniform.
    row_major float4x4 g_PrevWorldViewProj; // object -> clip (previous frame), for motion vectors

    float4 g_LightDir;   // xyz: normalized direction TO the light;  w unused
    float4 g_BaseColor;  // rgb: material albedo;                    w unused
    float4 g_Outline;    // rgb: outline color;                      w: object-space extrude width
    float4 g_Params;     // x: shading bands; y: ambient floor; z: roughness (SSR); w unused
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
    float4 CurrClip    : TEXCOORD1;  // clip-space pos this frame (for motion vectors)
    float4 PrevClip    : TEXCOORD2;  // clip-space pos last frame
};

// The scene pass writes three targets (MRT), all read by DiligentFX post effects:
// the shaded color, a world-space normal G-buffer (SSAO), and a screen-space motion
// vector (SSAO temporal / DoF). Both toon passes (fill + outline) output this struct
// so their PSO render-target counts match the bound targets.
struct PSOutput
{
    float4 Color  : SV_Target0;  // HDR scene color
    float4 Normal : SV_Target1;  // world-space normal in [-1,1] (xyz); w: roughness (SSR)
    float2 Motion : SV_Target2;  // NDC-space (currNDC - prevNDC); DiligentFX scales to UV
};

// Screen-space motion vector for a pixel: the NDC displacement from last frame to
// this one. DiligentFX stores motion in NDC and applies the NDC->UV (0.5,-0.5) scale
// itself, so we hand it the raw delta.
float2 ComputeMotion(float4 currClip, float4 prevClip)
{
    float2 currNDC = currClip.xy / currClip.w;
    float2 prevNDC = prevClip.xy / prevClip.w;
    return currNDC - prevNDC;
}
