#pragma once

#include "core/renderer.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

class ThumbnailCache {
public:
    TextureHandle Get(const std::string& path);
    void          Clear();

    static bool IsImageFile(const std::string& ext);

private:
    std::unordered_map<std::string, TextureHandle> cache_;
    std::unordered_set<std::string>                failed_;
};
