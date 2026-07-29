//============================================================================
//  core/ui/ui.h implementation: the Fleury immediate-over-cache UI core (roadmap #17).
//============================================================================
#include "core/ui/ui.h"

#include <algorithm>
#include <cstring>

namespace toon {

    namespace {

        // FNV-1a (64-bit), the same hash family the input system uses for action names. Combined
        // with the parent's key (below) so identical id strings under different parents differ.
        uint64_t Fnv1a(const char *s, std::size_t n) {
            uint64_t h = 1469598103934665603ull;
            for (std::size_t i = 0; i < n; ++i) {
                h ^= static_cast<unsigned char>(s[i]);
                h *= 1099511628211ull;
            }
            return h;
        }

        // Fold a child hash into a parent key; never returns 0 (0 is the nil key).
        uint64_t MixKey(uint64_t parent, uint64_t child) {
            uint64_t h = parent ^ (child + 0x9E3779B97F4A7C15ull + (parent << 6) + (parent >> 2));
            return h ? h : 1ull;
        }

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        Vec4 Lerp4(const Vec4 &a, const Vec4 &b, float t) {
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        }
        Vec4 Brighten(const Vec4 &c, float amt) {
            return {std::min(c.x + amt, 1.0f), std::min(c.y + amt, 1.0f), std::min(c.z + amt, 1.0f), c.w};
        }
        Vec4 Darken(const Vec4 &c, float amt) {
            return {std::max(c.x - amt, 0.0f), std::max(c.y - amt, 0.0f), std::max(c.z - amt, 0.0f), c.w};
        }

        // --- The semantic-size solve (run in dependency order by UI_EndBuild) ---------------

        // Pixels + TextContent: sizes that depend on nothing else.
        void SolveStandalone(UIContext &ui, UIBox *box) {
            for (int ax = 0; ax < 2; ++ax) {
                const UISize &s = box->semanticSize[ax];
                if (s.kind == UISizeKind::Pixels) {
                    box->computedSize[ax] = s.value;
                } else if (s.kind == UISizeKind::TextContent) {
                    float textPx = 0.0f;
                    if (ui.font) {
                        textPx = (ax == static_cast<int>(UIAxis::X)) ? MeasureText(*ui.font, box->text, box->fontSize)
                                                                     : ui.font->lineHeight * box->fontSize;
                    }
                    box->computedSize[ax] = textPx + 2.0f * box->padding;
                }
            }
            for (UIBox *c = box->first; c; c = c->next) { SolveStandalone(ui, c); }
        }

        // PercentOfParent: needs the parent sized first (pre-order). A percent-of-a-ChildrenSum
        // parent is a genuine cycle -- it reads whatever the parent has so far (best-effort);
        // menus size parents by Pixels/Percent, so the common cases are exact.
        void SolveUpward(UIContext &ui, UIBox *box) {
            for (int ax = 0; ax < 2; ++ax) {
                if (box->semanticSize[ax].kind == UISizeKind::PercentOfParent) {
                    const float parentSize =
                        box->parent ? box->parent->computedSize[ax] : (ax == 0 ? ui.screenW : ui.screenH);
                    box->computedSize[ax] = parentSize * box->semanticSize[ax].value;
                }
            }
            for (UIBox *c = box->first; c; c = c->next) { SolveUpward(ui, c); }
        }

        // ChildrenSum: needs the children sized first (post-order). Sum along the layout axis,
        // max across it, plus this box's padding on both sides.
        void SolveDownward(UIBox *box) {
            for (UIBox *c = box->first; c; c = c->next) { SolveDownward(c); }
            for (int ax = 0; ax < 2; ++ax) {
                if (box->semanticSize[ax].kind == UISizeKind::ChildrenSum) {
                    float total = 0.0f, largest = 0.0f;
                    for (UIBox *c = box->first; c; c = c->next) {
                        if (c->floating) { continue; } // floating boxes overlay; they don't size the parent
                        total += c->computedSize[ax];
                        largest = std::max(largest, c->computedSize[ax]);
                    }
                    const bool alongAxis = (ax == static_cast<int>(box->childLayoutAxis));
                    box->computedSize[ax] = (alongAxis ? total : largest) + 2.0f * box->padding;
                }
            }
        }

