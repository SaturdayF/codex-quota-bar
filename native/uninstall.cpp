#include "uninstall.hpp"
#include <wincrypt.h>
#include <wincred.h>
#include <shobjidl.h>

namespace quota {
static const std::vector<std::string> payloadNames = {
    "CodexQuotaBar.exe", "version.json", "LICENSE", "LICENSE-json.txt", "README.md", "CHANGELOG.md"};
static bool plainFile(const fs::path &path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
static std::string sha256(const fs::path &path) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        throw std::runtime_error("Cannot initialize file hash");
    bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    std::ifstream input(path, std::ios::binary);
    char buffer[16384];
    if (!input) ok = false;
    while (ok && input) {
        input.read(buffer, sizeof(buffer));
        if (input.gcount())
            ok = CryptHashData(hash, reinterpret_cast<const BYTE *>(buffer), DWORD(input.gcount()), 0);
    }
    BYTE digest[32]{};
    DWORD size = sizeof(digest);
    ok = ok && !input.bad() && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0);
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok) throw std::runtime_error("Cannot hash uninstall payload");
    std::ostringstream out;
    for (auto byte : digest) out << std::hex << std::setfill('0') << std::setw(2) << int(byte);
    return out.str();
}
static json readSmallJson(const fs::path &path) {
    if (!plainFile(path) || fs::file_size(path) > 16384) return {};
    std::ifstream input(path, std::ios::binary);
    return json::parse(input, nullptr, false);
}
bool nativeInstalled(const fs::path &dir) {
    wchar_t product[80]{}, version[80]{};
    auto marker = dir / L"installer.ini";
    if (!plainFile(marker) || !plainFile(dir / L"unins000.exe")) return false;
    GetPrivateProfileStringW(L"Installation", L"Product", L"", product, 80, marker.c_str());
    GetPrivateProfileStringW(L"Installation", L"Version", L"", version, 80, marker.c_str());
    // An in-place executable update must not disable the existing installer.
    return std::wstring(product) == L"CodexQuotaBar" && version[0];
}
json portableUninstallPlan(const fs::path &app) {
    auto exe = fs::canonical(app);
    auto dir = exe.parent_path();
    if (!plainFile(exe) || exe.filename() != L"CodexQuotaBar.exe" || dir == dir.root_path())
        throw std::runtime_error("Invalid uninstall directory");
    json files = json::array();
    auto add = [&](const fs::path &file) {
        files.push_back({{"name", utf8(file.filename().wstring())}, {"sha256", sha256(file)}});
    };
    add(exe);
    auto manifestPath = dir / L"uninstall-manifest.json";
    auto manifest = readSmallJson(manifestPath);
    bool validManifest = manifest.is_object() && manifest.value("product", json()) == "CodexQuotaBar" &&
                         manifest.contains("files") && manifest["files"].is_object();
    for (const auto &name : payloadNames) {
        if (name == "CodexQuotaBar.exe") continue;
        auto path = dir / wide(name);
        if (!plainFile(path)) continue;
        if (validManifest && manifest["files"].contains(name) &&
            manifest["files"][name] == sha256(path)) add(path);
        else if (!validManifest && name == "version.json") {
            auto version = readSmallJson(path);
            if (version.is_object() && version.value("runtime", json()) == "native-win32" &&
                version.value("architecture", json()) == "x64" && version.contains("version") &&
                version["version"].is_string()) add(path);
        }
    }
    if (validManifest) add(manifestPath);
    return {{"root", utf8(dir.wstring())}, {"files", files}};
}

