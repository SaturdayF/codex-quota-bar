#include "browser_token.hpp"
#include "deepseek.hpp"
#include <array>
#include <set>
#include <string_view>

namespace quota {
namespace {
using View = std::string_view;
constexpr size_t Limit = 64 * 1024 * 1024;
void require(bool ok) { if (!ok) throw std::runtime_error("Browser storage unavailable"); }
uint64_t fixed(View s, size_t pos, size_t bytes) {
    require(pos <= s.size() && bytes <= s.size() - pos);
    uint64_t n = 0;
    for (size_t i = 0; i < bytes; ++i) n |= uint64_t(uint8_t(s[pos + i])) << (8 * i);
    return n;
}
uint64_t varint(View &s) {
    uint64_t n = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
        require(!s.empty());
        auto c = uint8_t(s.front()); s.remove_prefix(1);
        require(shift != 63 || c <= 1);
        n |= uint64_t(c & 127) << shift;
        if (!(c & 128)) return n;
    }
    require(false); return 0;
}
View take(View &s, uint64_t n) {
    require(n <= s.size());
    auto out = s.substr(0, size_t(n)); s.remove_prefix(size_t(n)); return out;
}
View field(View &s) { auto n = varint(s); return take(s, n); }
uint32_t crc(View s) {
    static const auto table = [] {
        std::array<uint32_t, 256> out{};
        for (uint32_t i = 0; i < out.size(); ++i) {
            uint32_t n = i;
            for (int j = 0; j < 8; ++j) n = (n >> 1) ^ ((n & 1) ? 0x82f63b78u : 0);
            out[i] = n;
        }
        return out;
    }();
    uint32_t n = ~0u;
    for (unsigned char c : s) n = table[(n ^ c) & 255] ^ (n >> 8);
    n = ~n; return ((n >> 15) | (n << 17)) + 0xa282ead8u;
}
std::string readShared(const fs::path &path, size_t &budget) {
    struct File {
        HANDLE h;
        ~File() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    } file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    require(file.h != INVALID_HANDLE_VALUE);
    LARGE_INTEGER size{};
    require(GetFileSizeEx(file.h, &size) && size.QuadPart >= 0 && uint64_t(size.QuadPart) <= Limit &&
            uint64_t(size.QuadPart) <= budget);
    budget -= size_t(size.QuadPart);
    std::string data(size_t(size.QuadPart), '\0');
    size_t pos = 0;
    while (pos < data.size()) {
        DWORD count = 0;
        require(ReadFile(file.h, data.data() + pos, DWORD(std::min<size_t>(65536, data.size() - pos)), &count, nullptr) && count);
        pos += count;
    }
    return data;
}
// LevelDB physical records use masked CRC32C and can span 32 KiB blocks.
// An incomplete final append is ignored; corruption of a full record is fatal.
void records(View file, const std::function<void(View)> &visit) {
    size_t pos = 0;
    std::string partial;
    bool assembling = false;
    while (pos < file.size()) {
        size_t remaining = 32768 - pos % 32768;
        if (remaining < 7) { pos += remaining; continue; }
        if (file.size() - pos < 7) break;
        size_t length = size_t(fixed(file, pos + 4, 2));
        int type = uint8_t(file[pos + 6]);
        if (!length && !type) {
            auto padding = file.substr(pos, std::min(remaining, file.size() - pos));
            require(padding.find_first_not_of('\0') == View::npos);
            pos += remaining; continue;
        }
        require(length <= remaining - 7);
        if (length > file.size() - pos - 7) break;
        auto data = file.substr(pos + 7, length);
        require(crc(file.substr(pos + 6, length + 1)) == fixed(file, pos, 4));
        pos += length + 7;
        if (type == 1) { require(!assembling); visit(data); }
        else if (type == 2) { require(!assembling); partial.assign(data); assembling = true; }
        else if (type == 3 || type == 4) {
            require(assembling && partial.size() + data.size() <= Limit);
            partial.append(data);
            if (type == 4) { visit(partial); partial.clear(); assembling = false; }
        } else require(false);
    }
}
std::string snappy(View in) {
    auto length = varint(in);
    require(length <= Limit);
    std::string out; out.reserve(size_t(length));
    while (!in.empty()) {
        unsigned tag = uint8_t(in.front()); in.remove_prefix(1);
        uint64_t count = 0, offset = 0;
        if (!(tag & 3)) {
            count = (tag >> 2) + 1;
            if (count > 60) {
                auto bytes = size_t(count - 60);
                count = fixed(in, 0, bytes) + 1; in.remove_prefix(bytes);
            }
            require(count <= length - out.size()); out.append(take(in, count));
        } else {
            if ((tag & 3) == 1) {
                count = 4 + ((tag >> 2) & 7);
                offset = ((tag & 224) << 3) | fixed(in, 0, 1); in.remove_prefix(1);
            } else {
                count = 1 + (tag >> 2);
                size_t bytes = (tag & 3) == 2 ? 2 : 4;
                offset = fixed(in, 0, bytes); in.remove_prefix(bytes);
            }
            require(offset && offset <= out.size() && count <= length - out.size());
            while (count--) out.push_back(out[out.size() - size_t(offset)]);
        }
    }
    require(out.size() == length); return out;
}
void entries(View block, const std::function<void(View, View)> &visit) {
    require(block.size() >= 4);
    auto restarts = fixed(block, block.size() - 4, 4);
    require(restarts && restarts <= (block.size() - 4) / 4);
    size_t end = block.size() - 4 - size_t(restarts) * 4;
    for (size_t i = 0; i < restarts; ++i) require(fixed(block, end + i * 4, 4) <= end);
    View data = block.substr(0, end);
    std::string key;
    while (!data.empty()) {
        auto shared = varint(data), suffix = varint(data), valueSize = varint(data);
        require(shared <= key.size() && suffix <= Limit - shared);
        key.resize(size_t(shared)); key.append(take(data, suffix));
        auto value = take(data, valueSize); visit(key, value);
    }
}
struct Handle { uint64_t offset, size; };
Handle handle(View &v) { auto offset = varint(v); return {offset, varint(v)}; }
std::string tableBlock(View file, Handle h) {
    require(file.size() >= 48 && h.offset <= file.size() - 48 && h.size <= file.size() - 48 - h.offset &&
            file.size() - 48 - h.offset - h.size >= 5);
    auto offset = size_t(h.offset), size = size_t(h.size);
    require(crc(file.substr(offset, size + 1)) == fixed(file, offset + size + 1, 4));
    auto data = file.substr(offset, size);
    auto compression = uint8_t(file[offset + size]);
    if (!compression) return std::string(data);
    require(compression == 1); return snappy(data);
}
struct Latest {
    bool found = false;
    uint64_t sequence = 0;
    std::string value;
    bool schemaFound = false;
    uint64_t schemaSequence = 0;
    std::string schema;
    void offer(View key, View data, uint64_t seq, unsigned type) {
        if (key == "VERSION") {
            require(type <= 1);
            if (!schemaFound || seq > schemaSequence) {
                schemaFound = true; schemaSequence = seq; schema = type ? std::string(data) : std::string();
            }
            return;
        }
        const std::string prefix = std::string("_https://platform.deepseek.com") + '\0';
        if (key.substr(0, prefix.size()) != prefix) return;
        auto suffix = key.substr(prefix.size());
        const std::string latin = std::string(1, '\1') + "userToken";
        std::string utf16(1, '\0');
        for (char c : std::string("userToken")) { utf16 += c; utf16 += '\0'; }
        if (suffix != latin && suffix != utf16) return;
        require(type <= 1);
        if (!found || seq > sequence) {
            found = true; sequence = seq; value = type ? std::string(data) : std::string();
        }
    }
};
void table(View file, Latest &latest) {
    require(file.size() >= 48 && fixed(file, file.size() - 8, 8) == 0xdb4775248b80fb57ULL);
    auto footer = file.substr(file.size() - 48, 40);
    handle(footer); auto index = tableBlock(file, handle(footer));
    entries(index, [&](View, View value) {
        auto data = tableBlock(file, handle(value));
        entries(data, [&](View key, View v) {
            require(key.size() >= 8);
            auto tag = fixed(key, key.size() - 8, 8);
            latest.offer(key.substr(0, key.size() - 8), v, tag >> 8, unsigned(tag & 255));
        });
    });
}
void batch(View in, Latest &latest) {
    require(in.size() >= 12);
    auto sequence = fixed(in, 0, 8), count = fixed(in, 8, 4); in.remove_prefix(12);
    require(count <= in.size() && sequence <= ((uint64_t(1) << 56) - 1) - count);
    for (uint64_t i = 0; i < count; ++i) {
        require(!in.empty()); auto type = uint8_t(in.front()); in.remove_prefix(1);
        require(type <= 1);
        auto key = field(in); auto value = type ? field(in) : View();
        latest.offer(key, value, sequence + i, type);
    }
    require(in.empty());
}
struct Manifest {
    std::set<uint64_t> tables;
    uint64_t log = 0, previousLog = 0;
    bool hasLog = false;
    void edit(View in) {
        while (!in.empty()) {
            auto tag = varint(in);
            if (tag == 1) require(field(in) == "leveldb.BytewiseComparator");
            else if (tag == 2) { log = varint(in); hasLog = true; }
            else if (tag == 9) previousLog = varint(in);
            else if (tag == 3 || tag == 4) varint(in);
            else if (tag == 5) { require(varint(in) < 7); field(in); }
            else if (tag == 6) { require(varint(in) < 7); tables.erase(varint(in)); }
            else if (tag == 7) {
                require(varint(in) < 7); auto number = varint(in); varint(in); field(in); field(in); tables.insert(number);
            } else require(false);
        }
    }
};
std::wstring numbered(uint64_t n, const wchar_t *suffix) {
    std::wostringstream out; out << std::setw(6) << std::setfill(L'0') << n << suffix; return out.str();
}
bool digits(View s) { return !s.empty() && s.find_first_not_of("0123456789") == View::npos; }
std::string decodeValue(const std::string &raw) {
    if (raw.empty()) return {};
    std::string text;
    if (raw.front() == '\1') {
        for (size_t i = 1; i < raw.size(); ++i) { require(uint8_t(raw[i]) < 128); text += raw[i]; }
    } else {
        require(raw.front() == '\0' && raw.size() % 2 == 1);
        for (size_t i = 1; i < raw.size(); i += 2) { require(raw[i + 1] == '\0' && uint8_t(raw[i]) < 128); text += raw[i]; }
    }
    auto token = normalizeDeepSeekCredential(text);
    require(validDeepSeekCredential(token)); return token;
}
} // namespace

