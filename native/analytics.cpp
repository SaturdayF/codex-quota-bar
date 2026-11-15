#include "analytics.hpp"
#include <array>
#include <unordered_map>
namespace quota {
namespace {
constexpr int64_t MaxTokens = INT64_MAX / 16;
constexpr size_t MaxLine = 1024 * 1024, MaxCache = 48 * 1024 * 1024, MaxEntries = 140000;
std::string text(const json &j, const char *key, size_t limit = 1024) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return {};
    auto value = it->get<std::string>();
    if (value.size() > limit) return {};
    for (char &c : value) if (static_cast<unsigned char>(c) < 32) c = ' ';
    return value;
}
bool addCount(int64_t &target, int64_t value) {
    if (value < 0 || target > MaxTokens - value) return false;
    target += value; return true;
}
json tokenJson(const TokenUsage &t) {
    return {t.input, t.cached, t.written, t.output, t.reasoning};
}
TokenUsage cachedTokens(const json &a) {
    if (!a.is_array() || a.size() != 5) throw std::runtime_error("Invalid token cache");
    for (const auto &v : a) if (!v.is_number_integer() || v.get<double>() < 0 || v.get<double>() > MaxTokens)
        throw std::runtime_error("Invalid token cache");
    TokenUsage t{a[0].get<int64_t>(), a[1].get<int64_t>(), a[2].get<int64_t>(), a[3].get<int64_t>(), a[4].get<int64_t>()};
    if (!t.valid()) throw std::runtime_error("Invalid token cache");
    return t;
}
json readJson(const fs::path &file, size_t limit) {
    std::error_code ec;
    auto size = fs::file_size(file, ec);
    if (ec || size > limit) return {};
    std::ifstream in(file, std::ios::binary);
    return json::parse(in, nullptr, false);
}
std::string pathKey(const fs::path &p) {
    auto value = p.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    return utf8(value);
}
std::string fileAnchor(const fs::path &file, uint64_t offset) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    size_t head = size_t(std::min<uint64_t>(offset, 256));
    std::string value(head, '\0'); in.read(value.data(), std::streamsize(head));
    if (size_t(in.gcount()) != head) return {};
    if (offset > head) {
        size_t tail = size_t(std::min<uint64_t>(offset - head, 128));
        in.seekg(std::streamoff(offset - tail));
        std::string end(tail, '\0'); in.read(end.data(), std::streamsize(tail));
        if (size_t(in.gcount()) != tail) return {};
        value += end;
    }
    return privateKey(value + ":" + std::to_string(offset));
}
struct FileScan {
    std::string path, anchor, session, project, account, model, tier;
    uint64_t offset = 0, size = 0, ordinal = 0, gate = 0;
    int64_t stamp = 0, attempt = 0;
    TokenUsage previous;
    bool havePrevious = false, partial = false, unsupportedFork = false, skipping = false;
    std::vector<UsageEntry> rows;
};
json fileJson(const FileScan &f) {
    json rows = json::array();
    for (const auto &r : f.rows) rows.push_back({r.at, tokenJson(r.tokens), r.model, r.tier, r.requests});
    return {{"path", f.path}, {"anchor", f.anchor}, {"session", f.session}, {"project", f.project},
            {"account", f.account}, {"model", f.model}, {"tier", f.tier}, {"offset", f.offset},
            {"size", f.size}, {"stamp", f.stamp}, {"attempt", f.attempt}, {"ordinal", f.ordinal},
            {"gate", f.gate}, {"previous", tokenJson(f.previous)}, {"havePrevious", f.havePrevious},
            {"partial", f.partial}, {"fork", f.unsupportedFork}, {"skipping", f.skipping}, {"rows", rows}};
}
FileScan cachedFile(const json &j) {
    FileScan f;
    f.path = text(j, "path", 32768); f.anchor = text(j, "anchor"); f.session = text(j, "session");
    f.project = text(j, "project", 32768); f.account = text(j, "account"); f.model = text(j, "model"); f.tier = text(j, "tier");
    f.offset = j.at("offset").get<uint64_t>(); f.size = j.at("size").get<uint64_t>(); f.stamp = j.at("stamp").get<int64_t>();
    f.attempt = j.at("attempt").get<int64_t>(); f.ordinal = j.at("ordinal").get<uint64_t>(); f.gate = j.at("gate").get<uint64_t>();
    f.previous = cachedTokens(j.at("previous")); f.havePrevious = j.at("havePrevious").get<bool>();
    f.partial = j.at("partial").get<bool>(); f.unsupportedFork = j.at("fork").get<bool>(); f.skipping = j.at("skipping").get<bool>();
    if (f.path.empty() || f.offset > f.size || !j.at("rows").is_array() || j["rows"].size() > 40000) throw std::runtime_error("Invalid usage cache");
    for (const auto &a : j["rows"]) {
        if (!a.is_array() || a.size() != 5 || !a[2].is_string() || !a[3].is_string()) throw std::runtime_error("Invalid usage cache");
        UsageEntry r; r.at = a[0].get<int64_t>(); r.tokens = cachedTokens(a[1]); r.model = a[2].get<std::string>(); r.tier = a[3].get<std::string>(); r.requests = a[4].get<int>();
        if (r.at <= 0 || r.at > 253402300799LL || r.model.size() > 160 || r.tier.size() > 40 || r.requests < 0) throw std::runtime_error("Invalid usage cache");
        f.rows.push_back(std::move(r));
    }
    return f;
}
void processUsageLine(FileScan &f, const std::string &line, int64_t oldest, int64_t now) {
    uint64_t lineIndex = f.ordinal++;
    if (line.find("token_count") == std::string::npos && line.find("turn_context") == std::string::npos &&
        line.find("session_meta") == std::string::npos && line.find("thread_settings_applied") == std::string::npos) return;
    auto j = json::parse(line, nullptr, false);
    if (!j.is_object()) { f.partial = true; return; }
    auto it = j.find("payload");
    if (it == j.end() || !it->is_object()) return;
    const auto &p = *it; auto type = text(j, "type");
    // Codex's ownership boundary refers to the record's ordinal, not its physical JSONL line number.
    bool hasOrdinal = j.contains("ordinal") && (j["ordinal"].is_number_unsigned() ||
        (j["ordinal"].is_number_integer() && j["ordinal"].get<int64_t>() >= 0));
    uint64_t ordinal = hasOrdinal ? j["ordinal"].get<uint64_t>() : 0;
    bool inherited = f.gate && (!hasOrdinal || ordinal < f.gate);
    if (type == "session_meta") {
        if (lineIndex != 0 && !f.session.empty()) return; // Inherited metadata must not change the child's identity.
        auto session = text(p, "id", 160); if (session.empty()) session = text(p, "session_id", 160);
        if (!session.empty()) f.session = session;
        f.project = text(p, "cwd", 32768);
        if (p.contains("account_scoped") && p["account_scoped"].is_boolean() && p["account_scoped"].get<bool>()) { auto account = text(p, "creator_account_id", 160); if (!account.empty()) f.account = privateKey("account:" + account); }
        if (p.contains("subagent_history_start_ordinal") && p["subagent_history_start_ordinal"].is_number_unsigned()) f.gate = p["subagent_history_start_ordinal"].get<uint64_t>();
        else if (p.contains("subagent_history_start_ordinal") && p["subagent_history_start_ordinal"].is_number_integer() && p["subagent_history_start_ordinal"].get<int64_t>() >= 0) f.gate = uint64_t(p["subagent_history_start_ordinal"].get<int64_t>());
        else {
            auto source=p.find("source");bool subagent=source!=p.end()&&((source->is_object()&&source->contains("subagent"))||(source->is_string()&&source->get<std::string>()=="subagent"));
            if(subagent||!text(p,"forked_from_id").empty()||!text(p,"forkedFromId").empty()||!text(p,"parent_session_id").empty()||p.contains("forked_from")||p.contains("history_base")) { f.unsupportedFork=true;f.partial=true; }
        }
        return;
    }
    if (type == "turn_context") {
        auto model = text(p, "model", 160); if (!model.empty()) f.model = model;
        auto tier = p.find("service_tier"); if (tier != p.end()) f.tier = tier->is_string() ? text(p, "service_tier", 40) : "";
        // Do not inherit a parent's Fast tier when its request metadata predates the child's own history.
        if (inherited) f.tier.clear();
        return;
    }
    if (type != "event_msg") return;
    auto event = text(p, "type");
    if (event == "thread_settings_applied") {
        if (p.contains("thread_settings") && p["thread_settings"].is_object() && p["thread_settings"].contains("service_tier"))
            f.tier = text(p["thread_settings"], "service_tier", 40);
        return;
    }
    if (event != "token_count" || !p.contains("info") || !p["info"].is_object()) return;
    const auto &info = p["info"];
    auto total = info.contains("total_token_usage") ? TokenUsage::parse(info["total_token_usage"]) : std::nullopt;
    auto last = info.contains("last_token_usage") ? TokenUsage::parse(info["last_token_usage"]) : std::nullopt;
    if (!total) { f.partial = true; return; }
    auto delta = f.havePrevious ? total->delta(f.previous) : *total;
    if ((!f.havePrevious && f.gate && !inherited) || !delta.valid()) {
        f.partial=true;
        // A missing inherited baseline or reset cannot justify charging the entire cumulative counter.
        delta=last.value_or(TokenUsage{});
    }
    bool duplicate = f.havePrevious && *total == f.previous;
    f.previous = *total; f.havePrevious = true;
    if (f.gate && !hasOrdinal) f.partial = true;
    if (inherited || f.unsupportedFork || duplicate) return;
    if (!delta.valid()) { f.partial = true; return; }
    if (!delta.total()) return;
    int64_t at = parseUsageTime(text(j, "timestamp", 80));
    if (!at || at > now + 300) { f.partial = true; return; }
    if (at < oldest) return;
    // A cumulative jump covering unknown requests keeps its tokens but has no reliable per-request price.
    bool exactRequest = last && *last == delta;
    std::string tier = exactRequest ? f.tier : "unresolved";
    UsageEntry r; r.at = at; r.tokens = delta; r.model = f.model; r.tier = tier; r.requests = exactRequest ? 1 : 0;
    if (f.rows.size() >= 40000) { f.partial = true; return; }
    // Retain per-request token rows: long-context rates must not be applied to an hourly sum.
    auto cost = priceUsage(r.model, r.tokens, r.tier); r.priced = bool(cost); r.cost = cost.value_or(0);
    f.rows.push_back(std::move(r));
}
void scanFile(FileScan &f, const fs::path &path, const UsageScanOptions &options, size_t &remaining,
              ULONGLONG deadline, int64_t oldest, int64_t now, std::atomic<bool> &cancel) {
    std::ifstream input(path, std::ios::binary);
    if (!input) { f.partial = true; return; }
    input.seekg(std::streamoff(f.offset));
    std::array<char, 65536> buffer{}; std::string line; bool over = f.skipping;
    uint64_t position = f.offset; size_t spent = 0;
    while (remaining && spent < options.fileByteBudget && !cancel && GetTickCount64() < deadline) {
        size_t count = std::min({buffer.size(), remaining, options.fileByteBudget - spent});
        input.read(buffer.data(), std::streamsize(count)); size_t n = size_t(input.gcount()); if (!n) break;
        remaining -= n; spent += n;
        for (size_t i = 0; i < n; ++i) {
            char c = buffer[i]; ++position;
            if (c == '\n') {
                if (!over) { if (!line.empty() && line.back() == '\r') line.pop_back(); processUsageLine(f, line, oldest, now); }
                else { ++f.ordinal; f.partial = true; }
                line.clear(); over = false; f.offset = position; f.skipping = false;
            } else if (!over) {
                if (line.size() >= MaxLine) { line.clear(); over = true; f.partial = true; }
                else line += c;
            }
        }
        if (over) { f.offset = position; f.skipping = true; }
    }
    f.anchor = fileAnchor(path, f.offset);
}
} // namespace

