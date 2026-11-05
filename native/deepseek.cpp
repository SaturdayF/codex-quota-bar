#include "deepseek.hpp"
#include <winhttp.h>
#include <wincred.h>
#include <ctime>
#include <limits>
namespace quota {
int64_t beijingDay(int64_t now) {
    return (now + 28800) / 86400 * 86400 - 28800;
}
std::wstring beijingDate(int64_t now) {
    std::time_t stamp = now + 28800;
    std::tm date{};
    gmtime_s(&date, &stamp);
    wchar_t result[32]{};
    wcsftime(result, 32, L"%Y-%m-%d", &date);
    return result;
}
static int64_t add(int64_t a, int64_t b) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        throw std::runtime_error("DeepSeek numeric overflow");
    return a + b;
}
int64_t decimalMoney(const json &value) {
    auto text = value.is_string() ? value.get<std::string>() : value.is_number() ? value.dump() : "";
    if (text.empty() || text.size() > 64)
        throw std::runtime_error("Invalid DeepSeek amount");
    size_t i = text[0] == '-' ? 1 : 0;
    bool negative = i != 0, dot = false, digit = false;
    int decimals = 0;
    int64_t result = 0;
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (c == '.' && !dot) { dot = true; continue; }
        if (c < '0' || c > '9')
            throw std::runtime_error("Invalid DeepSeek decimal");
        digit = true;
        if (dot && ++decimals > 8) {
            if (c != '0')
                throw std::runtime_error("Unsupported DeepSeek decimal precision");
            continue;
        }
        if (result > (INT64_MAX - (c - '0')) / 10)
            throw std::runtime_error("DeepSeek amount overflow");
        result = result * 10 + c - '0';
    }
    if (!digit)
        throw std::runtime_error("Invalid DeepSeek decimal");
    for (int n = std::min(decimals, 8); n < 8; ++n) {
        if (result > INT64_MAX / 10)
            throw std::runtime_error("DeepSeek amount overflow");
        result *= 10;
    }
    return negative ? -result : result;
}
std::wstring moneyLabel(std::optional<int64_t> amount, const std::string &currency, int digits) {
    if (!amount)
        return L"--";
    std::wostringstream text;
    text << (currency == "CNY" ? L"¥" : currency == "USD" ? L"$" : wide(currency) + L" ");
    auto value = static_cast<long double>(*amount) / MoneyScale;
    if (*amount > 0 && value < std::pow(10.L, -digits))
        text << L"<" << std::fixed << std::setprecision(digits) << std::pow(10.L, -digits);
    else
        text << std::fixed << std::setprecision(digits) << value;
    auto result = text.str();
    if (digits == 8) {
        auto dot = result.find(L'.');
        while (dot != std::wstring::npos && result.size() > dot + 3 && result.back() == L'0') result.pop_back();
    }
    return result;
}
std::string DeepSeekState::currency() const {
    for (auto &b : balances)
        if (b.currency == "CNY") return b.currency;
    if (!balances.empty()) return balances.front().currency;
    if (costs.count("CNY")) return "CNY";
    return costs.empty() ? "CNY" : costs.begin()->first;
}
std::string DeepSeekState::billingCurrency() const {
    auto unit = currency();
    if (costs.count(unit)) return unit;
    return costs.empty() ? unit : costs.begin()->first;
}
std::optional<int64_t> DeepSeekState::todayCost(const std::string &unit, int64_t now) const {
    if (!costAt || day != beijingDay(now)) return {};
    auto found = costs.find(unit);
    return found == costs.end() ? std::optional<int64_t>{} : found->second;
}
static const json &platformData(const json &value) {
    if (!value.is_object() || !value.contains("code") || !value["code"].is_number_integer())
        throw std::runtime_error("Invalid DeepSeek platform envelope");
    int code = value["code"].get<int>();
    if (code == 40002 || code == 40003)
        throw std::runtime_error("DeepSeek session expired");
    if (code != 0 || !value.contains("data") || !value["data"].is_object())
        throw std::runtime_error("DeepSeek platform request rejected");
    auto &data = value["data"];
    if (data.value("biz_code", -1) == 40002 || data.value("biz_code", -1) == 40003)
        throw std::runtime_error("DeepSeek session expired");
    if (data.value("biz_code", -1) != 0 || !data.contains("biz_data") || !data["biz_data"].is_object())
        throw std::runtime_error("Invalid DeepSeek platform result");
    return data["biz_data"];
}
void parseDeepSeekBalance(DeepSeekState &state, const json &value) {
    auto &body = platformData(value);
    auto field = "normal_wallets";
    if (!body.contains(field) || !body[field].is_array() || body[field].empty())
        throw std::runtime_error("Missing DeepSeek wallets");
    std::vector<DeepSeekBalance> balances;
    for (auto &wallet : body[field]) {
        auto currency = wallet.at("currency").get<std::string>();
        if (currency != "CNY" && currency != "USD")
            throw std::runtime_error("Unknown DeepSeek wallet currency");
        DeepSeekBalance b;
        b.currency = currency;
        b.total = decimalMoney(wallet.at("balance"));
        if (std::any_of(balances.begin(), balances.end(), [&](auto &a) { return a.currency == currency; }))
            throw std::runtime_error("Duplicate DeepSeek wallet");
        balances.push_back(b);
    }
    auto available = std::any_of(balances.begin(), balances.end(), [](auto &b) { return b.total > 0; });
    state.balances = std::move(balances);
    state.available = available;
}
static DeepSeekModel &modelRow(std::vector<DeepSeekModel> &rows, const std::string &name) {
    auto found = std::find_if(rows.begin(), rows.end(), [&](auto &r) { return r.name == name; });
    if (found != rows.end()) return *found;
    rows.push_back({});
    rows.back().name = name;
    return rows.back();
}
static void checkWindow(const json &body, int64_t day) {
    if (body.at("start").get<int64_t>() != day || body.at("end").get<int64_t>() != day + 86400)
        throw std::runtime_error("Unexpected DeepSeek reporting window");
    auto &bucket = body.at("bucket");
    int64_t seconds = 0;
    if (bucket.is_number_integer()) seconds = bucket.get<int64_t>();
    else if (bucket.is_string()) {
        auto text = bucket.get<std::string>();
        if (text.size() >= 2 && text.size() <= 6) {
            int factor = text.back() == 's' ? 1 : text.back() == 'm' ? 60 :
                         text.back() == 'h' ? 3600 : text.back() == 'd' ? 86400 : 0;
            int64_t count = 0;
            for (size_t i = 0; i + 1 < text.size(); ++i) {
                if (text[i] < '0' || text[i] > '9') { count = 0; break; }
                count = count * 10 + text[i] - '0';
            }
            seconds = count * factor;
        }
    }
    if (seconds <= 0 || seconds > 86400 || 86400 % seconds != 0)
        throw std::runtime_error("Unsupported DeepSeek bucket");
}
void parseDeepSeekCosts(DeepSeekState &state, const json &value) {
    auto &body = platformData(value);
    checkWindow(body, state.day);
    auto &currencies = body.at("data");
    if (!currencies.is_array()) throw std::runtime_error("Missing DeepSeek cost series");
    std::map<std::string, int64_t> totals;
    std::map<std::string, std::array<int64_t, 24>> hourly;
    auto models = state.models;
    for (auto &r : models) r.costs.clear();
    for (auto &data : currencies) {
        auto currency = data.at("currency").get<std::string>();
        if (currency != "CNY" && currency != "USD") throw std::runtime_error("Unknown cost currency");
        if (!data.at("series").is_array() || totals.count(currency))
            throw std::runtime_error("Invalid DeepSeek cost series");
        totals[currency] = 0;
        auto &hours = hourly[currency];
        for (auto &series : data["series"]) {
            auto &row = modelRow(models, series.at("model").get<std::string>());
            if (!series.at("buckets").is_array()) throw std::runtime_error("Invalid cost buckets");
            for (auto &bucket : series["buckets"]) {
                auto time = bucket.at("time").get<int64_t>();
                if (time < state.day || time >= state.day + 86400) continue;
                auto cost = decimalMoney(bucket.at("cost"));
                row.costs[currency] = add(row.costs[currency], cost);
                totals[currency] = add(totals[currency], cost);
                // Bucket each in-day timestamp by Beijing hour (UTC+8) so the
                // detail card can chart the day's spend shape.
                auto hour = size_t((time + 28800) % 86400 / 3600);
                hours[hour] = add(hours[hour], cost);
            }
        }
    }
    // A valid empty daily result means no billed usage, not a failed request.
    if (totals.empty()) totals[state.currency()] = 0;
    state.costs = std::move(totals);
    state.hourly = std::move(hourly);
    state.models = std::move(models);
}
static int64_t tokenNumber(const json &value) {
    if (value.is_number_integer()) {
        auto n = value.get<int64_t>();
        if (n >= 0) return n;
    }
    if (value.is_string()) {
        auto s = value.get<std::string>();
        int64_t n = 0;
        if (!s.empty()) {
            for (char c : s) {
                if (c < '0' || c > '9' || n > (INT64_MAX - (c - '0')) / 10)
                    throw std::runtime_error("Invalid DeepSeek usage count");
                n = n * 10 + c - '0';
            }
            return n;
        }
    }
    throw std::runtime_error("Invalid DeepSeek usage count");
}
void parseDeepSeekTokens(DeepSeekState &state, const json &value) {
    auto &body = platformData(value);
    checkWindow(body, state.day);
    if (!body.at("series").is_array()) throw std::runtime_error("Missing DeepSeek token series");
    auto models = state.models;
    for (auto &r : models) r.hit = r.miss = r.output = r.requests = 0;
    for (auto &series : body["series"]) {
        auto &row = modelRow(models, series.at("model").get<std::string>());
        if (!series.at("buckets").is_array()) throw std::runtime_error("Invalid usage buckets");
        for (auto &bucket : series["buckets"]) {
            auto time = bucket.at("time").get<int64_t>();
            if (time < state.day || time >= state.day + 86400) continue;
            auto &usage = bucket.at("usage");
            if (!usage.is_object()) throw std::runtime_error("Invalid DeepSeek usage");
            for (auto key : {"PROMPT_CACHE_HIT_TOKEN", "PROMPT_CACHE_MISS_TOKEN", "RESPONSE_TOKEN", "REQUEST"})
                if (!usage.contains(key)) throw std::runtime_error("Incomplete DeepSeek usage counters");
            row.hit = add(row.hit, tokenNumber(usage["PROMPT_CACHE_HIT_TOKEN"]));
            row.miss = add(row.miss, tokenNumber(usage["PROMPT_CACHE_MISS_TOKEN"]));
            row.output = add(row.output, tokenNumber(usage["RESPONSE_TOKEN"]));
            row.requests = add(row.requests, tokenNumber(usage["REQUEST"]));
        }
    }
    int64_t total = 0, requests = 0;
    for (auto &row : models) {
        total = add(total, add(add(row.hit, row.miss), row.output));
        requests = add(requests, row.requests);
    }
    state.models = std::move(models);
}
struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET v) : value(v) {
        if (!v) throw std::runtime_error("DeepSeek network error");
    }
    ~InternetHandle() { WinHttpCloseHandle(value); }
};
static json get(const std::wstring &host, const std::wstring &path, const std::string &token,
                std::atomic<bool> &stop) {
    if (stop) throw std::runtime_error("Cancelled");
    if (host != L"platform.deepseek.com")
        throw std::runtime_error("Invalid DeepSeek host");
    if (!validDeepSeekCredential(token)) throw std::runtime_error("Invalid DeepSeek credential");
    InternetHandle session(WinHttpOpen(L"CodexQuotaBar/0.3.5", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 5000)) throw std::runtime_error("DeepSeek network error");
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
        throw std::runtime_error("DeepSeek network error");
    auto headers = L"Accept: application/json\r\nx-client-platform: web\r\nAuthorization: Bearer " + wide(token) + L"\r\n";
    if (stop || !WinHttpSendRequest(request.value, headers.c_str(), DWORD(headers.size()),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("DeepSeek network error");
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("DeepSeek network error");
    if (status == 401) throw std::runtime_error("DeepSeek session expired");
    if (status == 403) throw std::runtime_error("DeepSeek access denied");
    if (status == 429) throw std::runtime_error("DeepSeek rate limited");
    if (status != 200) throw std::runtime_error("DeepSeek HTTP " + std::to_string(status));
    std::string body;
    for (;;) {
        if (stop) throw std::runtime_error("Cancelled");
        char chunk[8192];
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk, sizeof(chunk), &read)) throw std::runtime_error("DeepSeek network error");
        if (!read) break;
        body.append(chunk, read);
        if (body.size() > 2 * 1024 * 1024) throw std::runtime_error("DeepSeek response too large");
    }
    return json::parse(body);
}
static std::wstring failure(const std::exception &e) {
    std::string kind = e.what();
    if (kind == "DeepSeek session expired") return L"登录态已失效，请重新登录平台并更新 userToken";
    if (kind == "DeepSeek access denied") return L"DeepSeek 拒绝访问，请检查凭证或稍后重试";
    if (kind == "DeepSeek rate limited") return L"请求过于频繁，稍后自动重试";
    if (kind == "DeepSeek network error") return L"网络请求失败，请检查网络或代理";
    if (kind == "Unsupported DeepSeek bucket") return L"平台时间分桶格式不兼容，请更新程序";
    if (kind == "Unexpected DeepSeek reporting window") return L"平台返回的统计日期范围不匹配";
    if (dynamic_cast<const json::exception *>(&e)) return L"平台数据字段格式不兼容，请更新程序";
    const std::string httpPrefix = "DeepSeek HTTP ";
    if (kind.rfind(httpPrefix, 0) == 0) {
        auto code = kind.substr(httpPrefix.size());
        if (code.size() == 3 && std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return L"平台请求失败（HTTP " + wide(code) + L"）";
    }
    return L"读取失败：网络异常或平台数据格式变化";
}
std::wstring validateDeepSeekCredential(const std::string &token, std::atomic<bool> &stop,
                                      DeepSeekTransport transport) {
    if (stop) return L"已取消获取";
    if (token.empty() || !validDeepSeekCredential(token)) return L"未找到有效的平台登录凭证";
    try {
        if (!transport) transport = get;
        DeepSeekState state;
        parseDeepSeekBalance(state, transport(L"platform.deepseek.com", L"/api/v0/users/get_user_summary", token, stop));
        return stop ? L"已取消获取" : L"";
    } catch (const std::exception &e) {
        if (std::string(e.what()) == "DeepSeek session expired")
            return L"Edge 中的 DeepSeek 登录已失效，请在打开的平台网页登录，等待几秒后再次点击“一键获取凭证”。";
        return failure(e);
    }
}
DeepSeekState readDeepSeek(const std::string &sessionText,
                          std::atomic<bool> &stop, DeepSeekTransport transport, int64_t now) {
    DeepSeekState result;
    result.day = beijingDay(now);
    if (stop) return result;
    if (!transport) transport = get;
    auto platformToken = normalizeDeepSeekCredential(sessionText);
    if (platformToken.empty()) {
        result.balanceError = L"请在设置中配置平台登录凭证 userToken";
        result.costError = L"请配置平台登录凭证以读取当日账单";
        return result;
    }
    if (!validDeepSeekCredential(platformToken)) {
        result.balanceError = result.costError = L"请输入平台 userToken，不接受 API Key";
        return result;
    }
    try {
        parseDeepSeekBalance(result, transport(L"platform.deepseek.com", L"/api/v0/users/get_user_summary",
                                              platformToken, stop));
        result.balanceAt = now;
        result.balanceError.clear();
    } catch (const std::exception &e) { result.balanceError = failure(e); }
    auto query = L"?start=" + std::to_wstring(result.day) + L"&end=" + std::to_wstring(result.day + 86400) + L"&tz=28800";
    if (stop) return result;
    try {
        parseDeepSeekCosts(result, transport(L"platform.deepseek.com", L"/api/v0/usage/by_api_key/cost" + query,
                                            platformToken, stop));
        result.costAt = now;
        result.costError.clear();
    } catch (const std::exception &e) { result.costError = failure(e); }
    if (stop) return result;
    try {
        parseDeepSeekTokens(result, transport(L"platform.deepseek.com", L"/api/v0/usage/by_api_key/amount" + query,
                                             platformToken, stop));
        result.tokensAt = now;
    } catch (const std::exception &e) { result.tokensError = failure(e); }
    return result;
}
static const wchar_t *credentialTarget() {
    return L"CodexQuotaBar.DeepSeek.PlatformToken";
}
bool validDeepSeekCredential(const std::string &value) {
    return value.rfind("sk-", 0) != 0 && value.rfind("SK-", 0) != 0 &&
           value.size() <= CRED_MAX_CREDENTIAL_BLOB_SIZE &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return c > 32 && c < 127 && c != '"' && c != '{' && c != '}' && c != '[' && c != ']';
           });
}
std::string normalizeDeepSeekCredential(const std::string &text) {
    auto trim = [](std::string value) {
        auto first = value.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? std::string() : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    };
    auto value = trim(text);
    if (!value.empty() && (value.front() == '"' || value.front() == '{')) {
        auto parsed = json::parse(value, nullptr, false);
        if (parsed.is_string()) value = parsed.get<std::string>();
        else if (parsed.is_object() && parsed.contains("value") && parsed["value"].is_string())
            value = parsed["value"].get<std::string>();
    }
    value = trim(value);
    if (value.size() >= 7) {
        auto prefix = value.substr(0, 7);
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (prefix == "bearer ") value = trim(value.substr(7));
    }
    return value;
}
std::string deepSeekCredential() {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(credentialTarget(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    std::string result(reinterpret_cast<char *>(credential->CredentialBlob), credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return result;
}
void saveDeepSeekCredential(const std::string &value) {
    if (!validDeepSeekCredential(value)) throw std::runtime_error("Invalid DeepSeek credential");
    if (value.empty()) {
        if (!CredDeleteW(credentialTarget(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND)
            throw std::runtime_error("Cannot delete DeepSeek credential");
        return;
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(credentialTarget());
    credential.UserName = const_cast<LPWSTR>(L"DeepSeek");
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.data()));
    credential.CredentialBlobSize = DWORD(value.size());
    if (!CredWriteW(&credential, 0)) throw std::runtime_error("Cannot save DeepSeek credential");
}
} // namespace quota
