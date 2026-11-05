#include "app.hpp"

namespace quota {
fs::path edgeUserData() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        throw std::runtime_error("Edge data unavailable");
    fs::path path(local); CoTaskMemFree(local);
    return path / L"Microsoft" / L"Edge" / L"User Data";
}
bool openEdgePlatform(HWND owner, const std::string &profile) {
    auto args = L"--profile-directory=" + quote(wide(profile)) + L" https://platform.deepseek.com/usage";
    return INT_PTR(ShellExecuteW(owner, L"open", L"msedge.exe", args.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
}
PlatformTokenResult acquireEdgePlatformToken(const fs::path &userData, const std::string &profile,
                                           std::atomic<bool> &stop) {
    PlatformTokenResult result;
    try {
        auto storage = userData / wide(profile) / L"Local Storage";
        std::error_code ec;
        if (fs::exists(storage, ec)) {
            // Never fall back to stale LevelDB data after a SQLite migration.
            for (auto &entry : fs::directory_iterator(storage)) {
                if (entry.path().extension() == L".sqlite" || entry.path().extension() == L".db")
                    throw std::runtime_error("Browser storage unavailable");
            }
        }
        if (ec) throw std::runtime_error("Browser storage unavailable");
        auto token = readBrowserPlatformToken(storage / L"leveldb", stop);
        if (stop) return {};
        if (token.empty()) {
            result.error = L"未找到 Edge 最近使用配置中的 DeepSeek 登录凭证。请在打开的平台网页登录，等待几秒后再次点击“一键获取凭证”。\n\n不支持 InPrivate 或自定义用户数据目录；也可按“凭证填写说明”手动填写。";
        } else {
            result.error = validateDeepSeekCredential(token, stop);
            if (result.error.empty() && !stop) result.token = std::move(token);
        }
    } catch (const std::exception &) {
        result.error = L"无法读取 Edge 本地存储：文件正在变化、权限受限或格式暂不支持。请稍后重试，或按“凭证填写说明”手动填写。";
    }
    return result;
}

void SettingsWindow::startPlatformToken() {
    if (tokenWorker.joinable()) return;
    tokenStop = false; tokenDone = false;
    tokenResult = {};
    int length = GetWindowTextLengthW(deepSeekToken);
    std::wstring text(size_t(length) + 1, L'\0');
    GetWindowTextW(deepSeekToken, text.data(), length + 1); text.resize(size_t(length));
    tokenInputAtStart = std::move(text);
    auto probe = tokenProbe;
    if (!probe) {
        try {
            auto userData = edgeUserData();
            auto profile = edgeProfileName(userData);
            if (!openEdgePlatform(window, profile)) {
                openTokenFailure(L"获取凭证失败", L"无法打开 Microsoft Edge，请确认 Edge 已安装，或手动填写平台凭证。");
                return;
            }
            probe = [userData, profile](std::atomic<bool> &stop) { return acquireEdgePlatformToken(userData, profile, stop); };
        } catch (const std::exception &) {
            ShellExecuteW(window, L"open", L"msedge.exe", L"https://platform.deepseek.com/usage", nullptr, SW_SHOWNORMAL);
            openTokenFailure(L"请先登录 DeepSeek", L"未能确定 Edge 最近使用的用户配置。请在 Edge 中登录 DeepSeek 平台，等待几秒后重试；也可手动填写凭证。");
            return;
        }
    }
    EnableWindow(GetDlgItem(content, PlatformLoginButton), FALSE);
    tokenStatus = L"正在读取并验证 Edge 登录凭证…";
    InvalidateRect(content, nullptr, FALSE);
    // The UI timer consumes results; destruction cancels and joins the worker.
    try {
        tokenWorker = std::thread([this, probe] {
            try { tokenResult = probe(tokenStop); }
            catch (...) { tokenResult = {{}, L"获取凭证失败，请稍后重试或手动填写。"}; }
            tokenDone = true;
        });
    } catch (...) {
        EnableWindow(GetDlgItem(content, PlatformLoginButton), TRUE);
        tokenStatus = L"无法启动凭证读取，请稍后重试。";
        InvalidateRect(content, nullptr, FALSE);
        openTokenFailure(L"获取凭证失败", L"无法启动凭证读取，请稍后重试。");
    }
}
void SettingsWindow::finishPlatformToken() {
    if (!tokenDone.exchange(false)) return;
    if (tokenWorker.joinable()) tokenWorker.join();
    EnableWindow(GetDlgItem(content, PlatformLoginButton), TRUE);
    auto result = std::move(tokenResult);
    if (tokenStop || closing) {
        tokenStatus.clear();
        InvalidateRect(content, nullptr, FALSE);
        return;
    }
    int length = GetWindowTextLengthW(deepSeekToken);
    std::wstring text(size_t(length) + 1, L'\0');
    GetWindowTextW(deepSeekToken, text.data(), length + 1); text.resize(size_t(length));
    // Preserve user edits made while disk/network operations were pending.
    if (text != tokenInputAtStart) {
        tokenStatus.clear();
        InvalidateRect(content, nullptr, FALSE);
        return;
    }
    if (!result.token.empty()) {
        // Persist only the acquired credential; other settings remain drafts.
        // The store is replaceable by in-memory fixtures in UI checks.
        try {
            tokenStore(result.token);
        } catch (...) {
            tokenStatus = L"自动保存失败，请重试或手动填写。";
            closeHelp();
            selectPage(3);
            if (!app->testing) reveal(monitor.handle);
            InvalidateRect(content, nullptr, FALSE);
            openTokenFailure(L"凭证保存失败", L"凭证已获取，但无法写入 Windows 凭据管理器。请检查当前用户权限后重试。");
            return;
        }
        clearDeepSeekCredentials = false;
        // Empty input now means keep this saved value; Cancel cannot undo a
        // completed one-click save, and later edits can still replace it.
        SetWindowTextW(deepSeekToken, L"");
        SendMessageW(deepSeekToken, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"已保存；留空保持不变"));
        tokenStatus = L"凭证已自动保存。";
        app->checkHarness(true);
        closeHelp();
        selectPage(3);
        if (!app->testing) reveal(monitor.handle);
        InvalidateRect(content, nullptr, FALSE);
        openHelp(L"获取 Token 成功", L"已成功获取 DeepSeek Token，并自动保存凭证。");
    } else {
        tokenStatus = L"获取未完成，请按提示重试。";
        InvalidateRect(content, nullptr, FALSE);
        if (!result.error.empty()) openTokenFailure(L"获取凭证失败", result.error);
    }
}
} // namespace quota