        // If the children overflow the box along the layout axis, shrink them -- each yields in
        // proportion to its size weighted by (1 - strictness), so a strictness-1 child never shrinks.
        void SolveViolations(UIBox *box) {
            const int ax = static_cast<int>(box->childLayoutAxis);
            const float content = box->computedSize[ax] - 2.0f * box->padding;
            float total = 0.0f;
            for (UIBox *c = box->first; c; c = c->next) {
                if (!c->floating) { total += c->computedSize[ax]; }
            }
            if (total > content && total > 0.0f) {
                const float overflow = total - content;
                float weight = 0.0f;
                for (UIBox *c = box->first; c; c = c->next) {
                    if (!c->floating) { weight += c->computedSize[ax] * (1.0f - c->semanticSize[ax].strictness); }
                }
                if (weight > 0.0f) {
                    for (UIBox *c = box->first; c; c = c->next) {
                        if (c->floating) { continue; }
                        const float w = c->computedSize[ax] * (1.0f - c->semanticSize[ax].strictness);
                        c->computedSize[ax] = std::max(0.0f, c->computedSize[ax] - overflow * (w / weight));
                    }
                }
            }
            for (UIBox *c = box->first; c; c = c->next) { SolveViolations(c); }
        }

        // Place a floating child by its anchor within the parent's rect, plus its offset.
        void AnchorPlace(const UIBox *parent, UIBox *c) {
            const float pw = parent->computedSize[0], ph = parent->computedSize[1];
            const float cw = c->computedSize[0], ch = c->computedSize[1];
            float ax = 0.0f, ay = 0.0f;
            switch (c->anchor) {
                case UIAnchor::TopLeft: ax = 0.0f; ay = 0.0f; break;
                case UIAnchor::Top: ax = (pw - cw) * 0.5f; ay = 0.0f; break;
                case UIAnchor::TopRight: ax = pw - cw; ay = 0.0f; break;
                case UIAnchor::Left: ax = 0.0f; ay = (ph - ch) * 0.5f; break;
                case UIAnchor::Center: ax = (pw - cw) * 0.5f; ay = (ph - ch) * 0.5f; break;
                case UIAnchor::Right: ax = pw - cw; ay = (ph - ch) * 0.5f; break;
                case UIAnchor::BottomLeft: ax = 0.0f; ay = ph - ch; break;
                case UIAnchor::Bottom: ax = (pw - cw) * 0.5f; ay = ph - ch; break;
                case UIAnchor::BottomRight: ax = pw - cw; ay = ph - ch; break;
            }
            c->rectMin[0] = parent->rectMin[0] + ax + c->anchorOffset[0];
            c->rectMin[1] = parent->rectMin[1] + ay + c->anchorOffset[1];
            c->rectMax[0] = c->rectMin[0] + cw;
            c->rectMax[1] = c->rectMin[1] + ch;
        }

        // Final placement: flow non-floating children along the layout axis from the content origin
        // (start-aligned across it); place floating children by their anchor instead.
        void SolvePositions(UIBox *box) {
            const int la = static_cast<int>(box->childLayoutAxis);
            const int oa = 1 - la;
            float pen = box->rectMin[la] + box->padding;
            const float offBase = box->rectMin[oa] + box->padding;
            for (UIBox *c = box->first; c; c = c->next) {
                if (c->floating) {
                    AnchorPlace(box, c);
                } else {
                    c->rectMin[la] = pen;
                    c->rectMin[oa] = offBase;
                    c->rectMax[0] = c->rectMin[0] + c->computedSize[0];
                    c->rectMax[1] = c->rectMin[1] + c->computedSize[1];
                    pen += c->computedSize[la];
                }
                SolvePositions(c);
            }
        }

        // --- Rendering (tree walk -> UIVertex batch) ----------------------------------------

        // Draw the accumulated batch with its currently-bound texture, then clear it.
        void FlushBatch(UIContext &ui, Renderer &renderer) {
            if (!ui.batch.empty() && ui.font) {
                renderer.DrawUI(ui.batch.data(), static_cast<uint32_t>(ui.batch.size()), ui.batchTexture,
                                ui.font->pixelRange);
                ui.batch.clear();
            }
        }

