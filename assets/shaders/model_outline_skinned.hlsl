//============================================================================
//  model_outline_skinned.hlsl — inverted-hull outline for animated (skinned)
//  glTF models.
//
//  Same idea as model_outline.hlsl (extrude along the shading normal, since
//  models carry no smooth normal), but the position/normal it extrudes from is
//  first skinned through the current bone pose -- and, for the previous-frame
//  extrude PrevClip needs, through the previous bone pose too (see
//  model_fill_skinned.hlsl's header comment on why both poses are needed).
//============================================================================
#include "toon_common.hlsli"
#include "joints_common.hlsli"

cbuffer SkinConstants
{
    uint g_JointCount;
};

struct ModelVSInput
{
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1;
    float2 UV      : ATTRIB2; // unused here, but the input layout carries it
    float4 Joints  : ATTRIB3;
    float4 Weights : ATTRIB4;
};

struct OutlinePSInput
{
    float4 Pos         : SV_POSITION;
    float3 WorldNormal : TEXCOORD0;
    float4 CurrClip    : TEXCOORD1;
    float4 PrevClip    : TEXCOORD2;
};

OutlinePSInput VSMain(ModelVSInput vin)
{
    OutlinePSInput o;

    const uint4 joints = uint4(vin.Joints);
    const float4x4 skinCurr = SampleSkin(joints, vin.Weights, 0);
    const float4x4 skinPrev = SampleSkin(joints, vin.Weights, g_JointCount);

    // Current pose: skin position + normal in model space, then extrude along the
    // true world normal (world^-1 trick) -- exactly model_outline.hlsl's recipe,
    // just fed a posed position/normal instead of the raw bind-pose one.
    const float3 skinnedPos    = mul(float4(vin.Pos, 1.0), skinCurr).xyz;
    const float3 skinnedNormal = mul(float4(vin.Normal, 0.0), skinCurr).xyz;

    // Some glTF assets ship with no NORMAL accessor at all (see model_fill_skinned.hlsl's
    // PSMain comment -- confirmed directly for the Khronos Fox test model this roadmap
    // item's demo entity uses). A vertex shader has no screen-space derivatives to fall
    // back on the way the fill pass's pixel shader does, and normalize() of the resulting
    // zero vector is a divide-by-zero (NaN, not a harmless zero) that would corrupt the
    // extruded position. Skip the extrude entirely when that happens: the outline then
    // draws exactly where the fill will, and the fill's later, nearer-or-equal depth
    // overwrites it completely -- the same "invisible, not broken" outcome a zero-width
    // outline already produces elsewhere, just reached defensively instead of by width.
    const bool hasNormal = dot(skinnedNormal, skinnedNormal) > 1e-6;
    float3 worldNormal = float3(0.0, 0.0, 0.0);
    float3 inflated = skinnedPos;
    if (hasNormal) {
        worldNormal = normalize(mul(float4(skinnedNormal, 0.0), g_NormalMatrix).xyz);
        float3x3 worldInv = transpose((float3x3)g_NormalMatrix);
        inflated = skinnedPos + mul(worldNormal, worldInv) * g_Outline.w;
    }
    o.Pos         = mul(float4(inflated, 1.0), g_WorldViewProj);
    o.WorldNormal = hasNormal ? mul(float4(skinnedNormal, 0.0), g_NormalMatrix).xyz : float3(0.0, 1.0, 0.0);
    o.CurrClip    = o.Pos;

    // Previous pose: redo the same skin + extrude with last frame's bone matrices
    // AND last frame's normal matrix -- reusing the current frame's extrude here
    // would under-report motion from both the object's rotation (see
    // model_outline.hlsl's own PrevClip comment) and the bone motion itself.
    const float3 prevSkinnedPos    = mul(float4(vin.Pos, 1.0), skinPrev).xyz;
    const float3 prevSkinnedNormal = mul(float4(vin.Normal, 0.0), skinPrev).xyz;
    float3 prevInflated = prevSkinnedPos;
    if (dot(prevSkinnedNormal, prevSkinnedNormal) > 1e-6) {
        float3   prevWorldNormal = normalize(mul(float4(prevSkinnedNormal, 0.0), g_PrevNormalMatrix).xyz);
        float3x3 prevWorldInv    = transpose((float3x3)g_PrevNormalMatrix);
        prevInflated = prevSkinnedPos + mul(prevWorldNormal, prevWorldInv) * g_Outline.w;
    }
    o.PrevClip    = mul(float4(prevInflated, 1.0), g_PrevWorldViewProj);
    return o;
}

PSOutput PSMain(OutlinePSInput pin)
{
    PSOutput o;
    o.Color  = float4(g_Outline.rgb, 1.0);
    // Guard the same zero-normal case as VSMain (see its comment): normalize() of a zero
    // vector is NaN, not a harmless zero, and this feeds the SSAO normal G-buffer.
    float3 N = dot(pin.WorldNormal, pin.WorldNormal) > 1e-6 ? normalize(pin.WorldNormal) : float3(0.0, 1.0, 0.0);
    o.Normal = float4(N, g_Params.z);
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
