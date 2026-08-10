//============================================================================
//  app/runtime_ui.h implementation: the in-game HUD/menu (roadmap #17).
//============================================================================
#include "app/runtime_ui.h"

#include "app/runtime_state.h"
#include "app/save_glue.h"         // kQuickSaveSlot
#include "core/input/input_system.h"
#include "core/platform/paths.h"   // Assets::Fonts
#include "core/save/savegame.h"    // SaveExists
#include "core/platform/clock.h"   // Clock::Now (RenderHUD's optional ui.solve_ms timing)
#include "core/ui/strings.h"       // Text / StrId (roadmap #17: localization-ready strings)
#include "core/ui/text.h"          // LoadFont

#include <GLFW/glfw3.h> // glfwGetWindowSize / glfwGetFramebufferSize

#include <cstdio>
#include <filesystem> // UI asset hot-reload (Debug)

namespace toon {

    namespace {

        // The 9-slice menu-panel frame (assets/sprites/9slice.png), loaded once by RenderHUD.
        // TextureHandle::Invalid until then, and MenuPanel falls back to a procedural rounded rect.
        TextureHandle g_panelTex = TextureHandle::Invalid;

#ifdef TOON_SHADER_HOT_RELOAD
        // Records `path`'s mtime into `out`; returns false if it can't be stat'd.
        bool FileMTime(const std::string &path, std::filesystem::file_time_type &out) {
            std::error_code ec;
            out = std::filesystem::last_write_time(path, ec);
            return !ec;
        }

        // Poll the UI asset files each frame; reload on an mtime change. Immediate mode makes this
        // trivial -- the next frame's rebuild just uses the new strings/font/texture, with no
        // retained UI state to reconcile. Debug-only (same gate + rationale as the shader hot-reload,
        // roadmap #10); ui.hlsl itself already hot-reloads through that shader watcher, so it isn't
        // polled here.
        void PollUIHotReload(RuntimeState &rs) {
            const std::string stringsPath = Assets::Root() + "/ui/strings.txt";
            const std::string fontJson = Assets::Fonts() + "/ui/ui_font.json";
            const std::string fontPng = Assets::Fonts() + "/ui/ui_font.png";
            const std::string panelPng = Assets::Sprites() + "/9slice.png";

            static std::filesystem::file_time_type tStrings, tFontJson, tFontPng, tPanel;
            static bool seeded = false;
            if (!seeded) { // first call: record baselines so nothing "reloads" on frame one
                seeded = true;
                FileMTime(stringsPath, tStrings);
                FileMTime(fontJson, tFontJson);
                FileMTime(fontPng, tFontPng);
                FileMTime(panelPng, tPanel);
                return;
            }

            std::filesystem::file_time_type t;
            if (FileMTime(stringsPath, t) && t != tStrings) {
                tStrings = t;
                LoadStrings(stringsPath.c_str());
            }
            bool fontChanged = false;
            if (FileMTime(fontJson, t) && t != tFontJson) {
                tFontJson = t;
                fontChanged = true;
            }
            if (FileMTime(fontPng, t) && t != tFontPng) {
                tFontPng = t;
                fontChanged = true;
            }
            if (fontChanged) {
                if (rs.uiFont.atlas != TextureHandle::Invalid) { rs.renderer.DestroyTexture(rs.uiFont.atlas); }
                rs.uiFont = LoadFont(rs.renderer, fontJson.c_str(), fontPng.c_str());
            }
            if (FileMTime(panelPng, t) && t != tPanel) {
                tPanel = t;
                if (g_panelTex != TextureHandle::Invalid) { rs.renderer.DestroyTexture(g_panelTex); }
                g_panelTex = rs.renderer.LoadTexture(panelPng.c_str(), /*srgb=*/false);
            }
        }
#endif

