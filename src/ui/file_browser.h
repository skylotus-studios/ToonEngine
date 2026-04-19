#pragma once

#include <filesystem>
#include <string>

void FileBrowserInit(const std::filesystem::path& projectRoot);
void FileBrowserRender();
void FileBrowserRefresh();
const std::string& FileBrowserSelectedPath();
