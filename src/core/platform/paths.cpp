//============================================================================
//  core/platform/paths.cpp: see core/platform/paths.h.
//============================================================================
#include "core/platform/paths.h"

#include <cstdlib> // std::getenv (the non-Windows UserData path)
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h> // GetModuleFileNameW: the OS's "where is my own exe" query
#endif

// Source-tree assets/ path, baked by CMake (target_compile_definitions) as the development
// fallback used only when no assets/ directory sits next to the exe. The #ifndef guard keeps
// this TU compilable on its own even if the define is somehow absent.
#ifndef TOON_ASSET_ROOT
#define TOON_ASSET_ROOT "assets"
#endif

namespace toon {
    namespace Assets {
        namespace {

            std::string g_root; // resolved once by Init(); every accessor below reads it

            // The directory containing the running executable, or an empty path if the platform
            // query fails or isn't implemented yet -- Init() then falls back to TOON_ASSET_ROOT.
            // Read from the OS rather than argv[0]/the working directory so it's correct however
            // the process was launched (shortcut, debugger, Steam), the whole point of this module.
            std::filesystem::path ExecutableDir() {
#if defined(_WIN32)
                wchar_t buf[MAX_PATH];
                const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
                if (n == 0 || n == MAX_PATH) { return {}; } // failed, or truncated (path >= MAX_PATH)
                return std::filesystem::path(buf, buf + n).parent_path();
#elif defined(__linux__)
                std::error_code ec;
                const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
                return ec ? std::filesystem::path{} : exe.parent_path();
#elif defined(__APPLE__)
                // macOS: _NSGetExecutablePath (mach-o dyld). Filled in when the macOS port lands
                // (see CLAUDE.md's Platform Support table); Windows is the only active target today.
                return {};
#else
                return {};
#endif
            }

        } // namespace

        void Init() {
            // Prefer an assets/ directory sitting next to the exe (a packaged build). Resolved via
            // the OS's own executable-path query so it's independent of the working directory.
            if (const std::filesystem::path exeDir = ExecutableDir(); !exeDir.empty()) {
                std::error_code ec;
                const std::filesystem::path bundled = exeDir / "assets";
                if (std::filesystem::exists(bundled, ec)) {
                    g_root = bundled.string();
                    return;
                }
            }
            // Otherwise fall back to the source-tree path CMake baked in (development runs out of
            // build/<preset>/, where nothing staged assets next to the exe).
            g_root = TOON_ASSET_ROOT;
        }

        const std::string &Root() { return g_root; }

        std::string Shaders() { return g_root + "/shaders"; }
        std::string Models() { return g_root + "/models"; }
        std::string Fonts() { return g_root + "/fonts"; }
        std::string Scenes() { return g_root + "/scenes"; }
        std::string Audio() { return g_root + "/audio"; }
        std::string Sprites() { return g_root + "/sprites"; }

        std::string Icon() { return g_root + "/sprites/icon.png"; }
        std::string InputJson() { return g_root + "/input.json"; }

        std::string Sprite(const std::string &file) { return g_root + "/sprites/" + file; }

    } // namespace Assets

    namespace UserData {
        namespace {

            // The OS's per-user writable data directory (no app subfolder yet), or an empty path
            // if it can't be determined. Read from an environment variable rather than
            // SHGetKnownFolderPath so this TU needs no extra link libraries (Shell32/Ole32) beyond
            // the windows.h it already uses; LOCALAPPDATA is reliably set for any interactive
            // Windows session (and for a Steam-launched process). Mirrors ExecutableDir's platform
            // ladder above: macOS stays a stub until that port lands.
            std::filesystem::path PlatformDataDir() {
#if defined(_WIN32)
                wchar_t buf[MAX_PATH];
                const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
                if (n == 0 || n >= MAX_PATH) { return {}; } // unset, or longer than the buffer
                return std::filesystem::path(buf, buf + n);
#elif defined(__linux__)
                if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg && xdg[0] != '\0') {
                    return std::filesystem::path(xdg);
                }
                if (const char *home = std::getenv("HOME"); home && home[0] != '\0') {
                    return std::filesystem::path(home) / ".local" / "share";
                }
                return {};
#else
                return {}; // macOS: filled in when the macOS port lands (see Platform Support)
#endif
            }

            // Return `dir` as a string after ensuring it exists, or "" if it couldn't be created.
            std::string Ensure(const std::filesystem::path &dir) {
                if (dir.empty()) { return {}; }
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                if (ec) { return {}; }
                return dir.string();
            }

        } // namespace

        std::string Root() {
            const std::filesystem::path base = PlatformDataDir();
            if (base.empty()) { return {}; }
            return Ensure(base / "ToonEngine");
        }

        std::string SaveDir() {
            const std::string root = Root();
            if (root.empty()) { return {}; }
            return Ensure(std::filesystem::path(root) / "saves" / "local");
        }

        std::string SaveSlot(int slot) {
            const std::string dir = SaveDir();
            if (dir.empty()) { return {}; }
            return dir + "/slot" + std::to_string(slot) + ".save";
        }

    } // namespace UserData
} // namespace toon