        // Gather this frame's UI input from Input:: (raw/ungated -- the runtime has no ImGui). GLFW
        // reports the cursor in WINDOW pixels; scale to FRAMEBUFFER pixels (the space the UI lays
        // out in) so hit-testing is correct on a HiDPI display. Nav is edge-triggered off the
        // keyboard arrows + Enter/Escape and gamepad 0's d-pad + A/B.
        UIInput GatherInput(RuntimeState &rs, float fbW, float fbH) {
            UIInput in;

            int winW = 0, winH = 0;
            glfwGetWindowSize(rs.window, &winW, &winH);
            const Input::Mouse &mouse = Input::RawMouse();
            in.mouseX = mouse.position.x * (winW > 0 ? fbW / static_cast<float>(winW) : 1.0f);
            in.mouseY = mouse.position.y * (winH > 0 ? fbH / static_cast<float>(winH) : 1.0f);
            in.mouseDown = mouse.IsDown(Input::MouseButton::Left);

            const Input::Keyboard &kb = Input::RawKeyboard();
            bool up = kb.WasPressed(Input::Key::Up);
            bool down = kb.WasPressed(Input::Key::Down);
            bool left = kb.WasPressed(Input::Key::Left);
            bool right = kb.WasPressed(Input::Key::Right);
            bool confirm = kb.WasPressed(Input::Key::Enter) || kb.WasPressed(Input::Key::Space);
            bool cancel = kb.WasPressed(Input::Key::Escape);

            if (Input::GamepadCount() > 0) {
                const Input::Gamepad &pad = Input::GetGamepad(0);
                if (pad.connected) {
                    up |= pad.WasButtonPressed(Input::GamepadButton::DPadUp);
                    down |= pad.WasButtonPressed(Input::GamepadButton::DPadDown);
                    left |= pad.WasButtonPressed(Input::GamepadButton::DPadLeft);
                    right |= pad.WasButtonPressed(Input::GamepadButton::DPadRight);
                    confirm |= pad.WasButtonPressed(Input::GamepadButton::A);
                    cancel |= pad.WasButtonPressed(Input::GamepadButton::B);
                }
            }
            in.navUp = up;
            in.navDown = down;
            in.navLeft = left;
            in.navRight = right;
            in.navConfirm = confirm;
            in.navCancel = cancel;
            return in;
        }

        // A fixed-width, centered-text menu button (equal widths make a tidy column, unlike the
        // text-content-sized default UI_Button). `id` is a STABLE box key (so focus/animation state
        // survives a language swap); the visible label comes from the string table.
        UISignal MenuButton(UIContext &ui, const char *id, StrId label) {
            UIBox *b = UI_MakeBox(ui,
                                  UIBoxFlag_Clickable | UIBoxFlag_DrawBackground | UIBoxFlag_DrawBorder |
                                      UIBoxFlag_DrawText | UIBoxFlag_HotAnimation | UIBoxFlag_ActiveAnimation |
                                      UIBoxFlag_TextCenterX | UIBoxFlag_TextCenterY,
                                  id);
            b->text = Text(label);
            b->padding = 12.0f;
            b->borderThickness = 1.5f;
            b->semanticSize[0] = {UISizeKind::Pixels, 300.0f, 1.0f};
            b->semanticSize[1] = {UISizeKind::TextContent, 0.0f, 1.0f};
            return UI_SignalFromBox(ui, b);
        }

        // A screen-centered menu panel: the 9-slice frame texture if it loaded, else a procedural
        // dark rounded rect. The caller pushes it as parent + fills it.
        UIBox *MenuPanel(UIContext &ui, const char *id) {
            UIBox *panel = UI_Panel(ui, id);
            UI_Anchor(panel, UIAnchor::Center);
            panel->padding = 28.0f; // inset content past the frame's rounded border
            if (g_panelTex != TextureHandle::Invalid) {
                UI_NineSlice(panel, g_panelTex, {44.0f, 44.0f, 44.0f, 44.0f});
                panel->bgColor = {1.0f, 1.0f, 1.0f, 1.0f}; // untinted: show the frame as authored
            } else {
                panel->bgColor = {0.05f, 0.06f, 0.09f, 0.92f};
                panel->borderColor = {0.30f, 0.34f, 0.42f, 1.0f};
                panel->borderThickness = 2.0f;
            }
            return panel;
        }

