#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "vendor/json.hpp"
namespace quota {
using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Gdiplus;
constexpr wchar_t Version[] = L"0.2.1";
constexpr char VersionUtf8[] = "0.2.1";
constexpr UINT StateMessage = WM_APP + 1, SettingsMessage = WM_APP + 2, RebuildMessage = WM_APP + 3;
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &s);
fs::path executable();
fs::path dataDirectory();
std::wstring quote(const std::wstring &s);
int64_t utcNow();
bool lightTheme();
bool motionEnabled();
std::wstring timeLabel(int64_t value, bool date = true);
Color parseColor(const std::string &s, Color fallback);
std::string hexColor(Color c);
struct Preference {
    bool remaining = true, percent = true, animate = true;
};
const std::vector<std::string> StyleIds = {"gauges", "underline", "badges", "ticks",
                                           "plain",  "outlined",  "side"};
const std::vector<std::wstring> StyleNames = {L"双仪表", L"细线（底线数字）", L"双徽标",  L"双刻度",
                                              L"纯数字", L"圆框数字",         L"侧边数字"};
constexpr int Intervals[] = {15, 30, 60, 120, 300};
struct Settings {
    std::string displayMode = "all", style = "gauges", hourColor = "#219E86", weekColor = "#648DE6";
    std::vector<std::string> displays;
    std::wstring codexPath;
    bool letters = true;
    double scale = 1;
    int interval = 30;
    std::vector<Preference> preferences = std::vector<Preference>(7);
    json extra = json::object();
    int styleIndex() const;
    Preference pref() const {
        return preferences[styleIndex()];
    }
    static Settings fromJson(const json &j);
    json toJson() const;
    static Settings load();
    void save() const;
};
bool startupEnabled();
bool setStartup(bool enabled);
bool nativeInstalled(const fs::path &directory);
bool launchUninstaller(HWND owner);
struct WindowQuota {
    double remaining = 0;
    int64_t resets = 0;
};
struct Quotas {
    std::optional<WindowQuota> hour, week;
    std::string plan;
    bool weekOnly() const;
    static Quotas parse(const json &j);
};
struct State {
    Quotas q;
    int64_t updated = 0;
    std::wstring error = L"等待首次读取";
    int interval = 30;
    bool active = false;
    bool stale(int64_t now = utcNow()) const {
        return !error.empty() || !updated || now - updated > std::max(75., interval * 2.5);
    }
};
std::wstring numberLabel(const std::optional<WindowQuota> &q, bool percent = true, double progress = 1);
struct Animation {
    std::optional<Quotas> previous;
    int64_t hourStart = -1, weekStart = -1;
    void update(const State &state, bool enabled, int64_t now, int64_t utc);
    void restart(int64_t now) {
        hourStart = weekStart = now;
    }
    double progress(bool hour, int64_t now) const;
    bool running(int64_t now) const;
};
struct Monitor {
    HMONITOR handle = nullptr;
    RECT bounds{}, work{};
    bool primary = false;
    std::wstring name;
    int dpi = 96;
};
std::vector<Monitor> monitors();
std::vector<Monitor> selectedMonitors(const Settings &s);
std::optional<RECT> taskbar(HMONITOR monitor);
bool desktopBinary(std::wstring name, std::wstring path);
bool desktopRunning();
fs::path findCodex(const std::wstring &configured);
class Rpc {
    HANDLE process = nullptr, job = nullptr, input = nullptr, output = nullptr;
    std::string buffer;
    int nextId = 0;
    std::atomic<bool> *cancel;
    std::string plan;
    std::function<void(Quotas)> notification;
    std::function<bool()> sessionEnded;
    ULONGLONG checkedSession = 0;
    void send(const json &value);
    std::optional<json> receive(int timeout);
    void dispatch(const json &value);

  public:
    Rpc(const fs::path &path, std::atomic<bool> *canceled, std::function<void(Quotas)> updated = {},
        bool fake = false, std::function<bool()> ended = {});
    ~Rpc();
    Rpc(const Rpc &) = delete;
    Rpc &operator=(const Rpc &) = delete;
    json request(const std::string &method, const json &args = json::object());
    Quotas read();
    void poll();
};
int fakeServer();
int selfTests(const fs::path &output);
} // namespace quota
