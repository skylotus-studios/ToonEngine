//============================================================================
//  ui/thumbnail_cache.cpp — see thumbnail_cache.h.
//============================================================================
#include "ui/thumbnail_cache.h"

#include <algorithm>

namespace toon {

    namespace {
        const char *kImageExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    }

    bool IsImageFile(const std::string &ext) {
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for (const char *e : kImageExtensions) {
            if (lower == e) { return true; }
        }
        return false;
    }

    TextureHandle GetThumbnail(ThumbnailCache &thumbnails, Renderer &renderer, const std::string &path) {
        if (const auto it = thumbnails.cache.find(path); it != thumbnails.cache.end()) { return it->second; }
        if (thumbnails.failed.count(path) > 0) { return TextureHandle::Invalid; }

        const TextureHandle handle = renderer.LoadTexture(path.c_str());
        if (handle != TextureHandle::Invalid) {
            thumbnails.cache[path] = handle;
        } else {
            thumbnails.failed.insert(path);
        }
        return handle;
    }

    void ClearThumbnails(ThumbnailCache &thumbnails, Renderer &renderer) {
        for (const auto &[path, handle] : thumbnails.cache) {
            renderer.DestroyTexture(handle);
        }
        thumbnails.cache.clear();
        thumbnails.failed.clear();
    }

} // namespace toon
