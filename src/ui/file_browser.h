#pragma once
//============================================================================
//  ui/file_browser.h — the "Asset Browser" editor panel.
//
//  Browses assets/ with a breadcrumb bar, a sortable file table, and an image
//  preview pane. Ported from ToonEngineOld/src/ui/file_browser.* (see
//  MEMORY.md's carry-over notes) onto the current Renderer seam. Rooted at
//  assets/ only (not the whole repo), so the old .gitignore-based FileFilter
//  is dropped in favor of just hiding dotfiles.
//
//  Passive besides navigation/selection: the one active behavior is reporting
//  a double-clicked file back to the caller. FileBrowser doesn't know what a
//  ".scene" file means — main.cpp decides that (see its loadScene lambda) —
//  so this stays decoupled from scene/serializer semantics.
//============================================================================
#include "core/renderer.h" // Renderer
#include "ui/thumbnail_cache.h"

#include <filesystem>
#include <string>
#include <vector>

namespace toon {

    struct FileBrowser {
        // Sets the browsable root (pass TOON_ASSETS_DIR) and scans it. Call once before the
        // first Render.
        void Init(const char *rootDir);

        // Draws the "Asset Browser" window: breadcrumb, sortable table, preview pane. Returns
        // the path of a file the user double-clicked THIS frame, or an empty string otherwise —
        // directories are navigated internally and never returned.
        std::string Render(Renderer &renderer);

        // Releases cached thumbnail textures. Call once at shutdown, before Renderer::Shutdown.
        void Shutdown(Renderer &renderer);

    private:
        // One scanned directory entry (a file or subdirectory).
        struct Entry {
            std::string name;
            std::filesystem::path fullPath;
            bool isDirectory = false;
            uintmax_t size = 0;
            std::filesystem::file_time_type lastWrite{};
            std::string extension; // lowercase, dot-prefixed (empty for a directory)
        };

        void Refresh();                                    // rescans currentDir into entries
        void Sort();                                       // re-sorts entries by sortColumn
        void NavigateTo(const std::filesystem::path &dir); // changes currentDir, flags a refresh

        std::filesystem::path root;
        std::filesystem::path currentDir;
        std::vector<Entry> entries;
        int selectedIdx = -1;
        bool needsRefresh = true;

        int sortColumn = 0; // 0=Name, 1=Modified, 2=Size, 3=Type
        bool sortAscending = true;

        ThumbnailCache thumbnails;
    };

} // namespace toon
