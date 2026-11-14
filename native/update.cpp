#include "update.hpp"
#include "uninstall.hpp"
#include <winhttp.h>
#include <array>
#include <charconv>

namespace quota {
namespace {
constexpr size_t MaxSetup = 32 * 1024 * 1024;
constexpr wchar_t RepoPath[] = L"/SaturdayF/codex-quota-bar/releases/download/";
constexpr wchar_t SetupFile[] = L"Setup.exe";
constexpr const wchar_t *Payload[] = {L"CodexQuotaBar.exe", L"version.json", L"LICENSE", L"LICENSE-json.txt",
                                     L"README.md", L"CHANGELOG.md"};
constexpr const wchar_t *InstallerFiles[] = {L"uninstall-manifest.json", L"installer.ini", L"unins000.exe", L"unins000.dat"};

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle &) = delete;
};
struct HttpHandle {
    HINTERNET value;
    explicit HttpHandle(HINTERNET h) : value(h) {
        if (!h) throw std::runtime_error("WinHTTP initialization failed");
    }
    ~HttpHandle() { WinHttpCloseHandle(value); }
};

std::optional<std::array<unsigned, 3>> versionParts(const std::string &version) {
    std::array<unsigned, 3> parts{};
    const char *pos = version.data(), *end = pos + version.size();
    for (size_t i = 0; i < parts.size(); ++i) {
        auto number = std::from_chars(pos, end, parts[i]);
        if (number.ec != std::errc{} || (number.ptr - pos > 1 && *pos == '0')) return {};
        if (i + 1 == parts.size()) {
            if (number.ptr != end) return {};
        } else {
            if (number.ptr == end || *number.ptr != '.') return {};
            pos = number.ptr + 1;
        }
    }
    return parts;
}

bool hexDigest(const std::string &digest) {
    return digest.size() == 64 && digest.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::wstring assetName(const std::string &tag) { return L"CodexQuotaBar-" + wide(tag) + L"-Setup.exe"; }

std::string fetch(std::wstring url, size_t limit, bool download, const std::atomic<bool> &cancel) {
    HttpHandle session(WinHttpOpen(L"CodexQuotaBar", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
    if (!WinHttpSetTimeouts(session.value, 4000, 4000, 4000, 4000))
        throw std::runtime_error("Cannot set update timeouts");
    auto deadline = GetTickCount64() + 45000;
    auto check = [&] {
        if (cancel || GetTickCount64() >= deadline) throw std::runtime_error("Update request cancelled or timed out");
    };
    for (int redirects = 0; redirects <= 3; ++redirects) {
        check();
        URL_COMPONENTS parts{sizeof(parts)};
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
            parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
        if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
            parts.nPort != INTERNET_DEFAULT_HTTPS_PORT || parts.dwUserNameLength || parts.dwPasswordLength)
            throw std::runtime_error("Invalid update URL");
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        bool allowed = !download ? host == L"api.github.com" :
            (redirects == 0 ? host == L"github.com" :
             host == L"release-assets.githubusercontent.com" || host == L"objects.githubusercontent.com");
        if (!allowed) throw std::runtime_error("Unexpected update host");
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        HttpHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
        HttpHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, nullptr,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        const wchar_t *headers = download ? WINHTTP_NO_ADDITIONAL_HEADERS : L"Accept: application/vnd.github+json\r\n";
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)) ||
            !WinHttpSendRequest(request.value, headers, download ? 0 : DWORD(-1L), nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr))
            throw std::runtime_error("Update request failed");
        check();
        DWORD status = 0, length = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 nullptr, &status, &length, nullptr))
            throw std::runtime_error("Missing update HTTP status");
        if (download && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            wchar_t location[8192]{};
            length = sizeof(location);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, nullptr, location, &length, nullptr))
                throw std::runtime_error("Invalid update redirect");
            url = location;
            continue;
        }
        if (status != 200) throw std::runtime_error("Update HTTP error " + std::to_string(status));
        std::string bytes;
        char buffer[16384];
        while (true) {
            check();
            DWORD read = 0;
            if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read))
                throw std::runtime_error("Update read failed");
            if (!read) return bytes;
            if (read > limit - bytes.size()) throw std::runtime_error("Update response exceeds size limit");
            bytes.append(buffer, read);
        }
    }
    throw std::runtime_error("Too many update redirects");
}