bool TokenUsage::valid() const {
    return input >= 0 && cached >= 0 && written >= 0 && output >= 0 && reasoning >= 0 &&
           input <= MaxTokens && output <= MaxTokens && cached <= input && written <= input - cached && reasoning <= output;
}
bool TokenUsage::operator==(const TokenUsage &o) const { return input == o.input && cached == o.cached && written == o.written && output == o.output && reasoning == o.reasoning; }
TokenUsage TokenUsage::delta(const TokenUsage &p) const { return {input-p.input,cached-p.cached,written-p.written,output-p.output,reasoning-p.reasoning}; }
std::optional<TokenUsage> TokenUsage::parse(const json &j) {
    if (!j.is_object()) return {};
    TokenUsage t;
    auto read = [&](const char *key, int64_t &v, bool required = false) {
        auto it = j.find(key); if (it == j.end()) return !required;
        if (!it->is_number_integer() || it->get<double>() < 0 || it->get<double>() > MaxTokens) return false;
        v = it->get<int64_t>(); return true;
    };
    if (!read("input_tokens",t.input,true) || !read("cached_input_tokens",t.cached) || !read("cache_write_input_tokens",t.written) ||
        !read("output_tokens",t.output,true) || !read("reasoning_output_tokens",t.reasoning) || !t.valid()) return {};
    return t;
}
void UsageTotal::add(const UsageEntry &e) {
    auto old = tokens;
    if (!addCount(tokens.input,e.tokens.input) || !addCount(tokens.cached,e.tokens.cached) || !addCount(tokens.written,e.tokens.written) ||
        !addCount(tokens.output,e.tokens.output) || !addCount(tokens.reasoning,e.tokens.reasoning)) { tokens = old; overflow = true; return; }
    if (e.priced) cost += e.cost; else ++unpriced;
    requests += e.requests; sessions.insert(e.session);
}
UsageTotal UsageReport::total(int64_t begin, int64_t end, const std::string &project) const {
    UsageTotal t; for (const auto &r : entries) if (r.at >= begin && r.at < end && (project.empty() || r.project == project)) t.add(r); return t;
}
int64_t parseUsageTime(const std::string &s) {
    if (s.size() < 20 || s[4]!='-' || s[7]!='-' || (s[10]!='T' && s[10]!=' ') || s[13]!=':' || s[16]!=':') return 0;
    auto number = [&](size_t p,size_t n) { int v=0; for(size_t i=p;i<p+n;++i){if(s[i]<'0'||s[i]>'9')return -1;v=v*10+s[i]-'0';}return v; };
    int y=number(0,4),m=number(5,2),d=number(8,2),hh=number(11,2),mm=number(14,2),ss=number(17,2);
    if(y<1970||m<1||m>12||d<1||d>31||hh<0||hh>23||mm<0||mm>59||ss<0||ss>59)return 0;
    tm t{}; t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=hh;t.tm_min=mm;t.tm_sec=ss;
    auto stamp=_mkgmtime64(&t); if(t.tm_year!=y-1900||t.tm_mon!=m-1||t.tm_mday!=d)return 0;
    size_t p=19;if(p<s.size()&&s[p]=='.'){++p;size_t start=p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;if(p==start)return 0;}
    if(p>=s.size())return 0;
    if(s[p]=='Z'||s[p]=='z')return p+1==s.size()?stamp:0;
    if((s[p]!='+'&&s[p]!='-')||s.size()!=p+6||s[p+3]!=':')return 0;
    int oh=number(p+1,2),om=number(p+4,2);if(oh<0||oh>23||om<0||om>59)return 0;
    return stamp+(s[p]=='+'?-1:1)*(oh*3600+om*60);
}
int64_t localDay(int64_t utc) { time_t at=utc;tm t{};if(localtime_s(&t,&at))return 0;t.tm_hour=t.tm_min=t.tm_sec=0;t.tm_isdst=-1;return _mktime64(&t); }
int64_t shiftLocalDay(int64_t day,int days) { time_t at=day;tm t{};if(localtime_s(&t,&at))return 0;t.tm_mday+=days;t.tm_isdst=-1;return _mktime64(&t); }
std::wstring usageDate(int64_t utc,bool year) { time_t at=utc;tm t{};if(localtime_s(&t,&at))return L"未知";wchar_t b[48]{};wcsftime(b,48,year?L"%Y/%m/%d":L"%m/%d",&t);return b; }
std::wstring compactTokens(int64_t n) { double divisor=n>=1000000000?1e9:n>=1000000?1e6:n>=1000?1e3:1;std::wostringstream s;s<<std::fixed<<std::setprecision(divisor==1?0:n/divisor>=100?0:2)<<n/divisor;if(divisor>1)s<<(divisor==1e9?L"B":divisor==1e6?L"M":L"K");return s.str(); }
std::wstring dollarAmount(double n,bool partial) { std::wostringstream s;if(partial)s<<L"≥ ";s<<L"$"<<std::fixed<<std::setprecision(2)<<n;return s.str(); }

