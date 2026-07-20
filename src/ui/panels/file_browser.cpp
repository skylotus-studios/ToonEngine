//============================================================================
//  ui/panels/file_browser.cpp — see file_browser.h.
//============================================================================
#include "ui/panels/file_browser.h"

#include "imgui.h"             // ImGui is seam-exempt — UI code may call it directly
#include "IconsFontAwesome6.h" // ICON_FA_* glyphs, merged into the UI font in main.cpp

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace fs = std::filesystem;

namespace toon {

    namespace {
        // Extensions the current engine's loader pipeline recognizes as a 3D asset (DiligentTools'
        // glTF loader handles .gltf/.glb; .fbx is listed for the icon tag only — the engine has no
        // FBX loader — so it still shows up, just not as "load-able").
        const char *kModelExtensions[] = {".gltf", ".glb", ".fbx"};

        bool IsModelFile(const std::string &ext) {
            std::string lower = ext;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            for (const char *e : kModelExtensions) {
                if (lower == e) { return true; }
            }
            return false;
        }

        std::string FormatSize(uintmax_t bytes) {
            if (bytes == 0) { return ""; }
            if (bytes < 1024) { return std::to_string(bytes) + " B"; }
            char buf[32];
            if (bytes < 1024 * 1024) {
                std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
            } else {
                std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
            }
            return buf;
        }

        // fs::file_time_type's clock isn't system_clock (and isn't even the same type across
        // standard libraries), so there's no direct conversion to a display-able calendar time
        // before C++20's clock_cast (this project is C++17). The portable workaround: measure
        // `ft`'s offset from the file clock's "now" and apply that same offset to the system
        // clock's "now" — exact given both `now()` calls happen back-to-back.
        std::string FormatTime(fs::file_time_type ft) {
            const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            const std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
            return buf;
        }

        // --- Scanning -----------------------------------------------------------------

        void Sort(FileBrowser &browser) {
            std::sort(
                browser.entries.begin(), browser.entries.end(),
                [&browser](const FileBrowserEntry &a, const FileBrowserEntry &b) {
                    if (a.isDirectory != b.isDirectory) {
                        return a.isDirectory > b.isDirectory; // folders first
                    }
                    switch (browser.sortColumn) {
                        case 1: // Modified
                            return browser.sortAscending ? (a.lastWrite < b.lastWrite) : (a.lastWrite > b.lastWrite);
                        case 2: // Size
                            return browser.sortAscending ? (a.size < b.size) : (a.size > b.size);
                        case 3: // Type
                            return browser.sortAscending ? (a.extension < b.extension) : (a.extension > b.extension);
                        default: // Name
                            return browser.sortAscending ? (a.name < b.name) : (a.name > b.name);
                    }
                });
        }

        void Refresh(FileBrowser &browser) {
            browser.entries.clear();
            browser.selectedIdx = -1;

            std::error_code ec;
            for (const auto &it : fs::directory_iterator(browser.currentDir, ec)) {
                // No .gitignore filter here (unlike the ToonEngineOld reference) — rooted at assets/,
                // there's nothing under it that needs hiding besides stray dotfiles.
                const std::string name = it.path().filename().string();
                if (!name.empty() && name[0] == '.') { continue; }

                FileBrowserEntry e;
                e.name = name;
                e.fullPath = it.path();
                e.isDirectory = it.is_directory(ec);
                e.extension = it.path().extension().string();
                std::transform(e.extension.begin(), e.extension.end(), e.extension.begin(), ::tolower);
                e.lastWrite = fs::last_write_time(it.path(), ec);
                if (!e.isDirectory) { e.size = fs::file_size(it.path(), ec); }
                browser.entries.push_back(std::move(e));
            }

            browser.needsRefresh = false;
            Sort(browser);
        }

        void NavigateTo(FileBrowser &browser, const fs::path &dir) {
            browser.currentDir = dir;
            browser.needsRefresh = true;
        }
    } // namespace

    void InitFileBrowser(FileBrowser &browser, const char *rootDir) {
        browser.root = rootDir;
        browser.currentDir = browser.root;
        browser.needsRefresh = true;
    }

    // --- Draw -----------------------------------------------------------------

