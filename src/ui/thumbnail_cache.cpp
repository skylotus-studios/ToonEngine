#include "ui/thumbnail_cache.h"

#include <algorithm>

static const char* kImageExts[] = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};

bool ThumbnailCache::IsImageFile(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto e : kImageExts)
        if (lower == e) return true;
    return false;
}

TextureHandle ThumbnailCache::Get(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) return it->second;
    if (failed_.count(path)) return {};

    TextureHandle h = LoadTexture(path.c_str());
    if (h) {
        cache_[path] = h;
    } else {
        failed_.insert(path);
    }
    return h;
}

void ThumbnailCache::Clear() {
    for (auto& [_, h] : cache_)
        DestroyTexture(h);
    cache_.clear();
    failed_.clear();
}