bool plainPath(const fs::path &path, bool directory = false) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
           bool(attrs & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

void checkDirectory(const fs::path &path) {
    if (!path.is_absolute() || path == path.root_path() || path != path.lexically_normal())
        throw std::runtime_error("Invalid update directory");
    for (auto part = path; part != part.root_path(); part = part.parent_path())
        if (!plainPath(part, true)) throw std::runtime_error("Update directory is missing or redirected");
}

std::wstring eventName(const fs::path &stage, const wchar_t *suffix) {
    return L"Local\\CodexQuotaBar." + stage.filename().wstring() + suffix;
}

HANDLE startProcess(const fs::path &program, const std::wstring &arguments, const fs::path &directory) {
    std::wstring command = quote(program.wstring()) + L" " + arguments;
    STARTUPINFOW info{sizeof(info)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, directory.c_str(), &info, &process))
        throw std::runtime_error("Could not launch update process");
    CloseHandle(process.hThread);
    return process.hProcess;
}

void verifyInstall(const fs::path &dir, const std::string &version) {
    if (!nativeInstalled(dir)) throw std::runtime_error("Installation marker unavailable");
    wchar_t marker[80]{};
    GetPrivateProfileStringW(L"Installation", L"Version", L"", marker, 80, (dir / L"installer.ini").c_str());
    if (wide(version) != marker) throw std::runtime_error("Installed version marker differs");
    for (auto file : InstallerFiles)
        if (!plainPath(dir / file)) throw std::runtime_error("Installed metadata is missing");
    std::ifstream versionFile(dir / L"version.json"), manifestFile(dir / L"uninstall-manifest.json");
    auto metadata = json::parse(versionFile);
    auto manifest = json::parse(manifestFile);
    if (metadata.at("version") != version || metadata.at("runtime") != "native-win32" ||
        metadata.at("architecture") != "x64" || manifest.at("product") != "CodexQuotaBar" ||
        !manifest.at("files").is_object() || manifest["files"].size() != std::size(Payload))
        throw std::runtime_error("Invalid installed manifest");
    for (auto file : Payload) {
        if (!plainPath(dir / file) || manifest["files"].at(utf8(file)) != sha256(dir / file))
            throw std::runtime_error("Installed payload verification failed");
    }
}

void restart(const fs::path &app, const fs::path &stage, const std::string &version) {
    auto name = eventName(stage, L".Started");
    Handle started(CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
    if (!started.value || !ResetEvent(started.value)) throw std::runtime_error("Cannot create update readiness event");
    Handle process(startProcess(app, L"--update-ready " + quote(name) + L" --update-version " + wide(version),
                                app.parent_path()));
    HANDLE waiting[] = {started.value, process.value};
    if (WaitForMultipleObjects(2, waiting, FALSE, 15000) != WAIT_OBJECT_0) {
        if (WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 1);
            WaitForSingleObject(process.value, 5000);
        }
        throw std::runtime_error("Updated application did not become ready");
    }
}
} // namespace

