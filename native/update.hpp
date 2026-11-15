#pragma once
#include "core.hpp"

namespace quota {
constexpr wchar_t UpdateLock[] = L"Local\\CodexQuotaBar.Updating";
constexpr DWORD UpdateDownloadTimeoutMs = 5 * 60 * 1000;
struct UpdateRelease {
    std::string tag, digest;
    size_t size = 0;
};
std::optional<UpdateRelease> parseUpdateRelease(const json &release, const std::string &current);
std::optional<UpdateRelease> checkForUpdate(const std::atomic<bool> &cancel);
fs::path downloadUpdate(const UpdateRelease &release, const std::atomic<bool> &cancel,
                        std::atomic<int> *progress = nullptr);
void launchUpdate(const fs::path &stage, const UpdateRelease &release);
void discardUpdate(const fs::path &stage);
int runUpdateHelper(const std::vector<std::wstring> &args);
}
