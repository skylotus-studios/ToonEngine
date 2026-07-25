#pragma once
//============================================================================
//  core/ui/ui.h: the in-game UI core (roadmap #17), Ryan Fleury's model -- an
//  immediate-mode API over a persistent, per-frame-pruned box cache keyed by hashed IDs.
//
//  "Immediate mode is about the API, not the internals." Builder code re-issues the whole UI
//  every frame (UI_Panel/UI_Button/... between UI_BeginBuild and UI_EndBuild); underneath, each
//  box is looked up in a persistent cache by a key hashed from its id string + its parent, so
//  per-box state (hover/press animation, and last frame's laid-out rect, which is what this
//  frame's input hit-tests against) survives across frames. Boxes not touched in a frame are
//  pruned. Layout is a semantic-size solve (Pixels/TextContent/PercentOfParent/ChildrenSum).
//
//  Plain structs + free functions, not a class: the same data-oriented shape as Scene/
//  EditorState, and Diligent-free -- it reaches the GPU only through Renderer::DrawUI, and text
//  through core/ui/text.h. One UIContext persists across frames (the cache lives on it); the
//  editor and the standalone player will both drive it through the shared RenderHUD (next step).
//============================================================================
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "core/rendering/renderer.h" // UIVertex, TextureHandle, Renderer
#include "core/ui/text.h"            // Font (text sizing + rendering)

namespace toon {

    using UIKey = uint64_t; // 0 = nil; every live box has a non-zero key (see UI_MakeBox)

    enum class UIAxis { X = 0, Y = 1 };

    // Where a floating box pins itself within its parent (see UIBox::floating / UI_Anchor). The
    // stopgap anchoring for HUD/menu placement until a fuller anchor system lands.
    enum class UIAnchor {
        TopLeft,
        Top,
        TopRight,
        Left,
        Center,
        Right,
        BottomLeft,
        Bottom,
        BottomRight,
    };

    // How a box's size along one axis is computed. The layout solve resolves these in
    // dependency order (see ui.cpp's UI_EndBuild).
    enum class UISizeKind {
        Pixels,          // exactly `value` px
        TextContent,     // text advance (X) / line height (Y), plus the box's padding on each side
        PercentOfParent, // `value` (0..1) of the parent's size on this axis
        ChildrenSum,     // sum of children (along the layout axis) / max (across it), + padding
    };

    struct UISize {
        UISizeKind kind = UISizeKind::ChildrenSum;
        float value = 0.0f;      // px (Pixels), 0..1 fraction (PercentOfParent); unused for the others
        float strictness = 1.0f; // 0 = fully yields when over-constrained, 1 = never shrinks
    };

    // Composable box behaviour flags -- the "widget-building language": a widget is a box with
    // some of these set plus child boxes, not a bespoke type.
    enum UIBoxFlags : uint32_t {
        UIBoxFlag_None = 0,
        UIBoxFlag_Clickable = 1u << 0,       // participates in UISignal hit-testing
        UIBoxFlag_DrawBackground = 1u << 1,  // fill the rect with bgColor
        UIBoxFlag_DrawBorder = 1u << 2,      // stroke the rect edge with borderColor
        UIBoxFlag_DrawText = 1u << 3,        // draw `text` (MSDF) in textColor
        UIBoxFlag_HotAnimation = 1u << 4,    // brighten the background toward hover (hotT)
        UIBoxFlag_ActiveAnimation = 1u << 5, // darken the background toward press (activeT)
        UIBoxFlag_TextCenterX = 1u << 6,     // else left-aligned (+ padding)
        UIBoxFlag_TextCenterY = 1u << 7,     // else baseline near the top (+ padding + ascent)
    };

    // One cached UI box. Hierarchy links are rebuilt every frame; the "persistent" block at the
    // bottom (hover/press animation + last frame's rect) is what survives, keyed by `key`.
    struct UIBox {
        UIKey key = 0;

        // Hierarchy for THIS frame (rebuilt each build).
        UIBox *first = nullptr, *last = nullptr, *next = nullptr, *prev = nullptr, *parent = nullptr;
        int childCount = 0;

