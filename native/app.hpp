#pragma once
#include "drawing.hpp"
#include "deepseek.hpp"
#include "taskbar.hpp"
namespace quota {
enum class WidgetKind { Codex, DeepSeek };
Layout deepSeekLayout(const Settings &settings, int dpi, int available);
void paintDeepSeek(Surface &image, const DeepSeekState &state, const Settings &settings, bool hover);
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
    bool hover = false, dragging = false;
    POINT dragStart{}, origin{};
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
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK detailProcedure(HWND, UINT, WPARAM, LPARAM);
};
struct SettingsWindow;
struct Application {
    HWND controller = nullptr;
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
    void refreshDeepSeek();
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
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
};
struct SettingsWindow {
    Application *app;
    HWND window = nullptr, viewport = nullptr, content = nullptr, palette = nullptr;
    Settings draft;
    Monitor monitor;
    int dpi = 96, page = 0, scroll = 0, oldStyle = 0;
    int wheelRemainder = 0;
    bool startup = false, closing = false;
    bool loading = false;
    bool layingOut = false;
    bool clearDeepSeekCredentials = false;
    int64_t previewStart = -1;
    int contentWidth = 700, contentHeight = 600;
    HFONT font = nullptr;
    HBRUSH background = nullptr, sidebar = nullptr;
    std::vector<HWND> controls;
    HWND styles = nullptr, scale = nullptr, display = nullptr, path = nullptr, interval = nullptr;
    HWND deepSeekToken = nullptr, deepSeekInterval = nullptr;
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
    void reveal(HMONITOR monitor);
    void selectPage(int index);
    void command(int id, int notification);
    void styleChanged();
    Settings previewSettings() const;
    void draw(HDC dc, bool inner);
    void drawButton(const DRAWITEMSTRUCT &item);
    void openPalette(bool hour);
    void closePalette();
    bool save();
    void capture(const fs::path &file);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK contentProcedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK viewportProcedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK paletteProcedure(HWND, UINT, WPARAM, LPARAM);
};
int uiTests(const fs::path &dir);
} // namespace quota