        // Switch the batch's bound texture: flush what's queued (drawn with the OLD texture) so
        // painter's order across textures is preserved, then bind the new one. Rounded/solid quads
        // don't sample, so they ride whatever batch is current; only text + 9-slice pin a texture.
        void SetBatchTexture(UIContext &ui, Renderer &renderer, TextureHandle tex) {
            if (tex == ui.batchTexture) { return; }
            FlushBatch(ui, renderer);
            ui.batchTexture = tex;
        }

        void RenderBox(UIContext &ui, Renderer &renderer, UIBox *box) {
            const float x0 = box->rectMin[0], y0 = box->rectMin[1];
            const float w = box->computedSize[0], h = box->computedSize[1];

            // Background: a 9-slice texture (pins its own texture) OR a rounded rect (texture-agnostic).
            if (box->bgTexture != TextureHandle::Invalid) {
                Vec4 tint = box->bgColor;
                if (box->flags & UIBoxFlag_HotAnimation) { tint = Lerp4(tint, Brighten(tint, 0.12f), box->hotT); }
                if (box->flags & UIBoxFlag_ActiveAnimation) { tint = Lerp4(tint, Darken(tint, 0.16f), box->activeT); }
                SetBatchTexture(ui, renderer, box->bgTexture);
                uint32_t tw = 0, th = 0;
                renderer.GetTextureSize(box->bgTexture, tw, th);
                AppendNineSlice(ui.batch, x0, y0, w, h, static_cast<float>(tw), static_cast<float>(th),
                                box->nineSliceInsets, tint);
            } else {
                const bool hasBg = (box->flags & UIBoxFlag_DrawBackground) != 0;
                const bool hasBorder = (box->flags & UIBoxFlag_DrawBorder) != 0 && box->borderThickness > 0.0f;
                if (hasBg || hasBorder) {
                    Vec4 bg = box->bgColor;
                    if (box->flags & UIBoxFlag_HotAnimation) { bg = Lerp4(bg, Brighten(bg, 0.12f), box->hotT); }
                    if (box->flags & UIBoxFlag_ActiveAnimation) { bg = Lerp4(bg, Darken(bg, 0.16f), box->activeT); }
                    // One rounded-rect quad carries fill + border together (a 0-alpha fill leaves a
                    // hollow outline; a 0 thickness leaves a plain rounded fill).
                    const Vec4 fill = hasBg ? bg : Vec4{0.0f, 0.0f, 0.0f, 0.0f};
                    const Vec4 bord = hasBorder ? box->borderColor : Vec4{0.0f, 0.0f, 0.0f, 0.0f};
                    const float thick = hasBorder ? box->borderThickness : 0.0f;
                    AppendRoundRect(ui.batch, x0, y0, w, h, box->cornerRadius, thick, fill, bord);
                }
            }

            if ((box->flags & UIBoxFlag_DrawText) && !box->text.empty() && ui.font) {
                SetBatchTexture(ui, renderer, ui.font->atlas); // text samples the font atlas
                const float textW = MeasureText(*ui.font, box->text, box->fontSize);
                const float tx = (box->flags & UIBoxFlag_TextCenterX) ? x0 + (w - textW) * 0.5f : x0 + box->padding;
                float baseline;
                if (box->flags & UIBoxFlag_TextCenterY) {
                    // Center the glyph band (baseline..ascender/descender) on the box's mid-line.
                    baseline = y0 + h * 0.5f + (ui.font->ascender + ui.font->descender) * 0.5f * box->fontSize;
                } else {
                    baseline = y0 + box->padding + ui.font->ascender * box->fontSize;
                }
                AppendText(ui.batch, *ui.font, box->text, tx, baseline, box->fontSize, box->textColor);
            }

            for (UIBox *c = box->first; c; c = c->next) { RenderBox(ui, renderer, c); }
        }

