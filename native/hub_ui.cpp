#include "app.hpp"
#include <wincred.h>
namespace quota {
void SettingsWindow::drawHub(Graphics &g,const GlassPalette &c){
 float w=float(contentWidth)-14;drawHeading(g,simulatedGlass?L"多设备同步 · 模拟数据":L"多设备同步",RectF(0,0,w,40),27,c.ink);
 drawUiText(g,L"与 Token Monitor 设备共享统计。应用后立即生效。",RectF(0,44,w,26),13,c.secondary);
 for(auto row:std::vector<std::pair<int,const wchar_t *>>{{84,L"同步方式"},{166,L"Hub 地址（连接模式）"},{248,L"监听端口（托管模式）"},{330,L"共享密钥（留空保留已保存密钥）"},{412,L"设备 ID（每台设备使用不同值）"}})drawUiText(g,row.second,RectF(0,float(row.first),w,25),14,c.ink);
 drawUiText(g,app->hub.status(),RectF(0,559,w,28),14,c.secondary);
 float y=604;if(app->hubConfig.mode==2){drawUiText(g,L"其他设备可使用以下地址连接",RectF(0,y,w,25),14,c.ink);y+=34;auto addresses=simulatedGlass?std::vector<std::wstring>{L"http://192.0.2.10:17321（模拟地址）"}:hubAddresses(app->hubConfig.port);int shown=0;for(const auto &address:addresses){drawUiText(g,address,RectF(0,y,w,25),13,c.secondary);y+=29;if(++shown>=3||y>700)break;}}
 auto view=app->hub.view();if(view.is_object()&&view.contains("devices")&&view["devices"].is_array()){drawUiText(g,L"已保存设备："+std::to_wstring(view["devices"].size()),RectF(0,y+16,w,25),14,c.ink);y+=52;int shown=0;for(const auto &d:view["devices"]){if(++shown>3)break;auto local=d.value("deviceId",std::string())==app->hubConfig.deviceId?L" · 本机":L"";drawUiText(g,wide(d.value("hostname",d.value("deviceId",std::string("设备"))))+local+(d.value("stale",false)?L" · 离线/旧数据":L" · 已同步"),RectF(0,y,w,24),13,c.secondary);y+=28;}}
 float base=900;
 if(view.is_object()&&view.contains("periods")){
  drawHeading(g,L"所有设备 · 已记录的用量",RectF(0,base,w,32),20,c.ink);base+=44;
  const wchar_t *labels[]={L"今日",L"本月",L"已保存累计"};int index=0;
  for(auto name:{"today","month","allTime"}){auto p=view["periods"].value(name,json::object());auto text=std::wstring(labels[index++])+L"    "+compactTokens(p.value("totalTokens",int64_t(0)))+L" Token    "+dollarAmount(p.value("costUsd",0.));drawUiText(g,text,RectF(0,base,w,28),15,c.ink);base+=37;}
  drawUiText(g,L"费用为标准 API 等价估算；未知价格不计入费用。",RectF(0,base,w,26),12,c.secondary);base+=34;
  auto history=view.value("historyPreview",json::object());bool monthly=GetPropW(window,L"HubHistoryMonthly")!=nullptr;auto tier=monthly?"monthly":"daily";
  drawHeading(g,monthly?L"已保存历史 · 月度":L"已保存历史 · 每日",RectF(0,base,w,30),18,c.ink);base+=80;
  if(history.contains(tier)&&history[tier].is_array()){const auto &rows=history[tier];auto pageNumber=size_t(reinterpret_cast<UINT_PTR>(GetPropW(window,L"HubHistoryPage")));size_t maximum=rows.empty()?0:(rows.size()-1)/7;pageNumber=std::min(pageNumber,maximum);SetPropW(window,L"HubHistoryPage",reinterpret_cast<HANDLE>(pageNumber));EnableWindow(GetDlgItem(content,709),pageNumber<maximum);EnableWindow(GetDlgItem(content,710),pageNumber>0);
   size_t end=rows.size()-std::min(rows.size(),pageNumber*7),start=end>7?end-7:0;
   for(size_t n=start;n<end;++n){const auto &row=rows[n];auto tokens=row.value("tokens",row.value("totalTokens",0.));drawUiText(g,wide(row.value(monthly?"month":"date",std::string()))+L"    "+compactTokens(int64_t(tokens))+L" Token    "+dollarAmount(row.value("cost",0.)),RectF(0,base,w,25),13,c.secondary);base+=29;}
   if(rows.empty()){drawUiText(g,L"尚无已保存的历史；这不代表实际用量为零。",RectF(0,base,w,25),13,c.secondary);base+=30;}}
  else{drawUiText(g,L"历史暂不可用；保留上次成功同步的统计。",RectF(0,base,w,25),13,c.secondary);base+=30;}

 }
 drawUiText(g,L"累计仅含已采集记录，首次使用不能补回已丢失的日志。\n同一会话在多台设备采集时会重复相加。",RectF(0,base+12,w,58),12,c.secondary);
 drawUiText(g,L"关闭设置后继续同步，退出额度工具会停止 Hub。\nHTTP 不加密传输；跨网络请使用可信 VPN 或 HTTPS Hub。",RectF(0,base+80,w,62),12,c.secondary);

}
void SettingsWindow::applyHub(){
 try{HubSettings next=hubDraft;next.mode=int(SendDlgItemMessageW(content,700,CB_GETCURSEL,0,0));wchar_t b[4096];GetDlgItemTextW(content,701,b,4096);next.url=utf8(b);if(next.mode==2){GetDlgItemTextW(content,702,b,4096);auto port=utf8(b);if(port.empty()||port.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid port");next.port=std::stoi(port);}GetDlgItemTextW(content,704,b,4096);next.deviceId=utf8(b);GetDlgItemTextW(content,703,b,4096);std::string secret=utf8(b);if(secret.empty()&&!app->testing&&!app->preview)secret=hubSecret();if(next.mode&&secret.size()<16)throw std::runtime_error("Secret too short");if(next.port<1||next.port>65535||next.deviceId.empty())throw std::runtime_error("Invalid port/device");
 if(app->testing||app->preview){hubDraft=next;InvalidateRect(content,nullptr,FALSE);return;}
 validateHubSettings(next,secret);
 auto previous=app->hubConfig;auto old=hubSecret();bool keySaved=false;
 try{app->hub.start(next,secret,dataDirectory());if(next.mode&&secret!=old){saveHubSecret(secret);keySaved=true;}next.save();}
 catch(...){if(keySaved){if(old.empty())CredDeleteW(L"CodexQuotaBar.SyncHub.Secret",CRED_TYPE_GENERIC,0);else try{saveHubSecret(old);}catch(...){}}
 try{app->hub.start(previous,old,dataDirectory());}catch(...){}throw;}
 app->hubConfig=hubDraft=next;SetDlgItemTextW(content,703,L"");InvalidateRect(content,nullptr,FALSE);
 }catch(...){MessageBoxW(window,L"无法应用同步设置。请检查Hub 地址、端口是否占用、设备 ID，以及至少 16 个字符的共享密钥。",L"多设备同步",MB_OK|MB_ICONERROR);}
}
}