// Code is fixed; JSON is escaped as a PowerShell single-quoted data string.
// No cmd.exe, wildcard deletion, recursive app-directory deletion or policy changes.
static const char cleanupScript[] = R"PS(
$ErrorActionPreference = 'Stop'
try {
    $root = [IO.Path]::GetFullPath([string]$plan.root)
    if ($root -eq [IO.Path]::GetPathRoot($root)) { throw 'Invalid uninstall root' }
    $allowed = @('CodexQuotaBar.exe','version.json','LICENSE','LICENSE-json.txt','README.md','CHANGELOG.md','uninstall-manifest.json')
    function Check-Root {
        $item = Get-Item -LiteralPath $root -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Uninstall directory changed' }
    }
    function Matches($entry) {
        Check-Root
        $path = Join-Path $root ([string]$entry.name)
        if (-not (Test-Path -LiteralPath $path)) { return $false }
        $item = Get-Item -LiteralPath $path -Force
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $false }
        return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $entry.sha256
    }
    Check-Root
    foreach ($entry in $plan.files) {
        if ($allowed -cnotcontains $entry.name -or $entry.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Invalid uninstall file' }
    }
    $parent = Get-Process -Id $plan.parentPid
    $ready = [Threading.EventWaitHandle]::OpenExisting([string]$plan.readyEvent)
    [void]$ready.Set()
    $ready.Dispose()
    if (-not $parent.WaitForExit(60000)) { throw 'The application did not exit' }
    $exe = @($plan.files | Where-Object { $_.name -ceq 'CodexQuotaBar.exe' })
    if ($exe.Count -ne 1 -or -not (Matches $exe[0])) { throw 'Application file changed' }
    if ($plan.userData) {
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName = Join-Path $root 'CodexQuotaBar.exe'
        $start.Arguments = '--uninstall-user-data'
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $cleanup = [Diagnostics.Process]::Start($start)
        if (-not $cleanup.WaitForExit(15000) -or $cleanup.ExitCode -ne 0) { throw 'Cannot remove application settings' }
        $cleanup.Dispose()
    }
    foreach ($entry in $plan.files) {
        # Preserve a file replaced or edited after the uninstall was requested.
        if (-not (Matches $entry)) { continue }
        $path = Join-Path $root ([string]$entry.name)
        for ($attempt = 0; $attempt -lt 80; $attempt++) {
            if (-not (Matches $entry)) { break }
            try { Remove-Item -LiteralPath $path -Force; break }
            catch { if ($attempt -eq 79) { throw }; Start-Sleep -Milliseconds 100 }
        }
    }
    Check-Root
    if (@(Get-ChildItem -LiteralPath $root -Force).Count -eq 0) { Remove-Item -LiteralPath $root }
    exit 0
} catch {
    if ($plan.interactive) {
        Add-Type -AssemblyName System.Windows.Forms
        [void][Windows.Forms.MessageBox]::Show(([string]$plan.errorMessage + [Environment]::NewLine + $root), 'Codex Quota Bar')
    }
    exit 1
}
)PS";

