//============================================================================
//  ToonEngine — entry point.
//
//  Owns the window + game loop and drives the renderer. All GPU/backend work
//  lives behind core/renderer.h (see that header for the seam rationale); this
//  file includes no Diligent header at all — that's the point of the seam.
//============================================================================
#include "core/renderer.h"
#include "core/primitives.h"
#include "core/scene.h"
#include "core/physics.h"
#include "core/script.h"
#include "core/scripts/spin_script.h"
#include "core/camera.h"
#include "core/input/action_map.h"
#include "core/input/binding_io.h"
#include "core/input/input_system.h"
#include "core/serializer.h"
#include "ui/file_browser.h"

// GLFW_INCLUDE_NONE is set engine-wide (CMakeLists.txt) since core/input/input_device.h now
// also pulls <GLFW/glfw3.h>, ahead of this file's own include below.
#include <GLFW/glfw3.h>

// Dear ImGui is a plain UI library, not a Diligent type, so engine/game code
// is free to include it directly and call ImGui:: between Renderer::BeginUI()
// and EndUI(). Diligent's ImGui *renderer* glue stays behind the seam.
#include "imgui.h"
#ifdef IMGUI_HAS_DOCK
#include "imgui_internal.h" // DockBuilder API, for the one-time default layout
#endif
#include "ImGuizmo.h" // editor transform gizmos (built on ImGui; seam-exempt like it)
#include "IconsFontAwesome6.h" // ICON_FA_* glyph macros for the Font Awesome icon font merged below

#include <algorithm> // std::max -- ScaledColliderExtents' non-uniform-scale fallback
#include <cmath>     // std::abs -- ScaledColliderExtents' non-uniform-scale detection
#include <cstdint>
#include <cstdio>
#include <filesystem> // .scene extension check, routing an asset-browser double-click to loadScene
#include <memory>
#include <string>
#include <vector>

namespace {
    // Upload a CPU mesh and return its handle (logs on failure).
    toon::MeshHandle Upload(toon::Renderer &r, const toon::MeshData &m, const char *name) {
        const toon::MeshHandle h = r.CreateMesh(m.vertices.data(), static_cast<uint32_t>(m.vertices.size()),
                                                m.indices.data(), static_cast<uint32_t>(m.indices.size()));
        if (h == toon::MeshHandle::Invalid) { std::fprintf(stderr, "Failed to create mesh '%s'\n", name); }
        return h;
    }

    // Create + upload a procedural mesh from `desc`, and record `desc` on the entity so a
    // saved scene can regenerate this mesh on load (a procedural mesh has no source file).
    void SetPrimitive(toon::Renderer &r, toon::Entity &e, const toon::PrimitiveDesc &desc) {
        e.primitive = desc;
        e.mesh = Upload(r, toon::MakePrimitiveMesh(desc), e.name.c_str());
    }

    // --- Physics (M2.1) -----------------------------------------------------------
    // A Box collider's half-extents are already 3 independent values, so a non-uniform
    // scale bakes in cleanly, one axis at a time. Sphere/Capsule only have 1-2 degrees of
    // freedom (a radius, a half-height), so a non-uniform scale there has no exact
    // representation as a plain SphereShape/CapsuleShape -- approximate with the largest
    // relevant axis and say so, rather than silently picking one. Exact ellipsoid/deformed-
    // capsule shapes are out of scope for M2.1's Box/Sphere/Capsule set.
    // logWarnings defaults on for the once-per-Play BuildPhysicsWorld call site; the
    // per-frame collider-wireframe overlay (Phase F, below) passes false so a misconfigured
    // entity doesn't spam stderr 60+ times a second on top of the warning Play already gave it.
    toon::Vec3 ScaledColliderExtents(const toon::ColliderComponent &collider, const toon::Vec3 &scale,
                                     const std::string &entityName, bool logWarnings = true) {
        switch (collider.shape) {
            case toon::ColliderShape::Box:
                return {collider.extents.x * scale.x, collider.extents.y * scale.y, collider.extents.z * scale.z};
            case toon::ColliderShape::Sphere: {
                const float s = std::max(scale.x, std::max(scale.y, scale.z));
                if (logWarnings &&
                    (std::abs(scale.x - scale.y) > 1e-4f || std::abs(scale.y - scale.z) > 1e-4f)) {
                    std::fprintf(stderr,
                                 "Entity '%s': non-uniform scale on a Sphere collider isn't supported -- using the "
                                 "largest axis (%.3f)\n",
                                 entityName.c_str(), s);
                }
                return {collider.extents.x * s, 0.0f, 0.0f};
            }
            case toon::ColliderShape::Capsule: {
                // extents.x = half-height (along the capsule's local Y), extents.y = radius
                // (the X/Z plane) -- see core/physics.h's BodyDesc comment.
                const float radialScale = std::max(scale.x, scale.z);
                if (logWarnings && std::abs(scale.x - scale.z) > 1e-4f) {
                    std::fprintf(stderr,
                                 "Entity '%s': non-uniform (x/z) scale on a Capsule collider isn't supported -- "
                                 "using the larger axis (%.3f)\n",
                                 entityName.c_str(), radialScale);
                }
                return {collider.extents.x * scale.y, collider.extents.y * radialScale, 0.0f};
            }
        }
        return collider.extents;
    }

    // Rebuild the physics world from the scene's current collider-bearing entities. Called
    // once whenever a Play/Step session starts (the Playback panel, below) -- Stop just
    // Clear()s, no rebuild, since the scene reverts to its pre-Play snapshot anyway.
    //
    // Assumes every collider-bearing entity is root-parented, so its local `transform` IS
    // its world pose: a real hierarchy fold (parent's world * local) is deliberately out of
    // scope for M2.1 (see the plan's "physics bodies should be root-parented" scope note) --
    // a collider on a nested entity is seeded once here but never correctly re-synced.
    void BuildPhysicsWorld(toon::PhysicsWorld &physicsWorld, toon::Scene &scene) {
        physicsWorld.Clear();
        for (toon::Entity &e : scene.entities) {
            if (!e.collider || !e.transform) continue;

            // A bare collider (no authored RigidBodyComponent) is an implicit static
            // collider -- a wall/floor. Synthesize one for this Play session only; Stop's
            // `scene = sceneBackup` discards it, same as everything else Play does (see
            // core/scene.h's RigidBodyComponent comment).
            if (!e.body) {
                toon::RigidBodyComponent implicitStatic;
                implicitStatic.type = toon::BodyType::Static;
                e.body = implicitStatic;
            }

            toon::BodyDesc desc;
            desc.shape = e.collider->shape;
            desc.extents = ScaledColliderExtents(*e.collider, e.transform->scale, e.name);
            desc.type = e.body->type;
            desc.mass = e.body->mass;
            desc.friction = e.body->friction;
            desc.restitution = e.body->restitution;
            desc.position = e.transform->position;
            desc.rotation = e.transform->rotation;

            e.body->handle = physicsWorld.CreateBody(desc);
        }
    }

    // --- Editor themes (ported from ToonEngineOld/src/ui/themes.cpp) --------------
    // Three selectable looks. ApplyTheme() resets the style to defaults, applies one theme's
    // colors + metrics, then scales every size by the display's DPI (the themes' pixel metrics
    // were authored at 1x). Pure style-struct edits — no backend state.
    enum class Theme { AmberYellow, GruvboxHard, GrayStone, Count };

    const char *ThemeName(Theme t) {
        switch (t) {
            case Theme::AmberYellow:
                return "Amber Yellow";
            case Theme::GruvboxHard:
                return "Gruvbox Hard";
            case Theme::GrayStone:
                return "Gray Stone";
            default:
                return "?";
        }
    }