    std::string RenderFileBrowser(FileBrowser &browser, Renderer &renderer) {
        if (browser.needsRefresh) { Refresh(browser); }

        std::string activated; // a file (never a directory) double-clicked this frame

        ImGui::SetNextWindowPos(ImVec2(800, 36), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(700, 320), ImGuiCond_FirstUseEver);
        ImGui::Begin("Contents");

        // Breadcrumb bar: "«" jumps to root, then one button per path segment below it.
        {
            const auto rel = fs::relative(browser.currentDir, browser.root);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
            if (ImGui::Button(ICON_FA_HOUSE "##nav_root")) { NavigateTo(browser, browser.root); }
            ImGui::PopStyleVar();
            ImGui::SameLine();

            fs::path accum = browser.root;
            ImGui::Text("%s", browser.root.filename().string().c_str());
            for (const auto &seg : rel) {
                if (seg == ".") { continue; }
                accum /= seg;
                ImGui::SameLine();
                ImGui::Text("/");
                ImGui::SameLine();
                if (ImGui::SmallButton(seg.string().c_str())) { NavigateTo(browser, accum); }
            }
        }
        ImGui::Separator();

        // Two-panel layout via a resizable outer table, so the user can drag the border between
        // the file list and the preview pane.
        if (!ImGui::BeginTable("##browser_layout", 2,
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize)) {
            ImGui::End();
            return activated;
        }
        ImGui::TableSetupColumn("##files", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        // -- File table (left column) --------------------------------------------
        ImGui::TableNextColumn();
        ImGui::BeginChild("##filetable", ImVec2(0, 0));

        constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable |
                                                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                ImGuiTableFlags_SizingStretchProp;

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::BeginTable("files", 4, kTableFlags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
            ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_None, 0.0f, 1);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 0.0f, 2);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None, 0.0f, 3);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs()) {
                if (specs->SpecsDirty && specs->SpecsCount > 0) {
                    browser.sortColumn = static_cast<int>(specs->Specs[0].ColumnUserID);
                    browser.sortAscending = (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                    Sort(browser);
                    specs->SpecsDirty = false;
                }
            }

            for (int i = 0; i < static_cast<int>(browser.entries.size()); ++i) {
                const FileBrowserEntry &e = browser.entries[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                constexpr float kPad = 6.0f;

                // Name column: a small inline icon/thumbnail, then the name.
                ImGui::TableNextColumn();
                ImGui::Indent(kPad);

                constexpr float kIconSize = 16.0f;
                const float textH = ImGui::GetTextLineHeight();
                const float padY = (textH > kIconSize) ? (textH - kIconSize) * 0.5f : 0.0f;
                const ImVec2 cursor = ImGui::GetCursorPos();

                if (e.isDirectory) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), ICON_FA_FOLDER);
                } else if (IsImageFile(e.extension)) {
                    if (const TextureHandle th = GetThumbnail(browser.thumbnails, renderer, e.fullPath.string());
                        th != TextureHandle::Invalid) {
                        ImGui::SetCursorPosY(cursor.y + padY);
                        // Default UVs (0,0)-(1,1): Diligent/Vulkan decodes images top-origin, unlike
                        // the GL reference this was ported from (which flipped V here).
                        ImGui::Image(static_cast<ImTextureID>(renderer.GetTextureImGuiID(th)),
                                     ImVec2(kIconSize, kIconSize));
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_FILE_IMAGE);
                    }
                } else if (IsModelFile(e.extension)) {
                    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), ICON_FA_CUBE);
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ICON_FA_FILE);
                }
                ImGui::SameLine();
                ImGui::SetCursorPosY(cursor.y);

                const bool selected = (browser.selectedIdx == i);
                if (ImGui::Selectable(e.name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    browser.selectedIdx = i;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (e.isDirectory) {
                            NavigateTo(browser, e.fullPath);
                        } else {
                            activated = e.fullPath.string();
                        }
                    }
                }
                ImGui::Unindent(kPad);

                // Modified column.
                ImGui::TableNextColumn();
                ImGui::Indent(kPad);
                ImGui::TextUnformatted(FormatTime(e.lastWrite).c_str());
                ImGui::Unindent(kPad);

                // Size column.
                ImGui::TableNextColumn();
                ImGui::Indent(kPad);
                if (!e.isDirectory) { ImGui::TextUnformatted(FormatSize(e.size).c_str()); }
                ImGui::Unindent(kPad);

                // Type column.
                ImGui::TableNextColumn();
                ImGui::Indent(kPad);
                ImGui::TextUnformatted(e.isDirectory ? "Folder" : e.extension.c_str());
                ImGui::Unindent(kPad);

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        ImGui::EndChild();

        // -- Preview panel (right column) ----------------------------------------
        ImGui::TableNextColumn();
        ImGui::BeginChild("##previewchild", ImVec2(0, 0));

        if (browser.selectedIdx >= 0 && browser.selectedIdx < static_cast<int>(browser.entries.size())) {
            const FileBrowserEntry &sel = browser.entries[browser.selectedIdx];
            ImGui::Text("%s", sel.name.c_str());
            ImGui::Separator();

            if (!sel.isDirectory && IsImageFile(sel.extension)) {
                if (const TextureHandle th = GetThumbnail(browser.thumbnails, renderer, sel.fullPath.string());
                    th != TextureHandle::Invalid) {
                    uint32_t texW = 0, texH = 0;
                    renderer.GetTextureSize(th, texW, texH);
                    const float aspect = (texH > 0) ? static_cast<float>(texW) / static_cast<float>(texH) : 1.0f;
                    const float dispW = ImGui::GetContentRegionAvail().x - 8.0f;
                    const float dispH = dispW / aspect;
                    ImGui::Image(static_cast<ImTextureID>(renderer.GetTextureImGuiID(th)), ImVec2(dispW, dispH));
                    ImGui::Text("%u x %u", texW, texH);
                }
            }

            if (!sel.isDirectory) {
                ImGui::Text("Size: %s", FormatSize(sel.size).c_str());
                ImGui::Text("Type: %s", sel.extension.c_str());
            }
            ImGui::Text("Modified: %s", FormatTime(sel.lastWrite).c_str());
        } else {
            ImGui::TextDisabled("Select a file to preview");
        }

        ImGui::EndChild();

        ImGui::EndTable(); // outer layout table
        ImGui::End();

        return activated;
    }

    void ShutdownFileBrowser(FileBrowser &browser, Renderer &renderer) { ClearThumbnails(browser.thumbnails, renderer); }

} // namespace toon
