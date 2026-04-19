#include "ui/file_browser.h"
#include "ui/file_filter.h"
#include "ui/thumbnail_cache.h"
#include "core/renderer.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct DirEntry {
    std::string          name;
    fs::path             fullPath;
    bool                 isDirectory = false;
    uintmax_t            size = 0;
    fs::file_time_type   lastWrite{};
    std::string          extension;
};

fs::path             gRoot;
fs::path             gCurrentDir;
std::vector<DirEntry> gEntries;
int                  gSelectedIdx = -1;
std::string          gSelectedPath;
bool                 gNeedsRefresh = true;

FileFilter       gFilter;
ThumbnailCache   gThumbs;

// Sort state.
int  gSortCol   = 0;     // 0=Name, 1=Date, 2=Size, 3=Type
bool gSortAsc   = true;

static const char* kModelExts[] = {".gltf", ".glb", ".fbx"};

bool IsModelFile(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto e : kModelExts)
        if (lower == e) return true;
    return false;
}

std::string FormatSize(uintmax_t bytes) {
    if (bytes == 0) return "";
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {
        char buf[32]; std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
        return buf;
    }
    char buf[32]; std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

std::string FormatTime(fs::file_time_type ft) {
    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    auto tt   = std::chrono::system_clock::to_time_t(sctp);
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

void RefreshListing() {
    gEntries.clear();
    gSelectedIdx = -1;
    gSelectedPath.clear();

    std::error_code ec;
    for (auto& it : fs::directory_iterator(gCurrentDir, ec)) {
        if (gFilter.IsIgnored(it.path(), gRoot)) continue;
        DirEntry e;
        e.name        = it.path().filename().string();
        e.fullPath    = it.path();
        e.isDirectory = it.is_directory();
        e.extension   = it.path().extension().string();
        std::transform(e.extension.begin(), e.extension.end(), e.extension.begin(), ::tolower);
        if (!e.isDirectory) {
            e.size      = fs::file_size(it.path(), ec);
            e.lastWrite = fs::last_write_time(it.path(), ec);
        } else {
            e.lastWrite = fs::last_write_time(it.path(), ec);
        }
        gEntries.push_back(std::move(e));
    }

    gNeedsRefresh = false;
}

void SortEntries() {
    std::sort(gEntries.begin(), gEntries.end(),
        [](const DirEntry& a, const DirEntry& b) {
            if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
            switch (gSortCol) {
            case 1: // Date
                return gSortAsc ? (a.lastWrite < b.lastWrite) : (a.lastWrite > b.lastWrite);
            case 2: // Size
                return gSortAsc ? (a.size < b.size) : (a.size > b.size);
            case 3: // Type
                return gSortAsc ? (a.extension < b.extension) : (a.extension > b.extension);
            default: // Name
                return gSortAsc ? (a.name < b.name) : (a.name > b.name);
            }
        });
}

void NavigateTo(const fs::path& dir) {
    gCurrentDir   = dir;
    gNeedsRefresh = true;
}

} // namespace

void FileBrowserInit(const fs::path& projectRoot) {
    gRoot       = projectRoot;
    gCurrentDir = projectRoot;
    gFilter.LoadGitignore(projectRoot);
    gNeedsRefresh = true;
}

void FileBrowserRefresh() {
    gNeedsRefresh = true;
}

const std::string& FileBrowserSelectedPath() {
    return gSelectedPath;
}

void FileBrowserRender() {
    if (gNeedsRefresh) {
        RefreshListing();
        SortEntries();
    }

    ImGui::SetNextWindowPos(ImVec2(800, 36), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("File Browser");

    // Breadcrumb bar.
    {
        auto rel = fs::relative(gCurrentDir, gRoot);
        if (ImGui::SmallButton("<<"))
            NavigateTo(gRoot);
        ImGui::SameLine();

        fs::path accum = gRoot;
        ImGui::Text("%s", gRoot.filename().string().c_str());
        for (auto& seg : rel) {
            if (seg == ".") continue;
            accum /= seg;
            ImGui::SameLine();
            ImGui::Text("/");
            ImGui::SameLine();
            if (ImGui::SmallButton(seg.string().c_str()))
                NavigateTo(accum);
        }
    }
    ImGui::Separator();

    // Two-panel layout via a resizable outer table so the user can drag
    // the border between the file list and the preview pane.
    if (!ImGui::BeginTable("##browser_layout", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize)) {
        ImGui::End();
        return;
    }
    ImGui::TableSetupColumn("##files",   ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableNextRow();

    // -- File table (left column) --------------------------------------------
    ImGui::TableNextColumn();
    ImGui::BeginChild("##filetable", ImVec2(0, 0));

    ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_SizingStretchProp;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 4.0f));
    if (ImGui::BeginTable("files", 4, flags)) {
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_None,        0.0f, 1);
        ImGui::TableSetupColumn("Size",     ImGuiTableColumnFlags_None,        0.0f, 2);
        ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_None,        0.0f, 3);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty && specs->SpecsCount > 0) {
                gSortCol = static_cast<int>(specs->Specs[0].ColumnUserID);
                gSortAsc = (specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
                SortEntries();
                specs->SpecsDirty = false;
            }
        }

        for (int i = 0; i < static_cast<int>(gEntries.size()); ++i) {
            auto& e = gEntries[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            constexpr float kPad = 6.0f;

            // Name column.
            ImGui::TableNextColumn();
            ImGui::Indent(kPad);

            // Small inline icon, vertically centered with the text row.
            bool isImage = ThumbnailCache::IsImageFile(e.extension);
            constexpr float iconSz = 16.0f;
            float textH  = ImGui::GetTextLineHeight();
            float padY   = (textH > iconSz) ? (textH - iconSz) * 0.5f : 0.0f;
            ImVec2 cursor = ImGui::GetCursorPos();

            if (e.isDirectory) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "[D]");
            } else if (isImage) {
                TextureHandle th = gThumbs.Get(e.fullPath.string());
                if (th) {
                    ImGui::SetCursorPosY(cursor.y + padY);
                    ImGui::Image(reinterpret_cast<ImTextureID>(GetTextureNativeID(th)),
                                 ImVec2(iconSz, iconSz),
                                 ImVec2(0, 1), ImVec2(1, 0));
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[I]");
                }
            } else if (IsModelFile(e.extension)) {
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "[M]");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "   ");
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(cursor.y);

            bool selected = (gSelectedIdx == i);
            if (ImGui::Selectable(e.name.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick)) {
                gSelectedIdx  = i;
                gSelectedPath = e.fullPath.string();
                if (ImGui::IsMouseDoubleClicked(0) && e.isDirectory)
                    NavigateTo(e.fullPath);
            }

            // Drag source for files (not folders).
            if (!e.isDirectory &&
                ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                std::string pathStr = e.fullPath.string();
                ImGui::SetDragDropPayload("TOON_ASSET_PATH",
                    pathStr.c_str(), pathStr.size() + 1);
                ImGui::Text("%s", e.name.c_str());
                ImGui::EndDragDropSource();
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
            if (!e.isDirectory)
                ImGui::TextUnformatted(FormatSize(e.size).c_str());
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

    if (gSelectedIdx >= 0 && gSelectedIdx < static_cast<int>(gEntries.size())) {
        auto& sel = gEntries[gSelectedIdx];
        ImGui::Text("%s", sel.name.c_str());
        ImGui::Separator();

        bool isImage = ThumbnailCache::IsImageFile(sel.extension);
        if (isImage && !sel.isDirectory) {
            TextureHandle th = gThumbs.Get(sel.fullPath.string());
            if (th) {
                int tw = GetTextureWidth(th);
                int th_ = GetTextureHeight(th);
                float aspect = (th_ > 0) ? static_cast<float>(tw) / th_ : 1.0f;
                float dispW  = ImGui::GetContentRegionAvail().x - 8.0f;
                float dispH  = dispW / aspect;
                ImGui::Image(reinterpret_cast<ImTextureID>(GetTextureNativeID(th)),
                             ImVec2(dispW, dispH),
                             ImVec2(0, 1), ImVec2(1, 0));
                ImGui::Text("%d x %d", tw, th_);
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

    ImGui::EndTable();  // outer layout table

    ImGui::End();
}