std::optional<UpdateRelease> parseUpdateRelease(const json &release, const std::string &current) {
    if (release.at("draft") != false || release.at("prerelease") != false)
        throw std::runtime_error("Unexpected release status");
    auto tag = release.at("tag_name").get<std::string>();
    auto installed = versionParts(current);
    auto available = !tag.empty() && tag[0] == 'v' ? versionParts(tag.substr(1)) : std::nullopt;
    if (!installed || !available) throw std::runtime_error("Invalid update version");
    if (*available <= *installed) return {};
    std::optional<UpdateRelease> result;
    for (const auto &asset : release.at("assets")) {
        if (asset.at("name") != utf8(assetName(tag))) continue;
        auto digest = asset.at("digest").get<std::string>();
        auto size = asset.at("size").get<int64_t>();
        if (result || asset.at("state") != "uploaded" || digest.compare(0, 7, "sha256:") != 0 ||
            !hexDigest(digest.substr(7)) || size <= 0 || size > int64_t(MaxSetup))
            throw std::runtime_error("Invalid release installer metadata");
        result = UpdateRelease{tag, digest.substr(7), size_t(size)};
    }
    if (!result) throw std::runtime_error("Release installer not found");
    return result;
}

std::optional<UpdateRelease> checkForUpdate(const std::atomic<bool> &cancel) {
    return parseUpdateRelease(json::parse(fetch(L"https://api.github.com/repos/SaturdayF/codex-quota-bar/releases/latest",
                                                1024 * 1024, false, cancel)), VersionUtf8);
}

