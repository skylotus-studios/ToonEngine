//============================================================================
//  ui/panels/themes.cpp: see themes.h.
//============================================================================
#include "ui/panels/themes.h"

#include "core/rendering/renderer.h" // toon::SetTitleBarTheme

#include "imgui.h" // ImGui is seam-exempt: UI code may call it directly

#include <GLFW/glfw3.h>

#include <cstdint>

namespace toon {

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

    namespace {

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
            s.WindowRounding = s.ChildRounding = s.FrameRounding = s.PopupRounding = s.ScrollbarRounding =
                s.GrabRounding = s.TabRounding = 2.0f;
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
            s.WindowRounding = s.FrameRounding = s.PopupRounding = s.ScrollbarRounding = s.GrabRounding =
                s.TabRounding = 2.0f;
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

    } // namespace

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
        SetTitleBarTheme(window, {menuBarBg.x, menuBarBg.y, menuBarBg.z}, {text.x, text.y, text.z});
    }

} // namespace toon
