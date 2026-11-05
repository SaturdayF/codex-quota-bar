#include "app.hpp"
#include <shlwapi.h>

namespace quota {
BrowserKind platformBrowserKind(const fs::path &executable) {
    auto filename = executable.filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    if (filename == L"msedge.exe") return BrowserKind::Edge;
    if (filename == L"chrome.exe") return BrowserKind::Chrome;
    return BrowserKind::Unsupported;
}
PlatformBrowser defaultPlatformBrowser() {
    wchar_t executable[32768]{};
    DWORD count = DWORD(std::size(executable));
    PlatformBrowser browser;
    if (FAILED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_EXECUTABLE, L"https", L"open", executable, &count)))
        return browser;
    browser.executable = executable;
    browser.kind = platformBrowserKind(browser.executable);
    browser.name = browser.kind == BrowserKind::Chrome ? L"Chrome" : browser.kind == BrowserKind::Edge ? L"Edge" : L"默认浏览器";
    if (browser.kind != BrowserKind::Unsupported) {
        auto local = edgeUserData().parent_path().parent_path().parent_path();
        browser.userData = browser.kind == BrowserKind::Chrome ? local / L"Google" / L"Chrome" / L"User Data" : edgeUserData();
    }
    return browser;
}
bool openBrowserPlatform(HWND owner, const PlatformBrowser &browser, const std::string &profile) {
    if (browser.kind == BrowserKind::Unsupported)
        return INT_PTR(ShellExecuteW(owner, L"open", L"https://platform.deepseek.com/usage", nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    auto args = profile.empty() ? L"https://platform.deepseek.com/usage" :
        L"--profile-directory=" + quote(wide(profile)) + L" https://platform.deepseek.com/usage";
    return INT_PTR(ShellExecuteW(owner, L"open", browser.executable.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
}
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
    return acquireBrowserPlatformToken(userData, profile, stop, L"Edge");
}
PlatformTokenResult acquireBrowserPlatformToken(const fs::path &userData, const std::string &profile,
                                              std::atomic<bool> &stop, const std::wstring &browserName) {
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
            result.error = L"未找到 " + browserName + L" 最近使用配置中的 DeepSeek 登录凭证。请在平台网页登录，等待几秒后再次获取。无痕窗口或自定义数据目录暂不支持，也可手动填写。";
        } else {
            result.error = validateDeepSeekCredential(token, stop);
            if (result.error.empty() && !stop) result.token = std::move(token);
        }
    } catch (const std::exception &) {
        result.error = L"无法读取 " + browserName + L" 本地存储：文件正在变化、权限受限或格式暂不支持。请稍后重试，或按“凭证填写说明”手动填写。";
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
        PlatformBrowser browser;
        try {
            browser = defaultPlatformBrowser();
            if (browser.kind == BrowserKind::Unsupported) {
                openBrowserPlatform(window, browser, {});
                openTokenFailure(L"自动获取暂不可用", L"已使用默认浏览器打开平台。自动获取目前支持 Microsoft Edge 和 Google Chrome，请将其中一个设为默认浏览器，或手动填写凭证。");
                return;
            }
            auto userData = browser.userData;
            auto profile = browserProfileName(userData);
            if (!openBrowserPlatform(window, browser, profile)) {
                openTokenFailure(L"获取凭证失败", L"无法打开默认浏览器，请确认浏览器已安装，或手动填写平台凭证。");
                return;
            }
            probe = [userData, profile, browser](std::atomic<bool> &stop) { return acquireBrowserPlatformToken(userData, profile, stop, browser.name); };
        } catch (const std::exception &) {
            openBrowserPlatform(window, browser, {});
            openTokenFailure(L"请先登录 DeepSeek", L"未能确定默认浏览器最近使用的用户配置。请在平台网页登录，等待几秒后重试；也可手动填写凭证。");
            return;
        }
    }
    EnableWindow(GetDlgItem(content, PlatformLoginButton), FALSE);
    tokenStatus = L"正在读取并验证浏览器登录凭证…";
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