std::optional<double> priceUsage(const std::string &name,const TokenUsage &t,const std::string &tier) {
    if(!t.valid())return {};
    struct Rate { const char *name;double input,cache,write,output;bool longContext; };
    // Current standard API equivalents, source/date above. No historical billing claim.
    static constexpr Rate rates[]={
        {"gpt-6-astra",10,1,12.5,50,true},{"gpt-6.1-sol",2,.1,2.5,10,true},{"gpt-6.1-terra",1,.05,1.25,5,true},
        {"gpt-6-luna",.1,.01,.125,.5,true},{"gpt-6-sol",2,.2,2.5,10,true},
        {"gpt-5.4",2.5,.25,0,15,true},{"gpt-5.4-mini",.75,.075,0,4.5,false},{"gpt-5.4-nano",.2,.02,0,1.25,false},
        {"gpt-5.3-codex",1.75,.175,0,14,false},{"gpt-5.2-codex",1.75,.175,0,14,false},
        {"gpt-5.2",1.75,.175,0,14,false},{"gpt-5.1-codex",1.25,.125,0,10,false},{"gpt-5.1",1.25,.125,0,10,false},
        {"gpt-5",1.25,.125,0,10,false},{"gpt-5-mini",.25,.025,0,2,false},{"gpt-5-nano",.05,.005,0,.4,false}};
    std::string model=name;
    if(model.size()>11&&model[model.size()-11]=='-'&&model[model.size()-6]=='-'&&model[model.size()-3]=='-'){
        auto suffix=model.substr(model.size()-10);bool dated=true;for(size_t i=0;i<suffix.size();++i)if(i!=4&&i!=7&&(suffix[i]<'0'||suffix[i]>'9'))dated=false;
        if(dated)model.resize(model.size()-11);
    }
    for(const auto &r:rates)if(model==r.name){
        double multiplier=1;
        if(tier=="priority"||tier=="fast") {
            if(model=="gpt-5-mini")multiplier=1.8;
            else if(model=="gpt-5-nano"||model=="gpt-5.4-nano"||model=="gpt-5.2-codex"||model=="gpt-5.1-codex")return {};
            else multiplier=2;
        }
        else if(tier=="flex"||tier=="batch")multiplier=.5;
        else if(tier=="ultrafast"&&model=="gpt-6.1-sol")multiplier=6;
        else if(!tier.empty()&&tier!="default"&&tier!="standard"&&tier!="auto")return {};
        if(t.written&&r.write==0)return {};
        bool longRequest=r.longContext&&t.input>272000;
        double inputRate=longRequest?r.input*2:r.input,cacheRate=longRequest?r.cache*2:r.cache,writeRate=longRequest?r.write*2:r.write;
        return ((t.input-t.cached-t.written)*inputRate+t.cached*cacheRate+t.written*writeRate+t.output*r.output*(longRequest?1.5:1))*multiplier/1e6;
    }
    return {};
}

