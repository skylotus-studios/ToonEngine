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

#include <cstdint>
#include <cstdio>
#include <filesystem> // .scene extension check, routing an asset-browser double-click to loadScene
#include <string>
#include <vector>

namespace {
    // A spinning entity: which scene entity, and the axis its local rotation animates around
    // (each frame, rotationEuler = axis * angle).
    struct Spinner {
        int entity;
        toon::Vec3 axis;
    };

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
    std::vector<Spinner> spinners; // entities whose local rotation animates each frame

    // Ground plane beneath the objects (catches their SSAO contact shadows; no spin/outline).
    {
        toon::Entity &e = scene.entities[toon::AddEntity(scene, 0, "Ground")];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Plane(5.0f));
        e.transform->position = {0.0f, -1.05f, 0.0f};
        e.material.baseColor = {0.60f, 0.60f, 0.63f};
        e.material.outlineWidth = 0.0f;
        e.material.roughness = 0.05f; // smooth -> reflective (SSR)
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
        spinners.push_back({i, {0.0f, 1.0f, 0.0f}});
    }
    // Cube — the satellite's parent.
    const int cubeIdx = toon::AddEntity(scene, 0, "Cube");
    {
        toon::Entity &e = scene.entities[cubeIdx];
        SetPrimitive(renderer, e, toon::PrimitiveDesc::Cube(0.9f));
        e.material = toon::Material{{0.30f, 0.45f, 0.85f}, {0.02f, 0.02f, 0.05f}, 0.050f};
        e.material.roughness = 0.15f;
        spinners.push_back({cubeIdx, {0.5f, 1.0f, 0.0f}});
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
        spinners.push_back({i, {1.0f, 0.0f, 0.0f}});
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
        spinners.push_back({i, {0.0f, 1.0f, 0.0f}});
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
    // browser's double-click-a-.scene behavior (below): LoadScene resets scene.selected and
    // invalidates every spinners[] index (see core/serializer.h), so spinners must be
    // cleared at every call site, not just the button's own.
    auto loadScene = [&](const char *path) {
        if (toon::LoadScene(path, scene, camera, renderer)) {
            spinners.clear(); // stale entity indices into the scene LoadScene just replaced
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
        spinners.clear();
        scene.selected = -1;
        camera = cameraDefault;
        sceneStatus = "New scene.";
    };

    // Contents: browses assets/ with thumbnails; passive besides double-click, which
    // this routes through loadScene when the activated file is a .scene.
    toon::FileBrowser assetBrowser;
    assetBrowser.Init(TOON_ASSETS_DIR);

    bool spin = true;
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
        // variable frame rate above, so gameplay state (spin today; entity Update hooks /
        // physics later -- see CLAUDE.md's roadmap) evolves deterministically. Usually one step
        // per frame; zero if rendering outruns the sim rate, several if the sim fell behind.
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

                // Animate the spinning entities' local rotation incrementally (added to whatever
                // rotationEuler currently is) -- the stand-in for a future per-entity Update hook.
                // Incremental rather than an absolute axis*sharedClock formula so a gizmo-set
                // orientation (set while paused) is the new baseline spin continues from on resume,
                // instead of the whole spin group snapping back to where a shared clock says it
                // "should" be.
                if (spin) {
                    constexpr float kSpinRate = 0.6f; // radians/sec
                    for (const Spinner &s : spinners) {
                        if (scene.entities[s.entity].transform) {
                            scene.entities[s.entity].transform->rotationEuler =
                                scene.entities[s.entity].transform->rotationEuler +
                                s.axis * static_cast<float>(kFixedDt * kSpinRate);
                        }
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
        // drag, any ImGui widget being edited, Spin continuously animating, or a Stop-restore/
        // Step from the Playback panel last frame (a pose jump, not smooth motion) all mean
        // post-fx temporal history shouldn't be trusted this frame.
        const bool suppressTemporalHistory =
            gizmoActive || ImGui::IsAnyItemActive() || spin || suppressNextFrameHistory;
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
                ImGui::MenuItem("Spin", nullptr, &spin);
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
            // A Playback strip across the very top (M1.2) -- two rows tall (transport +
            // gizmo settings), hence 0.10 rather than a single row's ~0.06; Objects on the far
            // left; Properties (top) + Settings (bottom) stacked on the right; Contents along
            // the bottom of what's left; the 3D scene shows through the remaining pass-through
            // center. The top split runs FIRST so every other split operates on the region
            // already below it.
            ImGuiID centerId = dockspaceId;
            const ImGuiID playbackId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Up, 0.10f, nullptr, &centerId);
            const ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f, nullptr, &centerId);
            ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.34f, nullptr, &centerId);
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

        // --- Playback: Play / Pause / Step / Stop for the fixed-timestep sim (M1.2), plus the
        // gizmo's Local/Snap/step-size controls (moved here from Properties -- they're global
        // editing settings, not per-object, so they don't need a selection to be usable). Docked
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
            const float btnW = ImGui::CalcTextSize("Pause").x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
            const float rowWidth = btnW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - rowWidth) * 0.5f);

            const bool isPlaying = (mode == EditorMode::Playing);
            if (ImGui::Button(isPlaying ? "Pause" : "Play", ImVec2(btnW, 0.0f))) {
                if (mode == EditorMode::Editing) {
                    sceneBackup = scene; // snapshot: Stop restores exactly this
                    mode = EditorMode::Playing;
                    accumulator = 0.0;
                } else if (mode == EditorMode::Playing) {
                    mode = EditorMode::Paused;
                } else { // Paused -> resume
                    mode = EditorMode::Playing;
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(isPlaying); // stepping while already continuously ticking isn't meaningful
            if (ImGui::Button("Step", ImVec2(btnW, 0.0f))) {
                if (mode == EditorMode::Editing) {
                    sceneBackup = scene;
                    mode = EditorMode::Paused; // step lands paused, not playing
                    accumulator = 0.0;
                }
                stepRequested = true;
                suppressNextFrameHistory = true; // one tick's worth of pose jump, not smooth motion
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(mode == EditorMode::Editing); // nothing to stop yet
            if (ImGui::Button("Stop", ImVec2(btnW, 0.0f))) {
                scene = sceneBackup; // discard everything Play did -- see the panel comment above
                spinners.clear();    // wholesale restore invalidates cached indices, same reason loadScene clears it
                mode = EditorMode::Editing;
                accumulator = 0.0;
                suppressNextFrameHistory = true;
            }
            ImGui::EndDisabled();

            // Row 2: gizmo editing settings (moved from Properties) -- Local space toggle, snap
            // toggle, and the active op's step size, squeezed to ~4 characters since this is a
            // quick-glance toolbar value, not a precision input.
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

                // Transform — rotation shown in DEGREES for editing, stored in radians.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Transform");
                    toon::Transform &t = *e.transform;
                    constexpr float kRad2Deg = 57.29578f, kDeg2Rad = 0.01745329f;
                    ImGui::DragFloat3("Position", &t.position.x, 0.01f);
                    float deg[3] = {t.rotationEuler.x * kRad2Deg, t.rotationEuler.y * kRad2Deg,
                                    t.rotationEuler.z * kRad2Deg};
                    if (ImGui::DragFloat3("Rotation", deg, 0.5f)) {
                        t.rotationEuler = {deg[0] * kDeg2Rad, deg[1] * kDeg2Rad, deg[2] * kDeg2Rad};
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

                // Light — only for light entities. Direction isn't a field here: it comes
                // from the entity's rotation (aim it with the gizmo, like Material's
                // transform above).
                if (e.light) {
                    ImGui::SeparatorText("Light");
                    ImGui::ColorEdit3("Color", &e.light->color.x);
                    ImGui::DragFloat("Intensity", &e.light->intensity, 0.01f, 0.0f, 10.0f, "%.2f");
                    ImGui::TextDisabled("Aim: rotate this entity (gizmo R).");
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
            ImGui::Checkbox("Spin", &spin);

            ImGui::SeparatorText("Post (HDR)");
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
    renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}