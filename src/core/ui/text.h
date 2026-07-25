#pragma once
//============================================================================
//  core/ui/text.h: MSDF font loading + text layout for the in-game UI (roadmap #17).
//
//  Diligent-free, above the renderer seam. LoadFont parses an msdf-atlas-gen atlas (a PNG +
//  JSON metrics pair) and AppendText turns a string into UIVertex glyph quads the renderer's
//  DrawUI draws onto the back buffer. The atlas today is a bootstrap single-channel SDF (see
//  tools/gen_ui_font.py, since msdf-atlas-gen isn't installed); a real msdf-atlas-gen "mtsdf"
//  atlas has the same schema and drops in unchanged (the median shader consumes both).
//============================================================================
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "core/rendering/renderer.h" // UIVertex, TextureHandle, Renderer

namespace toon {

    // One glyph's metrics (from the atlas JSON). Plane bounds are em units, baseline-relative,
    // y-up (as msdf-atlas-gen emits them); UVs are pre-converted to top-left texture origin.
    struct Glyph {
        float advance = 0.0f; // em; pen advance after this glyph
        float planeLeft = 0.0f, planeBottom = 0.0f, planeRight = 0.0f, planeTop = 0.0f;
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; // atlas UVs; (u0,v0) = top-left corner
        bool hasQuad = false;                             // false for whitespace (advance only)
    };

    // A loaded MSDF font: the atlas texture handle + per-codepoint metrics.
    struct Font {
        TextureHandle atlas = TextureHandle::Invalid;
        float pixelRange = 4.0f;                  // atlas distanceRange (texels), passed to DrawUI
        float lineHeight = 1.2f;                  // em (for multi-line layout later)
        float ascender = 0.8f, descender = -0.2f; // em
        std::unordered_map<uint32_t, Glyph> glyphs;
        bool valid() const { return atlas != TextureHandle::Invalid && !glyphs.empty(); }
    };

    // Load a font from an msdf-atlas-gen PNG + JSON pair (the PNG via the renderer, srgb=false:
    // an SDF atlas is sampled raw). Returns Font{} (valid()==false) on any failure, logging why.
    Font LoadFont(Renderer &renderer, const char *jsonPath, const char *pngPath);

    // Total advance width of `text` in pixels at `pixelSize` (the em -> px scale). ASCII only for
    // now: each byte is one codepoint (UTF-8 multi-byte decoding is a later concern, roadmap #32).
    float MeasureText(const Font &font, const std::string &text, float pixelSize);

    // Append `text`'s glyph quads to `out`, the pen starting at the baseline pixel (x, baselineY)
    // -- glyphs extend above baselineY by their ascent. `color` is straight RGBA. Returns the pen
    // x after the last glyph. Emits mode-1 (MSDF text) vertices.
    float AppendText(std::vector<UIVertex> &out, const Font &font, const std::string &text, float x,
                     float baselineY, float pixelSize, const Vec4 &color);

    // Append a solid-fill (mode-0) rect spanning pixels [x, x+w) x [y, y+h). A small convenience
    // for backgrounds/bars until the box system (next step) subsumes it.
    void AppendRect(std::vector<UIVertex> &out, float x, float y, float w, float h, const Vec4 &color);

    // Append a rounded rect (mode 2) spanning pixels [x, x+w) x [y, y+h): a `fill`-colored body with
    // `cornerRadius` corners and an inner `border`-px `borderColor` stroke, antialiased via the SDF
    // in the shader. border = 0 gives a plain rounded fill; cornerRadius = 0 gives a sharp rect with
    // a crisp edge; a zero-alpha `fill` gives a hollow outline.
    void AppendRoundRect(std::vector<UIVertex> &out, float x, float y, float w, float h, float cornerRadius,
                         float border, const Vec4 &fill, const Vec4 &borderColor);

} // namespace toon