        // Per-build configuration (set by the builder, reset each frame).
        uint32_t flags = 0;
        UISize semanticSize[2];              // [X], [Y]
        UIAxis childLayoutAxis = UIAxis::Y;  // children stack down by default
        Vec4 bgColor{}, borderColor{}, textColor{};
        float fontSize = 24.0f;
        float padding = 0.0f;       // inner padding (px): content inset + TextContent margin
        float borderThickness = 0.0f;
        float cornerRadius = 0.0f;  // rounded corners (px); 0 = sharp
        // 9-slice textured background: when bgTexture is set, RenderBox draws a 9-slice of it (split
        // by nineSliceInsets l/t/r/b px) instead of the rounded-rect bg/border. See UI_NineSlice.
        TextureHandle bgTexture = TextureHandle::Invalid;
        Vec4 nineSliceInsets{};
        std::string text;           // display string (already localized by the caller)

        // Floating placement: a floating box pins to `anchor` (+ anchorOffset) within its parent
        // rather than flowing among siblings, and doesn't consume flow space -- it overlays. Used
        // to center a menu / pin a HUD element (see UI_Anchor).
        bool floating = false;
        UIAnchor anchor = UIAnchor::TopLeft;
        float anchorOffset[2] = {0.0f, 0.0f};

        // Computed by the layout solve; rect PERSISTS to next frame, where UI_SignalFromBox
        // hit-tests this frame's input against it before the solve overwrites it.
        float computedSize[2] = {0.0f, 0.0f};
        float computedRelPos[2] = {0.0f, 0.0f}; // relative to the parent's origin
        float rectMin[2] = {0.0f, 0.0f};        // absolute screen px
        float rectMax[2] = {0.0f, 0.0f};

        // Persistent per-box state.
        float hotT = 0.0f, activeT = 0.0f; // eased hover / press, 0..1
        uint64_t lastBuildIndex = 0;       // != ctx.buildIndex after a build -> pruned
    };

    // The result of hit-testing a box against this frame's input (computed from last frame's
    // rect). `clicked` is the common one: a press+release cycle completed over the box.
    struct UISignal {
        bool hovering = false;
        bool pressed = false;  // mouse went down over the box this frame
        bool held = false;     // this box currently owns the press (mouse still down)
        bool released = false; // the press this box owned ended this frame
        bool clicked = false;  // released while still hovering, OR nav-confirmed while focused
        bool focused = false;  // has directional-nav (gamepad/keyboard) focus this frame
    };

    // Default look/layout applied to each new box; push a modified copy to theme a subtree.
    struct UIStyle {
        Vec4 bgColor{0.16f, 0.17f, 0.20f, 1.0f};
        Vec4 borderColor{0.35f, 0.37f, 0.42f, 1.0f};
        Vec4 textColor{0.92f, 0.93f, 0.96f, 1.0f};
        float fontSize = 24.0f;
        float padding = 6.0f;
        float borderThickness = 0.0f;
        float cornerRadius = 6.0f; // panels/buttons get rounded corners by default
        UIAxis childLayoutAxis = UIAxis::Y;
        UISize size[2] = {{UISizeKind::ChildrenSum, 0.0f, 1.0f}, {UISizeKind::ChildrenSum, 0.0f, 1.0f}};
    };

    // This frame's pointer input (the runtime/editor fills it before UI_BeginBuild). Directional
    // gamepad/keyboard focus nav lands when the menus are wired (next step).
    struct UIInput {
        float mouseX = 0.0f, mouseY = 0.0f;
        bool mouseDown = false; // left button held

        // Directional focus nav, edge-triggered by the caller (gamepad d-pad / keyboard arrows).
        bool navUp = false, navDown = false, navLeft = false, navRight = false;
        bool navConfirm = false; // A / Enter: activate the focused box (reads as a click)
        bool navCancel = false;  // B / Escape: back out (menus read this field themselves)
    };