        void BuildTitle(RuntimeState &rs) {
            UIContext &ui = rs.ui;
            UIBox *panel = MenuPanel(ui, "titleMenu");
            UI_PushParent(ui, panel);
            {
                UIBox *title = UI_Label(ui, Text(StrId::TitleHeading).c_str());
                title->fontSize = 52.0f;
                UI_Spacer(ui, UIAxis::Y, 18.0f);
                if (MenuButton(ui, "title.new_game", StrId::MenuNewGame).clicked) { BeginNewGame(rs); }
                if (SaveExists(kQuickSaveSlot)) {
                    UI_Spacer(ui, UIAxis::Y, 8.0f);
                    if (MenuButton(ui, "title.continue", StrId::MenuContinue).clicked) { BeginContinue(rs); }
                }
                UI_Spacer(ui, UIAxis::Y, 8.0f);
                if (MenuButton(ui, "title.quit", StrId::MenuQuit).clicked) { SetAppState(rs, AppState::Quit); }
            }
            UI_PopParent(ui);
            if (ui.input.navCancel) { SetAppState(rs, AppState::Quit); } // B/Escape quits from the title
        }

        void BuildLoading(RuntimeState &rs) {
            UIContext &ui = rs.ui;
            UIBox *panel = MenuPanel(ui, "loadingPanel");
            UI_PushParent(ui, panel);
            {
                UIBox *label = UI_Label(ui, Text(StrId::Loading).c_str());
                label->fontSize = 30.0f;
                UI_Spacer(ui, UIAxis::Y, 14.0f);

                // A progress bar: a fixed track with an inner fill sized by the load fraction.
                const float frac = rs.loadJob.total > 0
                                       ? static_cast<float>(rs.loadJob.done) / static_cast<float>(rs.loadJob.total)
                                       : 0.0f;
                UIBox *track = UI_Panel(ui, "loadTrack");
                track->padding = 0.0f;
                track->borderThickness = 1.0f;
                track->semanticSize[0] = {UISizeKind::Pixels, 320.0f, 1.0f};
                track->semanticSize[1] = {UISizeKind::Pixels, 16.0f, 1.0f};
                track->bgColor = {0.10f, 0.11f, 0.14f, 1.0f};
                UI_PushParent(ui, track);
                {
                    UIBox *fill = UI_MakeBox(ui, UIBoxFlag_DrawBackground, "loadFill");
                    fill->semanticSize[0] = {UISizeKind::PercentOfParent, frac, 1.0f};
                    fill->semanticSize[1] = {UISizeKind::PercentOfParent, 1.0f, 1.0f};
                    fill->bgColor = {0.42f, 0.75f, 0.62f, 1.0f};
                }
                UI_PopParent(ui);
            }
            UI_PopParent(ui);
        }

        void BuildPlayingHUD(RuntimeState &rs) {
            UIContext &ui = rs.ui;
            // A passive top-left readout, pinned to the corner (playtime + a control hint).
            UIBox *hud = UI_MakeBox(ui, UIBoxFlag_DrawBackground | UIBoxFlag_DrawBorder, "hud");
            UI_Anchor(hud, UIAnchor::TopLeft, 24.0f, 24.0f);
            hud->padding = 12.0f;
            hud->bgColor = {0.04f, 0.05f, 0.07f, 0.70f};
            hud->borderColor = {0.30f, 0.34f, 0.42f, 0.90f};
            hud->borderThickness = 1.5f;
            UI_PushParent(ui, hud);
            {
                char buf[80];
                const int secs = static_cast<int>(rs.playtimeSeconds);
                std::snprintf(buf, sizeof(buf), "%s  %02d:%02d", Text(StrId::HudTimeLabel).c_str(), secs / 60,
                              secs % 60);
                UIBox *t = UI_Label(ui, buf);
                t->fontSize = 26.0f;
                UI_Spacer(ui, UIAxis::Y, 4.0f);
                UIBox *hint = UI_Label(ui, Text(StrId::HudPauseHint).c_str());
                hint->fontSize = 18.0f;
                hint->textColor = {0.70f, 0.74f, 0.80f, 1.0f};
            }
            UI_PopParent(ui);
        }

