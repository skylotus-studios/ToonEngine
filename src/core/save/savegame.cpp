//============================================================================
//  core/save/savegame.cpp: see savegame.h.
//
//  Line-based text like core/scene/serializer.cpp, but opened in BINARY mode: the trailing
//  `gameBlob` is a length-prefixed raw-byte section, and Windows text-mode \r\n translation
//  would desync those bytes from their stored count. The header lines are plain ASCII written
//  with '\n', so binary mode reads them back identically anyway.
//
//  Format:
//    # ToonEngine save
//    version 1
//    scene assets/scenes/default.scene
//    playtime 42.500000
//    blob <byteCount>
//    <byteCount raw bytes, to EOF>
//============================================================================
#include "core/save/savegame.h"

#include "core/platform/paths.h" // UserData::SaveSlot (slot -> file path)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace toon {

    bool WriteSave(const std::string &path, const SaveGame &s) {
        std::error_code ec; // create_directories: report via return value, never throw
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) {
            std::fprintf(stderr, "Failed to write save: %s\n", path.c_str());
            return false;
        }

        f << "# ToonEngine save\n";
        f << "version " << kSaveVersion << "\n";
        // scenePath is written as the whole rest of the line (an absolute path can contain
        // spaces, e.g. "Program Files"), so ReadSave reads the line remainder, not one token.
        f << "scene " << s.scenePath << "\n";
        char playtimeBuf[64];
        std::snprintf(playtimeBuf, sizeof(playtimeBuf), "playtime %.6f\n", s.playtimeSeconds);
        f << playtimeBuf;

        // Length-prefixed trailing blob: the count, then exactly that many raw bytes to EOF. This
        // is the seam the real save schema grows into (see savegame.h) -- an opaque section today,
        // named fields/lines later.
        f << "blob " << s.gameBlob.size() << "\n";
        if (!s.gameBlob.empty()) { f.write(s.gameBlob.data(), static_cast<std::streamsize>(s.gameBlob.size())); }

        std::printf("Save written: %s (playtime %.1fs)\n", path.c_str(), s.playtimeSeconds);
        return true;
    }

    bool ReadSave(const std::string &path, SaveGame &out) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            // Missing file is the common "no save yet" case: log softly, still a false return.
            std::fprintf(stderr, "No save to read: %s\n", path.c_str());
            return false;
        }

        // Parse into a side buffer; only assign `out` on full success, so a malformed file leaves
        // the caller's state untouched (same contract as LoadScene).
        SaveGame loaded;
        bool sawVersion = false;
        std::string line;

        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') { continue; }

            std::istringstream ss(line);
            std::string key;
            ss >> key;

            if (key == "version") {
                ss >> loaded.version;
                if (loaded.version > kSaveVersion) {
                    // A file from a newer build than this one: refuse rather than misread it. The
                    // caller falls back to a fresh game.
                    std::fprintf(stderr, "Save %s is version %d, newer than this build (%d); ignoring.\n",
                                 path.c_str(), loaded.version, kSaveVersion);
                    return false;
                }
                sawVersion = true;
            } else if (key == "scene") {
                // Rest of the line after "scene " (may contain spaces); tolerate a scene-less save.
                if (line.size() > 6) { loaded.scenePath = line.substr(6); }
            } else if (key == "playtime") {
                ss >> loaded.playtimeSeconds;
            } else if (key == "blob") {
                std::size_t count = 0;
                ss >> count;
                if (count > 0) {
                    // The get pointer sits at the first blob byte (getline consumed the '\n'). Read
                    // exactly `count` bytes; a short read means a truncated/corrupt file.
                    std::vector<char> buf(count);
                    f.read(buf.data(), static_cast<std::streamsize>(count));
                    if (static_cast<std::size_t>(f.gcount()) != count) {
                        std::fprintf(stderr, "Save %s: truncated blob (wanted %zu bytes).\n", path.c_str(), count);
                        return false;
                    }
                    loaded.gameBlob.assign(buf.data(), count);
                }
                break; // blob is the final section
            }
        }

        if (!sawVersion) {
            std::fprintf(stderr, "Save %s: missing version line, treating as invalid.\n", path.c_str());
            return false;
        }

        out = std::move(loaded);
        std::printf("Save read: %s (version %d, playtime %.1fs)\n", path.c_str(), out.version, out.playtimeSeconds);
        return true;
    }

    bool SaveExists(int slot) {
        const std::string path = UserData::SaveSlot(slot);
        if (path.empty()) { return false; }
        std::error_code ec;
        return std::filesystem::exists(path, ec) && !ec;
    }

    bool DeleteSave(int slot) {
        const std::string path = UserData::SaveSlot(slot);
        if (path.empty()) { return false; }
        std::error_code ec;
        return std::filesystem::remove(path, ec) && !ec;
    }

} // namespace toon
