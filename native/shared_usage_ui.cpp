#include "app.hpp"
#include <windowsx.h>
namespace quota {
namespace {
double n(const json &j,const char *key){auto i=j.find(key);return i!=j.end()&&i->is_number()?i->get<double>():0;}
std::string text(const json &j,const char *key,const std::string &fallback={}){auto i=j.find(key);return i!=j.end()&&i->is_string()?i->get<std::string>():fallback;}
std::wstring tokens(double v){return compactTokens(int64_t(v));}
std::wstring money(const json &row){return row.value("costKnown",true)?dollarAmount(n(row,"cost"),n(row,"unpricedTokens")>0):L"未知";}
std::wstring quotaText(const json &provider){
 auto account=text(provider,"accountLabel");if(account.empty())account=text(provider,"planLabel","Codex");
 std::wstring out=wide(account);
 auto status=text(provider,"status");if(status!="ok")out+=L" · "+wide(status);
 for(const auto &window:provider.value("windows",json::array())){if(!window.is_object())continue;
  auto label=text(window,"label");if(label.empty())label=text(window,"kind","额度");out+=L"\n"+wide(label)+L"：";
  if(window.contains("remainingPercent")&&window["remainingPercent"].is_number())out+=std::to_wstring(int(std::round(n(window,"remainingPercent"))))+L"% 剩余";else out+=L"未提供剩余比例";
  auto reset=text(window,"resetsAt");if(!reset.empty())out+=L"，重置 "+timeLabel(parseUsageTime(reset));
 }
 auto stamp=text(provider,"updatedAt");if(!stamp.empty())out+=L"\n采样 "+timeLabel(parseUsageTime(stamp));return out;
}
}
void SettingsWindow::syncUsageSources(bool force){
 auto revision=app->hub.revision();if(!force&&revision==usageHubRevision)return;usageHubRevision=revision;
 auto view=app->hub.view();std::vector<std::string> ids={""};std::vector<std::wstring> labels={L"本机日志"};
 if(app->hubConfig.mode){ids.push_back("*");labels.push_back(L"全部设备");
  if(view.is_object())for(const auto &d:view.value("devices",json::array())){auto id=d.value("deviceId",std::string());if(id.empty())continue;ids.push_back(id);labels.push_back(wide(d.value("hostname",id))+(id==app->hubConfig.deviceId?L"（本机）":L""));}
 }
 if(usageSources.empty()&&app->hubConfig.mode)usageSource="*";
 if(std::find(ids.begin(),ids.end(),usageSource)==ids.end())usageSource=app->hubConfig.mode?"*":"";
 auto combo=GetDlgItem(content,UsageSource);bool rebuild=ids!=usageSources||SendMessageW(combo,CB_GETCOUNT,0,0)!=LRESULT(labels.size());
 if(!rebuild)for(size_t i=0;i<labels.size();++i){auto size=SendMessageW(combo,CB_GETLBTEXTLEN,i,0);if(size<0){rebuild=true;break;}std::wstring current(size_t(size)+1,L'\0');SendMessageW(combo,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(current.data()));current.resize(size_t(size));if(current!=labels[i]){rebuild=true;break;}}
 if(rebuild){SendMessageW(combo,CB_RESETCONTENT,0,0);for(const auto &label:labels)SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
 usageSources=ids;auto selected=std::find(ids.begin(),ids.end(),usageSource)-ids.begin();SendMessageW(combo,CB_SETCURSEL,selected,0);
 prepareSharedUsage();
 for(auto pair:std::vector<std::pair<int,const wchar_t *>>{{UsageSeven,usageSource.empty()?L"近 7 天":L"今日"},{UsageThirty,usageSource.empty()?L"近 30 天":L"本月"},{UsageAccounts,usageSource.empty()?L"账户":L"设备"},{UsageQuota,usageSource.empty()?L"额度详情":L"同步额度"}})SetDlgItemTextW(content,pair.first,pair.second);
 for(int id:{UsageHour,UsageDay,UsageExport,UsageImport})EnableWindow(GetDlgItem(content,id),usageSource.empty());
 if(!usageSource.empty())usageView.hourly=false;
 usageView.dirty=true;
}
void SettingsWindow::prepareSharedUsage(){
 if(usageSource.empty())return;sharedUsage=hubSharedUsage(app->hub.view(),usageSource,sharedPeriod,usageView.group);sharedRows=sharedUsage["groups"];
 usageView.groups.clear();for(const auto &row:sharedRows){UsageGroupRow group;group.key=row.value("key",std::string());group.title=wide(row.value("label",std::string()));usageView.groups.push_back(group);}
 usageView.groupPage=std::clamp(usageView.groupPage,0,std::max(0,(int(sharedRows.size())-1)/5));
}
void SettingsWindow::sharedUsageCommand(int id){
 if(id==UsageRefresh){app->hub.requestRefresh();return;}
 if(id==UsageQuota){std::wstring text;for(const auto &p:sharedUsage.value("providers",json::array())){if(!text.empty())text+=L"\n\n";text+=quotaText(p);}openHelp(L"同步的 Codex 账户额度",text.empty()?L"尚未收到此来源的额度采样。":text+L"\n\n按账户保留采样；设备额度百分比不相加。");return;}
 if(id==UsageSeven)sharedPeriod=0;else if(id==UsageThirty)sharedPeriod=1;else if(id==UsageAll)sharedPeriod=2;
 else if(id==UsageCost||id==UsageTokens)usageView.tokens=id==UsageTokens;
 else if(id>=UsageModels&&id<=UsageAccounts){usageView.group=id-UsageModels;usageView.groupPage=0;}
 else if(id==UsagePrevious||id==UsageNext)usageView.groupPage+=id==UsageNext?1:-1;
 else if(id==UsageClear){sharedPeriod=0;usageView.groupPage=0;}
 prepareSharedUsage();layout();InvalidateRect(content,nullptr,FALSE);
}
void SettingsWindow::sharedUsageClick(float x,float y){
 for(const auto &hit:usageView.hits)if(hit.action==23&&hit.rect.Contains(x,y)){
  auto index=size_t(hit.value);if(index>=sharedRows.size())return;const auto &row=sharedRows[index];auto metadata=row.value("metadata",json::object());
  std::wstring text=wide(row.value("label",std::string()))+L"\n"+wide(row.value("detail",std::string()))+L"\n\n已记录 "+tokens(n(row,"tokens"))+L" Token\n费用估算 "+money(row);
  for(auto field:std::vector<std::pair<const char *,const wchar_t *>>{{"contextTokens",L"上下文 Token"},{"contextWindow",L"上下文容量"},{"reasoningTokens",L"推理 Token"},{"messageCount",L"已记录消息"},{"timedDurationMs",L"计时毫秒"}})if(metadata.contains(field.first)&&metadata[field.first].is_number()&&n(metadata,field.first)>0)text+=L"\n"+std::wstring(field.second)+L"："+std::to_wstring(int64_t(n(metadata,field.first)));
  for(auto field:std::vector<std::pair<const char *,const wchar_t *>>{{"reasoningEffort",L"推理档位"},{"latestModel",L"最近模型"},{"startedAt",L"首次记录"},{"lastUsedAt",L"最近用量"}})if(metadata.contains(field.first)&&metadata[field.first].is_string()&&!metadata[field.first].get<std::string>().empty())text+=L"\n"+std::wstring(field.second)+L"："+wide(metadata[field.first].get<std::string>());
  if(metadata.contains("waitingForInput")&&metadata["waitingForInput"].is_boolean())text+=metadata["waitingForInput"].get<bool>()?L"\n正在等待输入":L"\n未标记等待输入";
  if(metadata.contains("turnEnded")&&metadata["turnEnded"].is_boolean())text+=metadata["turnEnded"].get<bool>()?L"\n本轮已结束":L"\n本轮未结束";
  openHelp(L"共享用量明细",text+L"\n\n仅显示来源已提供的统计；缺失字段不代表零。");return;
 }
}
void SettingsWindow::drawSharedUsage(Graphics &g,const GlassPalette &c){
 usageView.hits.clear();float w=float(contentWidth)-14;auto green=parseColor(draft.hourColor,c.focus),blue=parseColor(draft.weekColor,c.focus);
 drawHeading(g,L"Codex 用量",RectF(0,0,w-220,38),27,c.ink);
 auto received=sharedUsage.value("updatedAt",std::string());auto caption=received.empty()?L"等待共享统计":(usageSource=="*"?L"接收 ":L"上报 ")+timeLabel(parseUsageTime(received));
 drawUiText(g,caption+((simulatedGlass||app->testing||app->preview)?L" · 模拟数据":L""),RectF(234,42,w-234,24),13,c.secondary);
 if(!sharedUsage.value("available",false)){drawUiText(g,L"尚未收到此来源的统计。请检查“多设备同步”的连接状态。",RectF(0,132,w,60),14,c.secondary);return;}
 auto total=sharedUsage["totals"];bool expired=sharedUsage.value("periodExpired",false);const wchar_t *labels[]={L"API 等价费用估算",L"Codex Token",L"已同步会话"};std::wstring values[]={expired?L"—":money(total),expired?L"等待上报":tokens(n(total,"tokens")),!expired&&sharedUsage.value("sessionDetailsAvailable",false)?std::to_wstring(sharedUsage.value("sessionCount",0)):L"—"};
 for(int i=0;i<3;++i){drawUiText(g,labels[i],RectF(w*i/3,128,w/3-10,22),12,c.secondary);drawUiText(g,values[i],RectF(w*i/3,150,w/3-10,43),29,c.ink,true);}
 auto known=total.value("componentsKnown",false);
 std::wstring mix[]={L"缓存输入 "+(known?tokens(n(total,"cache")):L"未知"),L"输出 "+(known?tokens(n(total,"output")):L"未知"),L"设备 "+std::to_wstring(sharedUsage.value("deviceCount",0)),L"统计周期 "+std::wstring(sharedPeriod==0?L"今日":sharedPeriod==1?L"本月":L"累计")};
 for(int i=0;i<4;++i)drawUiText(g,mix[i],RectF(w*i/4,202,w/4-4,25),11.5f,c.secondary);
 Pen border(c.border,1);g.DrawLine(&border,0.f,235.f,w,235.f);drawHeading(g,L"每日共享用量",RectF(0,247,200,29),16,c.ink);
 drawUiText(g,L"最近 30 个已记录日期 · 各设备上报的日期口径",RectF(0,284,w,22),11,c.secondary);
 auto daily=sharedUsage["daily"];if(daily.size()>30)daily.erase(daily.begin(),daily.end()-30);
 double maxTokens=1,maxCost=1;for(const auto &d:daily){maxTokens=std::max(maxTokens,n(d,"tokens"));maxCost=std::max(maxCost,n(d,"cost"));}
 RectF plot(43,316,w-51,132);for(int i=0;i<3;++i){float y=plot.GetBottom()-plot.Height*i/2;g.DrawLine(&border,plot.X,y,plot.GetRight(),y);drawUiText(g,tokens(maxTokens*i/2),RectF(0,y-10,38,20),10.5f,c.muted,false,true);}
 std::vector<PointF> tokenPoints,costPoints;std::vector<std::vector<PointF>> costSegments;float step=daily.empty()?0:plot.Width/daily.size();
 for(size_t i=0;i<daily.size();++i){const auto &d=daily[i];float x=plot.X+step*(i+.5f);tokenPoints.emplace_back(x,plot.GetBottom()-plot.Height*float(n(d,"tokens")/maxTokens));if(n(d,"unpricedTokens")==0)costPoints.emplace_back(x,plot.GetBottom()-plot.Height*float(n(d,"cost")/maxCost));else if(!costPoints.empty()){costSegments.push_back(std::move(costPoints));costPoints.clear();}
  usageView.hits.push_back({RectF(x-step/2,plot.Y,step,plot.Height+24),20,int64_t(i),{}});
  if(i%std::max<size_t>(1,(daily.size()+4)/5)==0||i+1==daily.size())drawUiText(g,wide(d.value("date",std::string()).substr(5)),RectF(x-27,plot.GetBottom()+8,54,20),10,c.secondary,false,true);
 }
 if(!costPoints.empty())costSegments.push_back(std::move(costPoints));
 if(tokenPoints.size()>1){Pen line(blue,2.2f);g.DrawLines(&line,tokenPoints.data(),int(tokenPoints.size()));if(!usageView.tokens){Pen cost(green,2);for(const auto &points:costSegments)if(points.size()>1)g.DrawLines(&cost,points.data(),int(points.size()));}}
 else if(tokenPoints.size()==1){SolidBrush brush(blue);g.FillEllipse(&brush,tokenPoints[0].X-3,tokenPoints[0].Y-3,6.f,6.f);}
 drawUiText(g,usageView.tokens?L"蓝线：Token":L"蓝线：Token · 绿线：费用（独立刻度）",RectF(43,480,w-43,22),11,c.secondary);
 if(usageView.hovered>=0&&usageView.hovered<int(daily.size())){auto row=daily[size_t(usageView.hovered)];auto cost=n(row,"unpricedTokens")>0&&n(row,"cost")==0?L"价格未知":dollarAmount(n(row,"cost"),n(row,"unpricedTokens")>0);drawUiText(g,wide(row.value("date",std::string()))+L" · "+tokens(n(row,"tokens"))+L" Token · "+cost,RectF(43,451,w-43,22),11,c.ink);}
 g.DrawLine(&border,0.f,516.f,w,516.f);drawHeading(g,L"同步的账户额度",RectF(0,528,w,27),16,c.ink);
 auto providers=sharedUsage["providers"];int shown=0;
 for(const auto &provider:providers){if(shown==2)break;float y=567+shown*68;auto label=text(provider,"accountLabel");if(label.empty())label=text(provider,"planLabel","Codex");
  drawUiText(g,wide(label)+(provider.value("stale",false)?L" · 旧采样":L""),RectF(0,y,w,21),12,c.secondary);
  std::wstring summary;for(const auto &window:provider.value("windows",json::array())){if(!window.is_object())continue;if(!summary.empty())summary+=L"    ";auto kind=text(window,"kind"),label=text(window,"label");summary+=(label.empty()?(kind=="session"?L"会话额度":kind=="weekly"?L"每周额度":wide(kind)):wide(label))+L" ";summary+=window.contains("remainingPercent")&&window["remainingPercent"].is_number()?std::to_wstring(int(std::round(n(window,"remainingPercent"))))+L"% 剩余":L"未知";}
  drawUiText(g,summary,RectF(0,y+23,w,25),14,c.ink);++shown;
 }
 if(!shown)drawUiText(g,L"此来源尚未提供额度采样。",RectF(0,575,w,28),13,c.secondary);
 drawUiText(g,L"按账户区分，百分比不相加；“同步额度”可查看全部采样。",RectF(0,687,w,21),11,c.secondary);
 const wchar_t *groupLabels[]={L"模型",L"项目",L"已同步会话",L"设备"};drawUiText(g,groupLabels[std::clamp(usageView.group,0,3)],RectF(0,775,w*.5f,24),11,c.secondary);drawUiText(g,L"Token",RectF(w*.57f,775,w*.18f,24),11,c.secondary,false,true);drawUiText(g,L"估算费用",RectF(w*.78f,775,w*.22f,24),11,c.secondary,false,true);
 size_t first=std::min(sharedRows.size(),size_t(usageView.groupPage)*5),count=std::min<size_t>(5,sharedRows.size()-first);float y=802;
 for(size_t i=0;i<count;++i){const auto &row=sharedRows[first+i];drawUiText(g,wide(row.value("label",std::string())),RectF(0,y,w*.55f,25),14,c.ink,true);drawUiText(g,wide(row.value("detail",std::string())),RectF(0,y+26,w*.55f,21),10.5f,c.secondary);drawUiText(g,tokens(n(row,"tokens")),RectF(w*.57f,y,w*.18f,28),14,c.ink,false,true);drawUiText(g,money(row),RectF(w*.78f,y,w*.22f,28),14,c.ink,false,true);usageView.hits.push_back({RectF(0,y,w,53),23,int64_t(first+i),row.value("key",std::string())});y+=57;}
 if(!count){drawUiText(g,usageView.group==2?L"此周期没有同步的会话明细；可切换“今日”或“本月”。":L"此来源尚未提供该分组明细。",RectF(0,y,w,46),13,c.secondary);y+=88;}
 if(sharedRows.size()>5)y+=48;
 drawUiText(g,L"共享统计仅含已采集记录；不同设备复制的同一日志仍可能重复计数。",RectF(0,y+18,w,24),11,c.secondary);
 drawUiText(g,L"费用为标准 API 等价估算，非订阅实际扣费。",RectF(0,y+44,w,24),11,c.secondary);
 drawUiText(g,app->hub.status(),RectF(0,y+68,w,24),11,c.secondary);
}
}