        void BuildPaused(RuntimeState &rs) {
            UIContext &ui = rs.ui;
            UIBox *panel = MenuPanel(ui, "pauseMenu");
            UI_PushParent(ui, panel);
            {
                UIBox *title = UI_Label(ui, Text(StrId::PausedHeading).c_str());
                title->fontSize = 44.0f;
                UI_Spacer(ui, UIAxis::Y, 16.0f);
                if (MenuButton(ui, "pause.resume", StrId::MenuResume).clicked) { SetAppState(rs, rs.resumeTo); }
                UI_Spacer(ui, UIAxis::Y, 8.0f);
                if (MenuButton(ui, "pause.quit_to_title", StrId::MenuQuitToTitle).clicked) {
                    SetAppState(rs, AppState::Title);
                }
            }
            UI_PopParent(ui);
            if (ui.input.navCancel) { SetAppState(rs, rs.resumeTo); } // B/Escape also resumes
        }

    } // namespace

    UIScreen ScreenForAppState(AppState state) {
        switch (state) {
            case AppState::Title: return UIScreen::Title;
            case AppState::Loading: return UIScreen::Loading;
            case AppState::Playing: return UIScreen::Playing;
            case AppState::Paused: return UIScreen::Paused;
            case AppState::Boot:
            case AppState::Quit: return UIScreen::None;
        }
        return UIScreen::None;
    }

    void RenderHUD(RuntimeState &rs, UIScreen screen, double *uiSolveMsOut) {
        if (screen == UIScreen::None) { return; }

        // Lazy one-time font load (serves the player and the editor's play-in-editor alike).
        if (!rs.uiFontLoaded) {
            rs.uiFontLoaded = true;
            rs.uiFont = LoadFont(rs.renderer, (Assets::Fonts() + "/ui/ui_font.json").c_str(),
                                 (Assets::Fonts() + "/ui/ui_font.png").c_str());
            g_panelTex = rs.renderer.LoadTexture((Assets::Sprites() + "/9slice.png").c_str(), /*srgb=*/false);
            LoadStrings((Assets::Root() + "/ui/strings.txt").c_str()); // external table overrides code defaults
        }
        if (!rs.uiFont.valid()) { return; }

#ifdef TOON_SHADER_HOT_RELOAD
        PollUIHotReload(rs); // Debug-only: live-reload UI strings/font/panel texture on file save
#endif

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(rs.window, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) { return; }

        const UIInput in = GatherInput(rs, static_cast<float>(fbW), static_cast<float>(fbH));
        const double solveStart = uiSolveMsOut ? Clock::Now() : 0.0;
        UI_BeginBuild(rs.ui, in, 1.0f / 60.0f, static_cast<float>(fbW), static_cast<float>(fbH), rs.uiFont);
        switch (screen) {
            case UIScreen::Title: BuildTitle(rs); break;
            case UIScreen::Loading: BuildLoading(rs); break;
            case UIScreen::Playing: BuildPlayingHUD(rs); break;
            case UIScreen::Paused: BuildPaused(rs); break;
            case UIScreen::None: break;
        }
        UI_EndBuild(rs.ui);
        if (uiSolveMsOut) { *uiSolveMsOut = (Clock::Now() - solveStart) * 1000.0; }
        UI_Render(rs.ui, rs.renderer);
    }

} // namespace toon
