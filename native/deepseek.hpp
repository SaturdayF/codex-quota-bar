#pragma once
#include "core.hpp"
#include <map>
namespace quota {
constexpr int64_t MoneyScale = 100000000;
struct DeepSeekBalance {
    std::string currency;
    int64_t total = 0, granted = 0, paid = 0;
};
struct DeepSeekModel {
    std::string name;
    std::map<std::string, int64_t> costs;
    int64_t hit = 0, miss = 0, output = 0, requests = 0;
};
struct DeepSeekState {
    std::vector<DeepSeekBalance> balances;
    std::vector<DeepSeekModel> models;
    std::map<std::string, int64_t> costs;
    int64_t day = 0, balanceAt = 0, costAt = 0, tokensAt = 0;
    std::wstring balanceError = L"等待读取", costError = L"等待读取", tokensError;
    bool available = false;
    std::string currency() const;
    std::optional<int64_t> todayCost(const std::string &currency, int64_t now = utcNow()) const;
};
struct DeepSeekUpdate {
    int generation;
    DeepSeekState state;
};
int64_t beijingDay(int64_t now);
std::wstring beijingDate(int64_t now);
int64_t decimalMoney(const json &value);
std::wstring moneyLabel(std::optional<int64_t> amount, const std::string &currency, int digits = 2);
void parseDeepSeekBalance(DeepSeekState &state, const json &value, bool platform);
void parseDeepSeekCosts(DeepSeekState &state, const json &value);
void parseDeepSeekTokens(DeepSeekState &state, const json &value);
using DeepSeekTransport = std::function<json(const std::wstring &, const std::wstring &, const std::string &,
                                           std::atomic<bool> &)>;
DeepSeekState readDeepSeek(const std::string &apiKey, const std::string &platformToken,
                          std::atomic<bool> &stop, DeepSeekTransport transport = {}, int64_t now = utcNow());
std::string deepSeekCredential(bool platform);
void saveDeepSeekCredential(bool platform, const std::string &value);
bool validDeepSeekCredential(const std::string &value);
int deepSeekTests(std::ostream &log);
int deepSeekUiTests(const fs::path &directory);
} // namespace quota
