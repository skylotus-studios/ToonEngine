// ImGui theme definitions.

#pragma once

enum class Theme {
    AmberYellow,
    GruvboxHard,
    GrayStone,
    Count
};

const char* ThemeName(Theme t);

// Apply the given theme's style and colors.
void ApplyTheme(Theme t);
