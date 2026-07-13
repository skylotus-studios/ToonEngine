//============================================================================
//  ui/thumbnail_cache.cpp — see thumbnail_cache.h.
//============================================================================
#include "ui/thumbnail_cache.h"

#include <algorithm>

namespace toon {

    namespace {
        const char *kImageExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    }

    bool ThumbnailCache::IsImageFile(const std::string &ext) {
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const char *e : kImageExtensions) {
            if (lower == e) { return true; }
        }
        return false;
    }

    TextureHandle ThumbnailCache::Get(Renderer &renderer, const std::string &path) {
        if (const auto it = cache.find(path); it != cache.end()) { return it->second; }
        if (failed.count(path) > 0) { return TextureHandle::Invalid; }

        const TextureHandle handle = renderer.LoadTexture(path.c_str());
        if (handle != TextureHandle::Invalid) {
            cache[path] = handle;
        } else {
            failed.insert(path);
        }
        return handle;
    }

    void ThumbnailCache::Clear(Renderer &renderer) {
        for (const auto &[path, handle] : cache) {
            renderer.DestroyTexture(handle);
        }
        cache.clear();
        failed.clear();
    }

} // namespace toon
