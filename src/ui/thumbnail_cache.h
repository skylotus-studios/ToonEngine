#pragma once
//============================================================================
//  ui/thumbnail_cache.h — path -> texture cache for the asset browser.
//
//  Decodes an image file to a GPU texture once per path and remembers both hits and
//  failures, so redrawing the file table every frame never re-decodes anything. Ported
//  from ToonEngineOld/src/ui/thumbnail_cache.*, adapted to the current PIMPL Renderer
//  seam (the old one called free functions: LoadTexture/DestroyTexture/GetTextureNativeID).
//============================================================================
#include "core/renderer.h" // Renderer, TextureHandle

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace toon {

    // Caches decoded image textures by file path, for the asset browser's inline icons and
    // preview pane. Doesn't own a Renderer — every call takes one, so lifetime stays with the
    // caller (the same pattern core/serializer.h's LoadScene uses).
    struct ThumbnailCache {
        // Returns the cached handle for `path`, decoding it via renderer.LoadTexture on first
        // request. Remembers failures too, so a bad file is only ever attempted once.
        // TextureHandle::Invalid if `path` doesn't decode as an image.
        TextureHandle Get(Renderer &renderer, const std::string &path);

        // Releases every cached texture. Call once at shutdown only — never mid-session, since a
        // texture already recorded in this frame's ImGui draw list must not be destroyed.
        void Clear(Renderer &renderer);

        // True if `ext` (a dot-prefixed extension, any case) names a format
        // CreateTextureFromFile can decode.
        static bool IsImageFile(const std::string &ext);

    private:
        std::unordered_map<std::string, TextureHandle> cache;
        std::unordered_set<std::string> failed;
    };

} // namespace toon
