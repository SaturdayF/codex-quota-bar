#pragma once
#include "core.hpp"
#include <array>
#include <map>
namespace quota {
constexpr int64_t MoneyScale = 100000000;
struct DeepSeekBalance {
    std::string currency;
    int64_t total = 0;
};
struct DeepSeekModel {
    std::string name;
    std::map<std::string, int64_t> costs;
    int64_t hit = 0, miss = 0, output = 0, requests = 0;
};
struct DeepSeekHourUsage {
    int64_t hit = 0, miss = 0, output = 0, requests = 0;
};
// Per-API-key aggregate (tracking_id) so the detail card can filter spending.
struct DeepSeekKeyStats {
    std::map<std::string, int64_t> costs;
    std::vector<DeepSeekModel> models;
    std::map<std::string, std::array<int64_t, 24>> hourly;
    std::array<DeepSeekHourUsage, 24> hourlyTokens{};
};
struct DeepSeekState {
    std::vector<DeepSeekBalance> balances;
    std::vector<DeepSeekModel> models;
    std::map<std::string, int64_t> costs;
    std::map<std::string, std::array<int64_t, 24>> hourly;  // Beijing hour-of-day spend per currency
    std::array<DeepSeekHourUsage, 24> hourlyTokens{};        // Beijing hour-of-day token usage
    std::map<std::string, DeepSeekKeyStats> byKey;           // tracking_id -> stats
    int64_t day = 0, balanceAt = 0, costAt = 0, tokensAt = 0;
    std::wstring balanceError = L"等待读取", costError = L"等待读取", tokensError;
    bool available = false;
    std::string currency() const;
    std::string billingCurrency() const;
    std::optional<int64_t> todayCost(const std::string &currency, int64_t now = utcNow()) const;
};
struct DeepSeekUpdate {
    int generation;
    DeepSeekState state;
    int session = 0;
};
int64_t beijingDay(int64_t now);
std::wstring beijingDate(int64_t now);
int64_t decimalMoney(const json &value);
std::wstring moneyLabel(std::optional<int64_t> amount, const std::string &currency, int digits = 2);
void parseDeepSeekBalance(DeepSeekState &state, const json &value);
void parseDeepSeekCosts(DeepSeekState &state, const json &value);
void parseDeepSeekTokens(DeepSeekState &state, const json &value);
using DeepSeekTransport = std::function<json(const std::wstring &, const std::wstring &, const std::string &,
                                           std::atomic<bool> &)>;
DeepSeekState readDeepSeek(const std::string &platformToken,
                          std::atomic<bool> &stop, DeepSeekTransport transport = {}, int64_t now = utcNow());
std::string deepSeekCredential();
void saveDeepSeekCredential(const std::string &value);
bool validDeepSeekCredential(const std::string &value);
std::string normalizeDeepSeekCredential(const std::string &text);
std::wstring validateDeepSeekCredential(const std::string &token, std::atomic<bool> &stop,
                                      DeepSeekTransport transport = {});
int deepSeekTests(std::ostream &log);
int deepSeekUiTests(const fs::path &directory);
} // namespace quota
