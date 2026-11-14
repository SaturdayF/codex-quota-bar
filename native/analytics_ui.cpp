#include "app.hpp"
#include <windowsx.h>
namespace quota {
namespace {
std::wstring projectName(const std::string &path) {
    if(path.empty())return L"项目未知";
    auto p=fs::path(wide(path)).lexically_normal();auto name=p.filename().wstring();return name.empty()?p.wstring():name;
}
std::wstring costText(const UsageTotal &t) { return t.unpriced&&t.cost==0?L"价格未知":dollarAmount(t.cost,t.unpriced>0); }
void divider(Graphics &g,const GlassPalette &c,float y,float width) { Pen p(c.border,1);g.DrawLine(&p,0.f,y,width,y); }
void quietButton(Graphics &g,RectF r,const std::wstring &label,const GlassPalette &c,bool selected=false,float size=13) {
    fillRound(g,r,6,selected?c.active:c.hover);drawUiText(g,label,r,size,c.ink,selected,true);
}
std::wstring windowValue(const WindowQuota &q) {
    if(!q.minutes)return L"--";std::wostringstream s;s<<std::fixed<<std::setprecision(0)<<q.remaining<<L"%";return s.str();
}
} // namespace

void UsageView::prepare(const UsageReport &report) {
    if(!dirty)return;dirty=false;groups.clear();bins.clear();heat.clear();total={};coverageStart=0;
    int64_t today=localDay(report.updated?report.updated:utcNow());
    auto begin=selectedDay?selectedDay:shiftLocalDay(today,1-days),end=shiftLocalDay(selectedDay?selectedDay:today,1);
    std::map<std::string,UsageGroupRow> map;
    std::map<std::string,int> accounts;for(const auto &r:report.entries)if(!r.account.empty())accounts.emplace(r.account,0);
    int number=0;for(auto &a:accounts)a.second=++number;
    if(hourly){for(int h=0;h<24;++h)bins.push_back({h,std::to_wstring(h)+L":00",{}});}
    else for(auto at=begin;at<end;at=shiftLocalDay(at,1))bins.push_back({at,usageDate(at),{}});
    for(const auto &r:report.entries){
        auto day=localDay(r.at);auto &count=heat[day];if(count<=INT64_MAX-r.tokens.total())count+=r.tokens.total();
        if(!coverageStart||day<coverageStart)coverageStart=day;
        if(r.at<begin||r.at>=end||(!project.empty()&&r.project!=project))continue;
        total.add(r);
        if(hourly){if(day==(selectedDay?selectedDay:today)){time_t at=r.at;tm t{};localtime_s(&t,&at);bins[size_t(t.tm_hour)].total.add(r);}}
        else {auto it=std::lower_bound(bins.begin(),bins.end(),day,[](const auto &a,int64_t b){return a.at<b;});if(it!=bins.end()&&it->at==day)it->total.add(r);}
        std::string key=group==0?r.model:group==1?r.project:group==2?r.session:r.account;
        auto &row=map[key];row.key=key;row.total.add(r);
        if(group==0)row.title=key.empty()?L"模型未知":wide(key);
        else if(group==1){row.title=projectName(key);row.subtitle=key.empty()?L"本地记录未提供项目目录":wide(key);}
        else if(group==2){auto title=report.titles.find(key);row.title=title==report.titles.end()?L"会话 "+wide(key.substr(0,12)):wide(title->second);row.subtitle=projectName(r.project)+L" · "+wide(r.model)+L" · "+usageDate(r.at);}
        else {row.title=key.empty()?L"归属未知":L"账户 "+std::to_wstring(accounts[key]);row.subtitle=key.empty()?L"记录未提供可验证的账户归属":L"按日志中的账户标识归集";}
    }
    for(auto &p:map)groups.push_back(std::move(p.second));
    std::sort(groups.begin(),groups.end(),[](const auto &a,const auto &b){return a.total.cost!=b.total.cost?a.total.cost>b.total.cost:a.total.tokens.total()>b.total.tokens.total();});
}

void SettingsWindow::startUsageScan() {
    if(usageTask.valid())return;
    if(simulatedGlass){usageReport=demoUsageReport(utcNow());usageView.dirty=true;return;}
    try {
    usageCancel=false;
    UsageScanOptions options;options.home=codexHome();options.cache=dataDirectory()/L"codex-usage-cache.json";
    usageTask=std::async(std::launch::async,[this,options]{return scanCodexUsage(options,usageCancel);});
    usageNextScan=GetTickCount64()+120000;
    EnableWindow(GetDlgItem(content,UsageRefresh),FALSE);
    } catch (...) { usageReport.error=L"无法启动本地用量读取，请检查目录权限后重试。";usageNextScan=GetTickCount64()+120000; }
    InvalidateRect(content,nullptr,FALSE);
}
void SettingsWindow::pollUsageScan() {
    if(usageTask.valid()&&usageTask.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try{auto report=usageTask.get();if(!usageCancel){usageReport=std::move(report);usageView.dirty=true;usageNextScan=GetTickCount64()+(usageReport.pending?1500:120000);}}
        catch(...){usageReport.error=L"无法读取本地用量，已保留上次统计。请检查日志目录权限后重试。";usageNextScan=GetTickCount64()+120000;}
        EnableWindow(GetDlgItem(content,UsageRefresh),TRUE);if(page==4)layout();InvalidateRect(content,nullptr,FALSE);
    }
    if(page==4&&!closing&&!usageTask.valid()&&usageNextScan&&GetTickCount64()>=usageNextScan)startUsageScan();
}
void SettingsWindow::usageCommand(int id) {
    if(id==UsageRefresh){startUsageScan();return;}
    if(id==UsageQuota){if(!quotaPopup)quotaPopup=std::make_unique<QuotaPopup>(app,window,draft);quotaPopup->settings=draft;quotaPopup->reveal();return;}
    if(id==UsagePrevious||id==UsageNext){usageView.groupPage+=id==UsageNext?1:-1;usageView.keyboard=-1;layout();InvalidateRect(content,nullptr,FALSE);return;}
    if(id==UsageSeven||id==UsageThirty){usageView.days=id==UsageSeven?7:30;usageView.selectedDay=0;usageView.project.clear();}
    if(id==UsageCost||id==UsageTokens)usageView.tokens=id==UsageTokens;
    if(id==UsageDay||id==UsageHour)usageView.hourly=id==UsageHour;
    if(id>=UsageModels&&id<=UsageAccounts){usageView.group=id-UsageModels;usageView.project.clear();}
    if(id==UsageClear){usageView.selectedDay=0;usageView.project.clear();}
    usageView.dirty=true;usageView.hovered=-1;usageView.keyboard=-1;usageView.groupPage=0;
    for(auto ctrl:controls){int cid=GetDlgCtrlID(ctrl);if(cid>=UsageSeven&&cid<=UsageNext)InvalidateRect(ctrl,nullptr,TRUE);}
    layout();
    InvalidateRect(content,nullptr,FALSE);
}
void SettingsWindow::usageClick(float x,float y) {
    for(const auto &hit:usageView.hits)if(hit.rect.Contains(x,y)){
        if(hit.action==20){usageView.hovered=int(hit.value);InvalidateRect(content,nullptr,FALSE);return;}
        if(hit.action==21){usageView.selectedDay=usageView.selectedDay==hit.value?0:hit.value;usageView.project.clear();usageView.dirty=true;usageView.hovered=-1;layout();InvalidateRect(content,nullptr,FALSE);return;}
        if(hit.action==22){
            if(usageView.group==1){usageView.project=hit.key;usageView.group=2;usageView.groupPage=0;usageView.dirty=true;for(int id=UsageModels;id<=UsageAccounts;++id)InvalidateRect(GetDlgItem(content,id),nullptr,TRUE);layout();InvalidateRect(content,nullptr,FALSE);return;}
            auto found=std::find_if(usageView.groups.begin(),usageView.groups.end(),[&](const auto &r){return r.key==hit.key;});
            if(found==usageView.groups.end())return;
            const auto &t=found->total;
            auto message=found->title+L"\n"+found->subtitle+L"\n\n输入 "+compactTokens(t.tokens.input)+L"（缓存 "+compactTokens(t.tokens.cached)+L"）\n输出 "+compactTokens(t.tokens.output)+L"（推理 "+compactTokens(t.tokens.reasoning)+L"）\n费用估算 "+costText(t)+L"\n\n按已记录的本地用量估算，非订阅实际扣费。";
            openHelp(usageView.group==2?L"会话用量":L"用量明细",message);return;
        }
    }
}
void SettingsWindow::usageHover(float x,float y) {
    int hovered=-1;for(const auto &hit:usageView.hits)if(hit.action==20&&hit.rect.Contains(x,y)){hovered=int(hit.value);break;}
    if(hovered!=usageView.hovered){usageView.hovered=hovered;InvalidateRect(content,nullptr,FALSE);}
}
void SettingsWindow::drawUsage(Graphics &g,const GlassPalette &c) {
    usageView.prepare(usageReport);usageView.hits.clear();float w=float(contentWidth)-14;
    Color green=parseColor(draft.hourColor,Color(255,33,158,134)),blue=parseColor(draft.weekColor,Color(255,100,141,230));
    drawHeading(g,L"Codex 用量",RectF(0,0,w-220,38),27,c.ink);
    auto today=localDay(usageReport.updated?usageReport.updated:utcNow());
    auto begin=usageView.selectedDay?usageView.selectedDay:shiftLocalDay(today,1-usageView.days);
    auto range=usageDate(begin)+L" – "+usageDate(usageView.selectedDay?usageView.selectedDay:today);
    drawUiText(g,L"本机日志 · "+range+((simulatedGlass||app->testing||app->preview)?L" · 模拟数据":L""),RectF(0,42,w,24),13,c.secondary);
    drawUiText(g,usageTask.valid()?L"后台读取中…":usageReport.partial?L"记录覆盖不完整":L"本地估算",RectF(w-190,84,188,26),12,usageReport.partial?c.warning:c.secondary,false,true);
    const auto &t=usageView.total;float col=w/3;
    const wchar_t *labels[]={L"API 等价费用估算",L"已记录 Token",L"会话"};
    std::wstring values[]={costText(t),t.overflow?L"无法汇总":compactTokens(t.tokens.total()),std::to_wstring(t.sessions.size())};
    for(int i=0;i<3;++i){drawUiText(g,labels[i],RectF(col*i,128,col-10,22),12,c.secondary);drawUiText(g,values[i],RectF(col*i,150,col-10,43),29,c.ink,true);}
    const std::wstring mix[]={L"输入 "+compactTokens(t.tokens.input),L"缓存输入 "+compactTokens(t.tokens.cached),L"输出 "+compactTokens(t.tokens.output),L"推理 "+compactTokens(t.tokens.reasoning)};
    for(int i=0;i<4;++i)drawUiText(g,mix[i],RectF(w/4*i,202,w/4-4,25),11.5f,c.secondary);
    divider(g,c,235,w);drawHeading(g,L"用量趋势",RectF(0,247,130,29),16,c.ink);
    auto unit=usageView.tokens?L"Token · 输入 + 输出":L"USD · 标准 API 等价估算";
    if(usageView.hourly)unit=usageView.tokens?L"所选日期每小时 Token":L"所选日期每小时费用估算";
    drawUiText(g,unit,RectF(0,284,w,22),11,c.secondary);
    RectF plot(43,316,w-51,132);double maxValue=0;
    for(const auto &b:usageView.bins)maxValue=std::max(maxValue,usageView.tokens?double(b.total.tokens.total()):b.total.cost);
    double ceiling=usageView.tokens?std::max(1000.,std::ceil(maxValue/100000)*100000):std::max(1.,std::ceil(maxValue));
    Pen grid(c.border,1);
    for(int i=0;i<3;++i){float y=plot.GetBottom()-plot.Height*i/2;g.DrawLine(&grid,plot.X,y,plot.GetRight(),y);auto value=usageView.tokens?compactTokens(int64_t(ceiling*i/2)):dollarAmount(ceiling*i/2);drawUiText(g,value,RectF(0,y-10,38,20),10.5f,c.muted,false,true);}
    if(!usageView.bins.empty()){
        float step=plot.Width/usageView.bins.size(),bw=std::min(32.f,step*.64f);
        int labelsEvery=usageView.bins.size()<=7?1:usageView.hourly?6:int(std::ceil(usageView.bins.size()/5.));
        for(size_t i=0;i<usageView.bins.size();++i){const auto &b=usageView.bins[i];double value=usageView.tokens?double(b.total.tokens.total()):b.total.cost;float x=plot.X+step*i+(step-bw)/2,h=float(value/ceiling*plot.Height);
            if(h>0)fillRound(g,RectF(x,plot.GetBottom()-h,bw,std::max(1.f,h)),2,int(i)==usageView.hovered?blue:green);
            else if(!usageView.tokens&&b.total.unpriced){SolidBrush brush(c.muted);g.FillEllipse(&brush,x+bw/2-2,plot.GetBottom()-4,4.f,4.f);}
            usageView.hits.push_back({RectF(plot.X+step*i,plot.Y,step,plot.Height+22),20,int64_t(i),{}});
            if(int(i)%labelsEvery==0||i+1==usageView.bins.size())drawUiText(g,b.label,RectF(plot.X+step*i-12,plot.GetBottom()+8,step+24,20),10.5f,c.secondary,false,true);
        }
    }
    std::wstring inspected=usageView.hourly?usageDate(usageView.selectedDay?usageView.selectedDay:today)+L" · 每小时明细":L"移动到柱形上查看数值";
    if(usageView.hovered>=0&&usageView.hovered<int(usageView.bins.size())){const auto &b=usageView.bins[size_t(usageView.hovered)];inspected=b.label+L" · "+costText(b.total)+L" · "+compactTokens(b.total.tokens.total())+L" Token";}
    drawUiText(g,inspected,RectF(43,480,w-43,22),11,c.secondary);
    divider(g,c,516,w);drawHeading(g,L"Token 活跃度",RectF(0,528,200,27),16,c.ink);drawUiText(g,L"过去一年 · 本地记录",RectF(w-200,530,200,24),11,c.secondary,false,true);
    constexpr int weeks=53;float gap=3,cw=std::max(3.f,(w-28-gap*(weeks-1))/weeks),ch=std::min(10.f,cw);float heatY=586;
    time_t at=today;tm tmNow{};localtime_s(&tmNow,&at);int weekday=(tmNow.tm_wday+6)%7;
    auto first=shiftLocalDay(today,-weekday-52*7);int64_t maxHeat=1;for(const auto &h:usageView.heat)maxHeat=std::max(maxHeat,h.second);
    int lastMonth=-1;
    for(int week=0;week<weeks;++week){auto monday=shiftLocalDay(first,week*7);time_t m=monday;tm date{};localtime_s(&date,&m);
        if(date.tm_mon!=lastMonth){if(week<weeks-2)drawUiText(g,std::to_wstring(date.tm_mon+1)+L"月",RectF(28+week*(cw+gap),564,42,18),10.5f,c.secondary);lastMonth=date.tm_mon;}
        for(int d=0;d<7;++d){auto day=shiftLocalDay(monday,d);if(day>today)continue;RectF cell(28+week*(cw+gap),heatY+d*(ch+gap),cw,ch);
            auto found=usageView.heat.find(day);int64_t count=found==usageView.heat.end()?0:found->second;
            bool known=usageView.coverageStart&&day>=usageView.coverageStart&&(!usageReport.partial||count>0);
            Color fill=c.hover;if(count>0){int alpha=55+int(200*std::sqrt(double(count)/maxHeat));fill=Color(BYTE(alpha),green.GetR(),green.GetG(),green.GetB());}
            fillRound(g,cell,1.5f,fill);
            if(!known){Pen slash(c.border,1);g.DrawLine(&slash,cell.X,cell.GetBottom(),cell.GetRight(),cell.Y);}
            else usageView.hits.push_back({cell,21,day,{}});
            if(day==usageView.selectedDay){Pen focus(blue,1.6f);g.DrawRectangle(&focus,cell);}
        }
    }
    for(int d:{0,2,4})drawUiText(g,d==0?L"一":d==2?L"三":L"五",RectF(0,heatY+d*(ch+gap)-4,20,20),10.5f,c.secondary);
    float heatEnd=heatY+7*(ch+gap);
    auto heatNote=usageView.selectedDay?usageDate(usageView.selectedDay)+L" · "+compactTokens(usageView.heat[usageView.selectedDay])+L" Token":L"点击日期查看当日统计；斜线表示未覆盖。";
    drawUiText(g,heatNote,RectF(0,heatEnd+7,w,24),11,c.secondary);
    divider(g,c,710,w);
    float rowY=802;
    auto groupLabel=!usageView.project.empty()?projectName(usageView.project)+L" · 会话":usageView.group==0?L"模型":usageView.group==1?L"项目":usageView.group==2?L"会话":L"账户归属";
    drawUiText(g,groupLabel,RectF(0,775,w*.5f,24),11,c.secondary);
    drawUiText(g,L"Token",RectF(w*.57f,775,w*.18f,24),11,c.secondary,false,true);drawUiText(g,L"估算费用",RectF(w*.78f,775,w*.22f,24),11,c.secondary,false,true);
    usageView.groupPage=std::clamp(usageView.groupPage,0,std::max(0,(int(usageView.groups.size())-1)/5));
    size_t groupFirst=size_t(usageView.groupPage)*5,shown=std::min<size_t>(5,usageView.groups.size()-groupFirst);
    for(size_t n=0;n<shown;++n){size_t i=groupFirst+n;const auto &row=usageView.groups[i];divider(g,c,rowY-3,w);
        if(usageView.keyboard==int(i))fillRound(g,RectF(0,rowY-2,w,55),4,c.hover);
        drawUiText(g,row.title,RectF(0,rowY+2,w*.55f,24),13,c.ink);
        auto sub=row.subtitle.empty()?std::to_wstring(row.total.sessions.size())+L" 个会话":row.subtitle;
        drawUiText(g,sub,RectF(0,rowY+27,w*.55f,20),10.5f,c.secondary);
        drawUiText(g,compactTokens(row.total.tokens.total()),RectF(w*.57f,rowY+12,w*.18f,25),12,c.ink,false,true);
        drawUiText(g,costText(row.total),RectF(w*.78f,rowY+12,w*.22f,25),12,c.ink,false,true);
        usageView.hits.push_back({RectF(0,rowY,w,54),22,int64_t(i),row.key});rowY+=57;
    }
    if(!shown){drawUiText(g,usageTask.valid()?L"正在读取本地会话日志…":usageReport.error.empty()?L"所选日期没有可统计的本地记录。":usageReport.error,RectF(0,823,w,60),14,c.secondary,false,true);rowY=900;}
    if(usageView.groups.size()>5)drawUiText(g,L"第 "+std::to_wstring(usageView.groupPage+1)+L" / "+std::to_wstring((usageView.groups.size()+4)/5)+L" 页 · 共 "+std::to_wstring(usageView.groups.size())+L" 项",RectF(0,1102,w-210,24),11,c.secondary);
    rowY=std::max(rowY,1140.f);
    if(usageReport.partial||!usageReport.error.empty()||t.unpriced){
        auto status=!usageReport.error.empty()?usageReport.error:usageReport.pending?L"正在补齐本地历史，还有 "+std::to_wstring(usageReport.pending)+L" 个文件。":t.unpriced?L"部分记录价格未知，费用仅包含已定价部分。":L"部分记录无法完整解析，统计可能不完整。";
        drawUiText(g,status,RectF(0,rowY+28,w,40),11,c.warning);
    }
    divider(g,c,rowY+79,w);drawUiText(g,L"本地记录的标准 API 等价估算，非订阅实际扣费。",RectF(0,rowY+89,w,22),11,c.secondary);
    drawUiText(g,L"缓存为输入子集，推理为输出子集。单价核对：2026-11-14。",RectF(0,rowY+112,w,22),11,c.secondary);
}

int quotaCardHeight(const State &s,const Settings &settings,int maximum,bool history) {
    return std::min(maximum,std::max(history?440:0,170+100*settings.visibility(s.q).count()+int(s.q.extraWindows.size())*68));
}
void paintQuotaCard(Graphics &g,float width,float height,const State &s,const Settings &settings,const QuotaHistory &history,QuotaCardView &view,bool simulated) {
    const auto &c=glassPalette(settings.darkAppearance);Color green=parseColor(settings.hourColor,Color(255,33,158,134)),blue=parseColor(settings.weekColor,Color(255,100,141,230));
    view.hits.clear();RectF close(width-40,12,24,25);view.hits.push_back({close,5});
    float half=(width-40)/2;RectF current(20,50,half,29),past(20+half,50,half,29);
    quietButton(g,current,L"当前额度",c,!view.history,12);quietButton(g,past,L"额度历史",c,view.history,12);view.hits.push_back({current,1});view.hits.push_back({past,2});
    auto visibility=settings.visibility(s.q);auto windows=allQuotaWindows(s.q,visibility.hour,visibility.week);view.windows=windows;
    if(!view.history){
        float y=94;
        auto meter=[&](const std::wstring &label,WindowQuota q,Color accent,float yy,bool extra){
            drawUiText(g,label,RectF(20,yy,width-130,25),extra?13:16,c.ink,!extra);
            drawUiText(g,windowValue(q),RectF(width-112,yy-3,92,31),extra?20:24,s.stale()?c.secondary:accent,true,true);
            fillRound(g,RectF(20,yy+32,width-40,extra?4.f:5.f),2,c.active);
            if(q.minutes&&q.remaining>0)fillRound(g,RectF(20,yy+32,(width-40)*float(q.remaining/100),extra?4.f:5.f),2,s.stale()?c.muted:accent);
            drawUiText(g,L"重置 "+timeLabel(q.resets),RectF(20,yy+43,width-40,21),11,c.secondary);
        };
        if(visibility.hour){auto q=s.q.hour.value_or(WindowQuota{});if(s.q.hour)q.minutes=300;meter(L"5 小时",q,green,y,false);y+=100;}
        if(visibility.week){auto q=s.q.week.value_or(WindowQuota{});if(s.q.week)q.minutes=10080;meter(L"每周",q,blue,y,false);y+=100;}
        int capacity=std::max(0,int((height-76-y+1)/68));
        if(!s.q.extraWindows.empty()){
            view.extraScroll=std::clamp(view.extraScroll,0,std::max(0,int(s.q.extraWindows.size())-capacity));
            for(int i=view.extraScroll;i<int(s.q.extraWindows.size())&&i<view.extraScroll+capacity;++i){const auto &extra=s.q.extraWindows[size_t(i)];meter(wide(extra.label),extra.window,i%2?blue:green,y,true);y+=68;}
            if(capacity<int(s.q.extraWindows.size()))drawUiText(g,L"滚轮查看其他额度",RectF(20,height-77,width-40,20),10,c.secondary);
        }
    }else{
        if(windows.empty()){drawUiText(g,L"尚无可用额度窗口",RectF(20,100,width-40,80),14,c.secondary,false,true);}
        else{
            view.selected=std::clamp(view.selected,0,int(windows.size())-1);const auto &selected=windows[size_t(view.selected)];
            RectF picker(width-187,92,167,29);quietButton(g,picker,wide(selected.label)+L"  ▾",c,false,12);view.hits.push_back({picker,4});
            drawUiText(g,L"额度窗口",RectF(20,93,100,27),12,c.secondary);
            drawUiText(g,L"剩余额度",RectF(20,133,width-130,29),13,c.ink);drawUiText(g,windowValue(selected.window),RectF(width-112,130,92,36),26,blue,true,true);
            auto samples=history.series(s.q,selected.id);
            RectF plot(47,184,width-68,std::max(36.f,height-330));
            if(samples.empty()||s.q.accountKey.empty())drawUiText(g,s.q.accountKey.empty()?L"等待确认账户后记录历史":L"开始记录额度历史\n读取到额度后逐步积累采样。",RectF(20,183,width-40,plot.Height+22),13,c.secondary,false,true);
            else{
                Pen grid(c.border,1);for(int i=0;i<3;++i){float yy=plot.GetBottom()-plot.Height*i/2;g.DrawLine(&grid,plot.X,yy,plot.GetRight(),yy);drawUiText(g,std::to_wstring(i*50)+L"%",RectF(13,yy-10,31,20),10,c.secondary,false,true);}
                Pen guide(c.muted,1);guide.SetDashStyle(DashStyleDash);g.DrawLine(&guide,plot.X,plot.Y,plot.GetRight(),plot.GetBottom());
                std::vector<PointF> points;auto end=selected.window.resets,start=end-int64_t(selected.window.minutes)*60;
                for(const auto &p:samples){float x=plot.X+plot.Width*float(double(p.at-start)/std::max<int64_t>(1,end-start));float y=plot.Y+plot.Height*float(1-p.remaining/100);points.emplace_back(x,y);}
                Pen line(blue,2.2f);if(points.size()>1)g.DrawLines(&line,points.data(),INT(points.size()));SolidBrush dot(blue);for(const auto &p:points)g.FillEllipse(&dot,p.X-2,p.Y-2,4.f,4.f);
                drawUiText(g,selected.window.minutes<=300?timeLabel(start,false).substr(0,5):usageDate(start),RectF(plot.X-4,plot.GetBottom()+6,95,20),10,c.secondary);
                drawUiText(g,selected.window.minutes<=300?timeLabel(end,false).substr(0,5):usageDate(end),RectF(plot.GetRight()-90,plot.GetBottom()+6,95,20),10,c.secondary,false,true);
                drawUiText(g,L"采样 "+timeLabel(samples.back().at)+L" · 实线止于采样时刻",RectF(20,height-111,width-40,21),10,c.secondary);
            }
            drawUiText(g,L"实线：已记录额度   虚线：均匀消耗参考",RectF(20,height-91,width-40,20),10,c.secondary);
        }
    }
    RectF open(20,height-57,width-40,29);quietButton(g,open,L"查看用量统计  ›",c,false,12);view.hits.push_back({open,3});
    auto footer=simulated?L"模拟额度与历史":s.error.empty()?L"更新 "+(s.updated?timeLabel(s.updated,false):L"尚无")+L" · "+std::to_wstring(s.interval)+L" 秒刷新":s.error;
    drawUiText(g,footer,RectF(20,height-25,width-40,20),10,s.stale()?c.warning:c.secondary);
    drawHeading(g,L"Codex 额度",RectF(20,10,width-160,31),20,c.ink);drawUiText(g,s.q.planLabel(),RectF(width-139,13,85,27),13,c.secondary,false,true);drawUiText(g,L"×",close,18,c.secondary,false,true);
}
bool quotaCardAction(QuotaCardView &view,int action,HWND window,const State &) {
    if(action==1){view.history=false;return true;}if(action==2){view.history=true;return true;}
    if(action==4){const auto &windows=view.windows;HMENU menu=CreatePopupMenu();for(size_t i=0;i<windows.size();++i)AppendMenuW(menu,MF_STRING|(int(i)==view.selected?MF_CHECKED:0),UINT_PTR(i+1),wide(windows[i].label).c_str());
        POINT at{};GetCursorPos(&at);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,at.x,at.y,0,window,nullptr);DestroyMenu(menu);if(chosen>0)view.selected=chosen-1;return true;}
    return false;
}
QuotaPopup::QuotaPopup(Application *a,HWND parent,const Settings &appearance):app(a),settings(appearance) {
    simulated=app->testing||app->preview;dpi=GetDpiForWindow(parent);
    WNDCLASSEXW c{};c.cbSize=sizeof(c);c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=procedure;c.lpszClassName=L"CodexQuotaBar.Native.Quota";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&c);
    // Keep a native frame for system Acrylic; extend its client area over the caption.
    window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"CodexQuotaBar.Native.Quota",L"Codex 额度",WS_CAPTION|WS_THICKFRAME|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,c.hInstance,this);
    if(!window)throw std::runtime_error("Cannot open quota details");
}
QuotaPopup::~QuotaPopup(){if(window)DestroyWindow(window);}
void QuotaPopup::reveal(){
    auto parent=GetWindow(window,GW_OWNER);RECT owner{};GetWindowRect(parent,&owner);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&info);
    dpi=GetDpiForWindow(parent);float d=dpi/96.f;int width=int(372*d),height=int(quotaCardHeight(app->state,settings,int((info.rcWork.bottom-info.rcWork.top)/d)-40,view.history)*d);
    width=std::min(width,int(info.rcWork.right-info.rcWork.left));int x=std::clamp(int(owner.right)-width-30,int(info.rcWork.left),int(info.rcWork.right)-width),y=std::clamp(int(owner.top)+75,int(info.rcWork.top),std::max(int(info.rcWork.top),int(info.rcWork.bottom)-height));
    frost.reset();SetWindowPos(window,HWND_TOP,x,y,width,height,SWP_SHOWWINDOW);backdrop=configureBackdrop(window,settings.darkAppearance);InvalidateRect(window,nullptr,FALSE);SetTimer(window,1,1000,nullptr);if(!app->testing)SetForegroundWindow(window);
}
static void renderQuotaPopup(QuotaPopup &p,Surface &image){Graphics g(image.bitmap.get());g.SetSmoothingMode(SmoothingModeAntiAlias);
    if(p.simulated){if(!p.frost)p.frost=previewFrost(image.width,image.height);paintFrost(g,RectF(0,0,float(image.width),float(image.height)),0,glassPalette(p.settings.darkAppearance),p.frost.get(),RectF(0,0,float(image.width),float(image.height)));}
    else paintBackdrop(g,glassPalette(p.settings.darkAppearance),p.backdrop);
    float d=p.dpi/96.f;g.ScaleTransform(d,d);paintQuotaCard(g,image.width/d,image.height/d,p.app->state,p.settings,p.app->quotaHistory,p.view,p.simulated||p.app->testing||p.app->preview);g.Flush(FlushIntentionSync);
}
void QuotaPopup::draw(HDC dc){RECT client{};GetClientRect(window,&client);Surface image(client.right,client.bottom);renderQuotaPopup(*this,image);BitBlt(dc,0,0,image.width,image.height,image.dc,0,0,SRCCOPY);}
void QuotaPopup::capture(const fs::path &file){RECT r{};GetClientRect(window,&r);Surface image(r.right,r.bottom);renderQuotaPopup(*this,image);for(int i=0;i<image.width*image.height;++i)image.pixels[i*4+3]=255;image.save(file);}
LRESULT CALLBACK QuotaPopup::procedure(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<QuotaPopup *>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){self=static_cast<QuotaPopup *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(w,msg,wp,lp);
    switch(msg){case WM_NCCALCSIZE:return 0;case WM_ERASEBKGND:return 1;case WM_PAINT:{PAINTSTRUCT p{};auto dc=BeginPaint(w,&p);self->draw(dc);EndPaint(w,&p);return 0;}
    case WM_PRINTCLIENT:self->draw(reinterpret_cast<HDC>(wp));return 0;
    case WM_TIMER:if(IsWindowVisible(w))InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);RECT r{};GetClientRect(w,&r);return p.y<int(45*self->dpi/96.f)&&p.x<r.right-int(48*self->dpi/96.f)?HTCAPTION:HTCLIENT;}
    case WM_LBUTTONUP:{float d=self->dpi/96.f;for(const auto &h:self->view.hits)if(h.rect.Contains(GET_X_LPARAM(lp)/d,GET_Y_LPARAM(lp)/d)){if(h.action==5)ShowWindow(w,SW_HIDE);else if(h.action==3){ShowWindow(w,SW_HIDE);self->app->showSettings();self->app->settingsWindow->selectPage(4);}else {quotaCardAction(self->view,h.action,w,self->app->state);self->reveal();}InvalidateRect(w,nullptr,FALSE);break;}return 0;}
    case WM_MOUSEWHEEL:self->view.extraScroll=std::max(0,self->view.extraScroll+(GET_WHEEL_DELTA_WPARAM(wp)<0?1:-1));InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){ShowWindow(w,SW_HIDE);return 0;}if(wp==VK_LEFT||wp==VK_RIGHT){self->view.history=wp==VK_RIGHT;self->reveal();InvalidateRect(w,nullptr,FALSE);return 0;}break;
    case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
    case WM_DWMCOMPOSITIONCHANGED:case WM_THEMECHANGED:case WM_SETTINGCHANGE:self->backdrop=configureBackdrop(w,self->settings.darkAppearance);InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_NCDESTROY:self->window=nullptr;SetWindowLongPtrW(w,GWLP_USERDATA,0);break;}
    return DefWindowProcW(w,msg,wp,lp);
}

