//============================================================================
//  core/ui/text.h implementation.
//============================================================================
#include "core/ui/text.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

namespace toon {

    Font LoadFont(Renderer &renderer, const char *jsonPath, const char *pngPath) {
        Font font;

        std::ifstream f(jsonPath ? jsonPath : "");
        if (!f) {
            std::fprintf(stderr, "LoadFont: cannot open %s\n", jsonPath ? jsonPath : "(null)");
            return font;
        }
        json root;
        try {
            root = json::parse(f);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "LoadFont: JSON parse error in %s: %s\n", jsonPath, e.what());
            return font;
        }

        const json atlasObj = root.value("atlas", json::object());
        const float atlasW = atlasObj.value("width", 0.0f);
        const float atlasH = atlasObj.value("height", 0.0f);
        font.pixelRange = atlasObj.value("distanceRange", 4.0f);
        if (atlasW <= 0.0f || atlasH <= 0.0f) {
            std::fprintf(stderr, "LoadFont: missing/zero atlas size in %s\n", jsonPath);
            return font;
        }
        const bool yBottom = atlasObj.value("yOrigin", std::string("bottom")) == "bottom";

        const json metrics = root.value("metrics", json::object());
        font.lineHeight = metrics.value("lineHeight", 1.2f);
        font.ascender = metrics.value("ascender", 0.8f);
        font.descender = metrics.value("descender", -0.2f);

        for (const auto &g : root.value("glyphs", json::array())) {
            Glyph gl;
            gl.advance = g.value("advance", 0.0f);
            if (g.contains("planeBounds") && g.contains("atlasBounds")) {
                const json &pb = g["planeBounds"];
                const json &ab = g["atlasBounds"];
                gl.planeLeft = pb.value("left", 0.0f);
                gl.planeBottom = pb.value("bottom", 0.0f);
                gl.planeRight = pb.value("right", 0.0f);
                gl.planeTop = pb.value("top", 0.0f);

                const float aLeft = ab.value("left", 0.0f);
                const float aRight = ab.value("right", 0.0f);
                const float aBottom = ab.value("bottom", 0.0f);
                const float aTop = ab.value("top", 0.0f);
                gl.u0 = aLeft / atlasW;
                gl.u1 = aRight / atlasW;
                // yOrigin "bottom": atlasBounds y is measured up from the atlas bottom, but the
                // texture samples top-left, so flip -- the glyph's top edge (the larger
                // y-from-bottom) becomes the smaller v.
                gl.v0 = yBottom ? 1.0f - aTop / atlasH : aTop / atlasH;    // top edge
                gl.v1 = yBottom ? 1.0f - aBottom / atlasH : aBottom / atlasH; // bottom edge
                gl.hasQuad = true;
            }
            font.glyphs[g.value("unicode", 0u)] = gl;
        }

        font.atlas = renderer.LoadTexture(pngPath, /*srgb=*/false); // SDF atlas: sampled raw
        if (font.atlas == TextureHandle::Invalid) {
            std::fprintf(stderr, "LoadFont: cannot load atlas image %s\n", pngPath ? pngPath : "(null)");
        }
        return font;
    }

    static const Glyph *FindGlyph(const Font &font, uint32_t codepoint) {
        const auto it = font.glyphs.find(codepoint);
        return it == font.glyphs.end() ? nullptr : &it->second;
    }

    float MeasureText(const Font &font, const std::string &text, float pixelSize) {
        float width = 0.0f;
        for (const char c : text) {
            const auto ch = static_cast<unsigned char>(c); // explicit: bytes index the glyph map
            if (const Glyph *g = FindGlyph(font, ch)) { width += g->advance * pixelSize; }
        }
        return width;
    }

    float AppendText(std::vector<UIVertex> &out, const Font &font, const std::string &text, float x,
                     float baselineY, float pixelSize, const Vec4 &color) {
        float pen = x;
        for (const char c : text) {
            const auto ch = static_cast<unsigned char>(c); // explicit: bytes index the glyph map
            const Glyph *g = FindGlyph(font, ch);
            if (!g) { continue; } // no glyph for this byte: skip (still no advance)
            if (g->hasQuad) {
                const float x0 = pen + g->planeLeft * pixelSize;
                const float x1 = pen + g->planeRight * pixelSize;
                // Plane y is up, screen y is down: baselineY is the baseline row, planeTop is
                // above it (smaller screen y), planeBottom below (larger screen y).
                const float y0 = baselineY - g->planeTop * pixelSize;    // top edge
                const float y1 = baselineY - g->planeBottom * pixelSize; // bottom edge
                const UIVertex tl{{x0, y0}, {g->u0, g->v0}, color, 1.0f};
                const UIVertex tr{{x1, y0}, {g->u1, g->v0}, color, 1.0f};
                const UIVertex br{{x1, y1}, {g->u1, g->v1}, color, 1.0f};
                const UIVertex bl{{x0, y1}, {g->u0, g->v1}, color, 1.0f};
                out.push_back(tl);
                out.push_back(tr);
                out.push_back(br);
                out.push_back(tl);
                out.push_back(br);
                out.push_back(bl);
            }
            pen += g->advance * pixelSize;
        }
        return pen;
    }

    void AppendRect(std::vector<UIVertex> &out, float x, float y, float w, float h, const Vec4 &color) {
        const UIVertex tl{{x, y}, {0.0f, 0.0f}, color, 0.0f};
        const UIVertex tr{{x + w, y}, {0.0f, 0.0f}, color, 0.0f};
        const UIVertex br{{x + w, y + h}, {0.0f, 0.0f}, color, 0.0f};
        const UIVertex bl{{x, y + h}, {0.0f, 0.0f}, color, 0.0f};
        out.push_back(tl);
        out.push_back(tr);
        out.push_back(br);
        out.push_back(tl);
        out.push_back(br);
        out.push_back(bl);
    }

    void AppendRoundRect(std::vector<UIVertex> &out, float x, float y, float w, float h, float cornerRadius,
                         float border, const Vec4 &fill, const Vec4 &borderColor) {
        const float hw = w * 0.5f, hh = h * 0.5f;
        const float radius = std::min(cornerRadius, std::min(hw, hh)); // radius can't exceed the half-size
        const Vec4 params{hw, hh, radius, border};
        // uv carries each corner's pixel offset from the rect center, so the PS gets the
        // interpolated local position for the rounded-box SDF (see ui.hlsl mode 2).
        const UIVertex tl{{x, y}, {-hw, -hh}, fill, 2.0f, params, borderColor};
        const UIVertex tr{{x + w, y}, {hw, -hh}, fill, 2.0f, params, borderColor};
        const UIVertex br{{x + w, y + h}, {hw, hh}, fill, 2.0f, params, borderColor};
        const UIVertex bl{{x, y + h}, {-hw, hh}, fill, 2.0f, params, borderColor};
        out.push_back(tl);
        out.push_back(tr);
        out.push_back(br);
        out.push_back(tl);
        out.push_back(br);
        out.push_back(bl);
    }

} // namespace toon
