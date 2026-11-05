#pragma once
#include "core.hpp"

namespace quota {
constexpr int PlatformLoginButton = 270;
// Read-only Chromium Local Storage support. No browser database is opened for
// writing, copied to disk, recovered, or locked by this reader.
std::string readBrowserPlatformToken(const fs::path &database, std::atomic<bool> &stop);
std::string edgeProfileName(const fs::path &userData);
fs::path edgeUserData();
bool openEdgePlatform(HWND owner, const std::string &profile);
struct PlatformTokenResult {
    std::string token;
    std::wstring error;
};
PlatformTokenResult acquireEdgePlatformToken(const fs::path &userData, const std::string &profile,
                                           std::atomic<bool> &stop);
int browserTokenTests(std::ostream &log);
} // namespace quota
