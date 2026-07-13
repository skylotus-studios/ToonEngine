//============================================================================
//  toon_common.hlsli — shared bindings for the toon fill + outline passes.
//
//  The cbuffer layout here MUST match toon::ShaderConstants in
//  core/renderer.cpp (same field order, 16-byte rows). Matrices are declared
//  row_major so Diligent's row-major float4x4 uploads verbatim (no transpose),
//  and vectors are transformed row-vector style: mul(v, M).
//============================================================================

// Cascaded shadow maps: DiligentFX's own shared structures + PCF sampling helpers
// (ShadowMapAttribs/CascadeAttribs, FilterShadowMap). Bare filenames, not full paths --
// DiligentFX's shaders reference each other the same way (see e.g. ComputeReprojectedDepth.fx's
// own "#include "BasicStructures.fxh""), and its embedded shader source factory
// (DiligentFXShaderSourceStreamFactory, folded into our shader factory as a compound one in
// Renderer::Init) resolves bare names, not full "Shaders/Common/public/..." paths.
#include "BasicStructures.fxh"
#include "Shadows.fxh"

cbuffer Constants
{
    row_major float4x4 g_WorldViewProj;     // object -> clip (this frame)
    row_major float4x4 g_World;             // object -> world (positions; outline extrude base)
    row_major float4x4 g_NormalMatrix;      // inverse-transpose of g_World. Its 3x3 transforms
                                            // normals correctly under NON-UNIFORM scale (plain
                                            // g_World would skew them); its 3x3 transpose is
                                            // world^-1, used to keep the outline width uniform.
    row_major float4x4 g_PrevWorldViewProj; // object -> clip (previous frame), for motion vectors
    row_major float4x4 g_PrevNormalMatrix;  // inverse-transpose of the PREVIOUS frame's world.
                                            // The outline VS needs this to extrude the shell it
                                            // rendered LAST frame for its own motion vector --
                                            // reusing g_NormalMatrix (this frame's) for both
                                            // under-reports motion during rotation, since the
                                            // extrude direction itself is rotation-dependent.

    float4 g_LightDir;   // xyz: normalized direction TO the light;  w unused
    float4 g_LightColor; // rgb: light color * intensity, premultiplied; w unused
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
    float3 WorldPos    : TEXCOORD3;  // world-space pos (shadow-map lookup)
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

// Cel (toon) shading: quantize the diffuse term N·L into `bands` flat levels spanning
// [0,1], floored by an `ambient` shadow term (keeps the dark side off pure black), then
// modulate the base color. Shared by the procedural fill and the glTF model fill.
// `shadow` (1 = lit, 0 = fully shadowed) multiplies N·L BEFORE quantizing, so a shadowed
// pixel just lands on a darker rung of the same band ladder N·L already uses -- shadow
// reads as "less light reached this surface," not a separate flat overlay color.
float3 CelShade(float3 baseRGB, float3 N, float3 L, float bands, float ambient, float shadow)
{
    float NdotL = saturate(dot(normalize(N), normalize(L))) * shadow;
    float b     = max(bands, 1.0);
    float ramp  = saturate(floor(NdotL * b) / max(b - 1.0, 1.0));
    float shade = lerp(ambient, 1.0, ramp);
    return baseRGB * shade;
}

// --- Cascaded shadow maps (Diligent's ShadowMapManager + Shadows.fxh's PCF path) ---
// Only toon_fill.hlsl / model_fill.hlsl call ComputeShadowFactor; toon_outline.hlsl /
// model_outline.hlsl #include this file too (for PSInput/CelShade/ComputeMotion) but never
// reference these declarations, so the shader compiler drops them from those PSOs' resource
// layout entirely -- no extra bindings needed on the outline SRBs.
cbuffer ShadowAttribsCB
{
    ShadowMapAttribs g_ShadowAttribs;
}

Texture2DArray<float>  g_ShadowMap;
SamplerComparisonState g_ShadowMap_sampler;

// Cascaded-shadow visibility for a surface point: 1 = fully lit, 0 = fully occluded.
// `cameraSpaceZ` picks the cascade (our projection's clip-space W IS camera-space Z, so
// callers pass pin.CurrClip.w -- no separate camera-view matrix needed in this cbuffer).
// `worldPos` is transformed into the shared light-facing space every cascade shares
// (ShadowMapAttribs.mWorldToLightView); each cascade then just scales/biases that shared
// space into its own extent (see Shadows.fxh's FindCascade).
float ComputeShadowFactor(float3 worldPos, float cameraSpaceZ)
{
    float3 posInLightViewSpace = mul(float4(worldPos, 1.0), g_ShadowAttribs.mWorldToLightView).xyz;
    float3 ddXPos = ddx(posInLightViewSpace);
    float3 ddYPos = ddy(posInLightViewSpace);
    FilteredShadow shadow = FilterShadowMap(g_ShadowAttribs, g_ShadowMap, g_ShadowMap_sampler, posInLightViewSpace,
                                            ddXPos, ddYPos, cameraSpaceZ);
    return shadow.fLightAmount;
}