void saveAnalyticsJson(const fs::path &file,const json &value) {
    fs::create_directories(file.parent_path());
    fs::path temp=file;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    auto body=value.dump();
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot save local analytics");
    DWORD written=0;bool ok=body.size()<=MAXDWORD&&WriteFile(h,body.data(),DWORD(body.size()),&written,nullptr)&&written==body.size()&&FlushFileBuffers(h);
    CloseHandle(h);
    if(ok)ok=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("Cannot commit local analytics");}
}
UsageReport scanCodexUsage(const UsageScanOptions &given,std::atomic<bool> &cancel) {
    UsageScanOptions o=given;if(o.home.empty())o.home=codexHome();if(!o.now)o.now=utcNow();
    UsageReport report;report.updated=o.now;report.oldest=shiftLocalDay(localDay(o.now),-364);
    const auto deadline=GetTickCount64()+o.timeBudgetMs;
    std::map<std::string,FileScan> cache;auto saved=readJson(o.cache,MaxCache);
    if(saved.is_object()&&saved.value("schema",0)==2&&saved.value("home",std::string())==privateKey(pathKey(o.home))&&saved.contains("files")&&saved["files"].is_array()){
        for(const auto &v:saved["files"]){try{auto f=cachedFile(v);if(cache.size()<16000)cache.emplace(f.path,std::move(f));}catch(...){}}
    }
    struct Source { fs::path path;std::string key;uint64_t size;int64_t stamp; };std::vector<Source> files;std::set<std::string> seen,compressed;
    std::error_code ec;
    for(const wchar_t *name:{L"sessions",L"archived_sessions"}){
        auto root=o.home/name;if(!fs::is_directory(root,ec)){if(ec)report.partial=true;ec.clear();continue;}report.available=true;
        fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
        while(it!=end&&!cancel&&GetTickCount64()<deadline&&files.size()<o.fileLimit){
            auto p=it->path();std::error_code itemError;
            if(it->is_symlink(itemError)&&it->is_directory(itemError))it.disable_recursion_pending();
            if(it->is_regular_file(itemError)&&p.extension()==L".jsonl"){
                auto canonical=fs::weakly_canonical(p,itemError);auto key=pathKey(itemError?p:canonical);
                auto size=it->file_size(itemError);auto stamp=it->last_write_time(itemError).time_since_epoch().count();
                if(!itemError&&seen.insert(key).second)files.push_back({p,key,size,stamp});
                else if(itemError)++report.skipped;
            }else if(p.extension()==L".zst") {
                ++report.skipped;
                auto plain=p.parent_path()/p.stem();
                if(plain.extension()==L".jsonl")compressed.insert(pathKey(fs::weakly_canonical(plain,itemError)));
            }
            it.increment(ec);if(ec){report.partial=true;ec.clear();}
        }
        if(it!=end)report.partial=true;
    }
    if(cancel)return report;
    bool discoveryIncomplete=report.partial;
    std::sort(files.begin(),files.end(),[&](const Source &a,const Source &b){
        auto ai=cache.find(a.key),bi=cache.find(b.key);int64_t aa=ai==cache.end()?0:ai->second.attempt,bb=bi==cache.end()?0:bi->second.attempt;
        return aa!=bb?aa<bb:a.stamp>b.stamp;
    });
    size_t remaining=o.byteBudget;std::map<std::string,FileScan> current;
    for(const auto &source:files){
        if(cancel)break;
        FileScan f;auto old=cache.find(source.key);if(old!=cache.end())f=std::move(old->second);
        bool reset=f.path.empty()||source.size<f.offset||(source.stamp!=f.stamp&&source.size<=f.size)||
                   (f.offset&&fileAnchor(source.path,f.offset)!=f.anchor);
        if(reset){f={};f.path=source.key;}
        f.size=source.size;f.stamp=source.stamp;
        if(f.offset<source.size&&remaining&&GetTickCount64()<deadline){
            f.attempt=o.now;scanFile(f,source.path,o,remaining,deadline,report.oldest,o.now,cancel);
        }
        f.rows.erase(std::remove_if(f.rows.begin(),f.rows.end(),[&](const UsageEntry &r){return r.at<report.oldest;}),f.rows.end());
        if(f.offset<source.size)++report.pending;
        if(f.partial)report.partial=true;
        if(f.session.empty())f.session=privateKey(source.key).substr(0,24);
        current.emplace(source.key,std::move(f));
    }
    // Incomplete discovery cannot establish that an old source was deleted.
    // Retain same-home cached contributions until a complete inventory confirms it.
    for(auto &pair:cache)if(!seen.count(pair.first)&&!pair.second.path.empty()&&(discoveryIncomplete||compressed.count(pair.first)))current.emplace(pair.first,std::move(pair.second));
    for(auto &pair:current){auto &rows=pair.second.rows;rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const UsageEntry &r){return r.at<report.oldest;}),rows.end());}
    if(cancel)return report;
    // Keep exactly one copy of a rollout moved/copied between session roots.
    std::map<std::string,const FileScan *> sessions;
    for(const auto &pair:current){const auto &f=pair.second;auto found=sessions.find(f.session);
        if(found==sessions.end())sessions[f.session]=&f;
        else {auto tokens=[](const FileScan &s){int64_t n=0;for(const auto &r:s.rows)if(!addCount(n,r.tokens.total()))return MaxTokens;return n;};
            if(tokens(f)!=tokens(*found->second))report.partial=true;
            if(tokens(f)>tokens(*found->second))found->second=&f;
        }
    }
    for(const auto &pair:sessions)for(auto r:pair.second->rows){
        if(report.entries.size()>=MaxEntries){report.partial=true;break;}
        r.session=pair.first;r.project=pair.second->project;r.account=pair.second->account;
        auto price=priceUsage(r.model,r.tokens,r.tier);r.priced=bool(price);r.cost=price.value_or(0);
        report.entries.push_back(std::move(r));
    }
    report.files=current.size();report.partial=report.partial||report.pending||report.skipped;
    if(!report.entries.empty())report.available=true;
    // Titles only, never the first prompt or tool transcript. Keep bounded reads.
    std::ifstream index(o.home/L"session_index.jsonl",std::ios::binary);size_t titleBytes=0;std::string line;
    while(index&&titleBytes<4*1024*1024&&!cancel){char buffer[8193]{};index.getline(buffer,8193);size_t n=size_t(index.gcount());titleBytes+=n;
        if(index.fail()&&!index.eof()){index.clear();index.ignore(std::streamsize(std::min<size_t>(1024*1024,4*1024*1024-titleBytes)),'\n');titleBytes+=size_t(index.gcount());continue;}
        auto j=json::parse(buffer,nullptr,false);if(!j.is_object())continue;
        auto id=text(j,"id",160),title=text(j,"thread_name",512);if(title.empty())title=text(j,"title",512);
        if(!id.empty()&&!title.empty())report.titles[id]=title;
    }
    if(!o.cache.empty()&&!cancel){json persisted=json::array();size_t entries=0;
        for(const auto &pair:current){entries+=pair.second.rows.size();if(entries>MaxEntries){report.partial=true;break;}persisted.push_back(fileJson(pair.second));}
        auto value=json{{"schema",2},{"home",privateKey(pathKey(o.home))},{"pricing",UsagePricingDate},{"files",persisted}};
        if(value.dump().size()<=MaxCache){try{saveAnalyticsJson(o.cache,value);}catch(...){report.error=L"统计可用，但本地缓存保存失败";}}
        else report.error=L"本地缓存达到上限，部分历史将在下次重新扫描";
    }
    if(!o.cache.empty()&&!cancel){auto archive=o.cache.parent_path()/L"codex-restored-data.csv";
        if(fs::exists(archive))try{mergeUsageReport(report,readAnalyticsCsv(archive).usage);}catch(...){report.error=L"恢复档案不可读，已保留本地统计。";}
    }
    if(!report.available)report.error=L"未找到 Codex 本地会话日志；运行本地任务后可在此查看。";
    return report;
}