        // Pick the focusable box (from ui.navCandidates, last frame's set) best in direction
        // (dx,dy) from `from`'s center: nearest along the direction, penalizing off-axis offset.
        UIKey NavPick(UIContext &ui, UIKey from, int dx, int dy) {
            const auto fit = ui.cache.find(from);
            if (fit == ui.cache.end()) { return from; }
            const UIBox &fb = fit->second;
            const float fcx = (fb.rectMin[0] + fb.rectMax[0]) * 0.5f;
            const float fcy = (fb.rectMin[1] + fb.rectMax[1]) * 0.5f;
            UIKey best = from;
            float bestScore = 1e30f;
            for (const UIKey k : ui.navCandidates) {
                if (k == from) { continue; }
                const auto it = ui.cache.find(k);
                if (it == ui.cache.end()) { continue; }
                const UIBox &b = it->second;
                const float cx = (b.rectMin[0] + b.rectMax[0]) * 0.5f;
                const float cy = (b.rectMin[1] + b.rectMax[1]) * 0.5f;
                const float ddx = cx - fcx, ddy = cy - fcy;
                const float along = ddx * static_cast<float>(dx) + ddy * static_cast<float>(dy);
                if (along <= 1.0f) { continue; } // not in the requested direction
                const float perp = std::abs(ddx * static_cast<float>(dy) - ddy * static_cast<float>(dx));
                const float score = along + perp * 2.0f; // prefer close-along, small-off-axis
                if (score < bestScore) {
                    bestScore = score;
                    best = k;
                }
            }
            return best;
        }

    } // namespace

    // --- Lifecycle --------------------------------------------------------------

    void UI_BeginBuild(UIContext &ui, const UIInput &input, float dt, float screenW, float screenH,
                       const Font &font) {
        ++ui.buildIndex;
        ui.prevInput = ui.input;
        ui.input = input;
        ui.dt = dt;
        ui.screenW = screenW;
        ui.screenH = screenH;
        ui.font = &font;
        ui.hotKey = 0; // recomputed from hover each frame; activeKey persists across frames

        // Move directional-nav focus using LAST frame's focusable set (navCandidates) + their cached
        // rects, so the new navKey is live for THIS frame's signals and highlight; then clear it for
        // this frame's collection (UI_SignalFromBox repopulates it).
        {
            bool valid = false;
            for (const UIKey k : ui.navCandidates) {
                if (k == ui.navKey) {
                    valid = true;
                    break;
                }
            }
            if (!valid) {
                ui.navKey = ui.navCandidates.empty() ? 0 : ui.navCandidates.front();
            } else if (input.navUp) {
                ui.navKey = NavPick(ui, ui.navKey, 0, -1);
            } else if (input.navDown) {
                ui.navKey = NavPick(ui, ui.navKey, 0, 1);
            } else if (input.navLeft) {
                ui.navKey = NavPick(ui, ui.navKey, -1, 0);
            } else if (input.navRight) {
                ui.navKey = NavPick(ui, ui.navKey, 1, 0);
            }
        }
        ui.navCandidates.clear();

        ui.parentStack.clear();
        ui.styleStack.clear();
        ui.styleStack.push_back(UIStyle{});

        // The full-screen root: a stable key so it (and thus its transform of the tree) persists.
        const UIKey rootKey = MixKey(0, Fnv1a("###root", 7));
        UIBox &root = ui.cache[rootKey];
        root.key = rootKey;
        root.first = root.last = root.next = root.prev = root.parent = nullptr;
        root.childCount = 0;
        root.flags = 0;
        root.semanticSize[0] = {UISizeKind::Pixels, screenW, 1.0f};
        root.semanticSize[1] = {UISizeKind::Pixels, screenH, 1.0f};
        root.childLayoutAxis = UIAxis::Y;
        root.padding = 0.0f;
        root.borderThickness = 0.0f;
        root.text.clear();
        root.lastBuildIndex = ui.buildIndex;
        ui.root = &root;
        ui.parentStack.push_back(&root);
    }

    void UI_EndBuild(UIContext &ui) {
        // Prune boxes not touched this build (erasing from the node-based map keeps every other
        // box's address stable, so the live tree built this frame is unaffected).
        const size_t sizeBeforePrune = ui.cache.size();
        for (auto it = ui.cache.begin(); it != ui.cache.end();) {
            it = (it->second.lastBuildIndex != ui.buildIndex) ? ui.cache.erase(it) : std::next(it);
        }
        // app/metrics.h's ui.boxes_live/boxes_pruned (--headless-render only; RenderHUD never
        // runs under --sim-only, so these stay at their zero default there).
        ui.boxesLive = static_cast<uint32_t>(ui.cache.size());
        ui.boxesPruned = static_cast<uint32_t>(sizeBeforePrune - ui.cache.size());

        if (!ui.root) { return; }

        SolveStandalone(ui, ui.root);
        SolveUpward(ui, ui.root);
        SolveDownward(ui.root);
        SolveViolations(ui.root);

        ui.root->rectMin[0] = 0.0f;
        ui.root->rectMin[1] = 0.0f;
        ui.root->rectMax[0] = ui.root->computedSize[0];
        ui.root->rectMax[1] = ui.root->computedSize[1];
        SolvePositions(ui.root);
    }

