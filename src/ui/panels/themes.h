#pragma once
//============================================================================
//  ui/panels/themes.h: editor color themes (ported from ToonEngineOld/src/ui/themes.cpp).
//============================================================================
struct GLFWwindow;

namespace toon {

    enum class Theme { AmberYellow, GruvboxHard, GrayStone, Count };

    const char *ThemeName(Theme t);

    // Reset the style to ImGui's defaults, apply the selected theme, then scale every size to
    // the display's DPI (the themes' pixel metrics are authored at 1x). Colors a theme leaves
    // unset keep ImGui's dark defaults. Also carries the theme into the native title bar via
    // `window` (see themes.cpp's SetTitleBarTheme call).
    void ApplyTheme(Theme t, float dpiScale, GLFWwindow *window);

} // namespace toon
