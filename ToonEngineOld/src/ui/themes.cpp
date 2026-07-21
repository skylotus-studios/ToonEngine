// ImGui theme and font management.

#include "ui/themes.h"

#include <cmath>
#include <imgui.h>

const char* ThemeName(Theme t) {
    switch (t) {
    case Theme::AmberYellow: return "Amber Yellow";
    case Theme::GruvboxHard: return "Gruvbox Hard";
    case Theme::GrayStone:   return "Gray Stone";
    default:                 return "?";
    }
}

// ---------------------------------------------------------------------
// Amber Yellow
// ---------------------------------------------------------------------
static void ApplyAmberYellow() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowPadding   = ImVec2(8.0f, 8.0f);
    style.FramePadding    = ImVec2(5.0f, 3.0f);
    style.CellPadding     = ImVec2(6.0f, 4.0f);
    style.ItemSpacing     = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize   = 12.0f;
    style.GrabMinSize     = 10.0f;

    style.WindowRounding    = 2.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 1.0f;

    colors[ImGuiCol_Text]                 = ImVec4(1.00f, 0.95f, 0.80f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.45f, 0.30f, 1.00f);

    colors[ImGuiCol_WindowBg]             = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.07f, 0.07f, 0.06f, 0.96f);

    colors[ImGuiCol_Border]               = ImVec4(0.30f, 0.25f, 0.10f, 0.80f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.25f, 0.22f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);

    colors[ImGuiCol_TitleBg]              = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.20f, 0.18f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);

    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.40f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.55f, 0.50f, 0.20f, 1.00f);

    colors[ImGuiCol_CheckMark]            = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.70f, 0.60f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);

    colors[ImGuiCol_Tab]                  = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_TabSelected]          = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
    colors[ImGuiCol_TabDimmed]            = ImVec4(0.08f, 0.08f, 0.07f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]    = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);

    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.25f, 0.20f, 0.10f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.95f, 0.80f, 0.10f, 0.25f);
    colors[ImGuiCol_DragDropTarget]       = ImVec4(1.00f, 0.85f, 0.00f, 0.90f);
    colors[ImGuiCol_NavCursor]            = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);

    colors[ImGuiCol_DockingPreview]       = ImVec4(0.95f, 0.80f, 0.10f, 0.40f);
    colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
}

// ---------------------------------------------------------------------
// Gruvbox Hard
// ---------------------------------------------------------------------
static void ApplyGruvboxHard() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowPadding   = ImVec2(10.0f, 10.0f);
    style.FramePadding    = ImVec2(6.0f, 4.0f);
    style.ItemSpacing     = ImVec2(8.0f, 4.0f);
    style.ScrollbarSize   = 14.0f;
    style.GrabMinSize     = 12.0f;

    style.WindowRounding    = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;

    colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.86f, 0.70f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.57f, 0.51f, 0.45f, 1.00f);

    colors[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.11f, 0.13f, 0.13f, 0.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.11f, 0.13f, 0.13f, 0.95f);

    colors[ImGuiCol_Border]               = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    colors[ImGuiCol_FrameBg]              = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);

    colors[ImGuiCol_TitleBg]              = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);

    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.15f, 0.14f, 0.13f, 1.00f);

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.57f, 0.51f, 0.45f, 1.00f);

    colors[ImGuiCol_CheckMark]            = ImVec4(0.72f, 0.73f, 0.15f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.51f, 0.65f, 0.60f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.55f, 0.73f, 0.67f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.80f, 0.20f, 0.15f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);

    colors[ImGuiCol_Tab]                  = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_TabSelected]          = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);

    colors[ImGuiCol_PlotLines]            = ImVec4(0.98f, 0.74f, 0.18f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_NavCursor]            = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);

    colors[ImGuiCol_DockingPreview]       = ImVec4(0.72f, 0.73f, 0.15f, 0.50f);
    colors[ImGuiCol_DockingEmptyBg]       = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
}

// ---------------------------------------------------------------------
// Gray Stone (One Dark inspired)
// ---------------------------------------------------------------------
static ImVec4 FromARGB(uint32_t argb) {
    return ImVec4(
        ((argb >> 16) & 0xFF) / 255.0f,
        ((argb >>  8) & 0xFF) / 255.0f,
        ((argb      ) & 0xFF) / 255.0f,
        ((argb >> 24) & 0xFF) / 255.0f
    );
}

static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        std::lerp(a.x, b.x, t),
        std::lerp(a.y, b.y, t),
        std::lerp(a.z, b.z, t),
        std::lerp(a.w, b.w, t)
    );
}

