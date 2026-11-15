#pragma once
#include "analytics.hpp"
#include <cstdint>
namespace quota {
struct HubSettings {
    int mode = 0, port = 17321;
    std::string url, deviceId;
    static HubSettings load();
    void save() const;
};
void validateHubSettings(const HubSettings &settings,const std::string &secret,bool fixture=false);
std::string hubSecret();
void saveHubSecret(const std::string &secret);
std::string newHubSecret();
int hubAddressRank(const std::wstring &name);
bool usableHubAddress(uint32_t ip);
std::vector<std::wstring> hubAddresses(int port);
UsageReport hubRetainUsage(const UsageReport &report,const fs::path &archive);
json hubLocalSnapshot(const UsageReport &report, const State &state, const std::string &deviceId);
struct HubResponse { int status = 200; json body; };
class HubStore {
    fs::path file;
    std::map<std::string,json> devices;
public:
    explicit HubStore(fs::path path = {});
    json stats() const;
    json history() const;
    json list() const;
    HubResponse request(const std::string &method,const std::string &path,const json &body);
};
class HubRuntime {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    HubRuntime(); ~HubRuntime();
    void start(const HubSettings &settings,const std::string &secret,const fs::path &directory,bool fixture=false);
    int port() const;
    void stop();
    void publish(const json &snapshot);
    json view() const;
    std::wstring status() const;
};
HubResponse hubHttp(const std::string &base,const std::string &secret,const std::string &method,const std::string &path,const json &body={});
int hubTests(std::ostream &log,const fs::path &directory);
}