HANDLE startPortableUninstall(json plan, DWORD parentPid, bool userData, bool interactive) {
    std::wstring eventName = L"Local\\CodexQuotaBar.Uninstall." + std::to_wstring(GetCurrentProcessId()) +
                             L"." + std::to_wstring(GetTickCount64());
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (!ready) return nullptr;
    plan["parentPid"] = parentPid;
    plan["readyEvent"] = utf8(eventName);
    plan["userData"] = userData;
    plan["interactive"] = interactive;
    plan["errorMessage"] = utf8(L"卸载未能完成。请检查目录权限；程序文件仍在时可重新打开设置并重试。\n需要清理的目录：");
    std::string literal;
    for (char c : plan.dump(-1, ' ', true)) {
        literal += c;
        if (c == '\'') literal += '\'';
    }
    auto script = "& {\n$plan = '" + literal + "' | ConvertFrom-Json\n" + cleanupScript + "\n}\n\n";
    HANDLE read = nullptr, write = nullptr;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE null = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                              OPEN_EXISTING, 0, nullptr);
    bool ok = null != INVALID_HANDLE_VALUE && CreatePipe(&read, &write, &security, 0) &&
              SetHandleInformation(write, HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION child{};
    STARTUPINFOEXW info{};
    info.StartupInfo.cb = sizeof(info);
    info.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    info.StartupInfo.wShowWindow = SW_HIDE;
    info.StartupInfo.hStdInput = read;
    info.StartupInfo.hStdOutput = info.StartupInfo.hStdError = null;
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> storage(bytes);
    info.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool attributes = InitializeProcThreadAttributeList(info.lpAttributeList, 1, 0, &bytes);
    HANDLE handles[] = {read, null};
    ok = ok && attributes && UpdateProcThreadAttribute(info.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr);
    wchar_t system[32768]{};
    GetSystemDirectoryW(system, 32768);
    auto powershell = fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    auto command = quote(powershell.wstring()) + L" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -Command -";
    ok = ok && CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, system, &info.StartupInfo, &child);
    if (attributes) DeleteProcThreadAttributeList(info.lpAttributeList);
    if (read) CloseHandle(read);
    if (null != INVALID_HANDLE_VALUE) CloseHandle(null);
    if (ok) {
        CloseHandle(child.hThread);
        DWORD written = 0;
        ok = WriteFile(write, script.data(), DWORD(script.size()), &written, nullptr) && written == script.size();
    }
    if (write) CloseHandle(write);
    if (ok) {
        HANDLE waiting[] = {ready, child.hProcess};
        ok = WaitForMultipleObjects(2, waiting, FALSE, 15000) == WAIT_OBJECT_0;
    }
    CloseHandle(ready);
    if (!ok && child.hProcess) {
        TerminateProcess(child.hProcess, 1); // Only our unready cleanup child, never the application.
        CloseHandle(child.hProcess);
    }
    return ok ? child.hProcess : nullptr;
}
bool launchUninstaller(HWND owner) {
    try {
        auto dir = executable().parent_path();
        if (nativeInstalled(dir))
            return reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", (dir / L"unins000.exe").c_str(),
                L"/SILENT /NORESTART /RemoveSettings", dir.c_str(), SW_SHOWNORMAL)) > 32;
        auto child = startPortableUninstall(portableUninstallPlan(executable()), GetCurrentProcessId(), true);
        if (!child) return false;
        CloseHandle(child);
        return true;
    } catch (...) { return false; }
}
static bool sameFile(const fs::path &a, const fs::path &b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec) && !ec;
}
bool removeUninstallUserData(bool whatIf) {
    if (whatIf) return true; // No reads of credentials, registry writes or deletion in a dry run.
    bool ok = true;
    HKEY run = nullptr;
    auto runStatus = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                                   KEY_QUERY_VALUE | KEY_SET_VALUE, &run);
    if (runStatus == ERROR_SUCCESS) {
        wchar_t value[32768]{};
        DWORD size = sizeof(value);
        if (RegGetValueW(run, nullptr, L"CodexQuotaBar", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS) {
            int count = 0;
            auto args = CommandLineToArgvW(value, &count);
            if (args && count && sameFile(args[0], executable()))
                ok = RegDeleteValueW(run, L"CodexQuotaBar") == ERROR_SUCCESS;
            if (args) LocalFree(args);
        }
        RegCloseKey(run);
    } else if (runStatus != ERROR_FILE_NOT_FOUND) ok = false;
    PWSTR programs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs))) {
        auto folder = fs::path(programs) / L"Codex Quota Bar";
        CoTaskMemFree(programs);
        auto shortcut = folder / L"Codex Quota Bar Settings.lnk";
        IShellLinkW *link = nullptr;
        if (plainFile(shortcut) && SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                                             IID_IShellLinkW, reinterpret_cast<void **>(&link)))) {
            IPersistFile *persist = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&persist)))) {
                wchar_t target[32768]{};
                if (SUCCEEDED(persist->Load(shortcut.c_str(), STGM_READ)) &&
                    SUCCEEDED(link->GetPath(target, 32768, nullptr, SLGP_RAWPATH)) && sameFile(target, executable())) {
                    std::error_code ec;
                    fs::remove(shortcut, ec);
                    ok = ok && !ec;
                    if (fs::is_empty(folder, ec)) fs::remove(folder, ec);
                }
                persist->Release();
            }
            link->Release();
        }
    }
    for (auto target : {L"CodexQuotaBar.DeepSeek.PlatformToken", L"CodexQuotaBar.DeepSeek.ApiKey"})
        if (!CredDeleteW(target, CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) ok = false;
    auto dir = dataDirectory();
    auto attributes = GetFileAttributesW(dir.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    for (auto name : {L"settings.json", L"settings.native.tmp"}) {
        auto file = dir / name;
        if (plainFile(file)) {
            std::error_code ec;
            fs::remove(file, ec);
            ok = ok && !ec;
        }
    }
    std::error_code ec;
    if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec)) fs::remove(dir, ec);
    return ok;
}

