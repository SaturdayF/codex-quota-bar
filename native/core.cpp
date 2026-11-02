#include "core.hpp"
#include <cwctype>
namespace quota {
std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n, L'\0');
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}
std::string utf8(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    if (n)
        WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
fs::path executable() {
    wchar_t b[32768]{};
    GetModuleFileNameW(nullptr, b, 32768);
    return b;
}
fs::path dataDirectory() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        throw std::runtime_error("LocalAppData unavailable");
    fs::path r = fs::path(p) / L"CodexQuotaBar";
    CoTaskMemFree(p);
    return r;
}
std::wstring quote(const std::wstring &s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
int64_t utcNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
bool lightTheme() {
    DWORD v = 1, n = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n);
    return v != 0;
}
bool motionEnabled() {
    BOOL enabled = TRUE;
    SystemParametersInfoW(0x1042, 0, &enabled, 0);
    return enabled != FALSE;
}
std::wstring timeLabel(int64_t v, bool date) {
    if (v <= 0 || v > 253402300799LL)
        return L"未知";
    time_t t = v;
    tm local{};
    if (localtime_s(&local, &t))
        return L"未知";
    wchar_t b[80]{};
    wcsftime(b, 80, date ? L"%m/%d %H:%M" : L"%H:%M:%S", &local);
    return b;
}
Color parseColor(const std::string &s, Color fallback) {
    if (s.size() != 7 || s[0] != '#' ||
        !std::all_of(s.begin() + 1, s.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return fallback;
    return Color(255, BYTE(std::stoul(s.substr(1, 2), nullptr, 16)),
                 BYTE(std::stoul(s.substr(3, 2), nullptr, 16)),
                 BYTE(std::stoul(s.substr(5, 2), nullptr, 16)));
}
std::string hexColor(Color c) {
    char b[8];
    snprintf(b, sizeof(b), "#%02X%02X%02X", c.GetR(), c.GetG(), c.GetB());
    return b;
}
int Settings::styleIndex() const {
    auto it = std::find(StyleIds.begin(), StyleIds.end(), style);
    return it == StyleIds.end() ? 0 : int(it - StyleIds.begin());
}
QuotaVisibility Settings::visibility(const Quotas &q) const {
    return {showHour.value_or(!q.weekOnly()), showWeek.value_or(true)};
}
Settings Settings::fromJson(const json &j) {
    Settings s;
    if (!j.is_object())
        return s;
    s.extra = j;
    auto string = [&](const char *k, const std::string &def) {
        return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : def;
    };
    auto boolean = [&](const json &obj, const char *k, bool def) {
        return obj.contains(k) && obj[k].is_boolean() ? obj[k].get<bool>() : def;
    };
    s.style = string("WidgetStyle", "gauges");
    s.style = StyleIds[s.styleIndex()];
    s.displayMode = string("DisplayMode", "all");
    if (s.displayMode != "all" && s.displayMode != "primary" && s.displayMode != "selected")
        s.displayMode = "all";
    if (j.contains("Displays") && j["Displays"].is_array())
        for (auto &d : j["Displays"])
            if (d.is_string())
                s.displays.push_back(d.get<std::string>());
    s.codexPath = wide(string("CodexPath", ""));
    s.letters = boolean(j, "ShowDialLetters", true);
    s.deepSeekEnabled = boolean(j, "DeepSeekHarnessEnabled", true);
    s.deepSeekAlwaysShow = boolean(j, "DeepSeekShowWhenStopped", false);
    s.avoidIcons = boolean(j, "AvoidTaskbarIcons", true);
    if (j.contains("DeepSeekRefreshSeconds") && j["DeepSeekRefreshSeconds"].is_number_integer()) {
        auto interval = j["DeepSeekRefreshSeconds"].get<int64_t>();
        if (interval == 60 || interval == 120 || interval == 300) s.deepSeekInterval = int(interval);
    }
    if (j.contains("ShowHourQuota") && j["ShowHourQuota"].is_boolean())
        s.showHour = j["ShowHourQuota"].get<bool>();
    if (j.contains("ShowWeekQuota") && j["ShowWeekQuota"].is_boolean())
        s.showWeek = j["ShowWeekQuota"].get<bool>();
    if (j.contains("WidgetScale") && j["WidgetScale"].is_number()) {
        double v = j["WidgetScale"];
        s.scale = std::isfinite(v) ? std::clamp(v, .6, 1.4) : 1;
    }
    s.hourColor = hexColor(parseColor(string("HourColorHex", s.hourColor), Color(255, 33, 158, 134)));
    s.weekColor = hexColor(parseColor(string("WeekColorHex", s.weekColor), Color(255, 100, 141, 230)));
    if (j.contains("RefreshIntervalSeconds") && j["RefreshIntervalSeconds"].is_number_integer()) {
        auto v = j["RefreshIntervalSeconds"].get<int64_t>();
        if (std::find(std::begin(Intervals), std::end(Intervals), v) != std::end(Intervals))
            s.interval = int(v);
    }
    for (size_t i = 0; i < StyleIds.size(); ++i) {
        auto &p = s.preferences[i];
        p.animate = boolean(j, "AnimateQuotaReset", true);
        if (j.contains("StyleOptions") && j["StyleOptions"].is_object() &&
            j["StyleOptions"].contains(StyleIds[i])) {
            auto &v = j["StyleOptions"][StyleIds[i]];
            if (v.is_object()) {
                p.remaining = boolean(v, "ShowRemaining", true);
                p.percent = boolean(v, "ShowPercent", true);
                p.animate = boolean(v, "Animate", p.animate);
            }
        }
    }
    return s;
}
json Settings::toJson() const {
    json j = extra;
    if (!j.contains("StyleOptions") || !j["StyleOptions"].is_object())
        j["StyleOptions"] = json::object();
    j["WidgetStyle"] = style;
    j["DisplayMode"] = displayMode;
    j["Displays"] = displays;
    j["CodexPath"] = codexPath.empty() ? json(nullptr) : json(utf8(codexPath));
    j["ShowDialLetters"] = letters;
    j.erase("DeepSeekEnabled");
    j["DeepSeekHarnessEnabled"] = deepSeekEnabled;
    j["DeepSeekShowWhenStopped"] = deepSeekAlwaysShow;
    j["AvoidTaskbarIcons"] = avoidIcons;
    j["DeepSeekRefreshSeconds"] = deepSeekInterval;
    j["ShowHourQuota"] = showHour ? json(*showHour) : json(nullptr);
    j["ShowWeekQuota"] = showWeek ? json(*showWeek) : json(nullptr);
    j["WidgetScale"] = scale;
    j["HourColorHex"] = hourColor;
    j["WeekColorHex"] = weekColor;
    j["RefreshIntervalSeconds"] = interval;
    for (size_t i = 0; i < StyleIds.size(); ++i)
        j["StyleOptions"][StyleIds[i]] = {{"ShowRemaining", preferences[i].remaining},
                                          {"ShowPercent", preferences[i].percent},
                                          {"Animate", preferences[i].animate}};
    return j;
}
Settings Settings::load() {
    try {
        std::ifstream in(dataDirectory() / L"settings.json");
        json j;
        in >> j;
        return fromJson(j);
    } catch (...) {
        return {};
    }
}
void Settings::save() const {
    auto dir = dataDirectory();
    fs::create_directories(dir);
    auto temp = dir / L"settings.native.tmp";
    {
        std::ofstream out(temp, std::ios::binary);
        out << toJson().dump(2);
        out.flush();
        if (!out)
            throw std::runtime_error("Cannot write settings");
    }
    if (!MoveFileExW(temp.c_str(), (dir / L"settings.json").c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings");
}
bool startupEnabled() {
    DWORD n = 0;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"CodexQuotaBar", RRF_RT_REG_SZ, nullptr, nullptr, &n) == ERROR_SUCCESS;
}
bool setStartup(bool enabled) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LSTATUS status;
    auto text = quote(executable().wstring());
    if (enabled)
        status =
            RegSetValueExW(key, L"CodexQuotaBar", 0, REG_SZ, reinterpret_cast<const BYTE *>(text.c_str()),
                           DWORD((text.size() + 1) * sizeof(wchar_t)));
    else {
        status = RegDeleteValueW(key, L"CodexQuotaBar");
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
bool nativeInstalled(const fs::path &dir) {
    wchar_t product[80]{}, version[80]{};
    auto marker = dir / L"installer.ini";
    GetPrivateProfileStringW(L"Installation", L"Product", L"", product, 80, marker.c_str());
    GetPrivateProfileStringW(L"Installation", L"Version", L"", version, 80, marker.c_str());
    return std::wstring(product) == L"CodexQuotaBar" && std::wstring(version) == Version &&
           fs::is_regular_file(dir / L"unins000.exe");
}
bool launchUninstaller(HWND owner) {
    auto dir = executable().parent_path();
    if (!nativeInstalled(dir))
        return false;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", (dir / L"unins000.exe").c_str(),
                                                   L"/SILENT /NORESTART /RemoveSettings", dir.c_str(),
                                                   SW_SHOWNORMAL)) > 32;
}
static std::string planKey(const std::string &plan) {
    auto p = plan;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return p;
}
bool Quotas::weekOnly() const {
    auto p = planKey(plan);
    // Keep the app's weekly-only preference for every known consumer Pro tier.
    // A weekly-only payload must not invent an unavailable 5-hour window,
    // including for future plans that this version does not know by name.
    return p == "pro" || p == "prolite" || p == "promax" || (p != "plus" && week && !hour);
}
std::wstring Quotas::planLabel() const {
    auto p = planKey(plan);
    if (p == "prolite")
        return L"Pro Lite";
    if (p == "promax")
        return L"Pro Max";
    if (p == "edu")
        return L"Edu";
    if (p == "edu_plus")
        return L"Edu Plus";
    if (p == "edu_pro")
        return L"Edu Pro";
    if (p == "sci")
        return L"Sci";
    if (p == "sci_plus")
        return L"Sci Plus";
    if (p.empty() || p == "unknown")
        return L"套餐未知";
    for (auto name : {"Free", "Go", "Plus", "Pro", "Team", "Business", "Enterprise", "Education"}) {
        std::string key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (p == key)
            return wide(name);
    }
    return wide(plan);
}
Quotas Quotas::parse(const json &j) {
    Quotas q;
    if (!j.is_object())
        return q;
    json bucket;
    if (j.contains("rateLimitsByLimitId") && j["rateLimitsByLimitId"].is_object()) {
        if (!j["rateLimitsByLimitId"].contains("codex"))
            return q;
        bucket = j["rateLimitsByLimitId"]["codex"];
    } else if (j.contains("rateLimits"))
        bucket = j["rateLimits"];
    else
        return q;
    if (!bucket.is_object())
        return q;
    if (bucket.contains("limitId") && bucket["limitId"].is_string() && bucket["limitId"] != "codex")
        return q;
    for (const char *key : {"primary", "secondary"}) {
        if (!bucket.contains(key) || !bucket[key].is_object())
            continue;
        auto &w = bucket[key];
        if (!w.contains("windowDurationMins") || !w["windowDurationMins"].is_number_integer() ||
            !w.contains("usedPercent") || !w["usedPercent"].is_number())
            continue;
        auto duration = w["windowDurationMins"].get<int64_t>();
        double used = w["usedPercent"].get<double>();
        if (!std::isfinite(used))
            continue;
        WindowQuota value{std::clamp(100 - used, 0., 100.), 0};
        if (w.contains("resetsAt") && w["resetsAt"].is_number_integer()) {
            auto seconds = w["resetsAt"].get<int64_t>();
            if (seconds >= 0 && seconds <= 253402300799LL)
                value.resets = seconds;
        }
        if (duration == 300)
            q.hour = value;
        if (duration == 10080)
            q.week = value;
    }
    if (bucket.contains("planType") && bucket["planType"].is_string())
        q.plan = bucket["planType"].get<std::string>();
    return q;
}
std::wstring numberLabel(const std::optional<WindowQuota> &q, bool percent, double progress) {
    if (!q)
        return L"--";
    return std::to_wstring(int(std::floor(q->remaining * std::clamp(progress, 0., 1.) + 1e-9))) +
           (percent ? L"%" : L"");
}
void Animation::update(const State &s, bool enabled, int64_t now, int64_t utc) {
    if (!enabled)
        hourStart = weekStart = -1;
    if (s.stale(utc))
        return;
    auto renewed = [&](const std::optional<WindowQuota> &a, const std::optional<WindowQuota> &b) {
        return a && b && a->resets > 0 && a->resets <= utc && b->remaining > a->remaining + 1 &&
               (!b->resets || b->resets >= a->resets);
    };
    if (enabled) {
        if (!previous) {
            if (s.q.hour)
                hourStart = now;
            if (s.q.week)
                weekStart = now;
        } else if (previous->plan == s.q.plan) {
            if (renewed(previous->hour, s.q.hour))
                hourStart = now;
            if (renewed(previous->week, s.q.week))
                weekStart = now;
        }
    }
    previous = s.q;
}
double Animation::progress(bool hour, int64_t now) const {
    auto start = hour ? hourStart : weekStart;
    if (start < 0 || now - start >= 1200)
        return 1;
    double t = std::clamp((now - start) / 1200., 0., 1.);
    return 1 - std::pow(1 - t, 3);
}
bool Animation::running(int64_t now) const {
    return (hourStart >= 0 && now - hourStart < 1200) || (weekStart >= 0 && now - weekStart < 1200);
}
std::vector<Monitor> monitors() {
    std::vector<Monitor> out;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(h, &info)) {
                Monitor m;
                m.handle = h;
                m.bounds = info.rcMonitor;
                m.work = info.rcWork;
                m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
                m.name = info.szDevice;
                auto dll = LoadLibraryW(L"shcore.dll");
                if (dll) {
                    using Fn = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
                    auto f = reinterpret_cast<Fn>(GetProcAddress(dll, "GetDpiForMonitor"));
                    UINT x = 96, y = 96;
                    if (f && SUCCEEDED(f(h, 0, &x, &y)))
                        m.dpi = int(x);
                    FreeLibrary(dll);
                }
                reinterpret_cast<std::vector<Monitor> *>(p)->push_back(m);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}
std::vector<Monitor> selectedMonitors(const Settings &s) {
    auto all = monitors();
    std::vector<Monitor> selected;
    for (auto &m : all)
        if (s.displayMode == "all" || (s.displayMode == "primary" && m.primary) ||
            (s.displayMode == "selected" &&
             std::find(s.displays.begin(), s.displays.end(), utf8(m.name)) != s.displays.end()))
            selected.push_back(m);
    if (selected.empty())
        for (auto &m : all)
            if (m.primary)
                selected.push_back(m);
    return selected;
}
HWND taskbarWindow(HMONITOR m) {
    HWND w = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT r{};
    if (w && MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST) == m && GetWindowRect(w, &r))
        return w;
    w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr)))
        if (MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST) == m && GetWindowRect(w, &r))
            return w;
    return nullptr;
}
std::optional<RECT> taskbar(HMONITOR m) {
    RECT r{};
    auto w = taskbarWindow(m);
    if (w && GetWindowRect(w, &r))
        return r;
    return {};
}
static std::wstring lower(std::wstring v) {
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    return v;
}
bool desktopBinary(std::wstring name, std::wstring path) {
    name = lower(name);
    path = lower(path);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (name == L"chatgpt.exe")
        return true;
    if (name != L"codex.exe" || path.empty())
        return false;
    for (auto part : {L"\\bin\\", L"\\vendor\\", L"\\resources\\"})
        if (path.find(part) != path.npos)
            return false;
    return path.find(L"\\app\\") != path.npos || path.find(L"\\programs\\codex\\") != path.npos;
}
bool desktopRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snap, &entry))
        do {
            auto name = lower(entry.szExeFile);
            if (name == L"chatgpt.exe") {
                found = true;
                break;
            }
            if (name == L"codex.exe") {
                HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (p) {
                    wchar_t b[32768]{};
                    DWORD n = 32768;
                    if (QueryFullProcessImageNameW(p, 0, b, &n))
                        found = desktopBinary(name, b);
                    CloseHandle(p);
                }
                if (found)
                    break;
            }
        } while (Process32NextW(snap, &entry));
    CloseHandle(snap);
    return found;
}
bool harnessBinary(std::wstring name) {
    name = lower(name);
    return name == L"deepseek harness.exe" || name == L"deepseekharness.exe" || name == L"deepseek-harness.exe";
}
bool harnessRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snap, &entry))
        do { if (harnessBinary(entry.szExeFile)) { found = true; break; } }
        while (Process32NextW(snap, &entry));
    CloseHandle(snap);
    return found;
}
fs::path findCodex(const std::wstring &configured) {
    std::error_code ec;
    if (!configured.empty()) {
        fs::path path = configured;
        if (lower(path.extension().wstring()) != L".exe" || !fs::is_regular_file(path, ec))
            throw std::runtime_error("CLI path invalid");
        return path;
    }
    std::vector<fs::path> candidates;
    DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::wstring paths(n, L'\0');
    if (n)
        GetEnvironmentVariableW(L"PATH", paths.data(), n);
    std::wstringstream input(paths);
    std::wstring part;
    while (std::getline(input, part, L';')) {
        if (part.empty())
            continue;
        if (part.front() == L'\"')
            part.erase(0, 1);
        while (!part.empty() && (part.back() == L'\"' || part.back() == L'\0'))
            part.pop_back();
        fs::path dir = part;
        candidates.push_back(dir / L"node_modules/@openai/codex/node_modules/@openai/codex-win32-x64/vendor/"
                                   L"x86_64-pc-windows-msvc/codex/codex.exe");
        candidates.push_back(dir /
                             L"node_modules/@openai/codex/vendor/x86_64-pc-windows-msvc/codex/codex.exe");
        if (lower(part).find(L"windowsapps") == part.npos)
            candidates.push_back(dir / L"codex.exe");
    }
    auto bin = dataDirectory().parent_path() / L"OpenAI/Codex/bin";
    std::vector<fs::directory_entry> versions;
    if (fs::is_directory(bin, ec))
        for (fs::directory_iterator it(bin, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec))
            if (it->is_directory(ec))
                versions.push_back(*it);
    std::sort(versions.begin(), versions.end(), [](auto &a, auto &b) {
        std::error_code x, y;
        return a.last_write_time(x) > b.last_write_time(y);
    });
    for (auto &dir : versions)
        candidates.push_back(dir.path() / L"codex.exe");
    for (auto &path : candidates)
        if (fs::is_regular_file(path, ec))
            return path;
    throw std::runtime_error("Codex CLI not found");
}
Rpc::Rpc(const fs::path &path, std::atomic<bool> *canceled, std::function<void(Quotas)> updated, bool fake,
         std::function<bool()> ended)
    : cancel(canceled), notification(std::move(updated)), sessionEnded(std::move(ended)) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE childIn = nullptr, childOut = nullptr, error = nullptr;
    PROCESS_INFORMATION pi{};
    auto cleanup = [&] {
        for (HANDLE h : {childIn, childOut, error})
            if (h && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
    };
    try {
        if (!CreatePipe(&childIn, &input, &sa, 0) || !CreatePipe(&output, &childOut, &sa, 0))
            throw std::runtime_error("Pipe creation failed");
        SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
        error = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0,
                            nullptr);
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = childIn;
        si.StartupInfo.hStdOutput = childOut;
        si.StartupInfo.hStdError = error;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<BYTE> storage(bytes);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes))
            throw std::runtime_error("Handle attributes failed");
        HANDLE handles[] = {childIn, childOut, error};
        BOOL attr = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                              handles, sizeof(handles), nullptr, nullptr);
        auto command = quote(path.wstring()) + (fake ? L" --fake-server" : L" app-server");
        auto dir = dataDirectory();
        fs::create_directories(dir);
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            throw std::runtime_error("Job creation failed");
        BOOL ok = attr && CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                                         nullptr, dir.c_str(), &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        if (!ok)
            throw std::runtime_error("Cannot launch Codex");
        process = pi.hProcess;
        if (!AssignProcessToJobObject(job, process)) {
            TerminateProcess(process, 1);
            CloseHandle(pi.hThread);
            throw std::runtime_error("Child isolation failed");
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        cleanup();
        childIn = childOut = error = nullptr;
        request("initialize",
                {{"clientInfo",
                  {{"name", "codex_quota_bar"}, {"title", "Codex Quota Bar"}, {"version", VersionUtf8}}}});
        send({{"method", "initialized"}, {"params", json::object()}});
    } catch (...) {
        cleanup();
        if (job)
            CloseHandle(job);
        if (process)
            CloseHandle(process);
        if (input)
            CloseHandle(input);
        if (output)
            CloseHandle(output);
        throw;
    }
}
Rpc::~Rpc() {
    if (job)
        CloseHandle(job);
    if (process) {
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
    }
    if (input)
        CloseHandle(input);
    if (output)
        CloseHandle(output);
}
void Rpc::send(const json &value) {
    auto line = value.dump() + "\n";
    DWORD n = 0;
    if (cancel->load() || !WriteFile(input, line.data(), DWORD(line.size()), &n, nullptr) || n != line.size())
        throw std::runtime_error("Codex disconnected");
}
std::optional<json> Rpc::receive(int timeout) {
    auto until = GetTickCount64() + timeout;
    do {
        if (cancel->load())
            throw std::runtime_error("Canceled");
        if (sessionEnded && GetTickCount64() - checkedSession >= 2000) {
            checkedSession = GetTickCount64();
            if (sessionEnded())
                throw std::runtime_error("Desktop session ended");
        }
        auto end = buffer.find('\n');
        if (end != buffer.npos) {
            auto line = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            if (line.empty())
                continue;
            return json::parse(line);
        }
        DWORD available = 0;
        if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr))
            throw std::runtime_error("Codex disconnected");
        if (available) {
            char chunk[8192];
            DWORD n = 0;
            if (!ReadFile(output, chunk, std::min<DWORD>(available, sizeof(chunk)), &n, nullptr) || !n)
                throw std::runtime_error("Codex disconnected");
            buffer.append(chunk, n);
            if (buffer.size() > 1024 * 1024)
                throw std::runtime_error("Oversized Codex response");
            continue;
        }
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
            throw std::runtime_error("Codex exited");
        if (!timeout || GetTickCount64() >= until)
            return {};
        Sleep(15);
    } while (true);
}
void Rpc::dispatch(const json &value) {
    if (value.value("method", std::string()) != "account/rateLimits/updated" || !value.contains("params"))
        return;
    auto &p = value["params"];
    if (p.contains("rateLimits") && p["rateLimits"].is_object() &&
        p["rateLimits"].value("limitId", std::string("codex")) != "codex")
        return;
    auto q = Quotas::parse(p);
    if (q.plan.empty())
        q.plan = plan;
    if (notification)
        notification(q);
}
json Rpc::request(const std::string &method, const json &args) {
    int id = ++nextId;
    send({{"id", id}, {"method", method}, {"params", args}});
    auto until = GetTickCount64() + 20000;
    while (GetTickCount64() < until) {
        auto m = receive(int(until - GetTickCount64()));
        if (!m)
            break;
        if (m->contains("id") && (*m)["id"] == id) {
            if (m->contains("error"))
                throw std::runtime_error("Codex request rejected");
            if (!m->contains("result"))
                throw std::runtime_error("Invalid Codex reply");
            return (*m)["result"];
        }
        dispatch(*m);
    }
    throw std::runtime_error("Codex request timed out");
}
Quotas Rpc::read() {
    try {
        auto account = request("account/read", {{"refreshToken", false}});
        plan = account.contains("account") && account["account"].is_object()
                   ? account["account"].value("planType", std::string())
                   : "";
    } catch (const std::exception &) {
        plan.clear();
        if (cancel->load())
            throw;
    }
    auto q = Quotas::parse(request("account/rateLimits/read"));
    if (q.plan.empty())
        q.plan = plan;
    return q;
}
void Rpc::poll() {
    for (int i = 0; i < 32; ++i) {
        auto m = receive(0);
        if (!m)
            return;
        dispatch(*m);
    }
}
int fakeServer() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            auto in = json::parse(line);
            if (!in.contains("id"))
                continue;
            json result = json::object();
            auto method = in.value("method", std::string());
            if (method == "account/read")
                result = {{"account", {{"planType", "plus"}}}};
            if (method == "account/rateLimits/read")
                result = {
                    {"rateLimits",
                     {{"limitId", "codex"},
                      {"primary",
                       {{"windowDurationMins", 300}, {"usedPercent", 32}, {"resetsAt", utcNow() + 300}}},
                      {"secondary",
                       {{"windowDurationMins", 10080}, {"usedPercent", 73}, {"resetsAt", utcNow() + 600}}}}}};
            if (method == "account/rateLimits/read") {
                std::cout << json({{"method", "account/rateLimits/updated"}, {"params", result}}).dump()
                          << std::endl;
                std::cout << json({{"method", "account/rateLimits/updated"},
                                   {"params", {{"rateLimits", {{"limitId", "unrelated-model"}}}}}})
                                 .dump()
                          << std::endl;
            }
            std::cout << json({{"id", in["id"]}, {"result", result}}).dump() << std::endl;
        } catch (...) {
            return 1;
        }
    }
    return 0;
}
} // namespace quota
