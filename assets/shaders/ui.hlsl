//============================================================================
//  ui.hlsl -- screen-space in-game UI overlay (roadmap #17: in-game UI & HUD).
//
//  Self-contained (no toon_common.hlsli include), same "flat draw, no lighting/G-buffer" shape
//  as wireframe.hlsl. Drawn straight onto the resolved LDR back buffer AFTER EndScene (see
//  Renderer::DrawUI), never through bloom/tone-map -- the same target and timing the ImGui editor
//  overlay uses. Positions arrive in PIXELS (top-left origin) and are mapped to NDC here, so the
//  UI layer above the seam (core/ui/) works purely in pixels.
//
//  One vertex stream carries four primitive kinds, chosen per-vertex by Mode:
//    0 -- solid fill: emit Color.
//    1 -- MSDF text: sample a multi-channel signed-distance-field atlas; the median of the three
//         channels reconstructs the glyph edge (sharp corners a single-channel SDF would round),
//         scaled into screen space so it stays 1px-crisp at any font size.
//    2 -- rounded rect + SDF border: a rounded-box signed distance drives an antialiased fill
//         (Color) with an inner border stroke (BorderColor); Params = (halfW, halfH, radius,
//         borderThickness) and UV = the pixel offset from the rect center.
//    3 -- textured quad (9-slice panels/images): sample the bound texture at UV, times Color.
//============================================================================
cbuffer Constants
{
    float2 g_ScreenSize; // window size in pixels; maps Pos -> NDC in the VS
    float2 g_AtlasSize;   // MSDF atlas size in texels; for the screen-pixel-range below
    float  g_PixelRange;  // the atlas's MSDF distance range in texels (from its metrics)
    float3 g_Pad;
};

// _sampler suffix is Diligent's combined-texture-sampler convention (see sprite.hlsl's
// g_SpriteTex); bound DYNAMIC on one shared SRB, re-Set per DrawUI call (the font atlas, or a 1x1
// white fallback for a batch with no text), with an immutable linear-clamp sampler.
Texture2D    g_UIAtlas;
SamplerState g_UIAtlas_sampler;

struct VSInput
{
    float2 Pos         : ATTRIB0; // pixels, top-left origin
    float2 UV          : ATTRIB1; // atlas UV (text) / pixel offset from center (rounded rect)
    float4 Color       : ATTRIB2; // straight (non-premultiplied) RGBA fill/text color
    float  Mode        : ATTRIB3; // 0 = solid, 1 = MSDF text, 2 = rounded rect
    float4 Params      : ATTRIB4; // rounded rect: (halfW, halfH, cornerRadius, borderThickness)
    float4 BorderColor : ATTRIB5; // rounded rect: border color
};

struct VSOut
{
    float4 Pos         : SV_POSITION;
    float2 UV          : TEXCOORD0;
    float4 Color       : TEXCOORD1;
    float  Mode        : TEXCOORD2;
    float4 Params      : TEXCOORD3;
    float4 BorderColor : TEXCOORD4;
};

VSOut VSMain(VSInput vin)
{
    VSOut o;
    // Pixel -> NDC: x 0..w -> -1..+1; y 0..h -> +1..-1 (top of screen = +1, matching tonemap.hlsl's
    // full-screen triangle; Diligent applies the Vulkan y-flip in the backend).
    float2 ndc = float2(vin.Pos.x / g_ScreenSize.x * 2.0 - 1.0,
                        1.0 - vin.Pos.y / g_ScreenSize.y * 2.0);
    o.Pos = float4(ndc, 0.0, 1.0);
    o.UV = vin.UV;
    o.Color = vin.Color;
    o.Mode = vin.Mode;
    o.Params = vin.Params;
    o.BorderColor = vin.BorderColor;
    return o;
}

// The median of three -- the MSDF reconstruction operator (Chlumsky). For a single-channel SDF
// replicated across RGB (the bootstrap atlas) this is just that value, so one shader serves both.
float Median(float3 v) { return max(min(v.r, v.g), min(max(v.r, v.g), v.b)); }

// Signed distance to a rounded box centered at the origin (negative inside), half-size `b`, corner
// radius `r`. Inigo Quilez's rounded-box SDF.
float SdRoundBox(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float4 PSMain(VSOut i) : SV_Target
{
    if (i.Mode < 0.5)
    {
        return i.Color; // solid fill (blended SrcAlpha/InvSrcAlpha by the PSO)
    }
    if (i.Mode < 1.5)
    {
        // MSDF text: median distance -> per-pixel coverage. screenPxRange converts the atlas's
        // texel-space distance range into on-screen pixels (via fwidth of the UV), so the 1px-wide
        // antialiased edge is correct at any magnification. Standard msdf(gen) shader math.
        float sd = Median(g_UIAtlas.Sample(g_UIAtlas_sampler, i.UV).rgb);
        float2 unitRange = g_PixelRange / g_AtlasSize;
        float2 screenTexSize = 1.0 / fwidth(i.UV);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float coverage = saturate(screenPxRange * (sd - 0.5) + 0.5);
        return float4(i.Color.rgb, i.Color.a * coverage);
    }

    if (i.Mode < 2.5)
    {
        // Rounded rect + SDF border. UV is the pixel offset from the rect center, so fwidth gives a
        // ~1px antialiased edge regardless of size.
        float2 halfSize = i.Params.xy;
        float radius = i.Params.z;
        float border = i.Params.w;
        float d = SdRoundBox(i.UV, halfSize, radius);
        float aa = max(fwidth(d), 1e-4) * 0.7;
        float outer = 1.0 - smoothstep(-aa, aa, d);                     // coverage of the whole shape
        float inner = 1.0 - smoothstep(-border - aa, -border + aa, d); // coverage of the fill (inside the border)
        float3 rgb = lerp(i.BorderColor.rgb, i.Color.rgb, inner);
        float alpha = outer * lerp(i.BorderColor.a, i.Color.a, inner);
        return float4(rgb, alpha);
    }

    // Mode 3: plain textured quad (9-slice panels / images), tinted by Color. UV is a 0..1 texture
    // coord; the texture's own alpha (e.g. a panel frame's transparent corners) drives the blend.
    return g_UIAtlas.Sample(g_UIAtlas_sampler, i.UV) * i.Color;
}
