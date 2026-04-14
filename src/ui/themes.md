## Amber Yellow

void SetupImGuiAmberYellowStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // --- 1. Sizing and Spacing (Sharp & Technical) ---
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    // --- 2. Borders & Rounding (Low rounding for a technical look) ---
    style.WindowRounding = 2.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    // --- 3. Color Palette ---

    // Text
    colors[ImGuiCol_Text] = ImVec4(1.00f, 0.95f, 0.80f, 1.00f); // Soft cream-yellow
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.45f, 0.30f, 1.00f);

    // Backgrounds
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f); // Near black
    colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.06f, 0.96f);

    // Borders
    colors[ImGuiCol_Border] = ImVec4(0.30f, 0.25f, 0.10f, 0.80f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frames (Inputs, Checkboxes, etc.)
    colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.22f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);

    // Title Bars
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.18f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);

    // Menus
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.11f, 0.08f, 1.00f);

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.04f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.40f, 0.15f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.55f, 0.50f, 0.20f, 1.00f);

    // Interactables (The High-Vis Amber)
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.60f, 0.10f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.30f, 0.25f, 0.05f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.38f, 0.10f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.35f, 0.30f, 0.10f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.08f, 0.07f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.14f, 0.10f, 1.00f);

    // Tables
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.35f, 0.30f, 0.15f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.25f, 0.20f, 0.10f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    // Misc
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.95f, 0.80f, 0.10f, 0.25f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 0.85f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.95f, 0.80f, 0.10f, 1.00f);

#ifdef IMGUI_HAS_DOCK
    colors[ImGuiCol_DockingPreview] = ImVec4(0.95f, 0.80f, 0.10f, 0.40f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);
#endif
}

## Gruvbox Hard

void SetupImGuiGruvboxHardStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // --- 1. Sizing and Spacing (Industrial & Square) ---
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    // --- 2. Borders & Rounding (Gruvbox usually looks best with sharp or low rounding) ---
    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    // --- 3. The Gruvbox Dark Hard Palette ---
    // Background: #1d2021 (Dark Hard) | Foreground: #ebdbb2
    // Red: #fb4934 | Green: #b8bb26 | Yellow: #fabd2f | Blue: #83a598
    // Gray: #928374 | Orange: #fe8019

    // Text
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.86f, 0.70f, 1.00f); // #ebdbb2
    colors[ImGuiCol_TextDisabled] = ImVec4(0.57f, 0.51f, 0.45f, 1.00f); // #928374

    // Backgrounds
    colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f); // #1d2021
    colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.13f, 0.13f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.13f, 0.13f, 0.95f);

    // Borders
    colors[ImGuiCol_Border] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f); // #504945
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frames
    colors[ImGuiCol_FrameBg] = ImVec4(0.24f, 0.22f, 0.21f, 1.00f); // #3c3836
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f); // #504945
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f); // #665c54

    // Title Bars
    colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);

    // Menus
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.14f, 0.13f, 1.00f); // #282828

    // Scrollbars
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.57f, 0.51f, 0.45f, 1.00f);

    // Interactables
    colors[ImGuiCol_CheckMark] = ImVec4(0.72f, 0.73f, 0.15f, 1.00f); // #b8bb26 (Green)
    colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.65f, 0.60f, 1.00f); // #83a598 (Blue)
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.73f, 0.67f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.98f, 0.29f, 0.20f, 1.00f); // #fb4934 (Red)
    colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.20f, 0.15f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.36f, 0.33f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.24f, 0.22f, 0.21f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);

    // Misc
    colors[ImGuiCol_PlotLines] = ImVec4(0.98f, 0.74f, 0.18f, 1.00f); // #fabd2f (Yellow)
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.31f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);

#ifdef IMGUI_HAS_DOCK
    colors[ImGuiCol_DockingPreview] = ImVec4(0.72f, 0.73f, 0.15f, 0.50f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.11f, 0.13f, 0.13f, 1.00f);
#endif
}

## Gray Stone

{
    auto& style{ ImGui::GetStyle() };
    // Borders
    style.WindowBorderSize = 3.0f;

    // Rounding
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;

    // Docking
    style.DockingSeparatorSize = 3.0f;
}

