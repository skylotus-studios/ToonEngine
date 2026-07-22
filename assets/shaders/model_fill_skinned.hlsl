//============================================================================
//  model_fill_skinned.hlsl — cel fill for animated (skinned) glTF models.
//
//  Same as model_fill.hlsl, but each vertex is blended through its up-to-4
//  influencing bones (see joints_common.hlsli) before the usual WVP transform —
//  once at the current pose and once at the previous, since motion vectors on an
//  animated character need the bone motion, not just the object's own world-matrix
//  motion (see Renderer::DrawModel).
//============================================================================
#include "toon_common.hlsli"
#include "joints_common.hlsli"

// Base-color (albedo) texture — same combined-sampler setup as model_fill.hlsl.
Texture2DArray g_Albedo;
SamplerState   g_Albedo_sampler;

// How many joints one frame's worth of g_Joints holds. The previous frame's palette
// starts right after the current frame's, at g_Joints[g_JointCount + i].
cbuffer SkinConstants
{
    uint g_JointCount;
};

// Buffer 0: pos/normal/uv (same packing as model_fill.hlsl). Buffer 1: JOINTS_0/
// WEIGHTS_0, interleaved (see Renderer::ModelVertexAttribs / CreateSkinnedModelPipeline).
struct ModelVSInput
{
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1;
    float2 UV      : ATTRIB2;
    float4 Joints  : ATTRIB3; // JOINTS_0 -- glTF stores these as floats, cast to index
    float4 Weights : ATTRIB4; // WEIGHTS_0
};

struct ModelPSInput
{
    float4 Pos         : SV_POSITION;
    float3 WorldNormal : TEXCOORD0;
    float2 UV          : TEXCOORD1;
    float4 CurrClip    : TEXCOORD2; // clip pos this frame (motion vectors)
    float4 PrevClip    : TEXCOORD3; // clip pos last frame
    float3 WorldPos    : TEXCOORD4; // world-space pos (shadow-map lookup)
};

ModelPSInput VSMain(ModelVSInput vin)
{
    ModelPSInput o;

    const uint4 joints = uint4(vin.Joints);
    const float4x4 skinCurr = SampleSkin(joints, vin.Weights, 0);
    const float4x4 skinPrev = SampleSkin(joints, vin.Weights, g_JointCount);

    // Skin in model space first (bind pose -> current pose), same recipe
    // ToonEngineOld's model.vert used (mat3(skin) for the normal -- bones are
    // rigid, so skipping an inverse-transpose here is the same accepted shortcut).
    // g_World/g_WorldViewProj then carry the skinned position the rest of the way,
    // exactly as they already do for the unskinned path.
    const float3 skinnedPos     = mul(float4(vin.Pos, 1.0), skinCurr).xyz;
    const float3 skinnedNormal  = mul(float4(vin.Normal, 0.0), skinCurr).xyz;
    const float3 prevSkinnedPos = mul(float4(vin.Pos, 1.0), skinPrev).xyz;

    o.Pos         = mul(float4(skinnedPos, 1.0), g_WorldViewProj);
    o.WorldNormal = mul(float4(skinnedNormal, 0.0), g_NormalMatrix).xyz; // inverse-transpose (non-uniform scale)
    o.UV          = vin.UV;
    o.CurrClip    = o.Pos;
    o.PrevClip    = mul(float4(prevSkinnedPos, 1.0), g_PrevWorldViewProj);
    o.WorldPos    = mul(float4(skinnedPos, 1.0), g_World).xyz;
    return o;
}

PSOutput PSMain(ModelPSInput pin)
{
    // Some glTF assets (e.g. the Khronos Fox test model this roadmap item's demo entity
    // uses) ship with no NORMAL accessor at all -- confirmed directly in fox.glb's own
    // JSON ("attributes":{"POSITION":...,"JOINTS_0":...,"WEIGHTS_0":...}, no "NORMAL" key).
    // Diligent's loader leaves that vertex data zeroed rather than synthesizing one, so a
    // zero-length WorldNormal reaches here unchanged for a source primitive genuinely
    // missing this data (not a skinning bug -- position/UV/joints/weights all read
    // correctly for the same vertex). Fall back to a flat, per-triangle normal
    // reconstructed from screen-space position derivatives: a standard technique for
    // exactly this "no authored normal" case, matching the glTF spec's own guidance to
    // calculate flat normals when none are provided. Faceted, not smooth, but correct
    // and non-degenerate, and free (no extra vertex data, no CPU-side mesh processing).
    float3 N;
    if (dot(pin.WorldNormal, pin.WorldNormal) > 1e-6) {
        N = normalize(pin.WorldNormal);
    } else {
        N = normalize(cross(ddx(pin.WorldPos), ddy(pin.WorldPos)));
    }

    float shadow  = ComputeShadowFactor(pin.WorldPos, pin.CurrClip.w);
    float3 albedo = g_Albedo.Sample(g_Albedo_sampler, float3(pin.UV, 0.0)).rgb * g_BaseColor.rgb;

    PSOutput o;
    o.Color  = float4(CelShade(albedo, N, g_LightDir.xyz, g_Params.x, g_Params.y, shadow) * g_LightColor.rgb, 1.0);
    o.Normal = float4(N, g_Params.z);
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