void Application::flushQuotaHistory(bool final){
    if(testing||preview)return;
    if(quotaHistoryWrite.valid()){
        if(!final&&quotaHistoryWrite.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try{quotaHistoryWrite.get();quotaHistorySaveError=false;}catch(...){quotaHistory.dirty=true;quotaHistorySaveError=true;}
    }
    if(!quotaHistory.dirty||(!final&&GetTickCount64()<quotaHistorySaveAt))return;
    auto data=quotaHistory.toJson();auto file=dataDirectory()/L"codex-quota-history.json";quotaHistory.dirty=false;quotaHistorySaveAt=GetTickCount64()+30000;
    if(final){try{saveAnalyticsJson(file,data);}catch(...){quotaHistorySaveError=true;}}
    else quotaHistoryWrite=std::async(std::launch::async,[file,data=std::move(data)]{saveAnalyticsJson(file,data);});
}

int analyticsUiTests(const fs::path &dir){
    fs::create_directories(dir);std::ofstream log(dir/L"checks.txt");int count=0;
    auto check=[&](bool ok,const char *name){if(!ok){log<<"FAIL "<<name<<std::endl;throw std::runtime_error(name);}++count;log<<"PASS "<<name<<std::endl;};
    Application app(false,true);app.settings=Settings{};app.state.active=true;app.state.updated=utcNow();app.state.error.clear();app.state.q.plan="plus";app.state.q.accountKey=privateKey("demo-account");app.state.q.hour=WindowQuota{68,utcNow()+1800,300};app.state.q.week=WindowQuota{42,utcNow()+86400*2,10080};
    app.state.q.extraWindows={{"spark:primary:300","Codex Spark · 5 小时","spark",{91,utcNow()+1800,300}},{"codex:monthly:43200","月度额度","codex",{64,utcNow()+86400*16,43200}}};
    for(const auto &w:allQuotaWindows(app.state.q)){int64_t start=w.window.resets-int64_t(w.window.minutes)*60;for(int i=0;i<9;++i){auto at=start+(app.state.updated-start)*i/8;app.quotaHistory.samples.push_back({app.state.q.accountKey,app.state.q.plan,w.id,w.label,at,w.window.resets,w.window.minutes,100-(100-w.window.remaining)*i/8});}}
    app.showSettings();auto &ui=*app.settingsWindow;ui.usageReport=demoUsageReport(utcNow());ui.selectPage(4);ui.usageView.dirty=true;
    check(ui.page==4&&IsWindowVisible(GetDlgItem(ui.content,UsageSeven)),"native analytics page exposes range controls");
    ui.capture(dir/L"usage-light-simulated.png");check(!ui.usageView.groups.empty()&&ui.usageView.total.tokens.total()>0,"native analytics renders nonempty model summaries");
    auto seven=ui.usageView.total.tokens.total();ui.usageCommand(UsageThirty);ui.capture(dir/L"usage-month-simulated.png");check(ui.usageView.total.tokens.total()>seven,"native analytics range switches recalculate totals");
    ui.usageCommand(UsageSessions);ui.usageCommand(UsageNext);ui.capture(dir/L"usage-session-page2-simulated.png");
    check(ui.usageView.groups.size()>5&&std::any_of(ui.usageView.hits.begin(),ui.usageView.hits.end(),[](const auto &h){return h.action==22&&h.value>=5;}),"session pagination exposes records beyond the first five");
    ui.usageCommand(UsagePrevious);check(ui.usageView.groupPage==0,"session pagination returns to the first page");
    auto dayHit=std::find_if(ui.usageView.hits.begin(),ui.usageView.hits.end(),[](const auto &h){return h.action==21;});
    check(dayHit!=ui.usageView.hits.end(),"heatmap exposes covered date hit targets");auto date=*dayHit;ui.usageClick(date.rect.X+1,date.rect.Y+1);ui.usageView.prepare(ui.usageReport);
    check(ui.usageView.selectedDay==date.value&&ui.usageView.total.tokens.total()==ui.usageReport.total(date.value,shiftLocalDay(date.value,1)).tokens.total(),"heatmap date selection filters summary to that local calendar day");ui.usageCommand(UsageClear);
    ui.usageCommand(UsageProjects);ui.scrollTo(570);ui.capture(dir/L"usage-projects-simulated.png");
    // Hit-test the rendered project rows without touching real usage or settings.
    auto row=std::find_if(ui.usageView.hits.begin(),ui.usageView.hits.end(),[](const auto &h){return h.action==22;});
    check(row!=ui.usageView.hits.end(),"native analytics exposes project row hit targets");
    auto hit=*row;ui.usageClick(hit.rect.X+2,hit.rect.Y+2);check(ui.usageView.group==2&&!ui.usageView.project.empty(),"native analytics drills from a project into its sessions");
    ui.capture(dir/L"usage-sessions-simulated.png");ui.usageCommand(UsageClear);ui.usageCommand(UsageAccounts);ui.capture(dir/L"usage-accounts-simulated.png");
    check(std::any_of(ui.usageView.groups.begin(),ui.usageView.groups.end(),[](const auto &r){return r.key.empty();}),"native analytics preserves unknown account attribution");
    ui.usageCommand(UsageTokens);ui.usageCommand(UsageHour);ui.capture(dir/L"usage-hourly-simulated.png");check(ui.usageView.tokens&&ui.usageView.bins.size()==24,"native analytics supports Token and hourly plots");
    ui.usageCommand(UsageQuota);check(ui.quotaPopup&&IsWindowVisible(ui.quotaPopup->window),"native quota popup opens from analytics");ui.quotaPopup->capture(dir/L"quota-current-simulated.png");
    ui.quotaPopup->view.history=true;ui.quotaPopup->reveal();ui.quotaPopup->capture(dir/L"quota-history-simulated.png");check(!app.quotaHistory.series(app.state.q,"hour").empty(),"native quota history uses recorded owned samples");
    ui.draft.darkAppearance=true;ui.applyAppearance();ui.usageCommand(UsageSeven);ui.usageCommand(UsageProjects);ui.usageCommand(UsageCost);ui.usageCommand(UsageDay);ui.scrollTo(0);ui.capture(dir/L"usage-dark-simulated.png");
    check(ui.backdrop==BackdropMode::Acrylic||ui.backdrop==BackdropMode::Blur||ui.backdrop==BackdropMode::Solid,"native analytics uses the existing system backdrop with fallback");
    log<<count<<" analytics UI checks passed.\n";return count;
}
} // namespace quota