    void UI_Render(UIContext &ui, Renderer &renderer) {
        ui.batch.clear();
        if (!ui.font) { return; }
        ui.batchTexture = ui.font->atlas; // batches default to the font atlas; 9-slice boxes switch it
        if (ui.root) { RenderBox(ui, renderer, ui.root); }
        FlushBatch(ui, renderer); // draw the final (or only) batch
    }

    // --- Building ---------------------------------------------------------------

    UIBox *UI_MakeBox(UIContext &ui, uint32_t flags, const char *idString) {
        UIBox *parent = ui.parentStack.empty() ? nullptr : ui.parentStack.back();
        const UIKey parentKey = parent ? parent->key : 0;

        UIKey key;
        if (idString && idString[0]) {
            key = MixKey(parentKey, Fnv1a(idString, std::strlen(idString)));
        } else {
            // Anonymous: unique within the parent by child index. Fine for non-interactive boxes;
            // an interactive one wants a stable id so its hover/press state survives a reorder.
            const uint64_t idx = parent ? static_cast<uint64_t>(parent->childCount + 1) : 1ull;
            key = MixKey(parentKey, idx * 0x9E3779B97F4A7C15ull);
        }

        UIBox &box = ui.cache[key]; // find-or-create; addresses stay stable across inserts
        box.key = key;

        // Reset this-frame state; the persistent block (hotT/activeT + last frame's rect, which
        // UI_SignalFromBox is about to hit-test) is deliberately left intact.
        box.first = box.last = box.next = box.prev = nullptr;
        box.childCount = 0;
        box.flags = flags;
        box.lastBuildIndex = ui.buildIndex;
        box.text.clear();
        box.floating = false;
        box.anchor = UIAnchor::TopLeft;
        box.anchorOffset[0] = box.anchorOffset[1] = 0.0f;
        box.bgTexture = TextureHandle::Invalid;
        box.nineSliceInsets = Vec4{};

        const UIStyle &st = ui.styleStack.back();
        box.semanticSize[0] = st.size[0];
        box.semanticSize[1] = st.size[1];
        box.childLayoutAxis = st.childLayoutAxis;
        box.bgColor = st.bgColor;
        box.borderColor = st.borderColor;
        box.textColor = st.textColor;
        box.fontSize = st.fontSize;
        box.padding = st.padding;
        box.borderThickness = st.borderThickness;
        box.cornerRadius = st.cornerRadius;

        if (parent) {
            box.parent = parent;
            if (parent->last) {
                parent->last->next = &box;
                box.prev = parent->last;
                parent->last = &box;
            } else {
                parent->first = parent->last = &box;
            }
            ++parent->childCount;
        }
        return &box;
    }

    UISignal UI_SignalFromBox(UIContext &ui, UIBox *box) {
        UISignal sig;
        if (!box) { return sig; }

        // Hit-test against the box's CURRENT rect, which still holds LAST frame's layout (the
        // solve overwrites it in UI_EndBuild) -- the immediate-mode-over-cache trick.
        const float mx = ui.input.mouseX, my = ui.input.mouseY;
        const bool inside =
            mx >= box->rectMin[0] && mx <= box->rectMax[0] && my >= box->rectMin[1] && my <= box->rectMax[1];
        sig.hovering = inside;
        if (inside) { ui.hotKey = box->key; }

        const bool focused = (box->flags & UIBoxFlag_Clickable) && ui.navKey == box->key;
        sig.focused = focused;

        if (box->flags & UIBoxFlag_Clickable) {
            ui.navCandidates.push_back(box->key); // a focusable target for next frame's nav scoring
            if (inside && ui.input.mouseDown && !ui.prevInput.mouseDown) {
                sig.pressed = true;
                ui.activeKey = box->key;
            }
            sig.held = (ui.activeKey == box->key);
            if (sig.held && !ui.input.mouseDown) {
                sig.released = true;
                sig.clicked = inside; // released while still over the box == a click
                ui.activeKey = 0;
            }
            if (focused && ui.input.navConfirm) {
                sig.pressed = true;
                sig.clicked = true; // gamepad/keyboard confirm on the focused box == a click
            }
        }

        // Ease hover/focus + press toward their targets (frame-rate-independent-ish step).
        const float rate = Clamp01(ui.dt * 14.0f);
        const bool highlight = inside || focused;
        box->hotT += (highlight ? 1.0f - box->hotT : -box->hotT) * rate;
        const float activeTarget = (ui.activeKey == box->key || (focused && ui.input.navConfirm)) ? 1.0f : 0.0f;
        box->activeT += (activeTarget - box->activeT) * rate;
        return sig;
    }

