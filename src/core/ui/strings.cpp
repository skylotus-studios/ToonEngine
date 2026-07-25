//============================================================================
//  core/ui/strings.h implementation.
//============================================================================
#include "core/ui/strings.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>

namespace toon {

    namespace {

        constexpr std::size_t kCount = static_cast<std::size_t>(StrId::Count);

        // Stable data-file keys, index-matched with StrId. A per-language file (roadmap #32)
        // addresses strings by these, so translations survive an English-wording change.
        constexpr std::array<const char *, kCount> kKeyNames = {{
            "title.heading",
            "menu.new_game",
            "menu.continue",
            "menu.quit",
            "pause.heading",
            "menu.resume",
            "menu.quit_to_title",
            "loading.label",
            "hud.time_label",
            "hud.pause_hint",
        }};

        // The live table: English defaults, overridable by LoadStrings. Index-matched with StrId.
        std::array<std::string, kCount> g_strings = {{
            "ToonEngine",
            "New Game",
            "Continue",
            "Quit",
            "Paused",
            "Resume",
            "Quit to Title",
            "Loading...",
            "Time",
            "Esc: pause",
        }};

        // Trim ASCII whitespace from both ends of [begin, end).
        std::string Trim(const std::string &s) {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) { ++a; }
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) { --b; }
            return s.substr(a, b - a);
        }

    } // namespace

    const std::string &Text(StrId id) {
        const std::size_t i = static_cast<std::size_t>(id);
        return i < kCount ? g_strings[i] : g_strings[0]; // out-of-range guard (never in normal use)
    }

    const char *KeyName(StrId id) {
        const std::size_t i = static_cast<std::size_t>(id);
        return i < kCount ? kKeyNames[i] : "";
    }

    bool LoadStrings(const char *path) {
        std::ifstream f(path ? path : "");
        if (!f) {
            std::fprintf(stderr, "LoadStrings: cannot open %s\n", path ? path : "(null)");
            return false;
        }
        std::string line;
        while (std::getline(f, line)) {
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') { continue; }
            const std::size_t eq = trimmed.find('=');
            if (eq == std::string::npos) { continue; }
            const std::string key = Trim(trimmed.substr(0, eq));
            const std::string value = Trim(trimmed.substr(eq + 1));
            for (std::size_t i = 0; i < kCount; ++i) {
                if (key == kKeyNames[i]) {
                    g_strings[i] = value;
                    break;
                }
            }
        }
        return true;
    }

} // namespace toon
