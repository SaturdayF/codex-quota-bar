#include "hub.hpp"
namespace quota {
namespace {
double value(const json &j,const std::string &key){auto it=j.find(key);if(it==j.end()||!it->is_number())return 0;auto n=it->get<double>();return std::isfinite(n)&&n>=0&&n<=9007199254740991.?n:0;}
json object(const json &j,const char *key){auto it=j.find(key);return it!=j.end()&&it->is_object()?*it:json::object();}
json codexPeriod(const json &p){
 auto clients=object(p,"clients"),costs=object(p,"clientCosts");
 auto unpriced=value(object(p,"clientUnpricedTokens"),"codex");
 return {{"tokens",value(clients,"codex")},{"cost",value(costs,"codex")},{"costKnown",costs.contains("codex")&&(unpriced==0||value(costs,"codex")>0)},{"cache",value(object(p,"clientCacheReads"),"codex")},{"output",value(object(p,"clientOutputs"),"codex")},{"componentsKnown",p.value("capabilities",json::object()).value("tokenComponents",false)},{"unpricedTokens",unpriced}};
}
}
json hubSharedUsage(const json &view,const std::string &device,int periodIndex,int group){
 json result={{"available",false},{"groups",json::array()},{"daily",json::array()},{"providers",json::array()},{"deviceCount",0},{"sessionCount",0},{"sessionDetailsAvailable",periodIndex!=2}};
 if(!view.is_object()||!view.contains("periods"))return result;
 const char *names[]={"today","month","allTime"};const auto name=names[std::clamp(periodIndex,0,2)];json selected=view;
 if(device!="*"){
  bool found=false;for(const auto &d:view.value("devices",json::array()))if(d.value("deviceId",std::string())==device){selected=d;found=true;break;}
  if(!found||!selected.contains("periods"))return result;
 }
 auto p=object(selected,"periods").value(name,json::object());if(!p.is_object())return result;
 if(device!="*"&&periodIndex!=2){auto window=object(selected,"periodWindows").value(name,json::object());auto end=window.is_object()?parseUsageTime(window.value("endsAt",std::string())):0;if(end&&end<=utcNow()){p=json::object();result["periodExpired"]=true;}}
 result["available"]=true;result["totals"]=codexPeriod(p);result["updatedAt"]=selected.value("receivedAt",selected.value("updatedAt",std::string()));
 result["label"]=device=="*"?"全部设备":selected.value("hostname",std::string("设备"));
 auto history=object(selected,device=="*"?"historyPreview":"history");
 for(const auto &row:history.value("daily",json::array())){if(!row.is_object())continue;auto clients=object(row,"perClient");if(!clients.contains("codex")||!clients["codex"].is_object())continue;auto c=clients["codex"];
  result["daily"].push_back({{"date",row.value("date",std::string())},{"tokens",value(c,"tokens")},{"cost",value(c,"cost")},{"unpricedTokens",value(c,"unpricedTokens")}});
 }
 auto providers=object(selected,"limits").value("providers",json::array());if(providers.is_array())for(const auto &provider:providers)if(provider.is_object()&&provider.value("provider",std::string())=="codex")result["providers"].push_back(provider);
 auto sessions=object(p,"sessions");for(auto it=sessions.begin();it!=sessions.end();++it)if(it->is_object()&&it->value("client",std::string())=="codex")result["sessionCount"]=result["sessionCount"].get<int>()+1;
 auto append=[&](const std::string &key,const std::string &label,double tokens,double cost,bool known,const std::string &detail,const json &metadata=json::object()){
  result["groups"].push_back({{"key",key},{"label",label},{"tokens",tokens},{"cost",cost},{"costKnown",known},{"detail",detail},{"metadata",metadata}});
 };
 if(group==0){auto models=object(object(p,"clientModels"),"codex"),costs=object(object(p,"clientModelCosts"),"codex");
  for(auto it=models.begin();it!=models.end();++it)if(it->is_number())append(it.key(),it.key(),value(models,it.key()),value(costs,it.key()),costs.contains(it.key())&&!(value(costs,it.key())==0&&value(result["totals"],"unpricedTokens")>0),"Codex 模型");
 }else if(group==1){auto projects=object(p,"projects");for(auto it=projects.begin();it!=projects.end();++it){if(!it->is_object())continue;auto clients=object(*it,"clients");auto tokens=value(clients,"codex");if(tokens<=0)continue;double all=0;for(auto c=clients.begin();c!=clients.end();++c)all+=value(clients,c.key());bool known=all==tokens&&it->contains("costUsd")&&!(value(*it,"costUsd")==0&&value(*it,"unpricedTokens")>0);
  append(it.key(),it->value("label",it.key()),tokens,value(*it,"costUsd"),known,"已同步项目；多工具费用无法拆分时显示未知");
 }}else if(group==2){for(auto it=sessions.begin();it!=sessions.end();++it)if(it->is_object()&&it->value("client",std::string())=="codex"){
  auto id=it->value("sessionId",it.key());auto label="会话 "+id.substr(0,12);auto detail=it->value("projectLabel",std::string("项目未提供"));
  if(it->contains("latestModel")&&(*it)["latestModel"].is_string())detail+=" · "+(*it)["latestModel"].get<std::string>();
  append(it.key(),label,value(*it,"totalTokens"),value(*it,"costUsd"),it->contains("costUsd")&&!(value(*it,"costUsd")==0&&value(*it,"unpricedTokens")>0),detail,*it);
 }}else{
  for(const auto &d:view.value("devices",json::array())){if(device!="*"&&d.value("deviceId",std::string())!=device)continue;auto dp=object(d,"periods").value(name,json::object());auto total=codexPeriod(dp);
   append(d.value("deviceId",std::string()),d.value("hostname",std::string("设备")),value(total,"tokens"),value(total,"cost"),total.value("costKnown",false),d.value("osName",std::string())+" · "+d.value("agentVersion",std::string())+(d.value("stale",true)?" · 离线/旧数据":" · 已同步"));
  }
 }
 int devices=0;for(const auto &d:view.value("devices",json::array()))if(device=="*"||d.value("deviceId",std::string())==device)++devices;result["deviceCount"]=devices;
 std::sort(result["groups"].begin(),result["groups"].end(),[](const auto &a,const auto &b){return a["tokens"].template get<double>()>b["tokens"].template get<double>();});
 return result;
}
}