    void UI_PushParent(UIContext &ui, UIBox *box) {
        if (box) { ui.parentStack.push_back(box); }
    }
    void UI_PopParent(UIContext &ui) {
        if (ui.parentStack.size() > 1) { ui.parentStack.pop_back(); } // never pop the root
    }
    void UI_PushStyle(UIContext &ui, const UIStyle &style) { ui.styleStack.push_back(style); }
    UIStyle &UI_TopStyle(UIContext &ui) { return ui.styleStack.back(); }
    void UI_PopStyle(UIContext &ui) {
        if (ui.styleStack.size() > 1) { ui.styleStack.pop_back(); }
    }

    // --- Widget helpers ---------------------------------------------------------

    UIBox *UI_Panel(UIContext &ui, const char *idString) {
        UIBox *b = UI_MakeBox(ui, UIBoxFlag_DrawBackground | UIBoxFlag_DrawBorder, idString);
        if (b->borderThickness <= 0.0f) { b->borderThickness = 1.5f; }
        return b;
    }

    UIBox *UI_Label(UIContext &ui, const char *text) {
        UIBox *b = UI_MakeBox(ui, UIBoxFlag_DrawText, ""); // non-interactive: anonymous key is fine
        b->text = text ? text : "";
        b->semanticSize[0] = {UISizeKind::TextContent, 0.0f, 1.0f};
        b->semanticSize[1] = {UISizeKind::TextContent, 0.0f, 1.0f};
        return b;
    }

    UISignal UI_Button(UIContext &ui, const char *label) {
        UIBox *b = UI_MakeBox(ui,
                              UIBoxFlag_Clickable | UIBoxFlag_DrawBackground | UIBoxFlag_DrawBorder |
                                  UIBoxFlag_DrawText | UIBoxFlag_HotAnimation | UIBoxFlag_ActiveAnimation |
                                  UIBoxFlag_TextCenterX | UIBoxFlag_TextCenterY,
                              label);
        b->text = label ? label : "";
        b->padding = 12.0f; // roomier than the default so the label isn't cramped
        if (b->borderThickness <= 0.0f) { b->borderThickness = 1.5f; }
        b->semanticSize[0] = {UISizeKind::TextContent, 0.0f, 1.0f};
        b->semanticSize[1] = {UISizeKind::TextContent, 0.0f, 1.0f};
        return UI_SignalFromBox(ui, b);
    }

    UIBox *UI_Spacer(UIContext &ui, UIAxis axis, float px) {
        UIBox *b = UI_MakeBox(ui, UIBoxFlag_None, "");
        b->semanticSize[static_cast<int>(axis)] = {UISizeKind::Pixels, px, 1.0f};
        b->semanticSize[1 - static_cast<int>(axis)] = {UISizeKind::Pixels, 0.0f, 0.0f};
        return b;
    }

    void UI_Anchor(UIBox *box, UIAnchor anchor, float offsetX, float offsetY) {
        if (!box) { return; }
        box->floating = true;
        box->anchor = anchor;
        box->anchorOffset[0] = offsetX;
        box->anchorOffset[1] = offsetY;
    }

    void UI_NineSlice(UIBox *box, TextureHandle tex, const Vec4 &insets) {
        if (!box) { return; }
        box->bgTexture = tex;
        box->nineSliceInsets = insets;
    }

} // namespace toon