    // Gray Stone authors its palette as 0xAARRGGBB constants, with a per-channel lerp for its
    // derived (dimmed) tab tints.
    ImVec4 FromARGB(uint32_t argb) {
        return ImVec4(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                      ((argb >> 24) & 0xFF) / 255.0f);
    }
    ImVec4 LerpColor(const ImVec4 &a, const ImVec4 &b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    void ApplyAmberYellow() {
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowPadding = ImVec2(8, 8);
        s.FramePadding = ImVec2(5, 3);
        s.CellPadding = ImVec2(6, 4);
        s.ItemSpacing = ImVec2(6, 4);
        s.ScrollbarSize = 12;
        s.GrabMinSize = 10;
        s.WindowRounding = s.ChildRounding = s.FrameRounding = s.PopupRounding = s.ScrollbarRounding = s.GrabRounding =
            s.TabRounding = 2.0f;
        s.WindowBorderSize = 1.0f;
        s.FrameBorderSize = 1.0f;

        ImVec4 *c = s.Colors;
        c[ImGuiCol_Text] = ImVec4(1.00f, 0.95f, 0.80f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.45f, 0.30f, 1.00f);
        c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
        c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
        c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.06f, 0.96f);
        c[ImGuiCol_Border] = ImVec4(0.30f, 0.25f, 0.10f, 0.80f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.22f, 0.12f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.18f, 0.10f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.40f, 0.15f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.50f, 0.20f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.60f, 0.10f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
        c[ImGuiCol_Tab] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
        c[ImGuiCol_TabSelected] = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
        c[ImGuiCol_TabDimmed] = ImVec4(0.08f, 0.08f, 0.07f, 1.00f);
        c[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);
        c[ImGuiCol_TableBorderLight] = ImVec4(0.25f, 0.20f, 0.10f, 1.00f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.95f, 0.80f, 0.10f, 0.25f);
        c[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 0.85f, 0.00f, 0.90f);
        c[ImGuiCol_NavCursor] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
        c[ImGuiCol_DockingPreview] = ImVec4(0.95f, 0.80f, 0.10f, 0.40f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
    }

    void ApplyGruvboxHard() {
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowPadding = ImVec2(10, 10);
        s.FramePadding = ImVec2(6, 4);
        s.ItemSpacing = ImVec2(8, 4);
        s.ScrollbarSize = 14;
        s.GrabMinSize = 12;
        s.WindowRounding = s.FrameRounding = s.PopupRounding = s.ScrollbarRounding = s.GrabRounding = s.TabRounding =
            2.0f;
        s.WindowBorderSize = 1.0f;
        s.FrameBorderSize = 1.0f;
        s.PopupBorderSize = 1.0f;

        ImVec4 *c = s.Colors;
        c[ImGuiCol_Text] = ImVec4(0.92f, 0.86f, 0.70f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.57f, 0.51f, 0.45f, 1.00f);
        c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
        c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.13f, 0.13f, 0.00f);
        c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.13f, 0.13f, 0.95f);
        c[ImGuiCol_Border] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_FrameBg] = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);
        c[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
        c[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.14f, 0.13f, 1.00f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.57f, 0.51f, 0.45f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.72f, 0.73f, 0.15f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.65f, 0.60f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.73f, 0.67f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.20f, 0.15f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);
        c[ImGuiCol_Tab] = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_TabSelected] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_PlotLines] = ImVec4(0.98f, 0.74f, 0.18f, 1.00f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
        c[ImGuiCol_NavCursor] = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);
        c[ImGuiCol_DockingPreview] = ImVec4(0.72f, 0.73f, 0.15f, 0.50f);
        c[ImGuiCol_DockingEmptyBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    }

    void ApplyGrayStone() {
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowBorderSize = 3.0f;
        s.FrameRounding = 3.0f;
        s.PopupRounding = 3.0f;
        s.ScrollbarRounding = 3.0f;
        s.GrabRounding = 3.0f;
        s.DockingSeparatorSize = 3.0f;

        ImVec4 *c = s.Colors;
        c[ImGuiCol_Text] = FromARGB(0xFFABB2BF);
        c[ImGuiCol_TextDisabled] = FromARGB(0xFF565656);
        c[ImGuiCol_WindowBg] = FromARGB(0xFF282C34);
        c[ImGuiCol_ChildBg] = FromARGB(0xFF21252B);
        c[ImGuiCol_PopupBg] = FromARGB(0xFF2E323A);
        c[ImGuiCol_Border] = FromARGB(0xFF2E323A);
        c[ImGuiCol_BorderShadow] = FromARGB(0x00000000);
        c[ImGuiCol_FrameBg] = c[ImGuiCol_ChildBg];
        c[ImGuiCol_FrameBgHovered] = FromARGB(0xFF484C52);
        c[ImGuiCol_FrameBgActive] = FromARGB(0xFF54575D);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_WindowBg];
        c[ImGuiCol_TitleBgActive] = c[ImGuiCol_FrameBgActive];
        c[ImGuiCol_TitleBgCollapsed] = FromARGB(0x8221252B);
        c[ImGuiCol_MenuBarBg] = c[ImGuiCol_ChildBg];
        c[ImGuiCol_ScrollbarBg] = c[ImGuiCol_PopupBg];
        c[ImGuiCol_ScrollbarGrab] = FromARGB(0xFF3E4249);
        c[ImGuiCol_ScrollbarGrabHovered] = FromARGB(0xFF484C52);
        c[ImGuiCol_ScrollbarGrabActive] = FromARGB(0xFF54575D);
        c[ImGuiCol_CheckMark] = c[ImGuiCol_Text];
        c[ImGuiCol_SliderGrab] = FromARGB(0xFF353941);
        c[ImGuiCol_SliderGrabActive] = FromARGB(0xFF7A7A7A);
        c[ImGuiCol_Button] = c[ImGuiCol_SliderGrab];
        c[ImGuiCol_ButtonHovered] = c[ImGuiCol_FrameBgActive];
        c[ImGuiCol_ButtonActive] = c[ImGuiCol_ScrollbarGrabActive];
        c[ImGuiCol_Header] = c[ImGuiCol_ChildBg];
        c[ImGuiCol_HeaderHovered] = FromARGB(0xFF353941);
        c[ImGuiCol_HeaderActive] = c[ImGuiCol_FrameBgActive];
        c[ImGuiCol_Separator] = c[ImGuiCol_FrameBgActive];
        c[ImGuiCol_SeparatorHovered] = FromARGB(0xFF3E4452);
        c[ImGuiCol_SeparatorActive] = c[ImGuiCol_SeparatorHovered];
        c[ImGuiCol_ResizeGrip] = c[ImGuiCol_Separator];
        c[ImGuiCol_ResizeGripHovered] = c[ImGuiCol_SeparatorHovered];
        c[ImGuiCol_ResizeGripActive] = c[ImGuiCol_SeparatorActive];
        c[ImGuiCol_InputTextCursor] = FromARGB(0xFF528BFF);
        c[ImGuiCol_TabHovered] = c[ImGuiCol_HeaderHovered];
        c[ImGuiCol_Tab] = c[ImGuiCol_FrameBgActive];
        c[ImGuiCol_TabSelected] = c[ImGuiCol_HeaderHovered];
        c[ImGuiCol_TabSelectedOverline] = c[ImGuiCol_HeaderActive];
        c[ImGuiCol_TabDimmed] = LerpColor(c[ImGuiCol_Tab], c[ImGuiCol_TitleBg], 0.80f);
        c[ImGuiCol_TabDimmedSelected] = LerpColor(c[ImGuiCol_TabSelected], c[ImGuiCol_TitleBg], 0.40f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
        c[ImGuiCol_DockingPreview] = c[ImGuiCol_ChildBg];
        c[ImGuiCol_DockingEmptyBg] = c[ImGuiCol_WindowBg];
        c[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        c[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
        c[ImGuiCol_TableHeaderBg] = c[ImGuiCol_ChildBg];
        c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_SliderGrab];
        c[ImGuiCol_TableBorderLight] = c[ImGuiCol_FrameBgActive];
        c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
        c[ImGuiCol_TextLink] = FromARGB(0xFF3F94CE);
        c[ImGuiCol_TextSelectedBg] = FromARGB(0xFF243140);
        c[ImGuiCol_TreeLines] = c[ImGuiCol_Text];
        c[ImGuiCol_DragDropTarget] = c[ImGuiCol_Text];
        c[ImGuiCol_NavCursor] = c[ImGuiCol_TextLink];
        c[ImGuiCol_NavWindowingHighlight] = c[ImGuiCol_Text];
        c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        c[ImGuiCol_ModalWindowDimBg] = FromARGB(0xC821252B);
    }

    // Reset the style to ImGui's defaults, apply the selected theme, then scale every size to
    // the display's DPI (the themes' pixel metrics are authored at 1x). Colors a theme leaves
    // unset keep ImGui's dark defaults.
    void ApplyTheme(Theme t, float dpiScale, GLFWwindow *window) {
        ImGui::GetStyle() = ImGuiStyle();
        switch (t) {
            case Theme::AmberYellow:
                ApplyAmberYellow();
                break;
            case Theme::GruvboxHard:
                ApplyGruvboxHard();
                break;
            case Theme::GrayStone:
                ApplyGrayStone();
                break;
            default:
                break;
        }
        ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;
        if (dpiScale > 1.0f) { ImGui::GetStyle().ScaleAllSizes(dpiScale); }

        // Carry the theme into the native title bar (see SetTitleBarTheme) so the OS chrome
        // reads as a continuation of the main menu bar directly below it, instead of the
        // stock white bar every theme here otherwise clashes with.
        const ImVec4 &menuBarBg = ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg];
        const ImVec4 &text = ImGui::GetStyle().Colors[ImGuiCol_Text];
        toon::SetTitleBarTheme(window, {menuBarBg.x, menuBarBg.y, menuBarBg.z}, {text.x, text.y, text.z});
    }

    // Editor vs. simulation state (M1.2): Editing poses the scene with nothing ticking;
    // Playing runs the fixed-timestep sim from main()'s loop; Paused freezes it without
    // discarding progress. See the "Playback" panel below for the Play/Step/Stop transitions.
    enum class EditorMode { Editing, Playing, Paused };
} // namespace

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // We render with Vulkan, so tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Start maximized so the editor fills the screen and the right-docked panels (Inspector /
    // Debug) stay on-screen on any monitor. Creating oversize (e.g. 3840x2160 on a smaller
    // display) pushed the dock layout's right column off the visible area. The 1600x900 below
    // is just the restored-down size.
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    toon::SetWindowIcon(window, TOON_ICON_PATH);

    toon::Renderer renderer;
    if (!renderer.Init(window)) {
        std::fprintf(stderr, "Renderer init failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Physics (M2.1): Jolt's process-global setup (allocator/factory/type registry) happens
    // once here, alongside the renderer's own Init -- the world stays empty (no bodies)
    // until a Play/Step session calls BuildPhysicsWorld, below.
    toon::PhysicsWorld physicsWorld;
    if (!physicsWorld.Init()) { std::fprintf(stderr, "PhysicsWorld init failed\n"); }

    // Install input callbacks BEFORE InitUI, so ImGui's GLFW backend chains ours instead of
    // overwriting them (see core/input/input_system.h's Init banner).
    toon::Input::Init(window);

    // Seed the default editor bindings (camera fly/orbit + focus — see action_map.cpp's
    // RegisterDefaultEditorBindings), then let a saved assets/input.json override them if one
    // exists; otherwise write the defaults so the file exists next time. Same "load or create"
    // shape as scene save/load (core/serializer.h).
    toon::Input::RegisterDefaultEditorBindings();
    if (auto *editorBindings = toon::Input::GetContext("editor")) {
        if (!toon::Input::BindingIO::Load(TOON_INPUT_JSON, *editorBindings)) {
            toon::Input::BindingIO::Save(TOON_INPUT_JSON, *editorBindings);
        }
    }

    if (!renderer.InitUI(window)) {
        std::fprintf(stderr, "Renderer UI init failed\n");
        renderer.Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Editor-style docking: panels can be dragged to snap around the 3D view.
    // (ImGui is exempt from the renderer seam, so app code drives UI policy.)
    // Guarded on IMGUI_HAS_DOCK so the build stays green with a non-docking imgui
    // — docking activates only when a docking-branch imgui is checked out.
#ifdef IMGUI_HAS_DOCK
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

    // Editor look: load the UI font (Bai Jamjuree) at the display's DPI scale, then apply the
    // starting theme. ImGui is seam-exempt and the Diligent backend advertises
    // RendererHasTextures (imgui 1.92 dynamic atlas), so adding the font here — after InitUI
    // created the context, before the first frame — is enough; the glyph texture uploads on
    // first draw. uiScale also drives ApplyTheme's ScaleAllSizes so the whole UI matches DPI.
    float uiScale = 1.0f, uiScaleY = 1.0f;
    glfwGetWindowContentScale(window, &uiScale, &uiScaleY);
    ImGui::GetIO().Fonts->AddFontFromFileTTF(TOON_FONTS_DIR "/BaiJamjuree-Medium.ttf", 18.0f * uiScale);

    // Merge Font Awesome 6 solid's icon glyphs into that same font (MergeMode stitches them
    // into the range Bai Jamjuree just registered instead of starting a second font), so the
    // ICON_FA_* macros (ui/file_browser.cpp) render inline with body text — same baseline,
    // same line height. GlyphMinAdvanceX gives every icon the same advance width regardless
    // of its natural glyph width, which keeps a column of mixed icons visually aligned.
    ImFontConfig iconFontConfig;
    iconFontConfig.MergeMode = true;
    iconFontConfig.PixelSnapH = true;
    iconFontConfig.GlyphMinAdvanceX = 18.0f * uiScale;
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    ImGui::GetIO().Fonts->AddFontFromFileTTF(TOON_FONTS_DIR "/fa-solid-900.ttf", 18.0f * uiScale, &iconFontConfig,
                                              iconRanges);

    Theme uiTheme = Theme::AmberYellow;
    ApplyTheme(uiTheme, uiScale, window);

    // Transform-gizmo state (ImGuizmo): which handle is active + local/world space.
    ImGuizmo::OPERATION gizmoOp = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE gizmoMode = ImGuizmo::WORLD;

    // Gizmo snapping: per-op step sizes. Snapping engages while the toggle is on OR Ctrl is
    // held. ImGuizmo reads snap[0] for rotate/scale and snap[0..2] for translate, so one step
    // value per op is broadcast into a vec3 at the call site.
    bool gizmoSnap = false;
    float snapTranslate = 0.5f;  // world units
    float snapRotateDeg = 15.0f; // degrees
    float snapScale = 0.25f;     // scale factor

    // Route framebuffer resizes to the renderer's swap chain.
    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int width, int height) {
        if (auto *r = static_cast<toon::Renderer *>(glfwGetWindowUserPointer(w))) {
            r->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
    });

    // --- Scene graph ---------------------------------------------------------
    // A real entity tree (core/scene.h) instead of a hardcoded array. Root at index 0;
    // everything is a child of the root EXCEPT the satellite, which is parented to the cube
    // to demonstrate hierarchy composition (it orbits the cube as the cube spins).
    toon::Scene scene;
    toon::EnsureSceneRoot(scene);

    // Attach a Spin script (core/scripts/spin_script.h) to entity `i` — replaces the old
    // Spinner side-list; the script now lives inside the entity itself, so it survives
    // reparent/reload/Stop with no external index bookkeeping to keep in sync.
    auto addSpin = [&](int i, toon::Vec3 axis, float speed = 0.6f) {
        auto s = std::make_unique<toon::SpinScript>();
        s->axis = axis;
        s->speed = speed;
        scene.entities[i].scripts.push_back({toon::kSpinScriptName, std::move(s)});
    };

    // Ground plane beneath the objects (catches their SSAO contact shadows; no spin/outline).
    {
        toon::Entity &e = scene.entities[toon::AddEntity(scene, 0, "Ground")];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Plane(5.0f));
        e.transform->position = {0.0f, -1.05f, 0.0f};
        e.material.baseColor = {0.60f, 0.60f, 0.63f};
        e.material.outlineWidth = 0.0f;
        e.material.roughness = 0.05f; // smooth -> reflective (SSR)

        // Static physics collider (M2.1): a thin box matching the plane's own 5.0 half-extent.
        // Its center sits at the same position as the (zero-thickness) visual plane, so its
        // top surface reads ~0.1 units ABOVE the rendered ground -- a collider has no local
        // offset from its entity today, so this small mismatch is a deliberate simplification,
        // not an oversight (a future collider-offset field would let it align exactly).
        e.collider = toon::ColliderComponent{toon::ColliderShape::Box, {5.0f, 0.1f, 5.0f}};
    }
    // Sphere — non-uniformly scaled into a spinning ellipsoid (exercises the normal matrix).
    {
        const int i = toon::AddEntity(scene, 0, "Sphere");
        toon::Entity &e = scene.entities[i];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Sphere(1.0f, 32, 48));
        e.transform->position = {-2.8f, 0.0f, 0.0f};
        e.transform->scale = {1.5f, 0.8f, 1.0f};
        e.material = toon::Material{{0.85f, 0.30f, 0.35f}, {0.24f, 0.05f, 0.08f}, 0.030f};
        e.material.roughness = 0.15f; // lightly glossy so SSR reflects on it
        addSpin(i, {0.0f, 1.0f, 0.0f});
    }
    // Cube — the satellite's parent.
    const int cubeIdx = toon::AddEntity(scene, 0, "Cube");
    {
        toon::Entity &e = scene.entities[cubeIdx];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Cube(0.9f));
        e.material = toon::Material{{0.30f, 0.45f, 0.85f}, {0.02f, 0.02f, 0.05f}, 0.050f};
        e.material.roughness = 0.15f;
        addSpin(cubeIdx, {0.5f, 1.0f, 0.0f});
    }
    // Satellite — a small sphere PARENTED to the cube (the hierarchy demo). It has no spin of
    // its own; it orbits the cube purely by inheriting the cube's spinning world transform.
    // Created right after the cube so the flat outliner (vector order) lists it directly under
    // its parent — keeping the scripted scene in pre-order, as the editor mutations always are.
    {
        toon::Entity &e = scene.entities[toon::AddEntity(scene, cubeIdx, "Satellite")];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Sphere(0.22f, 16, 24));
        e.transform->position = {1.7f, 0.0f, 0.0f}; // offset from the cube (its parent)
        e.material = toon::Material{{0.40f, 0.90f, 0.55f}, {0.03f, 0.07f, 0.04f}, 0.014f};
        e.material.roughness = 0.15f;
    }
    // Torus.
    {
        const int i = toon::AddEntity(scene, 0, "Torus");
        toon::Entity &e = scene.entities[i];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Torus(0.75f, 0.32f, 48, 24));
        e.transform->position = {2.8f, 0.0f, 0.0f};
        e.material = toon::Material{{0.90f, 0.70f, 0.25f}, {0.32f, 0.20f, 0.03f}, 0.022f};
        e.material.roughness = 0.15f;
        addSpin(i, {1.0f, 0.0f, 0.0f});
    }
    // Falling primitives (M2.1 physics demo): dynamic rigid bodies, no spin script -- physics
    // owns their transform each Play tick, unlike the spinning showcase above. Dropped above
    // the ground at a clear spot (x=4) and stacked at increasing height, so pressing Play
    // makes them fall and land on one another as well as the ground -- the visible proof of
    // physics, the same role the spin demo (above) plays for native scripts.
    {
        const int i = toon::AddEntity(scene, 0, "PhysicsCube1");
        toon::Entity &e = scene.entities[i];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Cube(0.4f));
        e.transform->position = {4.0f, 3.0f, 0.0f};
        e.material = toon::Material{{0.25f, 0.80f, 0.35f}, {0.04f, 0.12f, 0.06f}, 0.018f};
        e.material.roughness = 0.4f;
        e.collider = toon::ColliderComponent{toon::ColliderShape::Box, {0.4f, 0.4f, 0.4f}};
        e.body = toon::RigidBodyComponent{toon::BodyType::Dynamic, 1.0f};
    }
    {
        const int i = toon::AddEntity(scene, 0, "PhysicsSphere1");
        toon::Entity &e = scene.entities[i];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Sphere(0.35f, 24, 32));
        e.transform->position = {4.0f, 5.0f, 0.0f};
        e.material = toon::Material{{0.90f, 0.55f, 0.15f}, {0.28f, 0.16f, 0.03f}, 0.015f};
        e.material.roughness = 0.3f;
        e.collider = toon::ColliderComponent{toon::ColliderShape::Sphere, {0.35f, 0.0f, 0.0f}};
        e.body = toon::RigidBodyComponent{toon::BodyType::Dynamic, 0.8f};
    }
    {
        const int i = toon::AddEntity(scene, 0, "PhysicsCube2");
        toon::Entity &e = scene.entities[i];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Cube(0.3f));
        e.transform->position = {4.0f, 7.0f, 0.0f};
        e.material = toon::Material{{0.55f, 0.30f, 0.80f}, {0.10f, 0.05f, 0.14f}, 0.014f};
        e.material.roughness = 0.4f;
        e.collider = toon::ColliderComponent{toon::ColliderShape::Box, {0.3f, 0.3f, 0.3f}};
        e.body = toon::RigidBodyComponent{toon::BodyType::Dynamic, 0.6f};
    }
    // Loaded glTF model (DiligentTools' loader): cel-shaded albedo + inverted-hull outline.
    const char *helmetPath = TOON_MODELS_DIR "/helmet.glb";
    const toon::ModelHandle helmet = renderer.LoadModel(helmetPath);
    if (helmet != toon::ModelHandle::Invalid) {
        const int i = toon::AddEntity(scene, 0, "Helmet");
        toon::Entity &e = scene.entities[i];
        e.model = helmet;
        e.modelPath = helmetPath; // so a saved scene can reload it (see core/serializer.h)
        e.transform->position = {0.0f, 2.5f, 0.0f};
        e.transform->scale = {1.4f, 1.4f, 1.4f};
        e.material.baseColor = {1.0f, 1.0f, 1.0f}; // white tint (glTF supplies the color)
        e.material.outlineColor = {0.02f, 0.02f, 0.03f};
        e.material.outlineWidth = 0.04f;
        e.material.roughness = 0.5f;
        addSpin(i, {0.0f, 1.0f, 0.0f});
    }
    // Sun — a directional light entity (no mesh/model, so the draw loop's isMesh/isModel
    // check skips it). Aimed by rotation (MakeLightTransform), reproducing the scene's old
    // fixed light direction exactly, so the default render is unchanged.
    {
        const int i = toon::AddEntity(scene, 0, "Sun");
        toon::Entity &e = scene.entities[i];
        e.transform = toon::MakeLightTransform({0.0f, 4.0f, 0.0f}, {0.5f, 0.8f, -0.3f});
        e.light = toon::LightComponent{};
    }

    // Start with the cube selected so the Inspector is populated on launch.
    scene.selected = cubeIdx;

    // Editor camera — driven by the mouse/keyboard in the loop (defaults: pivot at the
    // origin, distance 10, a slight downward pitch so the ground + its AO show).
    toon::Camera camera;
    const toon::Camera cameraDefault = camera; // for the "Reset camera" button

    // Style shared by every object each frame: band count + ambient floor (a global
    // shading look). Outline color/width are per-object (above), but this scales all of
    // their widths together — handy for dialing the whole scene's line weight at once.
    toon::Material style;
    float outlineScale = 1.0f; // global multiplier over each object's outline width

    // HDR post-processing (foundation for DiligentFX effects).
    toon::PostParams post;

    // Scene serialization (core/serializer.h): path field + Save/Load buttons live in the
    // Debug panel below. `sceneStatus` echoes the last op's result in the UI (SaveScene/
    // LoadScene also log to the console) since this dev environment has no reliable console.
    char scenePathBuf[256];
    std::snprintf(scenePathBuf, sizeof(scenePathBuf), "%s", TOON_SCENES_DIR "/default.scene");
    std::string sceneStatus;

    // Shared scene-load path for the Debug panel's "Load Scene" button AND the asset
    // browser's double-click-a-.scene behavior (below).
    auto loadScene = [&](const char *path) {
        if (toon::LoadScene(path, scene, camera, renderer)) {
            sceneStatus = "Loaded.";
        } else {
            sceneStatus = "Load failed (see console).";
        }
    };

    // File menu's "New Scene": drop every entity (GPU mesh/model handles stay alive but
    // unreferenced until Shutdown, same as a LoadScene replacing the vector -- see
    // core/scene.h's DestroyScene), then restore just the root for an empty-but-valid tree.
    auto newScene = [&]() {
        toon::DestroyScene(scene);
        toon::EnsureSceneRoot(scene);
        scene.selected = -1;
        camera = cameraDefault;
        sceneStatus = "New scene.";
    };

    // Contents: browses assets/ with thumbnails; passive besides double-click, which
    // this routes through loadScene when the activated file is a .scene.
    toon::FileBrowser assetBrowser;
    assetBrowser.Init(TOON_ASSETS_DIR);

    // Gates UpdateScripts (core/script.h) below -- lets scripts be paused without stopping
    // the rest of the simulation. Was a Spin-demo-specific toggle before M1.3; renamed now
    // that it gates every attached script, not just the Sphere/Cube/Torus/Helmet's spin.
    bool runScripts = true;

    // M2.1: overlay each collider-bearing entity's shape as a wireframe (Settings panel).
    bool showColliders = false;