int uninstallTests(std::ostream &log) {
    int count = 0;
    auto check = [&](bool value, const char *message) {
        log << (value ? "PASS " : "FAIL ") << message << std::endl;
        if (!value) throw std::runtime_error(message);
        ++count;
    };
    // Only this freshly-created disposable directory is ever deleted in these checks.
    auto fixture = fs::temp_directory_path() /
        (L"quota-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    check(fs::create_directory(fixture), "create isolated portable uninstall fixture");
    auto root = fixture / L"中文 空格 ' [x] $() &";
    fs::create_directory(root);
    auto app = root / L"CodexQuotaBar.exe";
    fs::copy_file(executable(), app);
    { std::ofstream out(root / L"version.json"); out << R"({"version":"0.2.2","runtime":"native-win32","architecture":"x64"})"; }
    { std::ofstream out(root / L"README.md"); out << "Unrelated readme must survive"; }
    auto plan = portableUninstallPlan(app);
    check(plan["files"].size() == 2, "legacy portable removes executable and recognized version only");
    { std::ofstream out(root / L"LICENSE"); out << "fixture bundled license"; }
    json manifest = {{"product", "CodexQuotaBar"}, {"files", {
        {"LICENSE", sha256(root / L"LICENSE")}, {"../outside.txt", "ignored"},
        {"README.md", std::string(64, '0')}}}};
    { std::ofstream out(root / L"uninstall-manifest.json"); out << manifest.dump(); }
    plan = portableUninstallPlan(app);
    check(plan["files"].size() == 4, "manifest includes matching payload and excludes modified or outside files");
    check(removeUninstallUserData(true), "uninstall -WhatIf preserves real user integration and credentials");
    { std::ofstream out(root / L"keep.txt"); out << "user file"; }
    { std::ofstream out(fixture / L"outside.txt"); out << "outside file"; }
    // The child remains suspended until the cleanup process has attached to it.
    PROCESS_INFORMATION child{};
    STARTUPINFOW info{sizeof(info)};
    auto command = quote(app.wstring()) + L" --uninstall-test-child";
    check(CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                         nullptr, fixture.c_str(), &info, &child), "launch isolated locked executable");
    auto cleanup = startPortableUninstall(plan, child.dwProcessId, false, false);
    bool waited = fs::exists(app);
    // A replacement after planning must also survive the cleanup.
    { std::ofstream out(root / L"LICENSE"); out << "user replacement after plan"; }
    ResumeThread(child.hThread);
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess, 15000);
    CloseHandle(child.hProcess);
    check(cleanup != nullptr, "portable cleanup process acknowledges readiness");
    check(waited, "cleanup waits for running application before deletion");
    bool finished = WaitForSingleObject(cleanup, 20000) == WAIT_OBJECT_0;
    DWORD result = 1;
    GetExitCodeProcess(cleanup, &result);
    CloseHandle(cleanup);
    check(finished && result == 0, "portable cleanup completes without user-data access");
    check(!fs::exists(app) && !fs::exists(root / L"version.json") &&
          !fs::exists(root / L"uninstall-manifest.json"), "portable executable and owned metadata removed");
    check(fs::exists(root / L"README.md") && fs::exists(root / L"LICENSE") &&
          fs::exists(root / L"keep.txt") && fs::exists(fixture / L"outside.txt"),
          "portable cleanup preserves unrelated, replaced and outside files");
    for (auto name : {L"README.md", L"LICENSE", L"keep.txt"}) fs::remove(root / name);
    fs::remove(root);
    fs::remove(fixture / L"outside.txt");
    // A standalone executable in an otherwise empty directory removes that directory too.
    fs::create_directory(root);
    fs::copy_file(executable(), app);
    command = quote(app.wstring()) + L" --uninstall-test-child";
    check(CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                         nullptr, fixture.c_str(), &info, &child), "launch standalone uninstall fixture");
    cleanup = startPortableUninstall(portableUninstallPlan(app), child.dwProcessId, false, false);
    ResumeThread(child.hThread);
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess, 15000);
    CloseHandle(child.hProcess);
    check(cleanup != nullptr, "standalone cleanup acknowledges readiness");
    finished = WaitForSingleObject(cleanup, 20000) == WAIT_OBJECT_0;
    GetExitCodeProcess(cleanup, &result);
    CloseHandle(cleanup);
    check(finished && result == 0 && !fs::exists(root), "standalone cleanup removes executable and empty directory");
    fs::remove(fixture);
    return count;
}
}
