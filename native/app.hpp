#pragma once
#include "drawing.hpp"
namespace quota {
struct Application;
struct Overlay {
    Application *app;
    Monitor monitor;
    HWND window = nullptr, details = nullptr;
    Layout layout;
    Animation animation;
    State state;
    Settings settings;
    bool hover = false, dragging = false;
    POINT dragStart{}, origin{};
    int64_t hoverStart = 0, lastInside = 0;
    int x = 0, y = 0;
    bool placed = false;
    Overlay(Application *a, const Monitor &m, const Settings &s, const State &initial);
    ~Overlay();
    void update(const State &s);
    void position(bool reset = false);
    void render();
    void tick(const POINT *simulatedPointer = nullptr);
    void showDetails(const fs::path &capture = {});
    void hideDetails();
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK detailProcedure(HWND, UINT, WPARAM, LPARAM);
};
struct SettingsWindow;
struct Application {
    HWND controller = nullptr;
    Settings settings = Settings::load();
    State state;
    std::vector<std::unique_ptr<Overlay>> overlays;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> stopping{false};
    bool refreshRequested = false;
    int generation = 0;
    HANDLE quitEvent = nullptr;
    bool preview = false;
    bool testing = false;
    std::function<bool()> desktopProbe = desktopRunning;
    bool fakeRpc = false; // Only enabled by in-process integration checks.
    fs::file_time_type settingsWrite{};
    Application(bool previewMode = false, bool test = false);
    ~Application();
    void runWorker();
    void apply(const State &value);
    void rebuild();
    void refresh();
    void saved(const Settings &value);
    void showSettings(HMONITOR monitor = nullptr);
    void checkSettingsFile();
    void stop();
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
};
struct SettingsWindow {
    Application *app;
    HWND window = nullptr, content = nullptr, palette = nullptr;
    Settings draft;
    Monitor monitor;
    int dpi = 96, page = 0, scroll = 0, oldStyle = 0;
    bool startup = false, closing = false;
    bool loading = false;
    int64_t previewStart = -1;
    int contentWidth = 700, contentHeight = 600;
    HFONT font = nullptr;
    HBRUSH background = nullptr, sidebar = nullptr;
    std::vector<HWND> controls;
    HWND styles = nullptr, scale = nullptr, display = nullptr, path = nullptr, interval = nullptr;
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
    void reveal(HMONITOR monitor);
    void selectPage(int index);
    void command(int id, int notification);
    void styleChanged();
    void draw(HDC dc, bool inner);
    void drawButton(const DRAWITEMSTRUCT &item);
    void openPalette(bool hour);
    void closePalette();
    bool save();
    void capture(const fs::path &file);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK contentProcedure(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK paletteProcedure(HWND, UINT, WPARAM, LPARAM);
};
int uiTests(const fs::path &dir);
} // namespace quota
