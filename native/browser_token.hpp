#pragma once
#include "core.hpp"

namespace quota {
constexpr int PlatformLoginButton = 270;
enum class BrowserKind { Unsupported, Edge, Chrome };
BrowserKind platformBrowserKind(const fs::path &executable);
struct PlatformBrowser {
    BrowserKind kind = BrowserKind::Unsupported;
    fs::path executable, userData;
    std::wstring name;
};
PlatformBrowser defaultPlatformBrowser();
// Read-only Chromium Local Storage support. No browser database is opened for
// writing, copied to disk, recovered, or locked by this reader.
std::string readBrowserPlatformToken(const fs::path &database, std::atomic<bool> &stop);
std::string browserProfileName(const fs::path &userData);
fs::path edgeUserData();
bool openEdgePlatform(HWND owner, const std::string &profile);
bool openBrowserPlatform(HWND owner, const PlatformBrowser &browser, const std::string &profile);
struct PlatformTokenResult {
    std::string token;
    std::wstring error;
};
PlatformTokenResult acquireEdgePlatformToken(const fs::path &userData, const std::string &profile,
                                           std::atomic<bool> &stop);
PlatformTokenResult acquireBrowserPlatformToken(const fs::path &userData, const std::string &profile,
                                               std::atomic<bool> &stop, const std::wstring &browserName);
int browserTokenTests(std::ostream &log);
} // namespace quota
