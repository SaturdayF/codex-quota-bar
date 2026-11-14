#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#undef NOMINMAX
#include "hub.hpp"
#include <wincred.h>
#include <wincrypt.h>
#include <winhttp.h>
namespace quota {
namespace {
constexpr size_t BodyLimit=2*1024*1024;
constexpr double Safe=9007199254740991.;
json sanitize(const json &value,int depth=0){if(depth>9)throw std::runtime_error("Hub data nesting limit");if(value.is_object()){if(value.size()>12000)throw std::runtime_error("Hub map limit");json result=json::object();for(auto i=value.begin();i!=value.end();++i){auto k=i.key();if(k.size()>256||k=="__proto__"||k=="constructor"||k=="prototype"||k=="title"||k=="canonicalTitle"||k=="prompt"||k=="path"||k=="cwd"||k=="token"||k=="secret"||k=="cookie")continue;result[k]=sanitize(*i,depth+1);}return result;}if(value.is_array()){if(value.size()>12000)throw std::runtime_error("Hub array limit");json result=json::array();for(const auto &item:value)result.push_back(sanitize(item,depth+1));return result;}if(value.is_number()){double n=value.get<double>();if(!std::isfinite(n)||n<0||n>Safe)throw std::runtime_error("Invalid Hub number");}if(value.is_string()&&value.get_ref<const std::string &>().size()>512)throw std::runtime_error("Hub text limit");return value;}
std::string stamp(int64_t at=utcNow()){time_t t=at;tm u{};gmtime_s(&u,&t);char b[40];strftime(b,sizeof(b),"%Y-%m-%dT%H:%M:%SZ",&u);return b;}
json read(const fs::path &p,size_t limit){std::error_code ec;auto n=fs::file_size(p,ec);if(ec||n>limit)return {};std::ifstream in(p,std::ios::binary);return json::parse(in,nullptr,false);}
double number(const json &j,const char *k){auto i=j.find(k);if(i==j.end()||!i->is_number())return 0;double v=i->get<double>();if(!std::isfinite(v)||v<0||v>Safe)throw std::runtime_error("Invalid Hub counter");return v;}
json period(const json &j){json r={{"capabilities",{{"tokenComponents",false},{"throughput",false}}}};
 for(auto k:{"totalTokens","costUsd","cacheReadTokens","cacheWriteTokens","outputTokens","unclassifiedTokens","unpricedTokens","timedTokens","timedOutputTokens","timedDurationMs"})r[k]=number(j,k);
 for(auto k:{"clients","clientCosts","clientCacheReads","clientCacheWrites","clientOutputs","models","modelCosts","modelCacheReads","modelCacheWrites","modelOutputs","clientUnpricedTokens","modelUnpricedTokens","clientUnclassifiedTokens","modelUnclassifiedTokens"}){
  r[k]=json::object();auto i=j.find(k);if(i!=j.end()&&i->is_object()){if(i->size()>1024)throw std::runtime_error("Too many Hub groups");for(auto a=i->begin();a!=i->end();++a){if(a.key().size()>160||!a->is_number())continue;json one={{"v",*a}};r[k][a.key()]=number(one,"v");}}
 }
 for(auto k:{"sessions","projects","clientModels","clientModelCosts","clientModelUnpricedTokens","modelThroughput"})r[k]=j.contains(k)&&j[k].is_object()?sanitize(j[k]):json::object();
 bool components=j.value("capabilities",json::object()).value("tokenComponents",false);
 r["capabilities"]["tokenComponents"]=components;
 r["capabilities"]["throughput"]=j.value("capabilities",json::object()).value("throughput",false);
 double known=number(r,"cacheReadTokens")+number(r,"cacheWriteTokens")+number(r,"outputTokens");
 if(known>number(r,"totalTokens"))throw std::runtime_error("Hub components exceed total");
 if(known+number(r,"unclassifiedTokens")>number(r,"totalTokens"))throw std::runtime_error("Hub classified totals exceed usage");
 if(!components&&!j.contains("unclassifiedTokens"))r["unclassifiedTokens"]=number(r,"totalTokens")-known;
 r["timedOutputTokens"]=std::min(number(r,"timedOutputTokens"),number(r,"outputTokens"));
 return r;
}
void sum(json &a,const json &b){for(auto it=b.begin();it!=b.end();++it){if(it->is_number()){double n=a.value(it.key(),0.)+it->get<double>();if(!std::isfinite(n)||n>Safe)throw std::runtime_error("Hub aggregate overflow");a[it.key()]=n;}else if(it->is_object()&&it.key()!="capabilities"){if(!a.contains(it.key())||!a[it.key()].is_object())a[it.key()]=json::object();sum(a[it.key()],*it);}else if(!a.contains(it.key()))a[it.key()]=*it;}}
// Preserve earlier counters for clients intentionally not collected in this update.
json retainUntracked(json incoming,const json &old,const json &tracked){
 if(!tracked.is_array())return incoming;
 std::set<std::string> active;for(const auto &v:tracked)if(v.is_string())active.insert(v.get<std::string>());
 auto retained=period(json::object());auto clients=old.value("clients",json::object());
 for(auto c=clients.begin();c!=clients.end();++c)if(!active.count(c.key())){
  for(auto pair:std::vector<std::pair<std::string,std::string>>{{"clients","totalTokens"},{"clientCosts","costUsd"},{"clientCacheReads","cacheReadTokens"},{"clientCacheWrites","cacheWriteTokens"},{"clientOutputs","outputTokens"},{"clientUnclassifiedTokens","unclassifiedTokens"},{"clientUnpricedTokens","unpricedTokens"}}){auto map=old.value(pair.first,json::object());double value=number(map,c.key().c_str());retained[pair.first][c.key()]=value;retained[pair.second]=number(retained,pair.second.c_str())+value;}
  for(auto pair:std::vector<std::pair<std::string,std::string>>{{"clientModels","models"},{"clientModelCosts","modelCosts"},{"clientModelUnpricedTokens","modelUnpricedTokens"}}){auto map=old.value(pair.first,json::object());if(map.contains(c.key())&&map[c.key()].is_object()){retained[pair.first][c.key()]=map[c.key()];sum(retained[pair.second],map[c.key()]);}}
 }
 for(auto i=old["sessions"].begin();i!=old["sessions"].end();++i)if(i->is_object()&&!active.count(i->value("client",std::string())))retained["sessions"][i.key()]=*i;
 if(number(retained,"totalTokens")>0){sum(incoming,retained);incoming["capabilities"]["throughput"]=false;incoming["capabilities"]["tokenComponents"]=false;}
 return incoming;
}
json retainUntrackedHistory(json incoming,const json &previous,const json &tracked){
 if(!incoming.is_object()||!previous.is_object()||!tracked.is_array())return incoming;
 std::set<std::string> active;for(const auto &c:tracked)if(c.is_string())active.insert(c.get<std::string>());bool restored=false;
 for(auto tier:{"daily","monthly"}){std::string key=std::string(tier)=="daily"?"date":"month";std::map<std::string,json> rows;
  if(incoming.contains(tier)&&incoming[tier].is_array())for(const auto &row:incoming[tier])if(row.is_object())rows[row.value(key,std::string())]=row;
  if(previous.contains(tier)&&previous[tier].is_array())for(const auto &row:previous[tier]){if(!row.is_object())continue;auto clients=row.value("perClient",json::object());if(!clients.is_object())continue;
   for(auto c=clients.begin();c!=clients.end();++c)if(!active.count(c.key())&&c->is_object()){
    json retained=json::object();for(auto field:{"tokens","cost","messages","activeTimeMs","cacheReadTokens","cacheWriteTokens","outputTokens","unclassifiedTokens","unpricedTokens"})if(c->contains(field)&&(*c)[field].is_number())retained[field]=number(*c,field);
    if(retained.empty())continue;auto &out=rows[row.value(key,std::string())];if(!out.is_object())out=json::object();sum(out,retained);out[key]=row.value(key,std::string());out["perClient"][c.key()]=*c;restored=true;
   }
  }
  incoming[tier]=json::array();for(auto &row:rows)incoming[tier].push_back(row.second);
 }
 if(restored){incoming.erase("dailyTotals");incoming["summary"]={{"activeDaysComplete",false}};double tokens=0,cost=0;for(const auto &m:incoming["monthly"]){tokens+=number(m,"tokens");cost+=number(m,"cost");}incoming["summary"]["totalTokens"]=tokens;incoming["summary"]["totalCost"]=cost;}
 return incoming;
}
bool validId(const std::string &id){return !id.empty()&&id.size()<=128&&std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_'||c=='.';});}
bool equal(const std::string &a,const std::string &b){size_t d=a.size()^b.size();for(size_t i=0;i<std::max(a.size(),b.size());++i)d|=(i<a.size()?a[i]:0)^(i<b.size()?b[i]:0);return d==0;}
struct Internet {HINTERNET h;~Internet(){if(h)WinHttpCloseHandle(h);}};
}
void validateHubSettings(const HubSettings &s,const std::string &secret,bool fixture){
 if(s.mode<0||s.mode>2||s.port<0||s.port>65535||(!fixture&&s.port==0)||!validId(s.deviceId))throw std::runtime_error("Invalid Hub configuration");
 if(s.mode&&(secret.size()<16||secret.size()>256||!std::all_of(secret.begin(),secret.end(),[](unsigned char c){return c>=33&&c<=126;})))throw std::runtime_error("Invalid Hub shared secret");
 if(s.mode==1){auto url=wide(s.url);URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=DWORD(-1);
 if(s.url.size()>2048||!WinHttpCrackUrl(url.c_str(),0,0,&u)||!u.dwHostNameLength||u.dwUserNameLength||u.dwPasswordLength||u.dwExtraInfoLength||u.dwUrlPathLength>1||(u.nScheme!=INTERNET_SCHEME_HTTP&&u.nScheme!=INTERNET_SCHEME_HTTPS))throw std::runtime_error("Hub URL must be an HTTP(S) origin");}
}
HubSettings HubSettings::load(){HubSettings s;auto j=read(dataDirectory()/L"hub-settings.json",16384);if(j.is_object()){s.mode=j.value("mode",0);s.port=j.value("port",17321);s.url=j.value("url",std::string());s.deviceId=j.value("deviceId",std::string());}if(!validId(s.deviceId))s.deviceId="quota-"+newHubSecret().substr(0,24);if(s.mode<0||s.mode>2)s.mode=0;return s;}
void HubSettings::save()const{if(mode<0||mode>2||port<1||port>65535||!validId(deviceId)||url.size()>2048)throw std::runtime_error("Invalid Hub settings");saveAnalyticsJson(dataDirectory()/L"hub-settings.json",{{"mode",mode},{"port",port},{"url",url},{"deviceId",deviceId}});}
std::string hubSecret(){PCREDENTIALW c=nullptr;if(!CredReadW(L"CodexQuotaBar.SyncHub.Secret",CRED_TYPE_GENERIC,0,&c))return {};std::string s(reinterpret_cast<char *>(c->CredentialBlob),c->CredentialBlobSize);SecureZeroMemory(c->CredentialBlob,c->CredentialBlobSize);CredFree(c);return s;}
void saveHubSecret(const std::string &s){if(s.size()<16||s.size()>256||s.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Hub secret needs 16-256 characters");CREDENTIALW c{};c.Type=CRED_TYPE_GENERIC;c.TargetName=const_cast<wchar_t *>(L"CodexQuotaBar.SyncHub.Secret");c.UserName=const_cast<wchar_t *>(L"Hub");c.Persist=CRED_PERSIST_LOCAL_MACHINE;c.CredentialBlobSize=DWORD(s.size());c.CredentialBlob=reinterpret_cast<BYTE *>(const_cast<char *>(s.data()));if(!CredWriteW(&c,0))throw std::runtime_error("Cannot save Hub secret");}
std::string newHubSecret(){HCRYPTPROV p=0;if(!CryptAcquireContextW(&p,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))throw std::runtime_error("Random generator unavailable");BYTE b[24];bool ok=CryptGenRandom(p,sizeof(b),b);CryptReleaseContext(p,0);if(!ok)throw std::runtime_error("Random generator unavailable");const char *h="0123456789abcdef";std::string s;for(BYTE v:b){s+=h[v>>4];s+=h[v&15];}return s;}
std::vector<std::wstring> hubAddresses(int port){ULONG n=16384;std::vector<BYTE>b(n);auto p=reinterpret_cast<IP_ADAPTER_ADDRESSES *>(b.data());if(GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,p,&n)==ERROR_BUFFER_OVERFLOW){b.resize(n);p=reinterpret_cast<IP_ADAPTER_ADDRESSES *>(b.data());}std::vector<std::wstring>r;if(GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,p,&n)==NO_ERROR)for(auto a=p;a;a=a->Next)if(a->OperStatus==IfOperStatusUp&&a->IfType!=IF_TYPE_SOFTWARE_LOOPBACK)for(auto u=a->FirstUnicastAddress;u;u=u->Next){wchar_t address[64];auto s=reinterpret_cast<sockaddr_in *>(u->Address.lpSockaddr);if(s->sin_family==AF_INET&&InetNtopW(AF_INET,&s->sin_addr,address,64))r.push_back(L"http://"+std::wstring(address)+L":"+std::to_wstring(port));}return r;}
HubStore::HubStore(fs::path p):file(std::move(p)){auto j=read(file,64*1024*1024);if(!file.empty()&&fs::exists(file)&&(!j.is_object()||j.value("schema",0)!=1||!j.contains("devices")||!j["devices"].is_object()))throw std::runtime_error("Stored Hub data is unreadable; file preserved");if(j.is_object()&&j.value("schema",0)==1&&j.contains("devices")&&j["devices"].is_object())for(auto i=j["devices"].begin();i!=j["devices"].end()&&devices.size()<128;++i)if(validId(i.key())&&i->is_object())devices[i.key()]=*i;}
json HubStore::list()const{json a=json::array();for(const auto &d:devices){auto v=d.second;auto age=std::max<int64_t>(0,utcNow()-parseUsageTime(v.value("receivedAt",std::string())))*1000;v["ageMs"]=age;v["stale"]=age>120000;a.push_back(v);}return a;}
json HubStore::history()const{
 json result={{"daily",json::array()},{"monthly",json::array()},{"summary",{{"activeDaysComplete",false}}}};
 bool complete=!devices.empty();double lifetimeTokens=0,lifetimeCost=0;
 for(const auto &device:devices){auto h=device.second.value("history",json());if(!h.is_object()||!device.second.value("historyAvailable",false)||!h.value("summary",json::object()).value("activeDaysComplete",false)||!h.contains("dailyTotals")||!h["dailyTotals"].is_array())complete=false;}
 for(auto tier:{"daily","monthly","dailyTotals"}){
  if(std::string(tier)=="dailyTotals"&&!complete)continue;
  std::map<std::string,json> rows;
  for(const auto &device:devices){auto h=device.second.value("history",json());if(!h.is_object()||!h.contains(tier)||!h[tier].is_array())continue;
   for(const auto &row:h[tier]){if(!row.is_object())continue;auto key=row.value(std::string(tier)=="monthly"?"month":"date",std::string());if(key.empty()||key.size()>10)continue;
    json cleaned=json::object();for(auto field:{"tokens","cost","messages","activeTimeMs","cacheReadTokens","cacheWriteTokens","outputTokens","unclassifiedTokens","unpricedTokens"})if(row.contains(field)&&row[field].is_number())cleaned[field]=number(row,field);
    for(auto field:{"perClient","perModel"})if(row.contains(field)&&row[field].is_object())cleaned[field]=sanitize(row[field]);
    auto &out=rows[key];if(!out.is_object())out=json::object();sum(out,cleaned);out[std::string(tier)=="monthly"?"month":"date"]=key;
   }
  }
  if(!result.contains(tier))result[tier]=json::array();
  auto oldest=stamp(utcNow()-369LL*86400).substr(0,10);double peak=0;for(const auto &row:rows)peak=std::max(peak,row.second.value("tokens",0.));
  for(auto &row:rows){if(std::string(tier)=="daily"&&row.first<oldest)continue;if(std::string(tier)=="daily")row.second["intensity"]=peak?int(std::ceil(4*row.second.value("tokens",0.)/peak)):0;result[tier].push_back(row.second);}
 }
 for(const auto &device:devices){auto h=device.second.value("history",json::object());if(!h.is_object())continue;auto summary=h.value("summary",json::object());if(summary.contains("totalTokens"))lifetimeTokens+=number(summary,"totalTokens");else if(h.contains("monthly")&&h["monthly"].is_array())for(const auto &row:h["monthly"])lifetimeTokens+=number(row,"tokens");if(summary.contains("totalCost"))lifetimeCost+=number(summary,"totalCost");else if(h.contains("monthly")&&h["monthly"].is_array())for(const auto &row:h["monthly"])lifetimeCost+=number(row,"cost");}
 result["summary"]["totalTokens"]=lifetimeTokens;result["summary"]["totalCost"]=lifetimeCost;result["summary"]["activeDaysComplete"]=complete;
 if(complete){int active=0;for(const auto &row:result["dailyTotals"])if(row.value("tokens",0.)>0)++active;result["summary"]["activeDays"]=active;}
 return result;
}
json HubStore::stats()const{json s={{"updatedAt",stamp()},{"staleAfterMs",120000},{"periods",json::object()},{"devices",list()},{"projectsIncomplete",true},{"limits",{{"providers",json::array()}}}};for(auto name:{"today","month","allTime"}){auto total=period(json::object());bool complete=true,throughput=true;for(const auto &d:devices){auto v=d.second["periods"][name];bool expired=false;if(std::string(name)!="allTime"){auto w=d.second.value("periodWindows",json::object());if(w.contains(name)&&w[name].is_object()){auto e=w[name].value("endsAt",std::string());expired=!e.empty()&&parseUsageTime(e)<=utcNow();}else expired=stamp().substr(0,std::string(name)=="today"?10:7)!=d.second.value("updatedAt",std::string()).substr(0,std::string(name)=="today"?10:7);}if(!expired){complete=complete&&v.value("capabilities",json::object()).value("tokenComponents",false);throughput=throughput&&v.value("capabilities",json::object()).value("throughput",false);
 // Session IDs belong to a device. Keep metadata and avoid collapsing independent sources.
 for(auto key:{"sessions"}){json namespaced=json::object();for(auto i=v[key].begin();i!=v[key].end();++i)namespaced[d.first+":"+i.key()]=*i;v[key]=namespaced;}
 sum(total,v);}}total["capabilities"]["tokenComponents"]=complete;total["capabilities"]["throughput"]=throughput;s["periods"][name]=total;}std::map<std::string,json> limits;
 bool unavailable=false;for(const auto &d:devices){unavailable=unavailable||d.second.value("allTimeTokensUnavailable",false);auto l=d.second.value("limits",json::object());if(l.contains("providers")&&l["providers"].is_array())for(const auto &p:l["providers"]){auto key=p.value("provider",std::string())+":"+p.value("accountKey",std::string(d.first));auto old=limits.find(key);if(old==limits.end()||parseUsageTime(p.value("updatedAt",std::string()))>parseUsageTime(old->second.value("updatedAt",std::string())))limits[key]=p;}}
 for(const auto &p:limits)s["limits"]["providers"].push_back(p.second);
 s["allTimeTokensUnavailable"]=unavailable;auto preview=history();for(auto pair:std::vector<std::pair<std::string,size_t>>{{"daily",30},{"monthly",12}}){auto &rows=preview[pair.first];if(rows.size()>pair.second)rows.erase(rows.begin(),rows.end()-pair.second);}s["historyPreview"]=preview;return s;}
HubResponse HubStore::request(const std::string &method,const std::string &path,const json &body){
 if(method=="GET"&&path=="/api/sync/content")return {200,{{"ok",true},{"version",1},{"sessionTitles",{{"enabled",false}}},{"sharedSettings",false}}};
 if(method=="GET"&&path=="/api/stats")return {200,stats()};if(method=="GET"&&path=="/api/devices")return {200,{{"devices",list()}}};if(method=="GET"&&path=="/api/history")return {200,history()};
 if(method=="POST"&&path=="/api/ingest"){try{
  if(!body.is_object()||!body.contains("deviceId")||!body["deviceId"].is_string())return {400,{{"error","invalid_device"}}};auto id=body["deviceId"].get<std::string>();if(!validId(id))return {400,{{"error","invalid_device"}}};if(!devices.count(id)&&devices.size()>=128)return {507,{{"error","device_limit"}}};
  json d={{"deviceId",id},{"receivedAt",stamp()},{"periods",json::object()},{"historyAvailable",false},{"stale",false},{"ageMs",0}};
  for(auto key:{"hostname","platform","osName","osVersion","agentVersion","agentRuntime","deviceKind","updatedAt"})if(body.contains(key)&&body[key].is_string()&&body[key].get_ref<const std::string &>().size()<256)d[key]=body[key];
  if(!d.contains("updatedAt"))d["updatedAt"]=stamp();for(auto name:{"today","month","allTime"}){auto p=body.contains(name)?body[name]:body.value("periods",json::object()).value(name,json::object());if(!p.is_object())return {400,{{"error","invalid_period"}}};d["periods"][name]=period(p);}
  if(devices.count(id)&&body.contains("trackedClients")&&body["trackedClients"].is_array())for(auto name:{"today","month","allTime"}){
   const auto &before=devices[id];bool same=std::string(name)=="allTime";
   if(!same){auto a=before.value("periodWindows",json::object()),b=body.value("periodWindows",json::object());same=a.contains(name)&&b.contains(name)&&a[name].value("key",std::string())==b[name].value("key",std::string());}
   if(same)d["periods"][name]=retainUntracked(d["periods"][name],before["periods"][name],body["trackedClients"]);
  }
  if(devices.count(id)){for(auto key:{"history","historyAvailable"})if(devices[id].contains(key))d[key]=devices[id][key];}
  if(body.contains("historyAvailable")&&body["historyAvailable"].is_boolean())d["historyAvailable"]=body["historyAvailable"];
  if(body.contains("history")){if(!body["history"].is_null()&&!body["history"].is_object())return {400,{{"error","invalid_history"}}};d["history"]=sanitize(body["history"]);
   if(d["history"].is_object()){auto &history=d["history"];if(history.contains("summary")&&history["summary"].is_object()&&history["summary"].contains("activeDaysComplete")&&!history["summary"]["activeDaysComplete"].is_boolean())return {400,{{"error","invalid_history"}}};if(history.contains("summary")&&!history["summary"].is_object())return {400,{{"error","invalid_history"}}};for(auto tier:{"daily","monthly","dailyTotals"})if(history.contains(tier)){if(!history[tier].is_array())return {400,{{"error","invalid_history"}}};for(const auto &row:history[tier]){if(!row.is_object())return {400,{{"error","invalid_history"}}};auto k=std::string(tier)=="monthly"?"month":"date";if(!row.contains(k)||!row[k].is_string()||row[k].get_ref<const std::string &>().size()!=(std::string(tier)=="monthly"?7:10))return {400,{{"error","invalid_history_date"}}};}}}
}
  if(devices.count(id)&&body.contains("history")&&body["history"].is_object()&&body.contains("trackedClients"))d["history"]=retainUntrackedHistory(d["history"],devices[id].value("history",json()),body["trackedClients"]);
  if(body.contains("limits")&&(!body["limits"].is_object()||!body["limits"].value("providers",json::array()).is_array()))return {400,{{"error","invalid_limits"}}};
  if(body.contains("limits"))for(const auto &p:body["limits"].value("providers",json::array())){if(!p.is_object())return {400,{{"error","invalid_limits"}}};for(auto key:{"provider","accountKey","updatedAt","status"})if(p.contains(key)&&!p[key].is_string())return {400,{{"error","invalid_limits"}}};if(p.contains("windows")&&!p["windows"].is_array())return {400,{{"error","invalid_limits"}}};}
  for(auto key:{"limits","trackedClients","projectsEnabled","allTimeProjectsOmitted","allTimeProjectsIncomplete"})if(body.contains(key))d[key]=sanitize(body[key]);
  if(body.contains("allTimeTokensUnavailable"))d["allTimeTokensUnavailable"]=body["allTimeTokensUnavailable"]==true;
  if(body.contains("periodWindows")){if(!body["periodWindows"].is_object())return {400,{{"error","invalid_period_windows"}}};auto windows=body["periodWindows"];for(auto name:{"today","month"})if(windows.contains(name)){if(!windows[name].is_object())return {400,{{"error","invalid_period_windows"}}};for(auto key:{"key","endsAt"})if(windows[name].contains(key)&&!windows[name][key].is_string())return {400,{{"error","invalid_period_windows"}}};}d["periodWindows"]=sanitize(windows);}
  auto old=devices;devices[id]=d;try{json persisted={{"schema",1},{"devices",devices}};if(persisted.dump().size()>16*1024*1024)throw std::runtime_error("Hub storage limit");if(!file.empty())saveAnalyticsJson(file,persisted);}catch(...){devices=std::move(old);return {507,{{"error","persistence_failed"}}};}return {200,{{"ok",true}}};
 }catch(...){return {400,{{"error","invalid_snapshot"}}};}}
 return {404,{{"error","not_found"}}};
}
HubResponse hubHttp(const std::string &base,const std::string &secret,const std::string &method,const std::string &path,const json &body){
 auto url=wide(base);URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=DWORD(-1);u.dwUrlPathLength=DWORD(-1);u.dwUserNameLength=DWORD(-1);u.dwPasswordLength=DWORD(-1);u.dwExtraInfoLength=DWORD(-1);if(!WinHttpCrackUrl(url.c_str(),0,0,&u)||u.dwUserNameLength||u.dwPasswordLength||(u.nScheme!=INTERNET_SCHEME_HTTP&&u.nScheme!=INTERNET_SCHEME_HTTPS)||u.dwUrlPathLength>1||u.dwExtraInfoLength)throw std::runtime_error("Hub URL must be an HTTP(S) origin");
 Internet session{WinHttpOpen(L"CodexQuotaBar Hub",WINHTTP_ACCESS_TYPE_NO_PROXY,0,0,0)};if(!session.h)throw std::runtime_error("Hub session unavailable");WinHttpSetTimeouts(session.h,2500,2500,3000,3000);std::wstring host(u.lpszHostName,u.dwHostNameLength);Internet conn{WinHttpConnect(session.h,host.c_str(),u.nPort,0)};Internet req{WinHttpOpenRequest(conn.h,wide(method).c_str(),wide(path).c_str(),0,0,0,u.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};if(!req.h)throw std::runtime_error("Hub request unavailable");DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(req.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));auto data=body.is_null()?std::string():body.dump();auto headers=L"Content-Type: application/json\r\nAuthorization: Bearer "+wide(secret)+L"\r\n";if(!WinHttpSendRequest(req.h,headers.c_str(),DWORD(headers.size()),data.empty()?WINHTTP_NO_REQUEST_DATA:data.data(),DWORD(data.size()),DWORD(data.size()),0)||!WinHttpReceiveResponse(req.h,0))throw std::runtime_error("Hub unreachable");DWORD status=0,n=sizeof(status);WinHttpQueryHeaders(req.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,0,&status,&n,0);std::string text;char b[16384];DWORD got;auto deadline=GetTickCount64()+10000;while(true){if(GetTickCount64()>deadline)throw std::runtime_error("Hub response deadline");if(!WinHttpReadData(req.h,b,sizeof(b),&got))throw std::runtime_error("Incomplete Hub response");if(!got)break;text.append(b,got);if(text.size()>32*1024*1024)throw std::runtime_error("Hub response exceeds limit");}auto j=json::parse(text,nullptr,false);if(j.is_discarded())throw std::runtime_error("Invalid Hub response");return {int(status),j};
}
// Durable daily rollups are independent from the disposable scan cache. No prompts,
// titles, paths or provider credentials are archived. A later incomplete scan cannot
// lower a previously observed per-session/day/model counter.
UsageReport hubRetainUsage(const UsageReport &report,const fs::path &archive){
 auto saved=read(archive,64*1024*1024);if(fs::exists(archive)&&(!saved.is_object()||saved.value("schema",0)!=1||!saved.contains("rows")))throw std::runtime_error("Stored archive is unreadable; file preserved");json rows=saved.is_object()?saved.value("rows",json::object()):json::object();
 if(!rows.is_object())throw std::runtime_error("Invalid Hub archive");
 std::map<std::string,UsageEntry> current;
 for(const auto &e:report.entries){auto day=localDay(e.at);auto key=json::array({e.session,day,e.model,e.tier}).dump();auto &r=current[key];if(r.session.empty())r.priced=true;r.at=day;r.session=e.session;r.model=e.model;r.tier=e.tier;r.cost+=e.cost;r.priced=r.priced&&e.priced;
  if(!e.tokens.valid()||r.tokens.total()>int64_t(Safe)-e.tokens.total())throw std::runtime_error("Hub archive counter overflow");r.tokens.input+=e.tokens.input;r.tokens.cached+=e.tokens.cached;r.tokens.written+=e.tokens.written;r.tokens.output+=e.tokens.output;r.tokens.reasoning+=e.tokens.reasoning;}
 std::map<std::string,int64_t> before,after;std::map<std::string,std::string> rowGroups;
 for(auto i=rows.begin();i!=rows.end();++i){auto key=json::parse(i.key());auto group=json::array({key.at(0),key.at(1)}).dump();rowGroups[i.key()]=group;auto t=(*i).at(1);auto count=t.at(0).get<int64_t>()+t.at(3).get<int64_t>();if(count<0||before[group]>int64_t(Safe)-count)throw std::runtime_error("Invalid Hub archive counter");before[group]+=count;}
 for(const auto &pair:current){auto group=json::array({pair.second.session,pair.second.at}).dump();if(after[group]>int64_t(Safe)-pair.second.tokens.total())throw std::runtime_error("Hub day counter overflow");after[group]+=pair.second.tokens.total();}
 for(const auto &pair:rowGroups)if(after.count(pair.second)&&after[pair.second]>=before[pair.second])rows.erase(pair.first);
 for(const auto &pair:current){const auto &e=pair.second;auto group=json::array({e.session,e.at}).dump();if(after[group]<before[group])continue;auto tokens=json::array({e.tokens.input,e.tokens.cached,e.tokens.written,e.tokens.output,e.tokens.reasoning});rows[pair.first]=json::array({e.at,tokens,e.model,e.tier,e.cost,e.priced});}
 if(rows.size()>180000)throw std::runtime_error("Hub archive row limit");
 auto persisted=json{{"schema",1},{"rows",rows}};if(persisted.dump().size()>64*1024*1024)throw std::runtime_error("Hub archive size limit");if(persisted!=saved)saveAnalyticsJson(archive,persisted);
 UsageReport result=report;result.entries.clear();result.titles.clear();
 for(auto i=rows.begin();i!=rows.end();++i){auto v=*i;UsageEntry e;e.at=v.at(0);auto t=v.at(1);e.tokens={t.at(0),t.at(1),t.at(2),t.at(3),t.at(4)};e.model=v.at(2);e.tier=v.at(3);if(!e.tokens.valid())throw std::runtime_error("Invalid Hub archive counters");e.cost=v.size()>4?v.at(4).get<double>():0;e.priced=v.size()>5&&v.at(5).get<bool>();if(!std::isfinite(e.cost)||e.cost<0)throw std::runtime_error("Invalid Hub archive cost");result.entries.push_back(e);}
 return result;
}
json hubLocalSnapshot(const UsageReport &report,const State &,const std::string &id){
 json r={{"deviceId",id},{"hostname",id},{"platform","win32-x64"},{"osName","Windows"},{"agentVersion",VersionUtf8},{"agentRuntime","codex-quota-bar"},{"updatedAt",stamp()},{"trackedClients",{"codex"}},{"projectsEnabled",false},{"historyAvailable",false},{"allTimeTokensUnavailable",true}};
 wchar_t computer[256];DWORD computerLength=256;if(GetComputerNameW(computer,&computerLength))r["hostname"]=utf8(std::wstring(computer,computerLength));
 std::map<std::string,json> daily,monthly;
 for(const auto &e:report.entries){auto date=utf8(usageDate(e.at,true));std::replace(date.begin(),date.end(),'/','-');auto &d=daily[date];if(!d.is_object())d={{"date",date},{"tokens",0LL},{"cost",0.0},{"messages",0},{"cacheReadTokens",0LL},{"cacheWriteTokens",0LL},{"outputTokens",0LL},{"unclassifiedTokens",0},{"tokenComponentsAvailable",true},{"unpricedTokens",0LL}};d["tokens"]=d["tokens"].get<int64_t>()+e.tokens.total();d["cost"]=d["cost"].get<double>()+e.cost;d["cacheReadTokens"]=d["cacheReadTokens"].get<int64_t>()+e.tokens.cached;d["cacheWriteTokens"]=d["cacheWriteTokens"].get<int64_t>()+e.tokens.written;d["outputTokens"]=d["outputTokens"].get<int64_t>()+e.tokens.output;if(!e.priced)d["unpricedTokens"]=d["unpricedTokens"].get<int64_t>()+e.tokens.total();
 json part={{"tokens",e.tokens.total()},{"cost",e.cost},{"cacheReadTokens",e.tokens.cached},{"cacheWriteTokens",e.tokens.written},{"outputTokens",e.tokens.output},{"unclassifiedTokens",0},{"unpricedTokens",e.priced?0:e.tokens.total()}};
 for(auto pair:std::vector<std::pair<std::string,std::string>>{{"perClient","codex"},{"perModel",e.model.empty()?"unknown":e.model}}){if(!d.contains(pair.first))d[pair.first]=json::object();if(!d[pair.first].contains(pair.second))d[pair.first][pair.second]=json::object();sum(d[pair.first][pair.second],part);}}

 json hist={{"daily",json::array()},{"monthly",json::array()},{"summary",{{"activeDaysComplete",false}}}};for(auto &entry:daily){if(entry.first>=stamp(utcNow()-369LL*86400).substr(0,10))hist["daily"].push_back(entry.second);auto key=entry.first.substr(0,7);auto &m=monthly[key];if(!m.is_object())m=json::object();auto row=entry.second;row.erase("date");sum(m,row);m["month"]=key;}for(auto &m:monthly)hist["monthly"].push_back(m.second);r["historyAvailable"]=true;r["history"]=hist;
 int64_t today=localDay(utcNow());time_t now=utcNow();tm t{};localtime_s(&t,&now);t.tm_mday=1;t.tm_hour=t.tm_min=t.tm_sec=0;t.tm_isdst=-1;auto month=_mktime64(&t);
 for(auto name:{"today","month","allTime"}){auto p=period(json::object());p["capabilities"]["tokenComponents"]=true;int64_t begin=std::string(name)=="today"?today:std::string(name)=="month"?month:0;auto v=report.total(begin);p["totalTokens"]=v.tokens.total();p["costUsd"]=v.cost;p["cacheReadTokens"]=v.tokens.cached;p["cacheWriteTokens"]=v.tokens.written;p["outputTokens"]=v.tokens.output;p["clients"]["codex"]=v.tokens.total();p["clientCosts"]["codex"]=v.cost;p["clientCacheReads"]["codex"]=v.tokens.cached;p["clientCacheWrites"]["codex"]=v.tokens.written;p["clientOutputs"]["codex"]=v.tokens.output;for(const auto &e:report.entries)if(e.at>=begin){auto m=e.model.empty()?"unknown":e.model;p["models"][m]=p["models"].value(m,0LL)+e.tokens.total();p["modelCosts"][m]=p["modelCosts"].value(m,0.)+e.cost;p["modelCacheReads"][m]=p["modelCacheReads"].value(m,0LL)+e.tokens.cached;p["modelCacheWrites"][m]=p["modelCacheWrites"].value(m,0LL)+e.tokens.written;p["modelOutputs"][m]=p["modelOutputs"].value(m,0LL)+e.tokens.output;p["clientModels"]["codex"][m]=p["models"][m];p["clientModelCosts"]["codex"][m]=p["modelCosts"][m];}p["unpricedTokens"]=0LL;for(const auto &e:report.entries)if(e.at>=begin&&!e.priced)p["unpricedTokens"]=p["unpricedTokens"].get<int64_t>()+e.tokens.total();r[name]=p;}
 auto date=utf8(usageDate(today,true));std::replace(date.begin(),date.end(),'/','-');t.tm_mon++;auto monthEnd=_mktime64(&t);
 r["periodWindows"]={{"today",{{"key",date},{"endsAt",stamp(shiftLocalDay(today,1))}}},{"month",{{"key",date.substr(0,7)},{"endsAt",stamp(monthEnd)}}}};
 return r;
}
struct HubRuntime::Impl {
 HubSettings settings;std::string secret;fs::path directory;std::unique_ptr<HubStore> store;
 std::atomic<bool> stopping{false};std::atomic<int> streams{0};bool wsa=false;std::atomic<SOCKET> listener{INVALID_SOCKET};std::vector<std::thread> workers;std::thread collector;
 mutable std::mutex lock;json remote;std::wstring message=L"仅本机";
 void setMessage(std::wstring text){std::lock_guard<std::mutex> g(lock);message=std::move(text);}
 static bool sendAll(SOCKET s,const std::string &text){size_t n=0;auto deadline=GetTickCount64()+5000;while(n<text.size()){if(GetTickCount64()>deadline)return false;int k=send(s,text.data()+n,int(std::min<size_t>(16384,text.size()-n)),0);if(k<=0)return false;n+=size_t(k);}return true;}
 void connection(SOCKET s){DWORD timeout=2500;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char *>(&timeout),sizeof(timeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char *>(&timeout),sizeof(timeout));
  const auto deadline=GetTickCount64()+5000;auto reply=[&](int status,const json &j){auto body=j.dump();sendAll(s,"HTTP/1.1 "+std::to_string(status)+" Result\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body);};
  try{std::string request;char b[8192];size_t end;while((end=request.find("\r\n\r\n"))==std::string::npos){if(stopping||GetTickCount64()>deadline)return;int n=recv(s,b,sizeof(b),0);if(n<=0)return;request.append(b,n);if(request.size()>16384){reply(431,{{"error","headers_too_large"}});return;}}
   auto head=request.substr(0,end);std::istringstream input(head);std::string method,path,version;input>>method>>path>>version;std::string line;std::getline(input,line);if(version!="HTTP/1.1"&&version!="HTTP/1.0"){reply(400,{{"error","invalid_http"}});return;}std::map<std::string,std::string> headers;while(std::getline(input,line)){auto at=line.find(':');if(at==std::string::npos)continue;auto key=line.substr(0,at);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});auto value=line.substr(at+1);while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.front())))value.erase(0,1);while(!value.empty()&&std::isspace(static_cast<unsigned char>(value.back())))value.pop_back();if(headers.count(key)){reply(400,{{"error","duplicate_header"}});return;}headers[key]=value;}
   path=path.substr(0,path.find('?'));
   if(method=="GET"&&path=="/api/health"){std::lock_guard<std::mutex>g(lock);reply(200,{{"ok",true},{"role","hub"},{"version",1},{"runtime","codex-quota-bar"},{"secretRequired",true},{"deviceCount",store->list().size()},{"now",stamp()}});return;}
   if(!equal(headers["authorization"],"Bearer "+secret)&&!equal(headers["x-token-monitor-secret"],secret)){reply(401,{{"error","unauthorized"}});return;}
   if(headers.count("transfer-encoding")){reply(400,{{"error","unsupported_transfer_encoding"}});return;}
   size_t length=0;if(headers.count("content-length")){auto text=headers["content-length"];if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos){reply(400,{{"error","invalid_length"}});return;}length=std::stoull(text);}if(length>BodyLimit){reply(413,{{"error","payload_too_large"}});return;}
   if(method=="GET"&&path=="/api/stats/stream"){
    if(streams.fetch_add(1)>=4){--streams;reply(503,{{"error","stream_limit"}});return;}struct Slot{std::atomic<int>&n;~Slot(){--n;}}slot{streams};if(!sendAll(s,"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"))return;std::string previous;int tick=0;while(!stopping){json stats;{std::lock_guard<std::mutex>g(lock);stats=store->stats();}auto comparable=stats;comparable.erase("updatedAt");for(auto &d:comparable["devices"])d.erase("ageMs");auto payload=comparable.dump();if(payload!=previous){if(!sendAll(s,"event: "+std::string(previous.empty()?"snapshot":"stats")+"\ndata: "+json({{"stats",stats}}).dump()+"\n\n"))return;previous=payload;}else if(++tick%30==0&&!sendAll(s,": hb\n\n"))return;for(int n=0;n<10&&!stopping;++n)Sleep(100);}return;
   }
   auto body=request.substr(end+4);while(body.size()<length){if(stopping||GetTickCount64()>deadline)return;int n=recv(s,b,int(std::min<size_t>(sizeof(b),length-body.size())),0);if(n<=0)return;body.append(b,n);}json j=length?json::parse(body.substr(0,length),nullptr,false):json();if(j.is_discarded()){reply(400,{{"error","invalid_json"}});return;}
   HubResponse result;{std::lock_guard<std::mutex>g(lock);result=store->request(method,path,j);}reply(result.status,result.body);
  }catch(...){reply(400,{{"error","invalid_request"}});}
 }
 void serve(){while(!stopping){auto s=listener.load();if(s==INVALID_SOCKET)break;fd_set f;FD_ZERO(&f);FD_SET(s,&f);timeval t{0,200000};if(select(0,&f,nullptr,nullptr,&t)<=0)continue;SOCKET c=accept(s,nullptr,nullptr);if(c==INVALID_SOCKET)continue;u_long blocking=0;ioctlsocket(c,FIONBIO,&blocking);connection(c);closesocket(c);}}
 void upload(const json &snapshot){if(settings.mode==2){std::lock_guard<std::mutex>g(lock);auto result=store->request("POST","/api/ingest",snapshot);message=result.status==200?L"Hub 已启动":L"Hub 保存失败";}else{if(snapshot.dump().size()>BodyLimit)throw std::runtime_error("Snapshot too large");auto uploaded=hubHttp(settings.url,secret,"POST","/api/ingest",snapshot);if(stopping)throw std::runtime_error("Hub stopped");if(uploaded.status!=200)throw std::runtime_error("Upload rejected");auto fetched=hubHttp(settings.url,secret,"GET","/api/stats");if(stopping)throw std::runtime_error("Hub stopped");if(fetched.status!=200)throw std::runtime_error("Read rejected");auto history=hubHttp(settings.url,secret,"GET","/api/history");if(stopping)throw std::runtime_error("Hub stopped");if(history.status==200)fetched.body["historyPreview"]=history.body;std::lock_guard<std::mutex>g(lock);if(!fetched.body.is_object()||!fetched.body.contains("periods")||!fetched.body["periods"].is_object()||!fetched.body.value("devices",json::array()).is_array())throw std::runtime_error("Invalid Hub stats schema");
 json safe={{"periods",json::object()},{"devices",json::array()}};for(auto name:{"today","month","allTime"})safe["periods"][name]=period(fetched.body["periods"].at(name));
 for(const auto &d:fetched.body.value("devices",json::array())){if(!d.is_object())continue;safe["devices"].push_back({{"deviceId",d.value("deviceId",std::string())},{"hostname",d.value("hostname",std::string())},{"stale",d.value("stale",false)}});}
 if(fetched.body.contains("historyPreview")&&fetched.body["historyPreview"].is_object())safe["historyPreview"]=sanitize(fetched.body["historyPreview"]);
 remote=std::move(safe);saveAnalyticsJson(directory/L"hub-remote-cache.json",{{"schema",1},{"origin",settings.url},{"stats",remote}});message=L"已连接 Hub";}}
 void collect(){std::atomic<bool> cancel{false};while(!stopping){try{UsageScanOptions o;o.cache=directory/L"hub-scan-cache.json";auto report=scanCodexUsage(o,stopping);if(stopping)break;if(report.pending){setMessage(L"正在补齐本地历史，保留上次同步结果");for(int n=0;n<15&&!stopping;++n)Sleep(100);continue;}auto retained=hubRetainUsage(report,directory/L"hub-local-history.json");auto snapshot=hubLocalSnapshot(retained,State{},settings.deviceId);upload(snapshot);}catch(...){setMessage(L"同步失败，请检查网络、配置或存储空间；保留上次结果");}for(int n=0;n<100&&!stopping;++n)Sleep(100);}}
};
HubRuntime::HubRuntime():impl(std::make_unique<Impl>()){}
HubRuntime::~HubRuntime(){stop();}
void HubRuntime::stop(){impl->stopping=true;auto s=impl->listener.exchange(INVALID_SOCKET);if(s!=INVALID_SOCKET)closesocket(s);if(impl->collector.joinable())impl->collector.join();for(auto &w:impl->workers)if(w.joinable())w.join();impl->workers.clear();if(impl->wsa){WSACleanup();impl->wsa=false;}}
void HubRuntime::start(const HubSettings &s,const std::string &secret,const fs::path &dir,bool fixture){try{validateHubSettings(s,secret,fixture);}catch(...){impl->setMessage(L"同步配置无效，请重新应用设置");throw;}stop();impl->remote=json();impl->store.reset();impl->settings=s;impl->secret=secret;impl->directory=dir;if(!s.mode){impl->setMessage(L"仅本机");return;}impl->stopping=false;if(s.mode==1){auto cached=read(dir/L"hub-remote-cache.json",32*1024*1024);if(cached.is_object()&&cached.value("schema",0)==1&&cached.value("origin",std::string())==s.url&&cached.contains("stats")&&cached["stats"].is_object())impl->remote=cached["stats"];impl->setMessage(L"正在连接 Hub；显示上次缓存");}try{
 if(s.mode==2){WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Network unavailable");impl->wsa=true;impl->store=std::make_unique<HubStore>(dir/L"hub-devices.json");SOCKET socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(socket==INVALID_SOCKET)throw std::runtime_error("Cannot create Hub socket");BOOL exclusive=TRUE;setsockopt(socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char *>(&exclusive),sizeof(exclusive));sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=fixture?htonl(INADDR_LOOPBACK):INADDR_ANY;address.sin_port=htons(u_short(s.port));if(bind(socket,reinterpret_cast<sockaddr *>(&address),sizeof(address))||listen(socket,16)){closesocket(socket);throw std::runtime_error("Hub port unavailable");}int addressLength=sizeof(address);getsockname(socket,reinterpret_cast<sockaddr *>(&address),&addressLength);impl->settings.port=ntohs(address.sin_port);u_long nonblocking=1;ioctlsocket(socket,FIONBIO,&nonblocking);impl->listener=socket;for(int n=0;n<8;++n)impl->workers.emplace_back([this]{impl->serve();});impl->setMessage(L"Hub 已启动");}
 if(!fixture)impl->collector=std::thread([this]{impl->collect();});
 }catch(...){stop();impl->setMessage(L"Hub 启动失败，请检查设置和端口");throw;}
}
int HubRuntime::port()const{return impl->settings.port;}
void HubRuntime::publish(const json &j){impl->upload(j);}
json HubRuntime::view()const{std::lock_guard<std::mutex>g(impl->lock);if(impl->settings.mode==2&&impl->store){auto view=impl->store->stats();view["historyPreview"]=impl->store->history();return view;}return impl->remote;}
std::wstring HubRuntime::status()const{std::lock_guard<std::mutex>g(impl->lock);return impl->message;}
}
