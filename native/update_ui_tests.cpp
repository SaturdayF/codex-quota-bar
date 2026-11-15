#include "app.hpp"

namespace quota {
int updateUiTests(const fs::path &dir) {
    fs::create_directories(dir);
    std::ofstream log(dir / L"checks.txt");
    int count = 0;
    auto check = [&](bool value, const char *name) {
        log << (value ? "PASS " : "FAIL ") << name << std::endl;
        if (!value) throw std::runtime_error(name);
        ++count;
    };
    Application app(false, true);
    app.showSettings();
    auto &ui = *app.settingsWindow;
    ui.selectPage(2);
    HWND button = GetDlgItem(ui.content, 247);
    auto caption = [&] { wchar_t text[128]{}; GetWindowTextW(button, text, 128); return std::wstring(text); };
    check(button && IsWindowVisible(button), "update button is accessible on the advanced page");
    std::promise<fs::path> completion;
    struct CompleteOnExit {
        std::promise<fs::path> &promise;
        ~CompleteOnExit() {
            try { promise.set_exception(std::make_exception_ptr(std::runtime_error("synthetic cancellation"))); }
            catch (...) {}
        }
    } complete{completion};
    ui.updateDownload = completion.get_future();
    ui.updateDownloading = true;
    ui.updateProgress = 0;
    SendMessageW(button, WM_MOUSELEAVE, 0, 0);
    ui.pollUpdate();
    check(caption() == L"0%", "download starts at zero without showing installation completion");
    ui.updateProgress = 37;
    ui.pollUpdate();
    check(caption() == L"37%", "background byte progress reaches the real button caption");
    ui.capture(dir / L"update-progress-light-simulated.png");
    SendMessageW(button, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    check(caption() == L"取消", "pointer entry immediately exposes cancellation");
    ui.capture(dir / L"update-cancel-light-simulated.png");
    ui.updateProgress = 81;
    ui.pollUpdate();
    check(caption() == L"取消", "progress updates do not overwrite the hovered cancel caption");
    SendMessageW(button, WM_MOUSELEAVE, 0, 0);
    check(caption() == L"81%", "pointer exit immediately restores the latest percentage");
    ui.draft.darkAppearance = true;
    ui.applyAppearance();
    ui.capture(dir / L"update-progress-dark-simulated.png");
    SendMessageW(button, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    ui.capture(dir / L"update-cancel-dark-simulated.png");
    SendMessageW(ui.window, WM_COMMAND, MAKEWPARAM(247, BN_CLICKED), reinterpret_cast<LPARAM>(button));
    check(ui.updateCancel && caption() == L"正在取消…" && !IsWindowEnabled(button),
          "click cancels the active transfer and prevents another request while cancellation drains");
    completion.set_exception(std::make_exception_ptr(std::runtime_error("synthetic cancellation")));
    ui.pollUpdate();
    check(!ui.updateDownloading && !ui.updateDownload.valid() &&
              caption() == L"检查更新" && IsWindowEnabled(button),
          "cancel completion restores the idle enabled button");
    check(!ui.helpDialog && !app.stopping, "cancelled downloads neither show a failure popup nor stop the application");
    // Cancellation may arrive after reception finished but before handoff.
    auto stage = dir / L"cancelled-complete-stage";
    fs::create_directories(stage);
    std::ofstream(stage / L"Setup.exe") << "synthetic fixture; never executable";
    std::promise<fs::path> finished;
    ui.updateDownload = finished.get_future();
    ui.updateDownloading = true;
    ui.updateCancel = false;
    finished.set_value(stage);
    ui.beginUpdateCheck();
    ui.pollUpdate();
    check(!fs::exists(stage) && !app.stopping && caption() == L"检查更新" && IsWindowEnabled(button),
          "cancellation after download completion discards staging and restores idle without handoff");
    log << count << " update UI checks passed (simulated progress; no network or installation).\n";
    return count;
}
} // namespace quota
