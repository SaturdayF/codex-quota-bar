#pragma once
#include "core.hpp"
#include <map>
#include <set>
namespace quota {
int64_t parseUsageTime(const std::string &text);
int64_t localDay(int64_t utc);
int64_t shiftLocalDay(int64_t day, int days);
std::wstring usageDate(int64_t utc, bool year = false);
std::wstring compactTokens(int64_t value);
std::wstring dollarAmount(double value, bool partial = false);

struct TokenUsage {
    int64_t input = 0, cached = 0, written = 0, output = 0, reasoning = 0;
    int64_t total() const { return input + output; }
    bool valid() const;
    bool operator==(const TokenUsage &other) const;
    TokenUsage delta(const TokenUsage &previous) const;
    static std::optional<TokenUsage> parse(const json &value);
};
struct UsageEntry {
    int64_t at = 0;
    TokenUsage tokens;
    std::string session, project, model, account, tier;
    double cost = 0;
    bool priced = false;
    int requests = 0;
};
struct UsageTotal {
    TokenUsage tokens;
    double cost = 0;
    int requests = 0, unpriced = 0;
    std::set<std::string> sessions;
    bool overflow = false;
    void add(const UsageEntry &entry);
};
struct UsageReport {
    std::vector<UsageEntry> entries;
    std::map<std::string, std::string> titles;
    int64_t updated = 0, oldest = 0;
    size_t files = 0, pending = 0, skipped = 0;
    bool partial = false, available = false;
    std::wstring error;
    UsageTotal total(int64_t begin = 0, int64_t end = INT64_MAX, const std::string &project = {}) const;
};
struct UsageScanOptions {
    fs::path home, cache;
    size_t byteBudget = 64 * 1024 * 1024, fileByteBudget = 8 * 1024 * 1024, fileLimit = 16000;
    DWORD timeBudgetMs = 8000;
    int64_t now = 0;
};
// Local only. Reads bounded JSONL records; never reads auth.json or launches a CLI.
UsageReport scanCodexUsage(const UsageScanOptions &options, std::atomic<bool> &cancel);
std::optional<double> priceUsage(const std::string &model, const TokenUsage &tokens, const std::string &tier = {});
constexpr const char *UsagePricingDate = "2026-11-14";
constexpr const char *UsagePricingSource = "https://developers.openai.com/api/docs/pricing.md";
UsageReport demoUsageReport(int64_t now);

struct QuotaSample {
    std::string account, plan, id, label;
    int64_t at = 0, reset = 0;
    int minutes = 0;
    double remaining = 0;
};
class QuotaHistory {
public:
    std::vector<QuotaSample> samples;
    bool dirty = false;
    bool observe(const State &state);
    std::vector<QuotaSample> series(const Quotas &current, const std::string &id) const;
    json toJson() const;
    void load(const fs::path &file);
};
struct AnalyticsBackup { UsageReport usage; QuotaHistory history; };
void writeAnalyticsCsv(const fs::path &file, const AnalyticsBackup &backup);
AnalyticsBackup readAnalyticsCsv(const fs::path &file);
void mergeUsageReport(UsageReport &current, const UsageReport &restored);
void mergeQuotaHistory(QuotaHistory &current, const QuotaHistory &restored);
int analyticsCsvTests(std::ostream &log, const fs::path &fixture);
void saveAnalyticsJson(const fs::path &file, const json &value);
std::vector<NamedQuota> allQuotaWindows(const Quotas &q, bool hour = true, bool week = true);
int analyticsTests(std::ostream &log, const fs::path &fixture);
} // namespace quota