std::vector<NamedQuota> allQuotaWindows(const Quotas &q,bool hour,bool week) {
    std::vector<NamedQuota> out;
    if(hour&&q.hour)out.push_back({"hour","5 小时","codex",{q.hour->remaining,q.hour->resets,300}});
    if(week&&q.week)out.push_back({"week","每周","codex",{q.week->remaining,q.week->resets,10080}});
    out.insert(out.end(),q.extraWindows.begin(),q.extraWindows.end());return out;
}
bool QuotaHistory::observe(const State &s) {
    if(s.q.accountKey.empty()||s.stale()||!s.updated||s.updated>utcNow()+300)return false;
    bool added=false;
    for(const auto &w:allQuotaWindows(s.q)){
        if(!w.window.resets||w.window.resets<=s.updated||w.window.minutes<=0)continue;
        auto previous=std::find_if(samples.rbegin(),samples.rend(),[&](const QuotaSample &p){return p.account==s.q.accountKey&&p.plan==s.q.plan&&p.id==w.id;});
        if(previous!=samples.rend()&&(s.updated<=previous->at||(s.updated-previous->at<300&&previous->remaining==w.window.remaining&&previous->reset==w.window.resets)))continue;
        samples.push_back({s.q.accountKey,s.q.plan,w.id,w.label,s.updated,w.window.resets,w.window.minutes,w.window.remaining});added=true;
    }
    auto oldest=utcNow()-366LL*86400;
    samples.erase(std::remove_if(samples.begin(),samples.end(),[&](const QuotaSample &s){return s.at<oldest;}),samples.end());
    if(samples.size()>24000)samples.erase(samples.begin(),samples.begin()+std::ptrdiff_t(samples.size()-24000));
    dirty=dirty||added;return added;
}
std::vector<QuotaSample> QuotaHistory::series(const Quotas &q,const std::string &id) const {
    std::vector<QuotaSample> out;
    for(const auto &w:allQuotaWindows(q))if(w.id==id){for(const auto &s:samples)
        if(s.account==q.accountKey&&s.plan==q.plan&&s.id==id&&s.reset==w.window.resets&&s.at>=s.reset-int64_t(s.minutes)*60&&s.at<=s.reset)out.push_back(s);break;}
    return out;
}
json QuotaHistory::toJson() const {
    json a=json::array();for(const auto &s:samples)a.push_back({s.account,s.plan,s.id,s.label,s.at,s.reset,s.minutes,s.remaining});return {{"schema",1},{"samples",a}};
}
void QuotaHistory::load(const fs::path &file) {
    auto j=readJson(file,8*1024*1024);if(!j.is_object()||j.value("schema",0)!=1||!j.contains("samples")||!j["samples"].is_array())return;
    samples.clear();for(const auto &a:j["samples"]){try{if(!a.is_array()||a.size()!=8)continue;QuotaSample s{a[0].get<std::string>(),a[1].get<std::string>(),a[2].get<std::string>(),a[3].get<std::string>(),a[4].get<int64_t>(),a[5].get<int64_t>(),a[6].get<int>(),a[7].get<double>()};
        if(s.account.size()!=64||s.id.size()>200||s.label.size()>200||s.plan.size()>100||s.at<utcNow()-366LL*86400||s.at>utcNow()+300||s.reset<=s.at||s.minutes<=0||s.minutes>5256000||!std::isfinite(s.remaining)||s.remaining<0||s.remaining>100)continue;
        if(samples.size()<24000)samples.push_back(std::move(s));}catch(...){}}
    std::stable_sort(samples.begin(),samples.end(),[](const auto &a,const auto &b){return a.at<b.at;});dirty=false;
}
UsageReport demoUsageReport(int64_t now) {
    UsageReport r;r.available=true;r.updated=now;r.oldest=shiftLocalDay(localDay(now),-29);r.files=32;
    for(int d=0;d<30;++d){if(d%9==0)continue;for(int s=0;s<4+d%4;++s){UsageEntry e;e.at=shiftLocalDay(localDay(now),d-29)+(9+s*2)*3600;if(e.at>now)continue;
        e.session="demo-"+std::to_string(d)+"-"+std::to_string(s);e.project=s%3==0?"E:/Projects/codex-quota-bar":s%3==1?"E:/Projects/文献整理工具":"E:/Projects/个人脚本";
        e.model=s%4==0?"gpt-5.4-mini":"gpt-6.1-sol";e.tokens={100000+d*1700+s*2100,60000+d*1000,0,10000+s*3000,4000+s*1200};e.requests=1;
        e.account=s%3==2?"":privateKey("demo-account");auto p=priceUsage(e.model,e.tokens);e.priced=bool(p);e.cost=p.value_or(0);r.entries.push_back(e);
        r.titles[e.session]=s%3==0?"扩展额度窗口显示":s%3==1?"绘制额度历史曲线":"整理核心测试用例";
    }}return r;
}
} // namespace quota