{
    constexpr auto ToRGBA = [](uint32_t argb) constexpr
    {
        ImVec4 color{};
        color.x = ((argb >> 16) & 0xFF) / 255.0f;
        color.y = ((argb >> 8) & 0xFF) / 255.0f;
        color.z = (argb & 0xFF) / 255.0f;
        color.w = ((argb >> 24) & 0xFF) / 255.0f;
        return color;
    };

    constexpr auto Lerp = [](const ImVec4& a, const ImVec4& b, float t) constexpr
    {
        return ImVec4{
            std::lerp(a.x, b.y, t),
            std::lerp(a.y, b.y, t),
            std::lerp(a.z, b.z, t),
            std::lerp(a.w, b.w, t)
        };
    };

    auto colors{ style.Colors };
    colors[ImGuiCol_Text] = ToRGBA(0xFFABB2BF);
    colors[ImGuiCol_TextDisabled] = ToRGBA(0xFF565656);
    colors[ImGuiCol_WindowBg] = ToRGBA(0xFF282C34);
    colors[ImGuiCol_ChildBg] = ToRGBA(0xFF21252B);
    colors[ImGuiCol_PopupBg] = ToRGBA(0xFF2E323A);
    colors[ImGuiCol_Border] = ToRGBA(0xFF2E323A);
    colors[ImGuiCol_BorderShadow] = ToRGBA(0x00000000);
    colors[ImGuiCol_FrameBg] = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_FrameBgHovered] = ToRGBA(0xFF484C52);
    colors[ImGuiCol_FrameBgActive] = ToRGBA(0xFF54575D);
    colors[ImGuiCol_TitleBg] = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TitleBgCollapsed] = ToRGBA(0x8221252B);
    colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_ScrollbarBg] = colors[ImGuiCol_PopupBg];
    colors[ImGuiCol_ScrollbarGrab] = ToRGBA(0xFF3E4249);
    colors[ImGuiCol_ScrollbarGrabHovered] = ToRGBA(0xFF484C52);
    colors[ImGuiCol_ScrollbarGrabActive] = ToRGBA(0xFF54575D);
    colors[ImGuiCol_CheckMark] = colors[ImGuiCol_Text];
    colors[ImGuiCol_SliderGrab] = ToRGBA(0xFF353941);
    colors[ImGuiCol_SliderGrabActive] = ToRGBA(0xFF7A7A7A);
    colors[ImGuiCol_Button] = colors[ImGuiCol_SliderGrab];
    colors[ImGuiCol_ButtonHovered] = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_ButtonActive] = colors[ImGuiCol_ScrollbarGrabActive];
    colors[ImGuiCol_Header] = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_HeaderHovered] = ToRGBA(0xFF353941);
    colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_Separator] = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_SeparatorHovered] = ToRGBA(0xFF3E4452);
    colors[ImGuiCol_SeparatorActive] = colors[ImGuiCol_SeparatorHovered];
    colors[ImGuiCol_ResizeGrip] = colors[ImGuiCol_Separator];
    colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_SeparatorHovered];
    colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_SeparatorActive];
    colors[ImGuiCol_InputTextCursor] = ToRGBA(0xFF528BFF);
    colors[ImGuiCol_TabHovered] = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_Tab] = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TabSelected] = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_HeaderActive];
    colors[ImGuiCol_TabDimmed] = Lerp(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
    colors[ImGuiCol_TabDimmedSelected] = Lerp(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4{ 0.50f, 0.50f, 0.50f, 0.00f };
    colors[ImGuiCol_DockingPreview] = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_DockingEmptyBg] = colors[ImGuiCol_WindowBg];
    colors[ImGuiCol_PlotLines] = ImVec4{ 0.61f, 0.61f, 0.61f, 1.00f };
    colors[ImGuiCol_PlotLinesHovered] = ImVec4{ 1.00f, 0.43f, 0.35f, 1.00f };
    colors[ImGuiCol_PlotHistogram] = ImVec4{ 0.90f, 0.70f, 0.00f, 1.00f };
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4{ 1.00f, 0.60f, 0.00f, 1.00f };
    colors[ImGuiCol_TableHeaderBg] = colors[ImGuiCol_ChildBg];
    colors[ImGuiCol_TableBorderStrong] = colors[ImGuiCol_SliderGrab];
    colors[ImGuiCol_TableBorderLight] = colors[ImGuiCol_FrameBgActive];
    colors[ImGuiCol_TableRowBg] = ImVec4{ 0.00f, 0.00f, 0.00f, 0.00f };
    colors[ImGuiCol_TableRowBgAlt] = ImVec4{ 1.00f, 1.00f, 1.00f, 0.06f };
    colors[ImGuiCol_TextLink] = ToRGBA(0xFF3F94CE);
    colors[ImGuiCol_TextSelectedBg] = ToRGBA(0xFF243140);
    colors[ImGuiCol_TreeLines] = colors[ImGuiCol_Text];
    colors[ImGuiCol_DragDropTarget] = colors[ImGuiCol_Text];
    colors[ImGuiCol_NavCursor] = colors[ImGuiCol_TextLink];
    colors[ImGuiCol_NavWindowingHighlight] = colors[ImGuiCol_Text];
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4{ 0.80f, 0.80f, 0.80f, 0.20f };
    colors[ImGuiCol_ModalWindowDimBg] = ToRGBA(0xC821252B);
};