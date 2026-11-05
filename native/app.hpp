#pragma once
#include "drawing.hpp"
#include "deepseek.hpp"
#include "browser_token.hpp"
#include "taskbar.hpp"
namespace quota {
constexpr UINT TrayMessage = WM_APP + 12;
constexpr UINT TraySettings = 40001, TrayExit = 40002;
enum class WidgetKind { Codex, DeepSeek };
Layout deepSeekLayout(const DeepSeekState &state, const Settings &settings, int dpi, int available);
std::optional<int64_t> deepSeekPrimaryBalance(const DeepSeekState &state);
// Downward slide transition for the widget's balance and spend numbers: the old
// value fades out moving down while the new value fades in from above.
struct DeepSeekRoll {
    float progress = 1;
    std::optional<int64_t> fromBalance, toBalance;
    std::optional<int64_t> fromToday, toToday;
    float flash = 0;
};
void paintDeepSeek(Surface &image, const DeepSeekState &state, const Settings &settings, bool hover,
                   const DeepSeekRoll *roll = nullptr);
struct Application;
struct Overlay {
    Application *app;
    Monitor monitor;
    HWND window = nullptr, details = nullptr, host = nullptr;
    Layout layout;
    Animation animation;
    State state;
    Settings settings;
    WidgetKind kind = WidgetKind::Codex;
    int detailPage = 0;
    // DeepSeek hourly-chart hit testing, filled while the details card renders.
    int chartHover = -1;
    bool chartVisible = false;
    float chartLeftPx = 0, chartTopPx = 0, chartBaselinePx = 0, chartBarW = 0, chartBarGap = 0;
    std::array<int64_t, 24> chartValues{};
    std::string chartBilling;
    bool detailsHoverTracked = false;
    // Rolling balance animation for the taskbar widget.
    int64_t balanceShown = -1;
    int64_t balanceFrom = 0, balanceTo = 0;
    int64_t balanceAnimStart = 0, balanceDuration = 700;
    int64_t balanceRevealAt = 0;
    bool balanceAnimating = false;
    int64_t todayShown = -1;
    int64_t todayFrom = 0, todayTo = 0;
    int64_t flashStart = 0;
    // API-key filter dropdown in the details card.
    int keyMenuOpen = 0;
    bool ddVisible = false;
    float ddX = 0, ddY = 0, ddW = 0, ddH = 0, ddItemH = 0;
    int ddCount = 0;
    std::vector<std::string> ddKeys;
    std::unique_ptr<Surface> detailFrost;
    std::unique_ptr<Surface> detailImage;
    BackdropMode detailBackdrop = BackdropMode::Solid;
    bool hover = false, dragging = false;
    POINT dragStart{}, origin{};
    std::optional<int> dragPeerOrigin;
    int64_t hoverStart = 0, lastInside = 0;
    int x = 0, y = 0;
    int visualX = 0, slideFrom = 0, slideTarget = 0;
    int64_t slideStart = -1;
    bool placed = false;
    bool blocked = false;
    Overlay(Application *a, const Monitor &m, const Settings &s, const State &initial,
            WidgetKind type = WidgetKind::Codex);
    ~Overlay();
    void update(const State &s);
    void position(bool reset = false);
    void measure(bool reset = false);
    void place(int target, bool hidden, bool animate = false);
    void animatePosition(int64_t now);
    void stopPositionAnimation();
    void beginDrag(POINT pointer);
    void dragTo(POINT pointer);
    void finishDrag(POINT pointer, bool cancel = false);
    void ensureLayer();
    void render();
    void tick(const POINT *simulatedPointer = nullptr);
    void showDetails(const fs::path &capture = {});
    void showDeepSeekDetails(const fs::path &capture = {});
    void hideDetails();
    void presentDetails(std::unique_ptr<Surface> image, int left, int top);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK detailProcedure(HWND, UINT, WPARAM, LPARAM);
};
struct SettingsWindow;
struct Application {
    HWND controller = nullptr;
    UINT taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    bool trayEnabled = false, trayAdded = false;
    ULONGLONG trayRetryAt = 0;
    std::function<BOOL(DWORD, NOTIFYICONDATAW *)> notifyIcon = Shell_NotifyIconW;
    Settings settings = Settings::load();
    State state;
    DeepSeekState deepseek;
    std::vector<std::unique_ptr<Overlay>> overlays;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::thread worker;
    std::thread deepSeekWorker;
    std::thread taskbarWorker;
    std::vector<TaskbarSpace> taskbarSpaces;
    struct WidgetPosition { std::wstring monitor; WidgetKind kind; int x; };
    std::vector<WidgetPosition> widgetPositions;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> stopping{false};
    bool refreshRequested = false;
    bool deepSeekRefreshRequested = false;
    std::atomic<int64_t> deepSeekRevealAt{0};
    std::string deepSeekKeyFilter;  // empty = all API keys
    bool harnessActive = false; // Component active: running Harness or standalone balance mode.
    int harnessSession = 0;
    std::shared_ptr<std::atomic<bool>> deepSeekSessionStop = std::make_shared<std::atomic<bool>>(true);
    int generation = 0;
    HANDLE quitEvent = nullptr;
    bool preview = false;
    bool testing = false;
    std::function<bool()> desktopProbe = desktopRunning;
    std::function<bool()> harnessProbe = harnessRunning;
    std::function<HWND(HMONITOR)> taskbarProbe = taskbarWindow;
    std::function<bool()> dragMotionProbe = motionEnabled;
    bool fakeRpc = false; // Only enabled by in-process integration checks.
    std::function<DeepSeekState(std::atomic<bool> &)> deepSeekProbe = [](std::atomic<bool> &stop) {
        return readDeepSeek(deepSeekCredential(), stop);
    };
    fs::file_time_type settingsWrite{};
    Application(bool previewMode = false, bool test = false);
    ~Application();
    void runWorker();
    void runDeepSeekWorker();
    void checkHarness(bool force = false);
    void refreshDeepSeek(bool reveal = false);
    void applyDeepSeek(const DeepSeekUpdate &update);
    void apply(const State &value);
    void rebuild();
    void checkTaskbars();
    void layoutWidgets(Overlay *moving = nullptr, bool animate = false);
    void runTaskbarWorker();
    void refresh();
    void saved(const Settings &value);
    void showSettings(HMONITOR monitor = nullptr);
    void checkSettingsFile();
    void stop();
    void addTrayIcon();
    void removeTrayIcon();
    void showTrayMenu();
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
};
struct SettingsWindow {
    Application *app;
    HWND window = nullptr, viewport = nullptr, content = nullptr, palette = nullptr, helpDialog = nullptr;
    Settings draft;
    Monitor monitor;
    int dpi = 96, page = 0, scroll = 0;
    int wheelRemainder = 0;
    int pageHeight = 0, thumbGrab = -1;
    std::unique_ptr<Surface> settingsFrost;
    std::unique_ptr<Surface> helpFrost;
    std::wstring helpTitle, helpMessage;
    BackdropMode backdrop = BackdropMode::Solid;
    BackdropMode helpBackdrop = BackdropMode::Solid;
    bool simulatedGlass = false;
    bool startup = false, closing = false;
    bool loading = false;
    bool layingOut = false;
    bool clearDeepSeekCredentials = false;
    int64_t previewStart = -1;
    int contentWidth = 700, contentHeight = 600;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    std::vector<HWND> controls;
    HWND styles = nullptr, scale = nullptr, opacitySlider = nullptr, iconOpacitySlider = nullptr, display = nullptr, path = nullptr, interval = nullptr;
    HWND deepSeekToken = nullptr, deepSeekInterval = nullptr;
    std::thread tokenWorker;
    std::atomic<bool> tokenStop{false}, tokenDone{false};
    PlatformTokenResult tokenResult;
    std::wstring tokenInputAtStart;
    std::wstring tokenStatus;
    std::function<PlatformTokenResult(std::atomic<bool> &)> tokenProbe;
    std::function<void(const std::string &)> tokenStore = saveDeepSeekCredential;
    Color selectedColor;
    bool colorHour = true;
    SettingsWindow(Application *owner, HMONITOR requested);
    ~SettingsWindow();
    int px(double n) const {
        return int(std::round(n * dpi / 96.));
    }
    HWND button(const wchar_t *text, int id, int p);
    void createControls();
    void layout();
    void scrollTo(int value);
    RECT scrollThumb() const;
    void applyAppearance();
    void paintGlass(Graphics &g, HWND target, int width, int height) const;
    void reveal(HMONITOR monitor);
    void selectPage(int index);
    void command(int id, int notification);
    void startPlatformToken();
    void finishPlatformToken();
    void styleChanged();
    Settings previewSettings() const;
    void draw(HDC dc, bool inner);
    void drawButton(const DRAWITEMSTRUCT &item);
    void openPalette(bool hour);
    void closePalette();
    void openHelp(const std::wstring &title = L"DeepSeek 凭证填写说明", const std::wstring &message = {});
    void openTokenFailure(const std::wstring &title, const std::wstring &message);
    SIZE helpSize() const;
    void closeHelp();
    void renderHelp(Surface &image);
    void captureHelp(const fs::path &file);
    bool save();
    void capture(const fs::path &file);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK contentProcedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK viewportProcedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK paletteProcedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK helpProcedure(HWND, UINT, WPARAM, LPARAM);
};
int uiTests(const fs::path &dir);
} // namespace quota