fs::path downloadUpdate(const UpdateRelease &release, const std::atomic<bool> &cancel) {
    auto url = L"https://github.com" + std::wstring(RepoPath) + wide(release.tag) + L"/" + assetName(release.tag);
    auto bytes = fetch(url, release.size, true, cancel);
    if (cancel || bytes.size() != release.size) throw std::runtime_error("Incomplete installer download");
    auto base = dataDirectory();
    fs::create_directories(base);
    checkDirectory(base);
    auto stage = base / (L"Update-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(stage)) throw std::runtime_error("Cannot create update staging directory");
    try {
        auto setup = stage / SetupFile;
        std::ofstream file(setup, std::ios::binary);
        file.write(bytes.data(), std::streamsize(bytes.size()));
        file.close();
        if (!file || sha256(setup) != release.digest || cancel)
            throw std::runtime_error("Downloaded installer verification failed");
        return stage;
    } catch (...) {
        std::error_code error;
        fs::remove(stage / SetupFile, error);
        fs::remove(stage, error);
        throw;
    }
}

void launchUpdate(const fs::path &stage, const UpdateRelease &release) {
    Handle lock(CreateMutexW(nullptr, FALSE, UpdateLock));
    if (!lock.value || GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("An update is already running");
    auto helper = stage / L"UpdateHelper.exe";
    fs::copy_file(executable(), helper);
    auto name = eventName(stage, L".Helper");
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
    if (!ready.value || GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("Cannot create updater event");
    Handle process(startProcess(helper, L"--update-helper " + std::to_wstring(GetCurrentProcessId()) + L" " +
                                wide(release.tag.substr(1)) + L" " + wide(release.digest), stage));
    HANDLE waiting[] = {ready.value, process.value};
    if (WaitForMultipleObjects(2, waiting, FALSE, 5000) != WAIT_OBJECT_0) {
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, 5000);
        throw std::runtime_error("Cannot hand off to updater");
    }
}
void discardUpdate(const fs::path &stage) {
    std::error_code error;
    fs::remove(stage / SetupFile, error);
    fs::remove(stage / L"UpdateHelper.exe", error);
    fs::remove(stage, error);
}

int runUpdateHelper(const std::vector<std::wstring> &args) {
    if (args.size() != 4) return 1;
    const auto stage = executable().parent_path();
    auto version = utf8(args[2]), digest = utf8(args[3]);
    auto setup = stage / SetupFile;
    auto backup = stage / L"backup";
    fs::path app, dir;
    bool handedOff = false, stopped = false, backedUp = false;
    Handle lock(OpenMutexW(SYNCHRONIZE, FALSE, UpdateLock));
    try {
        checkDirectory(stage);
        if (stage.parent_path() != dataDirectory() || stage.filename().wstring().compare(0, 7, L"Update-") ||
            !versionParts(version) || *versionParts(version) <= *versionParts(VersionUtf8) || !hexDigest(digest))
            throw std::runtime_error("Invalid update handoff");
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName(stage, L".Helper").c_str()));
        Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, std::stoul(args[1])));
        wchar_t path[32768];
        DWORD size = DWORD(std::size(path));
        if (!lock.value || !ready.value || !parent.value || !QueryFullProcessImageNameW(parent.value, 0, path, &size))
            throw std::runtime_error("Cannot open original application");
        app = std::wstring(path, size);
        dir = app.parent_path();
        checkDirectory(dir);
        if (app.filename() != L"CodexQuotaBar.exe" || !nativeInstalled(dir))
            throw std::runtime_error("The original directory is not an installed copy");
        Handle verified(CreateFileW(setup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (verified.value == INVALID_HANDLE_VALUE || !plainPath(setup) || fs::file_size(setup) > MaxSetup ||
            sha256(setup) != digest)
            throw std::runtime_error("Installer verification failed before launch");
        handedOff = SetEvent(ready.value) != FALSE;
        if (!handedOff || WaitForSingleObject(parent.value, 20000) != WAIT_OBJECT_0)
            throw std::runtime_error("Original application did not exit");
        stopped = true;
        if (!fs::create_directory(backup)) throw std::runtime_error("Cannot create update backup");
        auto save = [&](const wchar_t *file) {
            auto source = dir / file;
            if (!plainPath(source)) throw std::runtime_error("Cannot back up missing or redirected installation file");
            fs::copy_file(source, backup / file);
        };
        for (auto file : Payload) save(file);
        for (auto file : InstallerFiles) save(file);
        backedUp = true;
        auto flags = L"/QUOTAINPLACE=1 /VERYSILENT /SUPPRESSMSGBOXES /SP- /NORESTART /RESTARTEXITCODE=3010 /NOCLOSEAPPLICATIONS /TASKS=\"\" " +
                     quote(L"/DIR=" + dir.wstring()) + L" " + quote(L"/LOG=" + (stage / L"install.log").wstring());
        Handle installer(startProcess(setup, flags, stage));
        DWORD exitCode = 1;
        if (WaitForSingleObject(installer.value, INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(installer.value, &exitCode) || exitCode != 0)
            throw std::runtime_error("Installer did not complete: " + std::to_string(exitCode));
        CloseHandle(verified.value);
        verified.value = nullptr;
        verifyInstall(dir, version);
        restart(app, stage, version);
        std::error_code error;
        for (auto file : Payload) fs::remove(backup / file, error);
        for (auto file : InstallerFiles) fs::remove(backup / file, error);
        fs::remove(backup, error);
        fs::remove(setup, error);
        return 0;
    } catch (const std::exception &failure) {
        std::ofstream(stage / L"error.txt", std::ios::app) << failure.what() << '\n';
        if (!handedOff) return 1;
        bool recovered = false;
        if (stopped) {
            try {
                if (backedUp) {
                    checkDirectory(dir);
                    auto restore = [&](const wchar_t *file) {
                        auto target = dir / file;
                        if (fs::exists(target) && !plainPath(target))
                            throw std::runtime_error("Update destination was redirected");
                        fs::copy_file(backup / file, target, fs::copy_options::overwrite_existing);
                    };
                    for (auto file : Payload) restore(file);
                    for (auto file : InstallerFiles) restore(file);
                }
                restart(app, stage, VersionUtf8);
                recovered = true;
            } catch (const std::exception &error) {
                std::ofstream(stage / L"error.txt", std::ios::app) << error.what() << '\n';
            }
        }
        // Hold the old program's backup and the installer log for recovery.
        auto message = recovered ? L"更新未完成，已重新启动原版本。\n\n日志和备份目录：\n" :
                                   L"更新未完成。\n\n日志和可用备份目录：\n";
        MessageBoxW(nullptr, (message + stage.wstring()).c_str(), L"Codex Quota Bar", MB_OK | MB_ICONERROR);
        return 1;
    }
}
} // namespace quota
