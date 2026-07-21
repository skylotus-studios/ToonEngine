#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct FileFilter {
    std::vector<std::string> patterns;

    void LoadGitignore(const std::filesystem::path& root) {
        patterns.clear();
        auto p = root / ".gitignore";
        std::ifstream f(p);
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty()) patterns.push_back(line);
        }
    }

    bool IsIgnored(const std::filesystem::path& entry,
                   const std::filesystem::path& root) const {
        std::string name = entry.filename().string();
        if (!name.empty() && name[0] == '.') return true;

        auto rel = std::filesystem::relative(entry, root).string();
        std::replace(rel.begin(), rel.end(), '\\', '/');

        for (auto& pat : patterns) {
            if (pat.back() == '/') {
                std::string dir = pat.substr(0, pat.size() - 1);
                if (name == dir) return true;
                if (rel.starts_with(dir + "/")) return true;
            } else if (pat.front() == '*' && pat.size() > 1) {
                std::string ext = pat.substr(1);
                if (name.size() >= ext.size() &&
                    name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
                    return true;
            } else {
                if (name == pat) return true;
            }
        }
        return false;
    }
};
