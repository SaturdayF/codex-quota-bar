#include "app.hpp"
namespace quota {
namespace {
json fixture(const std::string &id,int tokens,bool mac){
 json period={{"totalTokens",tokens},{"costUsd",tokens*.001},{"cacheReadTokens",tokens/2},{"outputTokens",tokens/10},{"clients",{{"codex",tokens}}},{"clientCosts",{{"codex",tokens*.001}}},{"clientCacheReads",{{"codex",tokens/2}}},{"clientOutputs",{{"codex",tokens/10}}},{"clientModels",{{"codex",{{"fixture-model",tokens}}}}},{"clientModelCosts",{{"codex",{{"fixture-model",tokens*.001}}}}},{"models",{{"fixture-model",tokens}}},{"capabilities",{{"tokenComponents",true}}}};
 period["sessions"]={{"codex:fixture-session-"+id,{{"client","codex"},{"sessionId","fixture-session-"+id},{"totalTokens",tokens},{"costUsd",tokens*.001},{"contextTokens",1000},{"contextWindow",10000},{"waitingForInput",true},{"projectLabel","Simulated project"},{"prompt","must-not-survive"}}}};
 period["projects"]={{"simulated",{{"label","Simulated project"},{"tokens",tokens},{"costUsd",tokens*.001},{"clients",{{"codex",tokens}}}}}};
 auto daily=json::array({{{"date","2026-11-15"},{"tokens",tokens},{"cost",tokens*.001},{"perClient",{{"codex",{{"tokens",tokens},{"cost",tokens*.001}}}}}}});
 return {{"deviceId",id},{"hostname",mac?"Simulated Mac":"Simulated Windows"},{"platform",mac?"darwin-arm64":"win32-x64"},{"osName",mac?"macOS":"Windows"},{"osVersion",mac?"26":"11"},{"agentVersion","simulated"},{"updatedAt","2026-11-15T00:00:00Z"},{"today",period},{"month",period},{"allTime",period},{"historyAvailable",true},{"history",{{"daily",daily},{"monthly",json::array()}}},{"limits",{{"providers",json::array({{{"provider","codex"},{"accountKey","synthetic-"+id},{"planLabel","Simulated Plus"},{"status","ok"},{"updatedAt","2026-11-15T00:00:00Z"},{"windows",json::array({{{"kind","session"},{"remainingPercent",60},{"resetsAt","2026-11-16T00:00:00Z"}}})}}})}}}};
}
}
int sharedUsageTests(std::ostream &log,const fs::path &dir){
 fs::create_directories(dir);int count=0;auto check=[&](bool good,const char *name){log<<(good?"PASS ":"FAIL ")<<name<<std::endl;if(!good)throw std::runtime_error(name);++count;};
 HubStore store;auto win=fixture("fixture-win",100,false),mac=fixture("fixture-mac",200,true);
 check(store.request("POST","/api/ingest",win).status==200&&store.request("POST","/api/ingest",mac).status==200,"shared fixture accepts two distinct sources");
 auto view=hubNormalizeRemote(store.stats(),{{"devices",store.list()}},store.history());
 check(view["limits"]["providers"].size()==2,"remote cache retains account quota observations without adding percentages");
 check(view["devices"][1].contains("periods")&&view["devices"][1].contains("history")&&view["devices"][1].contains("osVersion"),"remote cache retains per-device periods history and metadata");
 check(!view["periods"]["today"]["sessions"].begin().value().contains("prompt"),"remote cache excludes conversation text");
 auto all=hubSharedUsage(view,"*",0,0);check(all["totals"]["tokens"]==300&&all["sessionCount"]==2,"shared totals use the Hub aggregate exactly once");
 auto one=hubSharedUsage(view,"fixture-mac",0,2);check(one["totals"]["tokens"]==200&&one["groups"].size()==1,"device selection isolates Mac usage and sessions");
 check(one["groups"][0]["metadata"]["contextWindow"]==10000&&one["groups"][0]["metadata"]["waitingForInput"]==true,"session context and waiting state survive receiving");
 auto models=hubSharedUsage(view,"*",0,0);check(models["groups"][0]["tokens"]==300,"shared model totals retain per-tool attribution");
 auto mixed=view;mixed["periods"]["today"]["totalTokens"]=800;mixed["periods"]["today"]["clients"]["claude"]=500;mixed["periods"]["today"]["models"]["fixture-model"]=800;
 check(hubSharedUsage(mixed,"*",0,0)["totals"]["tokens"]==300&&hubSharedUsage(mixed,"*",0,0)["groups"][0]["tokens"]==300,"other tools cannot leak into the Codex shared total or model breakdown");
 auto projects=hubSharedUsage(view,"*",0,1);check(projects["groups"].size()==1&&projects["groups"][0]["tokens"]==300,"shared project totals aggregate observed Codex attribution");
 auto devices=hubSharedUsage(view,"*",0,3);check(devices["groups"].size()==2,"device grouping exposes both saved devices");
 check(all["daily"].size()==1&&all["daily"][0]["tokens"]==300,"shared history reads Codex daily buckets rather than treating rollups as requests");
 check(hubSharedUsage(view,"missing-device",0,0)["available"]==false,"missing device never falls back to another source");
 auto expired=view;expired["devices"][0]["periodWindows"]["today"]={{"endsAt","2020-01-01T00:00:00Z"}};
 check(hubSharedUsage(expired,"fixture-mac",0,0)["totals"]["tokens"]==0,"expired device-day snapshots do not appear as today's usage");
 UsageReport report;report.available=true;UsageEntry e;e.at=utcNow();e.session="synthetic-own-session";e.project="E:/Private/root/Shared project";e.model="fixture-model";e.tokens={100,50,0,20,5};e.cost=.25;e.priced=true;report.entries={e};
 auto retained=hubRetainUsage(report,dir/L"archive.json");check(retained.entries[0].project=="Shared project","durable attribution retains a project label without its absolute path");
 auto lessMetadata=report;lessMetadata.entries[0].project.clear();check(hubRetainUsage(lessMetadata,dir/L"archive.json").entries[0].project=="Shared project","missing project metadata cannot erase previously observed archive attribution");
 State state;state.error.clear();state.updated=utcNow();state.q.accountKey=privateKey("synthetic-account");state.q.plan="plus";state.q.hour=WindowQuota{65,utcNow()+3600,300};state.q.week=WindowQuota{50,utcNow()+86400,10080};
 state.q.extraWindows={{"spark:primary:300","Simulated Spark","spark",{75,utcNow()+600,300}}};
 auto upload=hubLocalSnapshot(retained,state,"fixture-win");saveAnalyticsJson(dir/L"synthetic-upload.json",upload);check(upload["limits"]["providers"][0]["windows"].size()==3&&upload["limits"]["providers"][0]["windows"][0]["kind"]=="session"&&upload["limits"]["providers"][0]["windows"][2]["additional"]==true,"Windows publishes standard and additional quota windows using supported protocol kinds");
 check(upload["today"]["sessions"].size()==1&&upload["allTime"]["sessions"].empty(),"Windows publishes day/month sessions and omits unbounded lifetime sessions");
 check(upload["today"]["sessions"].begin().value()["inputTokens"]==50,"session cold input excludes cached and written input");
 check(upload["allTime"]["projects"].size()==1&&upload.dump().find("Private/root")==std::string::npos,"Windows project upload never includes absolute project paths");
 check(!upload["today"]["sessions"].begin().value().contains("startedAt")&&!upload["today"]["sessions"].begin().value().contains("contextTokens")&&!upload["history"]["daily"][0].contains("messages"),"archive rollups do not fabricate exact timing context or message counts");
 auto unknown=retained;unknown.entries[0].priced=false;unknown.entries[0].cost=0;HubStore unpriced;unpriced.request("POST","/api/ingest",hubLocalSnapshot(unknown,state,"fixture-win"));auto unknownView=hubNormalizeRemote(unpriced.stats(),{{"devices",unpriced.list()}},unpriced.history());check(hubSharedUsage(unknownView,"*",0,0)["totals"]["costKnown"]==false,"unknown model prices are not presented as free shared usage");
 state.error=L"offline";check(!hubLocalSnapshot(retained,state,"fixture-win").contains("limits"),"stale account state cannot be published as a fresh quota observation");
 HubSettings cfg;cfg.mode=2;cfg.port=0;cfg.deviceId="fixture-win";HubRuntime host;host.start(cfg,"synthetic-shared-secret-only",dir/L"server",true);host.publish(win);host.publish(mac);
 HubSettings clientCfg;clientCfg.mode=1;clientCfg.url="http://127.0.0.1:"+std::to_string(host.port());clientCfg.deviceId="read-only-client";HubRuntime client;client.start(clientCfg,"synthetic-shared-secret-only",dir/L"client",true);client.refresh();
 check(client.view()["devices"].size()==2&&client.view()["limits"]["providers"].size()==2,"read-only native receive works before any local upload");
 auto revision=client.revision();client.refresh();check(client.revision()>revision&&hubSharedUsage(client.view(),"*",0,0)["totals"]["tokens"]==300,"refresh advances the view without duplicate usage");
 client.stop();host.stop();client.start(clientCfg,"synthetic-shared-secret-only",dir/L"client",true);check(client.view()["devices"].size()==2&&client.view().contains("limits"),"offline restart restores complete per-device quota cache");client.stop();
 log<<count<<" shared statistics checks passed.\n";return count;
}
int sharedUsageUiTests(const fs::path &dir){
 fs::create_directories(dir);std::ofstream log(dir/L"checks.txt");int count=0;auto check=[&](bool good,const char *name){log<<(good?"PASS ":"FAIL ")<<name<<std::endl;if(!good)throw std::runtime_error(name);++count;};
 Application app(false,true);app.hubConfig.mode=2;app.hubConfig.deviceId="fixture-win";auto cfg=app.hubConfig;cfg.port=0;app.hub.start(cfg,"synthetic-shared-secret-only",dir/L"server",true);app.hub.publish(fixture("fixture-win",100,false));app.hub.publish(fixture("fixture-mac",200,true));app.showSettings();auto &ui=*app.settingsWindow;ui.selectPage(4);ui.syncUsageSources(true);ui.layout();
 check(ui.usageSource=="*"&&ui.sharedUsage["totals"]["tokens"]==300,"connected usage page defaults to all devices");
 check(IsWindowVisible(GetDlgItem(ui.content,UsageSource))&&SendDlgItemMessageW(ui.content,UsageSource,CB_GETCOUNT,0,0)==4,"source selector exposes local all and two devices");
 check(!IsWindowEnabled(GetDlgItem(ui.content,UsageExport))&&!IsWindowEnabled(GetDlgItem(ui.content,UsageHour)),"shared rollups cannot be exported as local requests or shown as hourly facts");
 ui.capture(dir/L"shared-all-light-simulated.png");
 auto mac=std::find(ui.usageSources.begin(),ui.usageSources.end(),"fixture-mac")-ui.usageSources.begin();SendDlgItemMessageW(ui.content,UsageSource,CB_SETCURSEL,mac,0);ui.command(UsageSource,CBN_SELCHANGE);
 check(ui.sharedUsage["totals"]["tokens"]==200,"Mac source selection updates displayed usage");ui.usageCommand(UsageSessions);check(ui.sharedRows.size()==1,"Mac session row is available in the main usage page");ui.scrollTo(520);ui.capture(dir/L"shared-mac-sessions-light-simulated.png");ui.scrollTo(0);
 ui.draft.darkAppearance=true;ui.applyAppearance();ui.capture(dir/L"shared-mac-dark-simulated.png");
 ui.usageCommand(UsageAll);check(!ui.sharedUsage.value("sessionDetailsAvailable",true),"lifetime view declares unavailable session detail");
 SendDlgItemMessageW(ui.content,UsageSource,CB_SETCURSEL,0,0);ui.command(UsageSource,CBN_SELCHANGE);check(ui.usageSource.empty()&&IsWindowEnabled(GetDlgItem(ui.content,UsageExport)),"local source preserves original analytics and CSV actions");
 log<<count<<" shared UI checks passed.\n";return count;
}
}