std::string readBrowserPlatformToken(const fs::path &database, std::atomic<bool> &stop) {
    std::error_code ec;
    if (!fs::exists(database, ec)) { require(!ec); return {}; }
    // Never scan obsolete tables: a compacted deletion can otherwise resurrect
    // credentials after logout. Retry if compaction changes CURRENT/MANIFEST.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (stop) return {};
        try {
            size_t budget = 256 * 1024 * 1024;
            auto current = readShared(database / L"CURRENT", budget);
            require(current.size() > 10 && current.back() == '\n');
            auto name = current.substr(0, current.size() - 1);
            require(name.rfind("MANIFEST-", 0) == 0 && digits(View(name).substr(9)));
            auto manifestBytes = readShared(database / wide(name), budget);
            Manifest manifest;
            records(manifestBytes, [&](View record) { manifest.edit(record); });
            require(manifest.hasLog);
            Latest latest;
            for (auto n : manifest.tables) {
                if (stop) return {};
                auto path = database / numbered(n, L".ldb");
                if (!fs::exists(path)) path = database / numbered(n, L".sst");
                table(readShared(path, budget), latest);
            }
            for (const auto &entry : fs::directory_iterator(database)) {
                if (stop) return {};
                auto file = utf8(entry.path().filename().wstring());
                if (entry.path().extension() != L".log") continue;
                auto number = View(file).substr(0, file.size() - 4);
                require(digits(number) && number.size() <= 20);
                auto n = std::stoull(std::string(number));
                if (n >= manifest.log || n == manifest.previousLog) {
                    auto data = readShared(entry.path(), budget);
                    records(data, [&](View record) { batch(record, latest); });
                }
            }
            require(readShared(database / L"CURRENT", budget) == current &&
                    readShared(database / wide(name), budget) == manifestBytes);
            require((!latest.schemaFound && !latest.found) || latest.schema == "1");
            return decodeValue(latest.value);
        } catch (const std::exception &) {
            if (attempt == 2) throw std::runtime_error("Browser storage unavailable");
        }
    }
    return {};
}