static void ApplyGrayStone() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowBorderSize    = 3.0f;
    style.FrameRounding       = 3.0f;
    style.PopupRounding       = 3.0f;
    style.ScrollbarRounding   = 3.0f;
    style.GrabRounding        = 3.0f;
    style.DockingSeparatorSize = 3.0f;

    colors[ImGuiCol_Text]                    = FromARGB(0xFFABB2BF);
    colors[ImGuiCol_TextDisabled]            = FromARGB(0xFF565656);
    colors[ImGuiCol_WindowBg]                = FromARGB(0xFF282C34);
    colors[ImGuiCol_ChildBg]                 = FromARGB(0xFF21252B);
    colors[ImGuiCol_PopupBg]                 = FromARGB(0xFF2E323A);
    colors[ImGuiCol_Border]                  = FromARGB(0xFF2E323A);
    colors[ImGuiCol_BorderShadow]            = FromARGB(0x00000000);
    colors[ImGuiCol_FrameBg]                 = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_FrameBgHovered]          = FromARGB(0xFF484C52);
    colors[ImGuiCol_FrameBgActive]           = FromARGB(0xFF54575D);
    colors[ImGuiCol_TitleBg]                 = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_TitleBgActive]           = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TitleBgCollapsed]        = FromARGB(0x8221252B);
    colors[ImGuiCol_MenuBarBg]               = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_ScrollbarBg]             = colors[ImGuiCol_PopupBg];
    colors[ImGuiCol_ScrollbarGrab]           = FromARGB(0xFF3E4249);
    colors[ImGuiCol_ScrollbarGrabHovered]    = FromARGB(0xFF484C52);
    colors[ImGuiCol_ScrollbarGrabActive]     = FromARGB(0xFF54575D);
    colors[ImGuiCol_CheckMark]               = colors[ImGuiCol_Text];
    colors[ImGuiCol_SliderGrab]              = FromARGB(0xFF353941);
    colors[ImGuiCol_SliderGrabActive]        = FromARGB(0xFF7A7A7A);
    colors[ImGuiCol_Button]                  = colors[ImGuiCol_SliderGrab];
    colors[ImGuiCol_ButtonHovered]           = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_ButtonActive]            = colors[ImGuiCol_ScrollbarGrabActive];
    colors[ImGuiCol_Header]                  = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_HeaderHovered]           = FromARGB(0xFF353941);
    colors[ImGuiCol_HeaderActive]            = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_Separator]               = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_SeparatorHovered]        = FromARGB(0xFF3E4452);
    colors[ImGuiCol_SeparatorActive]         = colors[ImGuiCol_SeparatorHovered];
    colors[ImGuiCol_ResizeGrip]              = colors[ImGuiCol_Separator];
    colors[ImGuiCol_ResizeGripHovered]       = colors[ImGuiCol_SeparatorHovered];
    colors[ImGuiCol_ResizeGripActive]        = colors[ImGuiCol_SeparatorActive];
    colors[ImGuiCol_InputTextCursor]         = FromARGB(0xFF528BFF);
    colors[ImGuiCol_TabHovered]              = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_Tab]                     = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TabSelected]             = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_TabSelectedOverline]     = colors[ImGuiCol_HeaderActive];
    colors[ImGuiCol_TabDimmed]               = LerpColor(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
    colors[ImGuiCol_TabDimmedSelected]       = LerpColor(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_DockingPreview]          = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_DockingEmptyBg]          = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_PlotLines]               = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]        = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]           = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]    = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]           = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_TableBorderStrong]       = colors[ImGuiCol_SliderGrab];
    colors[ImGuiCol_TableBorderLight]        = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TableRowBg]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]           = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextLink]                = FromARGB(0xFF3F94CE);
    colors[ImGuiCol_TextSelectedBg]          = FromARGB(0xFF243140);
    colors[ImGuiCol_TreeLines]               = colors[ImGuiCol_Text];
    colors[ImGuiCol_DragDropTarget]          = colors[ImGuiCol_Text];
    colors[ImGuiCol_NavCursor]               = colors[ImGuiCol_TextLink];
    colors[ImGuiCol_NavWindowingHighlight]   = colors[ImGuiCol_Text];
    colors[ImGuiCol_NavWindowingDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]        = FromARGB(0xC821252B);
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

void ApplyTheme(Theme t) {
    // Reset to ImGui defaults first so themes don't bleed into each other.
    ImGui::StyleColorsDark();
    ImGui::GetStyle() = ImGuiStyle();

    switch (t) {
    case Theme::AmberYellow: ApplyAmberYellow(); break;
    case Theme::GruvboxHard: ApplyGruvboxHard(); break;
    case Theme::GrayStone:   ApplyGrayStone();   break;
    default: break;
    }

    // Global style overrides that apply to every theme. Kept here (after
    // the reset + Apply*) so they survive theme switches.
    ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;
}
