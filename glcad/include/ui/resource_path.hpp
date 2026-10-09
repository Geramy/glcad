#pragma once

#include <filesystem>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace gcapp {

inline std::filesystem::path executable_dir() {
#if defined(__APPLE__)
    uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::vector<char> buf(n + 1, 0);
    if (_NSGetExecutablePath(buf.data(), &n) != 0) return {};
    std::error_code ec;
    auto p = std::filesystem::canonical(buf.data(), ec);
    if (ec) p = std::filesystem::path(buf.data());
    return p.parent_path();
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return p.parent_path();
#endif
}

inline std::string resource_fallback(const std::string& rel) {
    const auto dir = executable_dir();
    if (dir.empty()) return {};
    const std::filesystem::path candidates[] = {
        dir / ".." / "Resources" / rel,        // macOS bundle
        dir / ".." / "share" / "glcad" / rel,  // Linux prefix
        dir / rel,                             // Windows, next to the exe
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec))
            return c.lexically_normal().string();
    }
    return {};
}

}
