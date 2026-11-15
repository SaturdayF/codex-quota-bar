#include "analytics.hpp"
#include <climits>
#include <locale>

namespace quota {
namespace {
constexpr size_t MaxBytes=128*1024*1024, MaxRows=320000;
const std::vector<std::string> Header={"schema","type","at","session","project","model","account","tier","input_tokens","cached_input_tokens","cache_write_tokens","output_tokens","reasoning_tokens","requests","cost_usd","priced","title","quota_id","quota_label","plan","reset_at","window_minutes","remaining_percent","updated_at","oldest_at","partial","available"};
using Row=std::vector<std::string>;
void require(bool ok){if(!ok)throw std::runtime_error("CSV 数据格式或数值无效，请使用本程序导出的完整备份文件。");}
bool dangerous(char c){return c=='='||c=='+'||c=='-'||c=='@'||c=='\t'||c=='\r'||c=='\n'||c=='\'';}
std::string cell(std::string s){if(!s.empty()&&dangerous(s[0]))s="'"+s;std::string out="\"";for(char c:s){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
std::string number(double v){require(std::isfinite(v));std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17)<<v;return s.str();}
int64_t integer(const std::string &s,int64_t maximum=253402300799LL){require(!s.empty()&&s.size()<24);int64_t n=0;for(char c:s){require(c>='0'&&c<='9'&&n<=(maximum-(c-'0'))/10);n=n*10+c-'0';}require(n<=maximum);return n;}
double decimal(const std::string &s,double maximum){require(!s.empty()&&s.size()<64);std::istringstream in(s);in.imbue(std::locale::classic());double n=-1;in>>n;require(in&&in.peek()==EOF&&std::isfinite(n)&&n>=0&&n<=maximum);return n;}
std::string identity(const UsageEntry &e){return json::array({e.session,e.at,e.model,e.tier,e.tokens.input,e.tokens.cached,e.tokens.written,e.tokens.output,e.tokens.reasoning,e.requests}).dump();}
std::string quotaIdentity(const QuotaSample &q){return json::array({q.account,q.plan,q.id,q.reset,q.at}).dump();}
void validate(const AnalyticsBackup &b){
    require(b.usage.entries.size()<=140000&&b.history.samples.size()<=24000&&b.usage.titles.size()<=140000);
    int64_t requests=0;double cost=0;
    for(const auto &e:b.usage.entries){require(e.at>0&&e.at<=253402300799LL&&e.tokens.valid()&&!e.session.empty()&&e.session.size()<=160&&e.project.size()<=32768&&e.model.size()<=160&&e.tier.size()<=40&&e.account.size()<=160&&e.requests>=0&&requests<=INT_MAX-e.requests&&std::isfinite(e.cost)&&e.cost>=0&&e.cost<=1e12&&(!e.priced?e.cost==0:true));requests+=e.requests;cost+=e.cost;}
    require(std::isfinite(cost)&&!b.usage.total().overflow);
    for(const auto &t:b.usage.titles)require(!t.first.empty()&&t.first.size()<=160&&t.second.size()<=4096);
    for(const auto &q:b.history.samples)require(q.account.size()==64&&q.id.size()<=200&&q.label.size()<=200&&q.plan.size()<=100&&q.at>0&&q.at<=253402300799LL&&q.reset>=q.at&&q.reset<=253402300799LL&&q.minutes>0&&q.minutes<=5256000&&std::isfinite(q.remaining)&&q.remaining>=0&&q.remaining<=100);
}
std::vector<Row> parse(const std::string &body){
    require(body.size()<=MaxBytes&&body.find('\0')==std::string::npos);
    size_t start=body.compare(0,3,"\xef\xbb\xbf")==0?3:0;
    require(body.size()==start||MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,body.data()+start,int(body.size()-start),nullptr,0)>0);
    std::vector<Row> rows;Row row;std::string field;bool quoted=false,closed=false;
    auto finishField=[&]{if(field.size()>1&&field[0]=='\''&&dangerous(field[1]))field.erase(0,1);row.push_back(std::move(field));field.clear();closed=false;require(row.size()<=Header.size());};
    for(size_t i=start;i<body.size();++i){char c=body[i];
        if(quoted){if(c=='\"'){if(i+1<body.size()&&body[i+1]=='\"'){field+='\"';++i;}else{quoted=false;closed=true;}}else field+=c;}
        else if(c=='\"'){require(field.empty()&&!closed);quoted=true;}
        else if(c==',')finishField();
        else if(c=='\r'||c=='\n'){if(c=='\r'&&i+1<body.size()&&body[i+1]=='\n')++i;finishField();rows.push_back(std::move(row));row.clear();require(rows.size()<=MaxRows);}
        else{require(!closed);field+=c;}
        require(field.size()<=65536);
    }
    require(!quoted);if(!row.empty()||!field.empty()||closed){finishField();rows.push_back(std::move(row));}
    return rows;
}
} // namespace

void writeAnalyticsCsv(const fs::path &file,const AnalyticsBackup &b){
    validate(b);std::string body="\xef\xbb\xbf";
    auto append=[&](const Row &r){for(size_t i=0;i<r.size();++i){if(i)body+=',';body+=cell(r[i]);}body+="\r\n";require(body.size()<=MaxBytes);};
    append(Header);Row r(Header.size());r[0]="codex-quota-bar-1";r[1]="meta";r[23]=std::to_string(b.usage.updated);r[24]=std::to_string(b.usage.oldest);r[25]=b.usage.partial?"1":"0";r[26]=b.usage.available?"1":"0";append(r);
    for(const auto &e:b.usage.entries){r.assign(Header.size(),{});r[0]="codex-quota-bar-1";r[1]="usage";r[2]=std::to_string(e.at);r[3]=e.session;r[4]=e.project;r[5]=e.model;r[6]=e.account;r[7]=e.tier;r[8]=std::to_string(e.tokens.input);r[9]=std::to_string(e.tokens.cached);r[10]=std::to_string(e.tokens.written);r[11]=std::to_string(e.tokens.output);r[12]=std::to_string(e.tokens.reasoning);r[13]=std::to_string(e.requests);r[14]=number(e.cost);r[15]=e.priced?"1":"0";append(r);}
    for(const auto &t:b.usage.titles){r.assign(Header.size(),{});r[0]="codex-quota-bar-1";r[1]="title";r[3]=t.first;r[16]=t.second;append(r);}
    for(const auto &q:b.history.samples){r.assign(Header.size(),{});r[0]="codex-quota-bar-1";r[1]="quota";r[2]=std::to_string(q.at);r[6]=q.account;r[17]=q.id;r[18]=q.label;r[19]=q.plan;r[20]=std::to_string(q.reset);r[21]=std::to_string(q.minutes);r[22]=number(q.remaining);append(r);}
    fs::path temp=file;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("无法写入 CSV 文件。");
    DWORD written=0;bool ok=WriteFile(h,body.data(),DWORD(body.size()),&written,nullptr)&&written==body.size()&&FlushFileBuffers(h);CloseHandle(h);
    if(ok)ok=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temp.c_str());throw std::runtime_error("无法保存 CSV 文件，原文件已保留。");}
}
AnalyticsBackup readAnalyticsCsv(const fs::path &file){
    std::error_code ec;auto size=fs::file_size(file,ec);require(!ec&&size<=MaxBytes);
    std::ifstream in(file,std::ios::binary);require(bool(in));in.seekg(0,std::ios::end);auto bytes=in.tellg();require(bytes>=0&&uint64_t(bytes)<=MaxBytes);in.seekg(0);std::string body(size_t(bytes),'\0');in.read(body.data(),std::streamsize(bytes));require(in.gcount()==std::streamsize(bytes));
    auto rows=parse(body);require(rows.size()>=2&&rows.front()==Header);AnalyticsBackup b;bool meta=false;
    for(size_t i=1;i<rows.size();++i){const auto &r=rows[i];require(r.size()==Header.size()&&r[0]=="codex-quota-bar-1");
        if(r[1]=="meta"){require(!meta);meta=true;b.usage.updated=integer(r[23]);b.usage.oldest=integer(r[24]);b.usage.partial=integer(r[25],1)!=0;b.usage.available=integer(r[26],1)!=0;}
        else if(r[1]=="usage"){UsageEntry e;e.at=integer(r[2]);e.session=r[3];e.project=r[4];e.model=r[5];e.account=r[6];e.tier=r[7];e.tokens={integer(r[8],INT64_MAX/16),integer(r[9],INT64_MAX/16),integer(r[10],INT64_MAX/16),integer(r[11],INT64_MAX/16),integer(r[12],INT64_MAX/16)};e.requests=int(integer(r[13],INT_MAX));e.cost=decimal(r[14],1e12);e.priced=integer(r[15],1)!=0;b.usage.entries.push_back(std::move(e));require(b.usage.entries.size()<=140000);}
        else if(r[1]=="title"){require(b.usage.titles.emplace(r[3],r[16]).second);}
        else if(r[1]=="quota"){b.history.samples.push_back({r[6],r[19],r[17],r[18],integer(r[2]),integer(r[20]),int(integer(r[21],5256000)),decimal(r[22],100)});require(b.history.samples.size()<=24000);}
        else require(false);
    }
    require(meta);validate(b);return b;
}
void mergeUsageReport(UsageReport &current,const UsageReport &restored){
    UsageReport result=current;
    std::map<std::string,std::vector<size_t>> existing;std::map<std::string,size_t> seen;
    for(size_t i=0;i<result.entries.size();++i)existing[identity(result.entries[i])].push_back(i);
    for(const auto &e:restored.entries){auto key=identity(e);size_t ordinal=seen[key]++;if(ordinal>=existing[key].size())result.entries.push_back(e);
        else{auto &old=result.entries[existing[key][ordinal]];if(!old.priced&&e.priced){old.cost=e.cost;old.priced=true;}}}
    require(result.entries.size()<=140000);
    for(const auto &t:restored.titles)result.titles.emplace(t);
    result.updated=std::max(result.updated,restored.updated);if(!result.oldest||(restored.oldest&&restored.oldest<result.oldest))result.oldest=restored.oldest;
    result.partial=result.partial||restored.partial;result.available=result.available||restored.available||!result.entries.empty();
    if(result.available&&result.error==L"未找到 Codex 本地会话日志；运行本地任务后可在此查看。")result.error.clear();
    std::stable_sort(result.entries.begin(),result.entries.end(),[](const auto &a,const auto &b){return a.at<b.at;});
    validate(AnalyticsBackup{result,{}});
    current=std::move(result);
}
void mergeQuotaHistory(QuotaHistory &current,const QuotaHistory &restored){
    QuotaHistory result=current;
    std::set<std::string> keys;for(const auto &q:result.samples)keys.insert(quotaIdentity(q));
    for(const auto &q:restored.samples)if(keys.insert(quotaIdentity(q)).second)result.samples.push_back(q);
    require(result.samples.size()<=24000);std::stable_sort(result.samples.begin(),result.samples.end(),[](const auto &a,const auto &b){return a.at<b.at;});result.dirty=true;current=std::move(result);
}