std::string edgeProfileName(const fs::path &userData) {
    size_t budget = Limit;
    auto state = json::parse(readShared(userData / L"Local State", budget), nullptr, false);
    require(state.is_object() && state.contains("profile") && state["profile"].is_object());
    auto &profiles = state["profile"];
    require(profiles.contains("last_used") && profiles["last_used"].is_string());
    auto name = profiles["last_used"].get<std::string>();
    require(name == "Default" || (name.rfind("Profile ", 0) == 0 && digits(View(name).substr(8))));
    return name;
}

int browserTokenTests(std::ostream &log) {
    int checks = 0;
    auto check = [&](bool ok, const char *name) {
        log << (ok ? "PASS " : "FAIL ") << name << std::endl;
        if (!ok) throw std::runtime_error(name);
        ++checks;
    };
    auto rejects = [](const std::function<void()> &call) {
        try { call(); return false; } catch (...) { return true; }
    };
    // Independent known CRC32C check vector (unmasked 0xe3069283).
    uint32_t known = 0xe3069283u;
    check(crc("123456789") == ((known >> 15) | (known << 17)) + 0xa282ead8u,
          "browser storage CRC32C matches the published check vector");
    check(snappy(std::string("\7\10xab\1\2", 7)) == "xababab", "Snappy overlapping short backreference");
    check(snappy(std::string("\7\10xab\16\2\0", 8)) == "xababab", "Snappy two-byte backreference");
    check(snappy(std::string("\7\10xab\17\2\0\0\0", 10)) == "xababab", "Snappy four-byte backreference");
    check(rejects([&] { snappy(std::string("\4\1\0", 3)); }), "Snappy invalid offset fails closed");
    auto putFixed = [](std::string &out, uint64_t n, int bytes) {
        for (int i = 0; i < bytes; ++i) { out += char(n & 255); n >>= 8; }
    };
    auto putVar = [](std::string &out, uint64_t n) {
        while (n > 127) { out += char((n & 127) | 128); n >>= 7; } out += char(n);
    };
    auto putField = [&](std::string &out, View value) { putVar(out, value.size()); out.append(value); };
    auto physical = [&](View data, int type = 1) {
        std::string out, payload(1, char(type)); payload.append(data);
        putFixed(out, crc(payload), 4); putFixed(out, data.size(), 2); out += payload; return out;
    };
    auto key = std::string("_https://platform.deepseek.com") + '\0' + '\1' + "userToken";
    auto value = std::string(1, '\1') + "{\"value\":\"fixture-session\"}";
    auto writeBatch = [&](uint64_t sequence, const std::vector<std::pair<std::string, std::string>> &rows, bool deletion = false) {
        std::string out; putFixed(out, sequence, 8); putFixed(out, rows.size(), 4);
        for (auto &row : rows) { out += deletion ? '\0' : '\1'; putField(out, row.first); if (!deletion) putField(out, row.second); }
        return physical(out);
    };
    auto makeBlock = [&](const std::vector<std::pair<std::string, std::string>> &rows) {
        std::string out, previous;
        for (auto &row : rows) {
            size_t shared = 0;
            while (shared < previous.size() && shared < row.first.size() && previous[shared] == row.first[shared]) ++shared;
            putVar(out, shared); putVar(out, row.first.size() - shared); putVar(out, row.second.size());
            out += row.first.substr(shared); out += row.second; previous = row.first;
        }
        putFixed(out, 0, 4); putFixed(out, 1, 4); return out;
    };
    auto makeTable = [&] {
        auto internal = [&](std::string k, uint64_t sequence) { putFixed(k, (sequence << 8) | 1, 8); return k; };
        auto data = makeBlock({{internal("VERSION", 1), "1"}, {internal(key, 5), value}});
        // Snappy literal with a multi-byte length encoding exercises tables as
        // well as standalone decoding without adding a compression dependency.
        std::string compressed; putVar(compressed, data.size());
        compressed += char(60 << 2); compressed += char(data.size() - 1); compressed += data;
        std::string file = compressed; file += '\1'; putFixed(file, crc(file), 4);
        std::string pointer; putVar(pointer, 0); putVar(pointer, compressed.size());
        auto index = makeBlock({{internal(key, 5), pointer}});
        size_t indexOffset = file.size(); file += index; file += '\0';
        putFixed(file, crc(View(file).substr(indexOffset)), 4);
        std::string footer; putVar(footer, 0); putVar(footer, 0);
        putVar(footer, indexOffset); putVar(footer, index.size()); footer.resize(40, '\0');
        putFixed(footer, 0xdb4775248b80fb57ULL, 8); return file + footer;
    };
    auto fixture = fs::temp_directory_path() / (L"CodexQuotaBar-browser-token-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    require(fs::create_directory(fixture));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{fixture};
    auto write = [&](const fs::path &name, const std::string &data) {
        std::ofstream out(fixture / name, std::ios::binary | std::ios::trunc); out.write(data.data(), std::streamsize(data.size())); require(bool(out));
    };
    std::atomic<bool> stop{false};
    check(readBrowserPlatformToken(fixture / L"absent", stop).empty(), "missing browser database returns no credential");
    write(L"CURRENT", "MANIFEST-000001\n");
    std::string edit; putVar(edit, 1); putField(edit, "leveldb.BytewiseComparator"); putVar(edit, 2); putVar(edit, 2);
    write(L"MANIFEST-000001", physical(edit));
    auto wal = writeBatch(1, {{"VERSION", "1"}, {key, value}});
    write(L"000002.log", wal);
    check(readBrowserPlatformToken(fixture, stop) == "fixture-session", "Edge userToken JSON is normalized from an active WAL");
    write(L"000002.log", wal + writeBatch(3, {{key, ""}}, true));
    check(readBrowserPlatformToken(fixture, stop).empty(), "latest WAL tombstone prevents reusing a logged-out token");
    write(L"000002.log", writeBatch(1, {{"VERSION", "1"}, {"_https://example.com\0userToken", value}}));
    check(readBrowserPlatformToken(fixture, stop).empty(), "credentials from other origins are never selected");
    write(L"000002.log", wal + physical("incomplete").substr(0, 10));
    check(readBrowserPlatformToken(fixture, stop) == "fixture-session", "incomplete final WAL append preserves committed records");
    auto corrupt = wal; corrupt.back() ^= 1; write(L"000002.log", corrupt);
    check(rejects([&] { readBrowserPlatformToken(fixture, stop); }), "corrupt browser log fails without returning stale credentials");
    auto batchData = wal.substr(7);
    auto fragmented = physical(View(batchData).substr(0, 10), 2);
    fragmented.resize(32768, '\0'); fragmented += physical(View(batchData).substr(10), 4);
    write(L"000002.log", fragmented);
    check(readBrowserPlatformToken(fixture, stop) == "fixture-session", "fragmented WAL records cross physical blocks");
    auto tableData = makeTable(); write(L"000004.ldb", tableData);
    std::string add; putVar(add, 7); putVar(add, 0); putVar(add, 4); putVar(add, tableData.size()); putField(add, ""); putField(add, "");
    write(L"MANIFEST-000001", physical(edit) + physical(add));
    write(L"000002.log", "");
    check(readBrowserPlatformToken(fixture, stop) == "fixture-session", "compressed SSTable and prefix-compressed keys are read");
    write(L"000002.log", writeBatch(6, {{key, ""}}, true));
    check(readBrowserPlatformToken(fixture, stop).empty(), "WAL deletion supersedes an older SSTable token");
    std::string remove; putVar(remove, 6); putVar(remove, 0); putVar(remove, 4);
    write(L"MANIFEST-000001", physical(edit) + physical(add) + physical(remove));
    write(L"000002.log", "");
    write(L"000001.log", wal);
    check(readBrowserPlatformToken(fixture, stop).empty(), "obsolete tables and obsolete logs cannot resurrect a credential");
    write(L"000002.log", writeBatch(1, {{"VERSION", "2"}, {key, value}}));
    check(rejects([&] { readBrowserPlatformToken(fixture, stop); }), "unsupported browser schema is reported as a read failure");
    std::string utf16(1, '\0'); for (char c : std::string("fixture-utf16")) { utf16 += c; utf16 += '\0'; }
    write(L"000002.log", writeBatch(1, {{"VERSION", "1"}, {key, utf16}}));
    check(readBrowserPlatformToken(fixture, stop) == "fixture-utf16", "UTF16 browser token values decode correctly");
    write(L"000002.log", writeBatch(1, {{"VERSION", "1"}, {key, std::string(1, '\1') + "sk-test"}}));
    check(rejects([&] { readBrowserPlatformToken(fixture, stop); }), "browser acquisition rejects API keys");
    write(L"Local State", "{\"profile\":{\"last_used\":\"Profile 2\"}}");
    check(edgeProfileName(fixture) == "Profile 2", "Edge acquisition uses the last-used profile without scanning other accounts");
    write(L"Local State", "{\"profile\":{\"last_used\":\"../outside\"}}");
    check(rejects([&] { edgeProfileName(fixture); }), "Edge profile paths reject directory traversal");
    stop = true;
    check(readBrowserPlatformToken(fixture, stop).empty(), "cancelled browser acquisition returns no credential");
    return checks;
}
} // namespace quota