    // The persistent UI state: the box cache + this-frame build scratch. One instance lives on
    // RuntimeState (shared by editor and player). Plain data -- callers touch fields directly.
    struct UIContext {
        std::unordered_map<UIKey, UIBox> cache; // persistent; &cache[key] stays valid across inserts
        std::vector<UIBox *> parentStack;
        std::vector<UIStyle> styleStack;
        UIBox *root = nullptr;
        uint64_t buildIndex = 0;
        float screenW = 0.0f, screenH = 0.0f;
        float dt = 0.0f;

        UIInput input, prevInput; // prevInput = last frame's, for press/release edges
        UIKey hotKey = 0;         // hovered box this frame
        UIKey activeKey = 0;      // box that owns the current press (across frames until release)
        UIKey navKey = 0;         // directional-nav focused box (persists across frames)
        std::vector<UIKey> navCandidates; // this frame's focusable (Clickable) boxes, for nav scoring

        const Font *font = nullptr;  // text sizing + rendering
        std::vector<UIVertex> batch; // built by UI_Render, drawn via Renderer::DrawUI
        TextureHandle batchTexture = TextureHandle::Invalid; // texture bound for the in-flight batch
    };

    // --- Lifecycle --------------------------------------------------------------
    // Begin a frame: bump the prune counter, reset the stacks, (re)make the full-screen root and
    // push it as the current parent. `font` sizes TextContent boxes and renders text.
    void UI_BeginBuild(UIContext &ui, const UIInput &input, float dt, float screenW, float screenH,
                       const Font &font);
    // End a frame: prune boxes not touched this build, then run the semantic-size layout solve.
    void UI_EndBuild(UIContext &ui);
    // Walk the laid-out tree, tessellate every box (background/border/text) into `batch`, and
    // draw it with one Renderer::DrawUI call (after EndScene; see that method's contract).
    void UI_Render(UIContext &ui, Renderer &renderer);

    // --- Building ---------------------------------------------------------------
    // Find-or-create a box, keyed by `idString` hashed with the current parent (an empty string
    // gives an anonymous, per-parent-indexed key: fine for non-interactive boxes, but an
    // interactive one needs a stable id so its hover/press state persists). Applies the current
    // style; the caller may then override any field before adding children.
    UIBox *UI_MakeBox(UIContext &ui, uint32_t flags, const char *idString);
    // Hit-test `box` against this frame's input using its (last frame's) rect; also advances the
    // box's hotT/activeT easing. Call right after making a Clickable box.
    UISignal UI_SignalFromBox(UIContext &ui, UIBox *box);

    void UI_PushParent(UIContext &ui, UIBox *box);
    void UI_PopParent(UIContext &ui);
    void UI_PushStyle(UIContext &ui, const UIStyle &style); // push a copy (caller may pre-modify it)
    UIStyle &UI_TopStyle(UIContext &ui);                    // mutate the current defaults in place
    void UI_PopStyle(UIContext &ui);

    // --- Widget helpers (thin compositions over UI_MakeBox) ---------------------
    UIBox *UI_Panel(UIContext &ui, const char *idString); // a bordered background container
    UIBox *UI_Label(UIContext &ui, const char *text);     // non-interactive text, sized to content
    UISignal UI_Button(UIContext &ui, const char *label); // clickable, animated, centered text
    UIBox *UI_Spacer(UIContext &ui, UIAxis axis, float px); // a fixed empty gap along one axis

    // Make `box` float: pin it to `anchor` (+ offset px) within its parent, out of sibling flow.
    // e.g. UI_Anchor(panel, UIAnchor::Center) centers a menu; TopLeft + offset pins a HUD element.
    void UI_Anchor(UIBox *box, UIAnchor anchor, float offsetX = 0.0f, float offsetY = 0.0f);

    // Give `box` a 9-slice textured background: `tex` split by `insets` (l,t,r,b texture px),
    // replacing its rounded-rect bg/border. The box's bgColor tints the texture (white = as-is).
    void UI_NineSlice(UIBox *box, TextureHandle tex, const Vec4 &insets);

} // namespace toon