#ifdef IMGUI_HAS_DOCK
    bool dockLayoutBuilt = false;
#endif

    // Editor menu bar state: which panels are open (View menu checkboxes, and each panel's
    // own close button both write these) and which modal the File/Help menus queued this
    // frame (OpenPopup runs after EndMainMenuBar, below, at the same ID-stack depth
    // BeginPopupModal reads from -- calling it while still nested in the menu's own ID
    // stack would open a popup BeginPopupModal here never matches).
    bool showHierarchy = true;
    bool showInspector = true;
    bool showDebug = true;
    bool showAssetBrowser = true;
    bool showPlayback = true;
    bool openScenePopupRequested = false;
    bool saveScenePopupRequested = false;
    bool aboutPopupRequested = false;

    double lastTime = glfwGetTime();

    // Fixed-timestep simulation clock (M1.1): gameplay state advances in fixed kFixedDt steps,
    // decoupled from the variable render rate below, via an accumulator (see the loop's own
    // comments for the why). `accumulator` carries leftover sim time between frames.
    constexpr double kFixedDt = 1.0 / 60.0; // simulation rate: 60 Hz
    double accumulator = 0.0;

    // Play/Pause/Step/Stop state (M1.2). sceneBackup is the snapshot taken when Play starts
    // and wholesale-restored on Stop -- Scene's implicit copy (a vector<Entity> + an int, no
    // manual resource ownership) is the entire mechanism, no serialization needed. The other
    // two flags cross the frame boundary once: stepRequested is consumed at the top of the
    // NEXT frame's accumulator gating; suppressNextFrameHistory is folded into that frame's
    // suppressTemporalHistory (a Stop-restore or a Step is a pose jump, not smooth motion).
    EditorMode mode = EditorMode::Editing;
    toon::Scene sceneBackup;
    bool stepRequested = false;
    bool suppressNextFrameHistory = false;

    const toon::Color clearColor{0.10f, 0.11f, 0.13f, 1.0f};
    while (!glfwWindowShouldClose(window)) {
        // BeginFrame BEFORE PollEvents: it snapshots previous state and clears this frame's
        // mouse/scroll deltas, so the callbacks PollEvents fires (key/mouse/scroll) accumulate
        // into a clean frame and WasPressed/WasReleased edge-detect correctly. See
        // core/input/input_system.h.
        toon::Input::BeginFrame();
        glfwPollEvents();

        // Variable frame dt drives input-rate concerns (camera nav below): it should feel as
        // smooth as the display, not snap to the sim's fixed rate. Clamped so a stall (a
        // breakpoint, or a window drag -- glfwPollEvents blocks for the duration on Windows)
        // doesn't dump a large time debt into the accumulator below and trigger a many-step
        // "spiral of death" catch-up burst; the sim just falls a bit behind wall-clock instead.
        const double now = glfwGetTime();
        double frameTime = now - lastTime;
        lastTime = now;
        if (frameTime > 0.25) { frameTime = 0.25; }
        const float dt = static_cast<float>(frameTime);

        // Fixed-timestep simulation: advance in whole kFixedDt-sized steps regardless of the
        // variable frame rate above, so gameplay state (script Update hooks; physics later --
        // see CLAUDE.md's roadmap) evolves deterministically. Usually one step per frame; zero
        // if rendering outruns the sim rate, several if the sim fell behind.
        // M1.2: only Playing feeds the accumulator from wall-clock time -- Editing/Paused freeze
        // it so no time debt piles up while stopped. Step (from the "Playback" panel, below)
        // credits it with exactly one kFixedDt instead, so the SAME while loop below drains
        // exactly one iteration, no separate single-step code path needed.
        const bool runFixedStepsThisFrame = (mode == EditorMode::Playing) || stepRequested;
        if (mode == EditorMode::Playing) { accumulator += frameTime; }
        if (stepRequested) {
            accumulator += kFixedDt;
            stepRequested = false; // consumed
        }
        if (runFixedStepsThisFrame) {
            while (accumulator >= kFixedDt) {
                // Snapshot BEFORE integrating, so UpdateWorldTransforms below can interpolate the
                // render pose across the tick this step just produced.
                toon::SnapshotSimState(scene);

                // Run every entity's attached scripts for this tick (core/script.h) -- e.g. the
                // Sphere/Cube/Torus/Helmet's SpinScript, replacing the old hardcoded spin block.
                // Each script advances its own entity's state incrementally (SpinScript
                // pre-multiplies a small delta rotation onto whatever `rotation` currently is),
                // so a gizmo-set orientation (set while paused) is the new baseline it continues
                // from on resume, instead of snapping back to where an absolute clock-based
                // formula would say it "should" be.
                if (runScripts) { toon::UpdateScripts(scene, static_cast<float>(kFixedDt)); }

                // Physics (M2.1): push this tick's static/kinematic transforms into Jolt (so a
                // gizmo-dragged wall, say, is reflected before the step that would otherwise
                // ignore it), step once, then read every dynamic body back into its entity's
                // transform via ComposeWorldMatrix + SetEntityWorldMatrix (the same fold-out-
                // the-parent path the gizmo write-back uses). No separate "run physics" toggle,
                // unlike scripts' Run Scripts checkbox -- nothing in the roadmap called for one.
                for (const toon::Entity &e : scene.entities) {
                    if (e.body && e.body->type != toon::BodyType::Dynamic && e.transform) {
                        physicsWorld.SetBodyTransform(e.body->handle, e.transform->position, e.transform->rotation);
                    }
                }
                physicsWorld.Step(static_cast<float>(kFixedDt));
                for (int i = 0; i < static_cast<int>(scene.entities.size()); ++i) {
                    toon::Entity &e = scene.entities[i];
                    if (!e.body || e.body->type != toon::BodyType::Dynamic || !e.transform) { continue; }
                    toon::Vec3 bodyPos;
                    toon::Quat bodyRot;
                    if (physicsWorld.GetBodyTransform(e.body->handle, bodyPos, bodyRot)) {
                        const toon::Mat4 world = toon::ComposeWorldMatrix(bodyPos, bodyRot, e.transform->scale);
                        toon::SetEntityWorldMatrix(scene, i, world);
                    }
                }

                accumulator -= kFixedDt;
            }
        }

        // Compose the hierarchy's world matrices (parents before children), rendering each
        // entity's pose interpolated between its previous and current sim tick by how far
        // `accumulator` has drifted into the next one -- smooth motion even when the display's
        // refresh rate doesn't match the fixed sim rate. Motion vectors come from the cached
        // previous world matrices (see UpdateWorldTransforms), so no separate prev-angle
        // bookkeeping is needed here. Outside Playing (Editing/Paused), alpha is pinned to 1.0
        // -- accumulator isn't draining, so any interpolation fraction left over from the last
        // Play session is stale; rendering the exact current tick avoids blending a paused/
        // edited pose against that stale leftover.
        const float alpha =
            (mode == EditorMode::Playing) ? static_cast<float>(accumulator / kFixedDt) : 1.0f;
        toon::UpdateWorldTransforms(scene, alpha);

        // Editor camera: poll input, gate on ImGui's capture (last frame's UI state), then
        // navigate. Right-drag orbits (+ WASD/QE = fly); middle-drag pans; scroll zooms;
        // F focuses the origin. Dragging over the debug panel is suppressed by the gate.
        const ImGuiIO &io = ImGui::GetIO();
        // Gate the camera on ImGui capture OR an in-progress gizmo drag (both from last frame).
        const bool gizmoActive = ImGuizmo::IsUsing();
        toon::Input::SetCaptured(io.WantCaptureMouse || gizmoActive, io.WantCaptureKeyboard);
        // Feeds PostParams::suppressTemporalHistory (see its comment): an active gizmo
        // drag, any ImGui widget being edited, scripts continuously animating, or a
        // Stop-restore/Step from the Playback panel last frame (a pose jump, not smooth
        // motion) all mean post-fx temporal history shouldn't be trusted this frame.
        const bool suppressTemporalHistory =
            gizmoActive || ImGui::IsAnyItemActive() || runScripts || suppressNextFrameHistory;
        suppressNextFrameHistory = false; // consumed -- only suppresses the one frame right after
        {
            using M = toon::Input::MouseButton;
            float mdx = 0.0f, mdy = 0.0f;
            toon::Input::MouseDelta(mdx, mdy);
            if (toon::Input::IsMouseDown(M::Right)) {
                toon::CameraOrbit(camera, -mdx, -mdy);
                // Fly axes go through the action map (camera.fly.*) so keyboard AND a gamepad
                // stick drive the same names — see action_map.cpp's RegisterDefaultEditorBindings.
                // Guarded on WantCaptureKeyboard because GetAxis reads raw device state (it
                // bypasses SetCaptured, like the rest of the action-map layer) — without the
                // guard, typing in an ImGui field while right-dragging would also fly the camera.
                if (!io.WantCaptureKeyboard) {
                    const float fwd = toon::Input::GetAxis("camera.fly.forward");
                    const float rgt = toon::Input::GetAxis("camera.fly.right");
                    const float upv = toon::Input::GetAxis("camera.fly.up");
                    toon::CameraFly(camera, dt, fwd, rgt, upv);
                }
            }
            if (toon::Input::IsMouseDown(M::Middle)) { toon::CameraPan(camera, mdx, mdy); }
            if (const float s = toon::Input::ScrollDelta(); s != 0.0f) { toon::CameraZoom(camera, s); }
            if (!io.WantCaptureKeyboard && toon::Input::WasActionPressed("camera.focus")) {
                toon::CameraFocus(camera, {0.0f, 0.0f, 0.0f});
            }

            // Gamepad orbit (right stick) — a new capability the action map adds; ungated
            // (unlike the keyboard-sourced queries above) since a physical stick is never
            // ambiguous with ImGui text entry. Scaled by dt so the turn rate is frame-rate
            // independent, unlike the per-frame pixel deltas CameraOrbit otherwise expects from
            // a mouse drag.
            const float gpOrbitX = toon::Input::GetAxis("camera.orbit.x");
            const float gpOrbitY = toon::Input::GetAxis("camera.orbit.y");
            if (gpOrbitX != 0.0f || gpOrbitY != 0.0f) {
                // Pixel-equivalents/sec at full stick deflection. An untested starting point —
                // no controller in this environment to feel-tune it against (see the verify
                // skill); adjust if a full stick push turns too fast or too slow.
                constexpr float kGamepadOrbitRate = 150.0f;
                toon::CameraOrbit(camera, gpOrbitX * kGamepadOrbitRate * dt, -gpOrbitY * kGamepadOrbitRate * dt);
            }
        }

        // Post params + camera + light up front: SetCamera reads post.taa to decide the TAA
        // jitter, and the shadow cascade pre-pass below needs the camera + light already set
        // -- it renders into its own depth-only targets, so it must run before BeginFrame
        // binds the main G-buffer (scene first, so the debug UI still overlays it, below).
        post.suppressTemporalHistory = suppressTemporalHistory;
        renderer.SetPostParams(post);
        renderer.SetCamera(camera);

        // Light: driven by the scene's first light entity (aimed via its rotation), falling
        // back to this fixed default if the scene has none (e.g. the user deleted "Sun").
        toon::Vec3 lightDir{0.5f, 0.8f, -0.3f};
        toon::Vec3 lightColor{1.0f, 1.0f, 1.0f};
        float lightIntensity = 1.0f;
        toon::GetActiveLight(scene, lightDir, lightColor, lightIntensity);
        renderer.SetLight(lightDir, lightColor, lightIntensity);

        // Cascaded shadow map pre-pass: walks the same renderable entities as the main pass
        // below, once per cascade, into the shadow map's own depth-only targets. Must run
        // before BeginFrame (separate render targets, no interaction with the main G-buffer).
        // BeginShadowPass returns 0 (the loop below becomes a no-op) when the Debug panel's
        // Shadows toggle is off.
        const uint32_t shadowCascades = renderer.BeginShadowPass();
        for (uint32_t cascade = 0; cascade < shadowCascades; ++cascade) {
            renderer.BeginShadowCascade(cascade);
            for (const toon::Entity &e : scene.entities) {
                if (e.mesh != toon::MeshHandle::Invalid) {
                    renderer.DrawMeshShadow(e.mesh, e.worldMatrix);
                } else if (e.model != toon::ModelHandle::Invalid) {
                    renderer.DrawModelShadow(e.model, e.worldMatrix);
                }
            }
        }
        renderer.EndShadowPass();

        renderer.BeginFrame(clearColor);

        // Walk the scene, drawing every renderable entity with its hierarchy-composed world
        // matrix (+ last frame's, for motion vectors). The shared style overlays band count,
        // ambient, and the global outline-width multiplier onto each entity's own material.
        for (const toon::Entity &e : scene.entities) {
            const bool isMesh = e.mesh != toon::MeshHandle::Invalid;
            const bool isModel = e.model != toon::ModelHandle::Invalid;
            if (!isMesh && !isModel) {
                continue; // root / non-renderable
            }

            toon::Material m = e.material;
            m.bands = style.bands;
            m.ambient = style.ambient;
            m.outlineWidth = e.material.outlineWidth * outlineScale;
            if (isMesh) {
                renderer.DrawMesh(e.mesh, e.worldMatrix, e.prevWorldMatrix, m);
            } else {
                renderer.DrawModel(e.model, e.worldMatrix, e.prevWorldMatrix, m);
            }
        }

        // Resolve the HDR scene to the back buffer (post effects + exposure + tone map).
        renderer.EndScene();

        // Collider debug wireframes (M2.1) -- after EndScene, before the UI overlay (see
        // Renderer::DrawWireframe's call-timing contract). A fixed yellow-ish color for
        // every shape; distinguishing static/dynamic by color is future polish, not needed
        // to see whether a collider matches its entity's visual size/position.
        //
        // Mirrors BuildPhysicsWorld exactly (scaled extents via ScaledColliderExtents, a
        // scale-free position/rotation matrix, no parent-chain fold) rather than using
        // e.worldMatrix + raw extents directly -- so the overlay always shows what Jolt is
        // actually simulating, not the renderer's own (possibly-scaled, possibly-nested)
        // placement of the entity. The two agree for today's root-parented, unit-scale demo
        // entities, but only one of them is correct in general.
        if (showColliders) {
            const toon::Color wireColor{1.0f, 0.9f, 0.2f, 1.0f};
            for (const toon::Entity &e : scene.entities) {
                if (!e.collider || !e.transform) { continue; }
                const toon::Vec3 scaledExtents =
                    ScaledColliderExtents(*e.collider, e.transform->scale, e.name, /*logWarnings=*/false);
                const toon::Mat4 world =
                    toon::ComposeWorldMatrix(e.transform->position, e.transform->rotation, {1.0f, 1.0f, 1.0f});
                const std::vector<toon::Vec3> wireframe = toon::ColliderWireframe(e.collider->shape, scaledExtents);
                renderer.DrawWireframe(world, wireframe.data(), static_cast<uint32_t>(wireframe.size()), wireColor);
            }
        }

        renderer.BeginUI();
        ImGuizmo::BeginFrame(); // must follow ImGui's NewFrame (inside BeginUI), before Manipulate

        // Gizmo hotkeys (Unity-style): W/E/R switch move/rotate/scale, X toggles local/world.
        // Gated so they don't fire while typing in a field (WantCaptureKeyboard) or while
        // fly-navigating (right mouse held -> WASD drives the camera). Edge-triggered, no-repeat.
        if (!io.WantCaptureKeyboard && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { gizmoOp = ImGuizmo::TRANSLATE; }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { gizmoOp = ImGuizmo::ROTATE; }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { gizmoOp = ImGuizmo::SCALE; }
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                gizmoMode = (gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
            }
        }

        // --- Main menu bar: File / Edit / Tools / View / Help ---------------------------
        // Mirrors capabilities that already exist elsewhere (the Debug panel's Save/Load
        // and Reset camera, the hierarchy's right-click ops, the Inspector's gizmo controls)
        // under the menu layout a desktop editor is expected to have. Actions needing a path
        // just queue a modal (opened below, outside the menu) instead of running inline.
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) { newScene(); }
                if (ImGui::MenuItem("Open Scene...")) { openScenePopupRequested = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene")) {
                    sceneStatus =
                        toon::SaveScene(scenePathBuf, scene, camera) ? "Saved." : "Save failed (see console).";
                }
                if (ImGui::MenuItem("Save Scene As...")) { saveScenePopupRequested = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) { glfwSetWindowShouldClose(window, GLFW_TRUE); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Add Entity")) { scene.selected = toon::AddChildEntity(scene, 0, "Entity"); }
                const bool hasSelection = scene.selected > 0 && scene.selected < static_cast<int>(scene.entities.size());
                if (ImGui::MenuItem("Duplicate Entity", nullptr, false, hasSelection)) {
                    const int d = toon::DuplicateEntity(scene, scene.selected);
                    if (d >= 0) { scene.selected = d; }
                }
                if (ImGui::MenuItem("Delete Entity", nullptr, false, hasSelection)) {
                    toon::DeleteEntity(scene, scene.selected);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Deselect", nullptr, false, scene.selected >= 0)) { scene.selected = -1; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("Move", "W", gizmoOp == ImGuizmo::TRANSLATE)) { gizmoOp = ImGuizmo::TRANSLATE; }
                if (ImGui::MenuItem("Rotate", "E", gizmoOp == ImGuizmo::ROTATE)) { gizmoOp = ImGuizmo::ROTATE; }
                if (ImGui::MenuItem("Scale", "R", gizmoOp == ImGuizmo::SCALE)) { gizmoOp = ImGuizmo::SCALE; }
                ImGui::Separator();
                if (ImGui::MenuItem("Local Space", "X", gizmoMode == ImGuizmo::LOCAL)) {
                    gizmoMode = (gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
                }
                ImGui::Separator();
                ImGui::MenuItem("Run Scripts", nullptr, &runScripts);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::BeginMenu("Themes")) {
                    for (int i = 0; i < static_cast<int>(Theme::Count); ++i) {
                        const Theme t = static_cast<Theme>(i);
                        if (ImGui::MenuItem(ThemeName(t), nullptr, t == uiTheme)) {
                            uiTheme = t;
                            ApplyTheme(uiTheme, uiScale, window);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                ImGui::MenuItem("Playback", nullptr, &showPlayback);
                ImGui::MenuItem("Objects", nullptr, &showHierarchy);
                ImGui::MenuItem("Properties", nullptr, &showInspector);
                ImGui::MenuItem("Settings", nullptr, &showDebug);
                ImGui::MenuItem("Contents", nullptr, &showAssetBrowser);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About ToonEngine")) { aboutPopupRequested = true; }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Modals queued by the menu above. OpenPopup runs here, at the same ID-stack depth
        // BeginPopupModal below reads from -- see the comment on the request bools' declaration.
        if (openScenePopupRequested) {
            ImGui::OpenPopup("Open Scene");
            openScenePopupRequested = false;
        }
        if (saveScenePopupRequested) {
            ImGui::OpenPopup("Save Scene As");
            saveScenePopupRequested = false;
        }
        if (aboutPopupRequested) {
            ImGui::OpenPopup("About ToonEngine");
            aboutPopupRequested = false;
        }

        if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Path", scenePathBuf, sizeof(scenePathBuf));
            if (ImGui::Button("Open")) {
                loadScene(scenePathBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Path", scenePathBuf, sizeof(scenePathBuf));
            if (ImGui::Button("Save")) {
                sceneStatus =
                    toon::SaveScene(scenePathBuf, scene, camera) ? "Saved." : "Save failed (see console).";
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("About ToonEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("ToonEngine");
            ImGui::TextDisabled("A from-scratch, cross-platform toon-shaded game engine.");
            ImGui::Separator();
            ImGui::Text("Built on Diligent Engine (Vulkan) + GLFW + Dear ImGui.");
            if (ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

#ifdef IMGUI_HAS_DOCK
        // Full-window dock space with a see-through center so the scene shows
        // through; panels dock around it. Build the default layout once — after that,
        // whatever the user arranges sticks. DockSpaceOverViewport reads the main viewport's
        // WorkPos/WorkSize, which Dear ImGui already shrinks around the main menu bar above
        // (BeginMainMenuBar, submitted earlier this frame) — so this whole dockspace, Playback
        // strip included, composes below it automatically; no coordinate math needed here.
        const ImGuiID dockspaceId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (!dockLayoutBuilt) {
            dockLayoutBuilt = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
            // Objects on the far left; Properties (top) + Settings (bottom) stacked on the
            // right; a thin Playback strip (M1.2) -- one row tall, just the transport buttons;
            // the gizmo Local/Snap controls live in Settings, not here -- sits above the
            // remaining center, so it reads as a sliver between Objects and Properties/Settings
            // rather than a bar spanning the whole width. Left/Right split FIRST so Playback's
            // Up split then only carves the narrower center strip left over.
            ImGuiID centerId = dockspaceId;
            const ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f, nullptr, &centerId);
            ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.34f, nullptr, &centerId);
            const ImGuiID playbackId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Up, 0.0415f, nullptr, &centerId);
            const ImGuiID rightTopId = ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.55f, nullptr, &rightId);
            const ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, nullptr, &centerId);
            ImGui::DockBuilderDockWindow("Playback", playbackId);
            ImGui::DockBuilderDockWindow("Objects", leftId);
            ImGui::DockBuilderDockWindow("Properties", rightTopId);
            ImGui::DockBuilderDockWindow("Settings", rightId);
            ImGui::DockBuilderDockWindow("Contents", bottomId);
            // Playback reads as a fixed toolbar, not a document -- no tab/title to show or drag.
            if (ImGuiDockNode *playbackNode = ImGui::DockBuilderGetNode(playbackId)) {
                playbackNode->SetLocalFlags(ImGuiDockNodeFlags_NoTabBar);
            }
            ImGui::DockBuilderFinish(dockspaceId);
        }
#endif

        // --- Playback: Play / Pause / Step / Stop for the fixed-timestep sim (M1.2). Docked
        // as the top strip set up above, with its dock node's tab bar suppressed (NoTabBar) so
        // it reads as a fixed toolbar, not a document with a title. Editing (default): nothing
        // simulates. Playing: today's M1.1 accumulator runs. Paused: frozen mid-play, scene
        // stays put. Stop always restores sceneBackup -- Play is a disposable sandbox, never a
        // permanent edit, which is what makes testing future gameplay/physics safe to experiment
        // with. Mode itself has no text readout -- Play/Pause's label plus Step/Stop's enabled
        // state already tell the whole story.
        // playbackOpen captures the pre-Begin value: Begin(..., &showPlayback) may flip
        // showPlayback to false itself (the window's own close button), but Begin was still
        // CALLED this frame whenever we entered here, so End() below must still be paired
        // against that, not against showPlayback's possibly-just-changed value (same reasoning
        // the other panels below use for their own hierarchyOpen/inspectorOpen/debugOpen).
        const bool playbackOpen = showPlayback;
        if (playbackOpen &&
            ImGui::Begin("Playback", &showPlayback,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            // Row 1: Play/Pause, Step, Stop -- same fixed width (sized off "Pause", the longest
            // label) so the group centers cleanly and reads as one toolbar instead of 3 buttons
            // each hugging their own label.
            const float btnW = ImGui::CalcTextSize(ICON_FA_FORWARD_STEP).x + ImGui::GetStyle().FramePadding.x * 4.0f + 8.0f;
            const float rowWidth = btnW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - rowWidth) * 0.5f);

            const bool isPlaying = (mode == EditorMode::Playing);
            if (ImGui::Button(isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(btnW, btnW))) {
                if (mode == EditorMode::Editing) {
                    sceneBackup = scene; // snapshot: Stop restores exactly this
                    mode = EditorMode::Playing;
                    accumulator = 0.0;
                    toon::CreateScripts(scene); // fire OnCreate once, entering this Play session
                    BuildPhysicsWorld(physicsWorld, scene); // seed bodies from collider-bearing entities
                } else if (mode == EditorMode::Playing) {
                    mode = EditorMode::Paused;
                } else { // Paused -> resume
                    mode = EditorMode::Playing;
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(isPlaying); // stepping while already continuously ticking isn't meaningful
            if (ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(btnW, btnW))) {
                if (mode == EditorMode::Editing) {
                    sceneBackup = scene;
                    mode = EditorMode::Paused; // step lands paused, not playing
                    accumulator = 0.0;
                    toon::CreateScripts(scene); // fire OnCreate once, entering this Play session
                    BuildPhysicsWorld(physicsWorld, scene); // seed bodies from collider-bearing entities
                }
                stepRequested = true;
                suppressNextFrameHistory = true; // one tick's worth of pose jump, not smooth motion
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(mode == EditorMode::Editing); // nothing to stop yet
            if (ImGui::Button(ICON_FA_STOP, ImVec2(btnW, btnW))) {
                physicsWorld.Clear(); // release this session's bodies before the scene reverts
                scene = sceneBackup; // discard everything Play did -- see the panel comment above
                mode = EditorMode::Editing;
                accumulator = 0.0;
                suppressNextFrameHistory = true;
            }
            ImGui::EndDisabled();
        }
        if (playbackOpen) { ImGui::End(); }

        // --- Objects: select / add / duplicate / delete / drag-drop reparent -----
        // A flat list over scene.entities (parents always precede children), indented by
        // depth so it reads as a tree. Structural edits reorder the vector and invalidate
        // indices, so the loop only RECORDS a pending op / drop and applies them afterward.
        enum class HierOp { None, AddChild, Duplicate, Delete };
        HierOp pendingOp = HierOp::None;
        int pendingTarget = -1;
        enum class DropKind { Child, Before, After };
        int dropSrc = -1, dropDst = -1;
        DropKind dropKind = DropKind::Child;

        // hierarchyOpen captures the pre-Begin value: Begin(..., &showHierarchy) may flip
        // showHierarchy to false itself (the window's own close button), but Begin was still
        // CALLED this frame whenever we entered here, so End() below must still be paired
        // against that, not against showHierarchy's possibly-just-changed value.
        const bool hierarchyOpen = showHierarchy;
        if (hierarchyOpen && ImGui::Begin("Objects", &showHierarchy)) {
            const int n = static_cast<int>(scene.entities.size());
            for (int i = 0; i < n; ++i) {
                const toon::Entity &e = scene.entities[i];
                const bool isRoot = (e.parent == -1);

                // Depth = length of the parent chain (drives the indent).
                int depth = 0;
                for (int p = e.parent, guard = 0; p >= 0 && p < n && guard < n; p = scene.entities[p].parent, ++guard) {
                    ++depth;
                }

                ImGui::PushID(i);
                if (depth > 0) { ImGui::Indent(depth * 16.0f); }

                const bool selected = (scene.selected == i);
                if (ImGui::Selectable(e.name.c_str(), selected, ImGuiSelectableFlags_SpanAvailWidth)) {
                    scene.selected = selected ? -1 : i; // click toggles selection off
                }

                // Drag source (everything but the root).
                if (!isRoot && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("TOON_ENTITY_IDX", &i, sizeof(int));
                    ImGui::Text("%s", e.name.c_str());
                    ImGui::EndDragDropSource();
                }
                // Drop target: cursor-Y within the row picks the zone — top/bottom quarter =
                // sibling before/after, middle = make-child (the root only accepts children).
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("TOON_ENTITY_IDX")) {
                        const ImVec2 rmin = ImGui::GetItemRectMin();
                        const ImVec2 rmax = ImGui::GetItemRectMax();
                        const float frac = (ImGui::GetIO().MousePos.y - rmin.y) / (rmax.y - rmin.y);
                        dropSrc = *static_cast<const int *>(pl->Data);
                        dropDst = i;
                        if (isRoot) {
                            dropKind = DropKind::Child;
                        } else if (frac < 0.25f) {
                            dropKind = DropKind::Before;
                        } else if (frac > 0.75f) {
                            dropKind = DropKind::After;
                        } else {
                            dropKind = DropKind::Child;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                // Right-click: structural ops (Duplicate/Delete disabled on the root).
                if (ImGui::BeginPopupContextItem()) {
                    scene.selected = i;
                    if (ImGui::MenuItem("Add Child")) {
                        pendingOp = HierOp::AddChild;
                        pendingTarget = i;
                    }
                    if (ImGui::MenuItem("Duplicate", nullptr, false, !isRoot)) {
                        pendingOp = HierOp::Duplicate;
                        pendingTarget = i;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", nullptr, false, !isRoot)) {
                        pendingOp = HierOp::Delete;
                        pendingTarget = i;
                    }
                    ImGui::EndPopup();
                }

                if (depth > 0) { ImGui::Unindent(depth * 16.0f); }
                ImGui::PopID();
            }
        }
        if (hierarchyOpen) { ImGui::End(); }

        // Apply the one recorded structural op, then the drag-drop — indices are stable now.
        switch (pendingOp) {
            case HierOp::AddChild:
                scene.selected = toon::AddChildEntity(scene, pendingTarget, "Entity");
                break;
            case HierOp::Duplicate: {
                const int d = toon::DuplicateEntity(scene, pendingTarget);
                if (d >= 0) { scene.selected = d; }
            } break;
            case HierOp::Delete:
                toon::DeleteEntity(scene, pendingTarget);
                break;
            case HierOp::None:
                break;
        }
        if (dropSrc >= 0 && dropDst >= 0) {
            if (dropKind == DropKind::Child) {
                toon::ReparentEntity(scene, dropSrc, dropDst);
            } else {
                toon::MoveEntityAsSibling(scene, dropSrc, dropDst, dropKind == DropKind::Before);
            }
        }

        // --- Properties: edit the selected entity (name / transform / material) -----------
        const bool inspectorOpen = showInspector; // see hierarchyOpen's comment above
        if (inspectorOpen && ImGui::Begin("Properties", &showInspector)) {
            if (scene.selected < 0 || scene.selected >= static_cast<int>(scene.entities.size())) {
                ImGui::TextDisabled("Select an entity in the hierarchy.");
            } else {
                toon::Entity &e = scene.entities[scene.selected];
                const bool isRoot = (e.parent == -1);

                char nameBuf[128];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { e.name = nameBuf; }

                // Transform — rotation shown in DEGREES for editing, stored as a quaternion
                // (core/renderer.h's Transform::rotation); QuatToEuler/QuatFromEuler
                // (core/math.h) convert at this widget boundary only. Euler is re-derived
                // from the live quaternion every frame rather than cached, so a value can
                // display renormalized (e.g. 190 shown as -170) and, near gimbal lock, the
                // other two axes can jump when one is edited — the same trade-off Unity's
                // inspector accepts without its extra hidden-Euler-cache bookkeeping.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Transform");
                    toon::Transform &t = *e.transform;
                    constexpr float kRad2Deg = 57.29578f, kDeg2Rad = 0.01745329f;
                    ImGui::DragFloat3("Position", &t.position.x, 0.01f);
                    const toon::Vec3 eulerRad = toon::QuatToEuler(t.rotation);
                    float deg[3] = {eulerRad.x * kRad2Deg, eulerRad.y * kRad2Deg, eulerRad.z * kRad2Deg};
                    if (ImGui::DragFloat3("Rotation", deg, 0.5f)) {
                        t.rotation = toon::QuatFromEuler({deg[0] * kDeg2Rad, deg[1] * kDeg2Rad, deg[2] * kDeg2Rad});
                    }
                    ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
                } else if (isRoot) {
                    ImGui::TextDisabled("(scene root — a pure anchor, no transform)");
                }

                // Material — only for renderables (a mesh or a model).
                if (e.mesh != toon::MeshHandle::Invalid || e.model != toon::ModelHandle::Invalid) {
                    ImGui::SeparatorText("Material");
                    ImGui::ColorEdit3("Base color", &e.material.baseColor.x);
                    ImGui::ColorEdit3("Outline color", &e.material.outlineColor.x);
                    ImGui::DragFloat("Outline width", &e.material.outlineWidth, 0.001f, 0.0f, 0.5f, "%.3f");
                    ImGui::SliderFloat("Roughness", &e.material.roughness, 0.0f, 1.0f);
                }

                // Light — a true optional component (core/scene.h): Add/Remove it directly,
                // rather than assuming it's attached in code. Direction isn't a field here:
                // it comes from the entity's rotation (aim it with the gizmo, like
                // Material's transform above).
                if (e.light) {
                    ImGui::SeparatorText("Light");
                    if (ImGui::Button("Remove Light")) {
                        e.light.reset();
                    } else {
                        ImGui::ColorEdit3("Color", &e.light->color.x);
                        ImGui::DragFloat("Intensity", &e.light->intensity, 0.01f, 0.0f, 10.0f, "%.2f");
                        ImGui::TextDisabled("Aim: rotate this entity (gizmo R).");
                    }
                } else {
                    ImGui::SeparatorText("Light");
                    if (ImGui::Button("Add Light")) { e.light = toon::LightComponent{}; }
                }

                // Collider and Rigid Body (M2.1) — two fully independent optional
                // components (core/scene.h), each Add/Remove-able on its own; neither gates
                // the other's visibility here, even though a RigidBody only does anything
                // once the entity also has a Collider (BuildPhysicsWorld, below, silently
                // skips a body with no collider). Both need a transform to be placed at,
                // same gate as the Transform section above. Edits here only take effect on
                // the NEXT Play session -- the physics world is (re)built once when Play
                // starts, not continuously re-read from these fields while it's running.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Collider");
                    if (e.collider) {
                        if (ImGui::Button("Remove Collider")) {
                            e.collider.reset();
                        } else {
                            const char *kShapeNames[] = {"Box", "Sphere", "Capsule"};
                            int shapeIdx = static_cast<int>(e.collider->shape);
                            if (ImGui::Combo("Shape", &shapeIdx, kShapeNames, IM_ARRAYSIZE(kShapeNames))) {
                                e.collider->shape = static_cast<toon::ColliderShape>(shapeIdx);
                            }
                            switch (e.collider->shape) {
                                case toon::ColliderShape::Box:
                                    ImGui::DragFloat3("Half-extents", &e.collider->extents.x, 0.01f, 0.001f, 100.0f);
                                    break;
                                case toon::ColliderShape::Sphere:
                                    ImGui::DragFloat("Radius", &e.collider->extents.x, 0.01f, 0.001f, 100.0f);
                                    break;
                                case toon::ColliderShape::Capsule:
                                    ImGui::DragFloat("Half-height", &e.collider->extents.x, 0.01f, 0.001f, 100.0f);
                                    ImGui::DragFloat("Radius", &e.collider->extents.y, 0.01f, 0.001f, 100.0f);
                                    break;
                            }
                        }
                    } else {
                        if (ImGui::Button("Add Collider")) { e.collider = toon::ColliderComponent{}; }
                    }

                    ImGui::SeparatorText("Rigid Body");
                    if (e.body) {
                        if (ImGui::Button("Remove Rigid Body")) {
                            e.body.reset();
                        } else {
                            const char *kTypeNames[] = {"Static", "Dynamic", "Kinematic"};
                            int typeIdx = static_cast<int>(e.body->type);
                            if (ImGui::Combo("Type", &typeIdx, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
                                e.body->type = static_cast<toon::BodyType>(typeIdx);
                            }
                            ImGui::BeginDisabled(e.body->type != toon::BodyType::Dynamic); // ignored otherwise
                            ImGui::DragFloat("Mass", &e.body->mass, 0.01f, 0.001f, 1000.0f);
                            ImGui::EndDisabled();
                            ImGui::DragFloat("Friction", &e.body->friction, 0.01f, 0.0f, 2.0f);
                            ImGui::DragFloat("Restitution", &e.body->restitution, 0.01f, 0.0f, 1.0f);
                        }
                    } else {
                        if (ImGui::Button("Add Rigid Body")) { e.body = toon::RigidBodyComponent{}; }
                    }
                }

                // Scripts (core/script.h) — a vector, not a single optional component: an
                // entity can carry several independent scripts at once (M1.3's own
                // reasoning, e.g. a Health script alongside a PlayerMovement script), so
                // each attached script gets its own Remove button, and "Add Script" picks a
                // registered type by name (the same registry CreateScript/serialization use)
                // rather than a single attach/detach toggle. Editing a script's OWN fields
                // (e.g. SpinScript's axis/speed) is deliberately not exposed here — that
                // needs Script to grow its own ImGui-drawing hook (beyond Save/Load), a
                // separate, larger feature this pass doesn't include.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Scripts");

                    int pendingRemove = -1;
                    for (int si = 0; si < static_cast<int>(e.scripts.size()); ++si) {
                        ImGui::PushID(si);
                        ImGui::TextUnformatted(e.scripts[si].name.c_str());
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) { pendingRemove = si; }
                        ImGui::PopID();
                    }
                    if (pendingRemove >= 0) { e.scripts.erase(e.scripts.begin() + pendingRemove); }

                    const std::vector<std::string> availableScripts = toon::GetRegisteredScriptNames();
                    if (availableScripts.empty()) {
                        ImGui::TextDisabled("(no script types registered)");
                    } else {
                        static int addScriptTypeIdx = 0;
                        addScriptTypeIdx = std::min(addScriptTypeIdx, static_cast<int>(availableScripts.size()) - 1);
                        if (ImGui::BeginCombo("##AddScriptType", availableScripts[addScriptTypeIdx].c_str())) {
                            for (int ti = 0; ti < static_cast<int>(availableScripts.size()); ++ti) {
                                const bool isSelected = (ti == addScriptTypeIdx);
                                if (ImGui::Selectable(availableScripts[ti].c_str(), isSelected)) { addScriptTypeIdx = ti; }
                                if (isSelected) { ImGui::SetItemDefaultFocus(); }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Add Script")) {
                            const std::string &typeName = availableScripts[addScriptTypeIdx];
                            if (auto instance = toon::CreateScript(typeName)) {
                                e.scripts.push_back({typeName, std::move(instance)});
                            }
                        }
                    }
                }
            }
        }
        if (inspectorOpen) { ImGui::End(); }

        // --- Transform gizmo over the scene (ImGuizmo) -----------------------------------
        // Manipulate the selected entity's world matrix; on edit, fold the parent back out and
        // decompose to its local TRS (SetEntityWorldMatrix). Diligent's row-major matrices feed
        // ImGuizmo (column-major) directly — the conventions are transposes, so the raw 16
        // floats already match, no explicit transpose needed.
        if (scene.selected > 0 && scene.selected < static_cast<int>(scene.entities.size()) &&
            scene.entities[scene.selected].transform) {
            toon::Mat4 view, proj;
            renderer.GetViewProj(view, proj);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::AllowAxisFlip(false); // show true axis directions (don't auto-face camera)
            ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
            ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
            toon::Mat4 world = scene.entities[scene.selected].worldMatrix;
            const bool snapping = gizmoSnap || io.KeyCtrl;
            const float step = (gizmoOp == ImGuizmo::ROTATE)  ? snapRotateDeg
                               : (gizmoOp == ImGuizmo::SCALE) ? snapScale
                                                              : snapTranslate;
            const float snapVec[3] = {step, step, step};
            if (ImGuizmo::Manipulate(view.m, proj.m, gizmoOp, gizmoMode, world.m, nullptr,
                                     snapping ? snapVec : nullptr)) {
                toon::SetEntityWorldMatrix(scene, scene.selected, world);
            }
        }

        const bool debugOpen = showDebug; // see hierarchyOpen's comment above
        if (debugOpen && ImGui::Begin("Settings", &showDebug)) {
            ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

            // Gizmo editing settings (moved from Properties) -- Local space toggle, snap
            // toggle, and the active op's step size, squeezed to ~4 characters since this is a
            // quick-glance toolbar value, not a precision input.
            ImGui::SeparatorText("Snapping");
            bool localSpace = (gizmoMode == ImGuizmo::LOCAL);
            if (ImGui::Checkbox("Local", &localSpace)) { gizmoMode = localSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD; }
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &gizmoSnap);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("0000").x + ImGui::GetStyle().FramePadding.x * 2.0f);
            if (gizmoOp == ImGuizmo::ROTATE) {
                ImGui::DragFloat("Angle", &snapRotateDeg, 1.0f, 1.0f, 90.0f, "%.0f");
            } else if (gizmoOp == ImGuizmo::SCALE) {
                ImGui::DragFloat("Scale", &snapScale, 0.05f, 0.001f, 10.0f, "%.2f");
            } else {
                ImGui::DragFloat("Move", &snapTranslate, 0.1f, 0.001f, 10.0f, "%.2f");
            }
            // Theme lives in the View menu now, and Save/Load in the File menu + Contents
            // (double-click a .scene) -- both dropped here as redundant with those.
            ImGui::SeparatorText("Shader");
            ImGui::SliderFloat("Bands", &style.bands, 1.0f, 8.0f, "%.0f");
            ImGui::SliderFloat("Ambient", &style.ambient, 0.0f, 1.0f);
            ImGui::SliderFloat("Outline Thickness", &outlineScale, 0.0f, 3.0f);

            ImGui::SeparatorText("Camera");
            ImGui::TextDisabled("Right-drag: orbit (+WASD/QE fly)");
            ImGui::TextDisabled("Mid-drag: pan | Scroll: zoom | F: focus");
            ImGui::TextDisabled(toon::Input::GamepadCount() > 0
                                    ? "Gamepad: left stick fly, right stick orbit"
                                    : "Gamepad: left stick fly, right stick orbit (none connected)");
            ImGui::TextDisabled("Rebind: edit assets/input.json, then relaunch.");
            ImGui::SliderAngle("FOV", &camera.fovY, 20.0f, 100.0f);
            if (ImGui::Button("Reset camera")) { camera = cameraDefault; }
            ImGui::Checkbox("Run Scripts", &runScripts);

            ImGui::SeparatorText("Physics");
            ImGui::Checkbox("Show Colliders", &showColliders);

            ImGui::SeparatorText("Post Processing (HDR)");
            ImGui::Checkbox("Tone map (ACES)", &post.toneMap);
            ImGui::SliderFloat("Exposure", &post.exposure, 0.1f, 4.0f);

            ImGui::Checkbox("Bloom", &post.bloom);
            if (post.bloom) {
                ImGui::SliderFloat("Intensity", &post.bloomIntensity, 0.0f, 1.0f);
                ImGui::SliderFloat("Threshold", &post.bloomThreshold, 0.0f, 1.5f);
                ImGui::SliderFloat("Soft knee", &post.bloomSoftKnee, 0.0f, 1.0f);
                ImGui::SliderFloat("Bloom radius", &post.bloomRadius, 0.3f, 0.85f);
            }

            ImGui::Checkbox("SSAO", &post.ssao);
            if (post.ssao) {
                ImGui::SliderFloat("AO strength", &post.ssaoStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("AO radius", &post.ssaoRadius, 0.1f, 3.0f);
                ImGui::Checkbox("AO temporal (motion-vector denoise)", &post.ssaoTemporal);
            }

            ImGui::Checkbox("Shadows (cascaded shadow maps)", &post.shadows);

            ImGui::Checkbox("Depth of field", &post.dof);
            if (post.dof) {
                ImGui::SliderFloat("Focus distance", &post.dofFocusDist, 3.0f, 25.0f);
                ImGui::SliderFloat("Aperture (f-stop)", &post.dofFStop, 1.0f, 16.0f);
                ImGui::SliderFloat("Max blur (CoC)", &post.dofMaxCoC, 0.0f, 0.05f, "%.3f");
            }

            ImGui::Checkbox("TAA (softens toon edges)", &post.taa);

            ImGui::Checkbox("SSR (reflections in the ground)", &post.ssr);
            if (post.ssr) { ImGui::SliderFloat("Reflection strength", &post.ssrStrength, 0.0f, 1.5f); }
        }
        if (debugOpen) { ImGui::End(); }

        // Contents: passive navigation/preview, except a double-clicked .scene file, which
        // loads through the same path as the File menu's Open Scene (see loadScene).
        // FileBrowser::Render owns its Begin/End internally (no p_open param), so unlike the
        // three panels above, hiding it via the View menu has no in-panel close button.
        if (showAssetBrowser) {
            if (const std::string activated = assetBrowser.Render(renderer);
                !activated.empty() && std::filesystem::path(activated).extension() == ".scene") {
                loadScene(activated.c_str());
            }
        }

        renderer.EndUI();

        renderer.EndFrame();
    }

    toon::Input::Shutdown();
    assetBrowser.Shutdown(renderer); // frees cached thumbnails; must run before the device does
    physicsWorld.Shutdown();
    renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}