int analyticsCsvTests(std::ostream &log,const fs::path &fixture){
    int count=0;auto check=[&](bool ok,const char *name){log<<(ok?"PASS ":"FAIL ")<<name<<std::endl;if(!ok)throw std::runtime_error(name);++count;};
    AnalyticsBackup b;b.usage=demoUsageReport(utcNow());b.usage.titles["quoted"]=u8"=中文, \"标题\"\n下一行";auto at=utcNow();b.history.samples.push_back({std::string(64,'a'),"plus","hour",u8"5 小时",at,at+100,300,42.125});auto file=fixture/L"备份.csv";
    writeAnalyticsCsv(file,b);auto restored=readAnalyticsCsv(file);
    check(restored.usage.total().tokens==b.usage.total().tokens&&restored.usage.entries.size()==b.usage.entries.size(),"CSV round trip preserves all token subsets and request rows");
    check(restored.usage.titles==b.usage.titles&&restored.history.samples[0].remaining==42.125,"CSV restores Unicode quoted multiline formula-like titles and quota samples");
    check(std::abs(restored.usage.total().cost-b.usage.total().cost)<1e-10,"CSV round trip preserves recorded prices without repricing");
    auto merged=b.usage;mergeUsageReport(merged,restored.usage);mergeUsageReport(merged,restored.usage);check(merged.entries.size()==b.usage.entries.size(),"CSV repeated restore and live-log overlap do not double count");
    auto duplicate=b.usage;duplicate.entries.push_back(duplicate.entries.front());auto copy=duplicate;mergeUsageReport(copy,duplicate);check(copy.entries.size()==duplicate.entries.size(),"CSV merge preserves legitimate identical-timestamp record multiplicity");
    auto unpriced=b.usage;for(auto &row:unpriced.entries){row.priced=false;row.cost=0;}mergeUsageReport(unpriced,b.usage);check(std::abs(unpriced.total().cost-b.usage.total().cost)<1e-10,"CSV restored known prices survive overlap with unpriced local records");
    auto history=b.history;mergeQuotaHistory(history,restored.history);check(history.samples.size()==1,"CSV repeated quota restore merges by account window and sampling time");
    auto bad=fixture/L"bad.csv";{std::ofstream out(bad);out<<"invalid,header\n";}bool rejected=false;try{readAnalyticsCsv(bad);}catch(...){rejected=true;}check(rejected,"CSV rejects foreign or truncated schemas before restore");
    auto invalid=b;invalid.usage.entries[0].tokens.cached=INT64_MAX;rejected=false;try{writeAnalyticsCsv(file,invalid);}catch(...){rejected=true;}check(rejected&&readAnalyticsCsv(file).usage.entries.size()==b.usage.entries.size(),"CSV invalid values cannot replace an existing valid backup");
    auto recovery=fixture/L"restore";fs::create_directories(recovery);writeAnalyticsCsv(recovery/L"codex-restored-data.csv",b);UsageScanOptions options;options.home=recovery/L"missing-home";options.cache=recovery/L"cache.json";std::atomic<bool> cancel{false};auto afterRestart=scanCodexUsage(options,cancel);
    check(afterRestart.available&&afterRestart.total().tokens==b.usage.total().tokens,"CSV imported archive survives restart and rescanning without source logs");
    return count;
}
} // namespace quota
