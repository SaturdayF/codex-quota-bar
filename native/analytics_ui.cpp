#include "app.hpp"
#include <windowsx.h>
#include <commdlg.h>
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
std::wstring exactTokens(int64_t count){auto value=std::to_wstring(count);for(int i=int(value.size())-3;i>0;i-=3)value.insert(size_t(i),L",");return value;}
void usageTip(Graphics &g,RectF rect,const std::wstring &text,const GlassPalette &colors){
    drawUiText(g,text,RectF(rect.X+9,rect.Y+3,rect.Width-18,rect.Height-6),11,colors.ink,false,true);
}
void smoothLine(Graphics &g,const std::vector<PointF> &samples,Color color,float width=2.2f) {
    std::vector<PointF> points;
    for(const auto &p:samples){if(points.empty()||p.X>points.back().X)points.push_back(p);else if(p.X==points.back().X)points.back()=p;}
    if(points.empty())return;
    if(points.size()==1){SolidBrush dot(color);g.FillEllipse(&dot,points[0].X-1.5f,points[0].Y-1.5f,3.f,3.f);return;}
    std::vector<float> slope(points.size()-1),tangent(points.size());
    for(size_t i=0;i<slope.size();++i)slope[i]=(points[i+1].Y-points[i].Y)/(points[i+1].X-points[i].X);
    tangent.front()=slope.front();tangent.back()=slope.back();
    for(size_t i=1;i+1<points.size();++i)tangent[i]=slope[i-1]*slope[i]<=0?0:2*slope[i-1]*slope[i]/(slope[i-1]+slope[i]);
    // Limit tangents to keep monotone intervals within their observed range.
    for(size_t i=0;i<slope.size();++i){if(slope[i]==0){tangent[i]=tangent[i+1]=0;continue;}
        float a=tangent[i]/slope[i],b=tangent[i+1]/slope[i],norm=a*a+b*b;
        if(norm>9){float scale=3/std::sqrt(norm);tangent[i]*=scale;tangent[i+1]*=scale;}
    }
    GraphicsPath path;
    for(size_t i=1;i<points.size();++i){auto a=points[i-1],b=points[i];float dx=(b.X-a.X)/3;
        path.AddBezier(a,PointF(a.X+dx,a.Y+dx*tangent[i-1]),PointF(b.X-dx,b.Y-dx*tangent[i]),b);
    }
    Pen pen(color,width);pen.SetStartCap(LineCapRound);pen.SetEndCap(LineCapRound);g.DrawPath(&pen,&path);
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

int usageRowsEnd(const UsageView &v){size_t first=std::min(v.groups.size(),size_t(std::max(0,v.groupPage))*5);size_t shown=std::min<size_t>(5,v.groups.size()-first);return shown?802+int(shown)*57:890;}
int usagePageHeight(const UsageView &v,const UsageReport &r){return usageRowsEnd(v)+(v.groups.size()>5?48:0)+(r.partial||!r.error.empty()||v.total.unpriced?62:0)+86;}
bool quotaCardHover(QuotaCardView &v,float x,float y){int day=-1;for(const auto &hit:v.hits)if(hit.action==6&&hit.rect.Contains(x,y)){day=int(hit.value);break;}if(day==v.hoveredDay)return false;v.hoveredDay=day;return true;}
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
    auto previous=usageHubRevision;syncUsageSources();if(previous!=usageHubRevision&&page==4){layout();InvalidateRect(content,nullptr,FALSE);}
    if(usageTask.valid()&&usageTask.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try{auto report=usageTask.get();if(!usageCancel){usageReport=std::move(report);usageView.dirty=true;usageNextScan=GetTickCount64()+(usageReport.pending?1500:120000);}}
        catch(...){usageReport.error=L"无法读取本地用量，已保留上次统计。请检查日志目录权限后重试。";usageNextScan=GetTickCount64()+120000;}
        EnableWindow(GetDlgItem(content,UsageRefresh),TRUE);if(page==4)layout();InvalidateRect(content,nullptr,FALSE);
    }
    if(page==4&&!closing&&!usageTask.valid()&&usageNextScan&&GetTickCount64()>=usageNextScan)startUsageScan();
}
void SettingsWindow::usageCommand(int id) {
    if(id==UsageSource){auto index=SendDlgItemMessageW(content,UsageSource,CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<usageSources.size())usageSource=usageSources[size_t(index)];usageView.groupPage=0;usageView.hovered=-1;syncUsageSources(true);layout();InvalidateRect(content,nullptr,FALSE);return;}
    if(!usageSource.empty()){sharedUsageCommand(id);return;}
    if(id==UsageExport||id==UsageImport){
        wchar_t file[32768]=L"Codex用量备份.csv";OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=window;dialog.lpstrFilter=L"Codex 数据备份 (*.csv)\0*.csv\0\0";dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"csv";dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(id==UsageExport?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
        if(!(id==UsageExport?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog)))return;
        try{
            AnalyticsBackup data{usageReport,app->quotaHistory};mergeUsageReport(data.usage,app->popupUsage);
            if(id==UsageExport){if(!data.usage.available&&data.history.samples.empty())throw std::runtime_error("暂无已采集数据，请等待统计读取后重试。");writeAnalyticsCsv(file,data);openHelp(L"导出完成",L"已导出全部已采集用量和额度历史，不受当前日期筛选限制。");}
            else{
                auto imported=readAnalyticsCsv(file);auto archive=dataDirectory()/L"codex-restored-data.csv";
                if(fs::exists(archive)){auto previous=readAnalyticsCsv(archive);mergeUsageReport(data.usage,previous.usage);mergeQuotaHistory(data.history,previous.history);}
                mergeUsageReport(data.usage,imported.usage);mergeQuotaHistory(data.history,imported.history);
                // Drain pre-import scans so an older result cannot replace restored data.
                usageCancel=true;app->popupUsageCancel=true;
                if(usageTask.valid()){usageTask.wait();std::optional<UsageReport> latest;try{latest=usageTask.get();}catch(...){}if(latest)mergeUsageReport(data.usage,*latest);}
                if(app->popupUsageTask.valid()){app->popupUsageTask.wait();std::optional<UsageReport> latest;try{latest=app->popupUsageTask.get();}catch(...){}if(latest)mergeUsageReport(data.usage,*latest);}
                auto popupCopy=data.usage;fs::create_directories(archive.parent_path());writeAnalyticsCsv(archive,data);
                usageReport=std::move(data.usage);app->quotaHistory=std::move(data.history);app->quotaHistory.dirty=true;app->popupUsage=std::move(popupCopy);
                usageCancel=false;app->popupUsageCancel=false;usageNextScan=GetTickCount64()+120000;app->popupUsageNextScan=GetTickCount64()+120000;EnableWindow(GetDlgItem(content,UsageRefresh),TRUE);
                usageView.dirty=true;layout();InvalidateRect(content,nullptr,FALSE);
                for(auto &overlay:app->overlays)if(overlay->kind==WidgetKind::Codex&&IsWindowVisible(overlay->details))overlay->showDetails();
                openHelp(L"导入完成",L"数据已合并并保存，重复记录不会再次累加。重启后仍可查看恢复的统计。");
            }
        }catch(const std::exception &error){usageCancel=false;app->popupUsageCancel=false;EnableWindow(GetDlgItem(content,UsageRefresh),TRUE);openHelp(id==UsageExport?L"导出失败":L"导入失败",wide(error.what()));}
        return;
    }
    if(id==UsageRefresh){startUsageScan();return;}
    if(id==UsageQuota){if(!quotaPopup)quotaPopup=std::make_unique<QuotaPopup>(app,window,draft);quotaPopup->settings=draft;quotaPopup->reveal();return;}
    if(id==UsagePrevious||id==UsageNext){usageView.groupPage+=id==UsageNext?1:-1;usageView.keyboard=-1;layout();InvalidateRect(content,nullptr,FALSE);return;}
    if(id==UsageSeven||id==UsageThirty){usageView.days=id==UsageSeven?7:30;usageView.selectedDay=0;usageView.project.clear();}
    if(id==UsageCost||id==UsageTokens)usageView.tokens=id==UsageTokens;
    if(id==UsageDay||id==UsageHour)usageView.hourly=id==UsageHour;
    if(id>=UsageModels&&id<=UsageAccounts){usageView.group=id-UsageModels;usageView.project.clear();}
    if(id==UsageClear){usageView.selectedDay=0;usageView.project.clear();}
    usageView.dirty=true;usageView.hovered=-1;usageView.keyboard=-1;usageView.groupPage=0;
    for(auto ctrl:controls){int cid=GetDlgCtrlID(ctrl);if(cid>=UsageSeven&&cid<=UsageImport)InvalidateRect(ctrl,nullptr,TRUE);}
    layout();
    InvalidateRect(content,nullptr,FALSE);
}
void SettingsWindow::usageClick(float x,float y) {
    if(!usageSource.empty()){sharedUsageClick(x,y);return;}
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
    if(!usageSource.empty()){drawSharedUsage(g,c);return;}
    usageView.prepare(usageReport);usageView.hits.clear();float w=float(contentWidth)-14;
    Color green=parseColor(draft.hourColor,Color(255,33,158,134)),blue=parseColor(draft.weekColor,Color(255,100,141,230));
    drawHeading(g,L"Codex 用量",RectF(0,0,w-220,38),27,c.ink);
    auto today=localDay(usageReport.updated?usageReport.updated:utcNow());
    auto begin=usageView.selectedDay?usageView.selectedDay:shiftLocalDay(today,1-usageView.days);
    auto range=usageDate(begin)+L" – "+usageDate(usageView.selectedDay?usageView.selectedDay:today);
    drawUiText(g,L"本机日志 · "+range+((simulatedGlass||app->testing||app->preview)?L" · 模拟数据":L""),RectF(234,42,w-234,24),13,c.secondary);

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
        for(size_t i=0;i<usageView.bins.size();++i){const auto &b=usageView.bins[i];float x=plot.X+step*i+(step-bw)/2;
            if(!usageView.tokens&&b.total.unpriced){SolidBrush brush(c.muted);g.FillEllipse(&brush,x+bw/2-2,plot.GetBottom()-4,4.f,4.f);}
            usageView.hits.push_back({RectF(plot.X+step*i,plot.Y,step,plot.Height+22),20,int64_t(i),{}});
            if(int(i)%labelsEvery==0||i+1==usageView.bins.size())drawUiText(g,b.label,RectF(plot.X+step*i-12,plot.GetBottom()+8,step+24,20),10.5f,c.secondary,false,true);
        }
    }
    {
        int64_t peak=1;for(const auto &bin:usageView.bins)peak=std::max(peak,bin.total.tokens.total());
        if(usageView.tokens)peak=int64_t(ceiling);
        std::vector<PointF> segment;auto stroke=[&]{smoothLine(g,segment,blue);segment.clear();};
        float step=usageView.bins.empty()?0:plot.Width/usageView.bins.size();
        for(size_t i=0;i<usageView.bins.size();++i){const auto &bin=usageView.bins[i];
            bool known=(!usageReport.partial&&usageReport.oldest&&(usageView.hourly||bin.at>=localDay(usageReport.oldest)))||bin.total.requests>0||bin.total.tokens.total()>0;
            if(!known){stroke();continue;}
            PointF point(plot.X+step*(i+.5f),plot.GetBottom()-plot.Height*float(double(bin.total.tokens.total())/peak));segment.push_back(point);
        }stroke();
        drawUiText(g,(usageView.hourly?L"蓝线：每小时 Token · 顶部 ":L"蓝线：每日 Token · 顶部 ")+compactTokens(peak),RectF(w-290,280,290,22),10.5f,blue,false,true);
    }
    if(!usageView.tokens){
        std::vector<PointF> points;auto stroke=[&]{smoothLine(g,points,green);points.clear();};float step=usageView.bins.empty()?0:plot.Width/usageView.bins.size();
        for(size_t i=0;i<usageView.bins.size();++i){const auto &bin=usageView.bins[i];if((usageReport.partial&&!bin.total.requests)||bin.total.unpriced){stroke();continue;}
            points.emplace_back(plot.X+step*(i+.5f),plot.GetBottom()-plot.Height*float(bin.total.cost/ceiling));
        }stroke();
    }
    std::wstring inspected=usageView.hourly?usageDate(usageView.selectedDay?usageView.selectedDay:today)+L" · 每小时明细":L"移动到曲线上查看当日数值";
    if(usageView.hovered>=0&&usageView.hovered<int(usageView.bins.size())){const auto &b=usageView.bins[size_t(usageView.hovered)];inspected=b.label+L" · "+costText(b.total)+L" · "+exactTokens(b.total.tokens.total())+L" Token";if(usageReport.partial&&!b.total.requests&&!b.total.tokens.total())inspected=b.label+L" · 未覆盖记录";}
    if(usageView.hovered>=0&&usageView.hovered<int(usageView.bins.size())){
        const auto &bin=usageView.bins[size_t(usageView.hovered)];bool known=(!usageReport.partial&&usageReport.oldest&&(usageView.hourly||bin.at>=localDay(usageReport.oldest)))||bin.total.requests>0||bin.total.tokens.total()>0;
        float x=plot.X+plot.Width*(usageView.hovered+.5f)/usageView.bins.size();Pen marker(blue,1);marker.SetDashStyle(DashStyleDash);g.DrawLine(&marker,x,plot.Y,x,plot.GetBottom());
        usageTip(g,RectF(std::clamp(x-160,plot.X,plot.GetRight()-320),plot.Y+4,320,30),bin.label+L" · "+(known?exactTokens(bin.total.tokens.total())+L" Token":L"未覆盖记录"),c);
    }
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
    if(!shown){drawUiText(g,usageTask.valid()?L"正在读取本地会话日志…":usageReport.error.empty()?L"所选日期没有可统计的本地记录。":usageReport.error,RectF(0,823,w,60),14,c.secondary,false,true);rowY=890;}
    rowY=float(usageRowsEnd(usageView));
    if(usageView.groups.size()>5){drawUiText(g,L"第 "+std::to_wstring(usageView.groupPage+1)+L" / "+std::to_wstring((usageView.groups.size()+4)/5)+L" 页",RectF(0,rowY+10,w-210,24),11,c.secondary);rowY+=48;}
    if(usageReport.partial||!usageReport.error.empty()||t.unpriced){
        auto status=!usageReport.error.empty()?usageReport.error:usageReport.pending?L"正在补齐本地历史记录。":t.unpriced?L"部分记录价格未知，费用仅包含已定价部分。":L"部分记录无法完整解析，统计可能不完整。";
        drawUiText(g,status,RectF(0,rowY+10,w,40),11,c.warning);rowY+=62;
    }
    divider(g,c,rowY+12,w);drawUiText(g,L"本地记录的标准 API 等价估算，非订阅实际扣费。",RectF(0,rowY+22,w,22),11,c.secondary);
    drawUiText(g,L"缓存为输入子集，推理为输出子集。单价核对：2026-11-14。",RectF(0,rowY+45,w,22),11,c.secondary);

}

int quotaCardHeight(const State &s,const Settings &settings,int maximum,bool history) {
    (void)settings;(void)history;return std::min(maximum,700+int(s.q.extraWindows.size())*108);
}
double TokenRoll::position(size_t column,int64_t now) const {
    const auto &c=columns.at(column);
    if(started<0)return c.to;
    double p=std::clamp(double(now-started-int64_t(column)*Stagger)/Duration,0.,1.);
    double eased=1-std::pow(1-p,3);
    return c.from+(c.to-c.from)*eased;
}
bool TokenRoll::running(int64_t now) const {
    return started>=0&&!columns.empty()&&now<started+Duration+int64_t(columns.size()-1)*Stagger;
}
void TokenRoll::update(std::optional<int64_t> tokens,int64_t localDate,bool enabled,int64_t now) {
    if(!tokens){*this={};return;}
    auto next=compactTokens(*tokens);
    bool opening=!value||day!=localDate;
    if(!opening&&next==text&&enabled){value=tokens;return;}
    int direction=opening||*tokens>=value.value_or(0)?1:-1;
    bool compatible=!opening&&next.size()==text.size();
    std::vector<Column> nextColumns(next.size());
    bool moving=false;
    for(size_t i=0;i<next.size();++i){
        if(next[i]<L'0'||next[i]>L'9')continue;
        double digit=next[i]-L'0',from=digit,to=digit;
        if(enabled){
            if(opening){from=0;to=10+digit;}
            else if(compatible&&text[i]>=L'0'&&text[i]<=L'9'){
                from=position(i,now);
                to=digit+10*(direction>0?std::ceil((from-digit)/10):std::floor((from-digit)/10));
            }else from=digit-direction*10;
        }
        nextColumns[i]={from,to};moving=moving||from!=to;
    }
    text=std::move(next);columns=std::move(nextColumns);value=tokens;day=localDate;started=moving?now:-1;
}
static void drawRollingTokens(Graphics &g,RectF area,const TokenRoll &roll,int64_t now,Color ink) {
    if(roll.text.empty())return;
    float size=24;
    auto widthAt=[&](float em){float w=0;for(auto ch:roll.text)w+=uiTextWidth(g,std::wstring(1,ch),em,true,true);return w+em/3;};
    while(size>6&&widthAt(size)>area.Width)size-=.5f;
    Font font(L"Segoe UI",size,FontStyleBold,UnitPixel);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(format.GetFormatFlags()|StringFormatFlagsNoWrap|StringFormatFlagsNoClip);
    SolidBrush brush(ink);
    auto saved=g.Save();g.SetClip(area,CombineModeIntersect);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    float x=area.X+size/6,top=area.Y+(area.Height-font.GetHeight(&g))/2;
    auto glyph=[&](wchar_t ch,float offset){wchar_t str[]={ch,0};g.DrawString(str,1,&font,PointF(x,top+offset),&format,&brush);};
    for(size_t i=0;i<roll.text.size();++i){
        auto ch=roll.text[i];float advance=uiTextWidth(g,std::wstring(1,ch),size,true,true);
        if(ch>=L'0'&&ch<=L'9'){
            double p=roll.position(i,now),whole=std::floor(p);float fraction=float(p-whole);
            int digit=(int(whole)%10+10)%10;
            glyph(wchar_t(L'0'+digit),-fraction*area.Height);
            if(fraction>0)glyph(wchar_t(L'0'+(digit+1)%10),(1-fraction)*area.Height);
        }else glyph(ch,0);
        x+=advance;
    }
    g.Restore(saved);
}
void paintQuotaCard(Graphics &g,float width,float height,const State &s,const Settings &settings,const QuotaHistory &history,QuotaCardView &view,bool simulated,const UsageReport *usage,int64_t frameTime,bool animate) {
    auto now=frameTime<0?int64_t(GetTickCount64()):frameTime;
    const auto &c=glassPalette(settings.darkAppearance);Color green=parseColor(settings.hourColor,Color(255,33,158,134)),blue=parseColor(settings.weekColor,Color(255,100,141,230));
    view.hits.clear();RectF close(width-40,12,24,25);view.hits.push_back({close,5});
    view.history=false;
    drawUiText(g,L"当前额度与用量",RectF(20,50,width-40,29),13,c.secondary);
    auto visibility=settings.visibility(s.q);auto windows=allQuotaWindows(s.q,visibility.hour,visibility.week);view.windows=windows;
    if(!view.history){
        auto body=g.Save();
        float bodyScale=std::min(1.f,std::max(.35f,(height-170)/530));
        g.TranslateTransform(0,94);g.ScaleTransform(1,bodyScale);g.TranslateTransform(0,-94);
        float y=94;
        auto meter=[&](const std::wstring &label,WindowQuota q,Color accent,float x,float yy,float w,bool extra){
            drawUiText(g,label,RectF(x,yy,w,23),extra?13:15,c.ink,!extra);
            drawUiText(g,windowValue(q),RectF(x,yy+25,w,33),extra?20:26,s.stale()?c.secondary:accent,true);
            fillRound(g,RectF(x,yy+65,w,4),2,c.active);
            if(q.minutes&&q.remaining>0)fillRound(g,RectF(x,yy+65,w*float(std::clamp(q.remaining,0.,100.)/100),4),2,s.stale()?c.muted:accent);
            drawUiText(g,L"重置 "+timeLabel(q.resets),RectF(x,yy+75,w,21),10.5f,c.secondary);
        };
        float column=visibility.count()==2?(width-56)/2:width-40;
        if(visibility.hour){auto q=s.q.hour.value_or(WindowQuota{});if(s.q.hour)q.minutes=300;meter(L"5 小时",q,green,20,y,column,false);}
        if(visibility.week){auto q=s.q.week.value_or(WindowQuota{});if(s.q.week)q.minutes=10080;meter(L"每周",q,blue,visibility.hour?36+column:20,y,column,false);}
        y+=108;
        bool ready=usage&&usage->available;
        UsageView trend;auto today=localDay(utcNow());
        for(int i=-6;i<=0;++i){auto day=shiftLocalDay(today,i);trend.bins.push_back({day,usageDate(day),{}});}
        if(ready)for(const auto &entry:usage->entries){
            if(entry.at<trend.bins.front().at||entry.at>=shiftLocalDay(today,1))continue;
            auto day=localDay(entry.at);auto bin=std::lower_bound(trend.bins.begin(),trend.bins.end(),day,[](const auto &a,int64_t b){return a.at<b;});
            if(bin!=trend.bins.end()&&bin->at==day){bin->total.add(entry);trend.total.add(entry);}
        }
        auto total=trend.bins.back().total;
        view.todayTokens.update(ready&&!total.overflow?std::optional<int64_t>(total.tokens.total()):std::nullopt,today,animate&&settings.pref().animate&&motionEnabled(),now);
        drawUiText(g,L"今日 Token",RectF(20,y,width/2-20,22),12,c.secondary);
        if(ready&&!total.overflow)drawRollingTokens(g,RectF(20,y+23,width/2-20,31),view.todayTokens,now,c.ink);
        else drawUiText(g,ready?L"无法汇总":L"--",RectF(20,y+23,width/2-20,31),24,c.ink,true);
        drawUiText(g,L"近 7 天 Token",RectF(width/2,y,width/2-20,22),12,c.secondary);
        drawUiText(g,ready?(trend.total.overflow?L"无法汇总":compactTokens(trend.total.tokens.total())):L"--",RectF(width/2,y+23,width/2-20,31),24,c.ink,true);
        drawUiText(g,ready?L"输入 "+compactTokens(total.tokens.input)+L"   ·   输出 "+compactTokens(total.tokens.output):L"正在读取本地会话用量…",RectF(20,y+59,width-40,21),11,c.secondary);
        drawUiText(g,ready?L"缓存输入 "+compactTokens(total.tokens.cached)+L"   ·   "+std::to_wstring(total.requests)+L" 次请求":L"仅统计本机已记录的 Codex 会话",RectF(20,y+81,width-40,21),11,c.secondary);
        y+=111;
        drawUiText(g,L"额度历史",RectF(20,y,110,26),13,c.ink,true);
        if(windows.empty())drawUiText(g,L"尚无可用额度窗口",RectF(20,y+38,width-40,80),12,c.secondary,false,true);
        else {
            view.selected=std::clamp(view.selected,0,int(windows.size())-1);const auto &selected=windows[size_t(view.selected)];
            RectF picker(width-190,y,170,26);quietButton(g,picker,wide(selected.label)+L"  ▾",c,false,11);view.hits.push_back({RectF(picker.X,94+(picker.Y-94)*bodyScale,picker.Width,picker.Height*bodyScale),4});
            auto samples=history.series(s.q,selected.id);RectF historyPlot(46,y+42,width-68,70);
            Pen grid(c.border,1);for(int i=0;i<3;++i){float yy=historyPlot.GetBottom()-historyPlot.Height*i/2;g.DrawLine(&grid,historyPlot.X,yy,historyPlot.GetRight(),yy);drawUiText(g,std::to_wstring(i*50)+L"%",RectF(13,yy-9,31,18),9,c.secondary,false,true);}
            if(samples.empty()||s.q.accountKey.empty())drawUiText(g,L"等待采样以记录额度历史",historyPlot,12,c.secondary,false,true);
            else {
                Pen guide(c.muted,1);guide.SetDashStyle(DashStyleDash);g.DrawLine(&guide,historyPlot.X,historyPlot.Y,historyPlot.GetRight(),historyPlot.GetBottom());
                auto end=selected.window.resets,start=end-int64_t(selected.window.minutes)*60;std::vector<PointF> points;
                for(const auto &sample:samples)points.emplace_back(historyPlot.X+historyPlot.Width*float(double(sample.at-start)/std::max<int64_t>(1,end-start)),historyPlot.Y+historyPlot.Height*float(1-sample.remaining/100));
                smoothLine(g,points,blue);

                drawUiText(g,selected.window.minutes<=300?timeLabel(start,false).substr(0,5):usageDate(start),RectF(44,y+115,95,20),10,c.secondary);
                drawUiText(g,selected.window.minutes<=300?timeLabel(end,false).substr(0,5):usageDate(end),RectF(width-112,y+115,95,20),10,c.secondary,false,true);
            }

        }
        y+=140;
        drawUiText(g,L"每日 Token 消耗 · 近 7 天",RectF(20,y,width-40,24),13,c.ink,true);
        RectF plot(46,y+40,width-70,75);Pen grid(c.border,1);
        for(int i=0;i<3;++i){float yy=plot.Y+plot.Height*i/2;g.DrawLine(&grid,plot.X,yy,plot.GetRight(),yy);}
        if(ready&&!trend.bins.empty()){
            int64_t peak=1;for(const auto &b:trend.bins)peak=std::max(peak,b.total.tokens.total());
            drawUiText(g,compactTokens(peak),RectF(16,plot.Y-8,29,18),9,c.secondary);
            drawUiText(g,L"0",RectF(20,plot.GetBottom()-9,24,18),9,c.secondary);
            std::vector<PointF> segment;
            auto stroke=[&]{smoothLine(g,segment,blue);segment.clear();};
            for(size_t i=0;i<trend.bins.size();++i){const auto &b=trend.bins[i];float x=plot.X+plot.Width*i/std::max<size_t>(1,trend.bins.size()-1);
                float left=i==0?plot.X: x-plot.Width/12, right=i+1==trend.bins.size()?plot.GetRight():x+plot.Width/12;
                bool covered=(!usage->partial&&usage->oldest&&b.at>=localDay(usage->oldest))||b.total.requests>0||b.total.tokens.total()>0;
                auto caption=usageDate(b.at,true)+L" · "+(covered?exactTokens(b.total.tokens.total())+L" Token":L"未覆盖记录");
                view.hits.push_back({RectF(left,94+(plot.Y-94)*bodyScale,right-left,(plot.Height+22)*bodyScale),6,int64_t(i),utf8(caption)});
                bool known=(!usage->partial&&usage->oldest&&b.at>=localDay(usage->oldest))||b.total.requests>0||b.total.tokens.total()>0;
                if(!known)stroke();else {float yy=plot.GetBottom()-plot.Height*float(double(b.total.tokens.total())/peak);segment.emplace_back(x,yy);}
                if(i==0||i==3||i+1==trend.bins.size())drawUiText(g,b.label,RectF(x-25,plot.GetBottom()+5,50,20),9.5f,c.secondary,false,true);
            }stroke();
        }else drawUiText(g,usage&&!usage->error.empty()?L"用量读取失败，请查看统计页":L"暂无可用的本地用量记录",plot,12,c.secondary,false,true);
        y=plot.GetBottom()+29;
        drawUiText(g,ready?(!usage->error.empty()?L"用量读取异常 · 显示上次统计":usage->partial||usage->pending?L"本地记录覆盖不完整 · 缓存已包含在输入中":L"本机日志统计 · 缓存已包含在输入中"):L"用量不代表订阅额度或实际账单",RectF(20,y,width-40,20),10,c.secondary);
        y+=27;
        int capacity=std::max(0,int(((height-76-94)/bodyScale+94-y+1)/108));
        if(!s.q.extraWindows.empty()){
            view.extraScroll=std::clamp(view.extraScroll,0,std::max(0,int(s.q.extraWindows.size())-capacity));
            for(int i=view.extraScroll;i<int(s.q.extraWindows.size())&&i<view.extraScroll+capacity;++i){const auto &extra=s.q.extraWindows[size_t(i)];meter(wide(extra.label),extra.window,i%2?blue:green,20,y,width-40,true);y+=108;}
            if(capacity<int(s.q.extraWindows.size()))drawUiText(g,L"滚轮查看其他额度",RectF(20,(height-77-94)/bodyScale+94,width-40,20),10,c.secondary);
        }
        g.Restore(body);
    }
    for(const auto &hit:view.hits)if(hit.action==6&&hit.value==view.hoveredDay){
        float x=hit.rect.X+hit.rect.Width/2;Pen marker(blue,1);marker.SetDashStyle(DashStyleDash);g.DrawLine(&marker,x,hit.rect.Y,x,hit.rect.GetBottom()-15);
        usageTip(g,RectF(std::clamp(x-160,20.f,std::max(20.f,width-340)),hit.rect.Y+3,320,29),wide(hit.key),c);break;
    }
    RectF open(20,height-57,width-40,29);quietButton(g,open,L"查看用量统计  ›",c,false,12);view.hits.push_back({open,3});
    auto footer=simulated?L"模拟额度与 Token 数据":s.error.empty()?L"更新 "+(s.updated?timeLabel(s.updated,false):L"尚无")+L" · "+std::to_wstring(s.interval)+L" 秒刷新":s.error;
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
    if(!IsWindowVisible(window))view.todayTokens={};
    app->refreshPopupUsage();
    auto parent=GetWindow(window,GW_OWNER);RECT owner{};GetWindowRect(parent,&owner);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&info);
    dpi=GetDpiForWindow(parent);float d=dpi/96.f;int width=int(440*d),height=int(quotaCardHeight(app->state,settings,int((info.rcWork.bottom-info.rcWork.top)/d)-40,view.history)*d);
    width=std::min(width,int(info.rcWork.right-info.rcWork.left));int x=std::clamp(int(owner.right)-width-30,int(info.rcWork.left),int(info.rcWork.right)-width),y=std::clamp(int(owner.top)+75,int(info.rcWork.top),std::max(int(info.rcWork.top),int(info.rcWork.bottom)-height));
    frost.reset();SetWindowPos(window,HWND_TOP,x,y,width,height,SWP_SHOWWINDOW);backdrop=configureBackdrop(window,settings.darkAppearance);InvalidateRect(window,nullptr,FALSE);lastInside=GetTickCount64();SetTimer(window,1,16,nullptr);if(!app->testing)SetForegroundWindow(window);
}
static void renderQuotaPopup(QuotaPopup &p,Surface &image,bool animate=true){Graphics g(image.bitmap.get());g.SetSmoothingMode(SmoothingModeAntiAlias);
    if(p.simulated){if(!p.frost)p.frost=previewFrost(image.width,image.height);paintFrost(g,RectF(0,0,float(image.width),float(image.height)),0,glassPalette(p.settings.darkAppearance),p.frost.get(),RectF(0,0,float(image.width),float(image.height)));}
    else paintBackdrop(g,glassPalette(p.settings.darkAppearance),p.backdrop);
    float d=p.dpi/96.f;g.ScaleTransform(d,d);paintQuotaCard(g,image.width/d,image.height/d,p.app->state,p.settings,p.app->quotaHistory,p.view,p.simulated||p.app->testing||p.app->preview,&p.app->popupUsage,-1,animate);g.Flush(FlushIntentionSync);
    if(IsWindowVisible(p.window))SetTimer(p.window,1,p.view.todayTokens.running(GetTickCount64())?16:50,nullptr);
}
void QuotaPopup::draw(HDC dc){RECT client{};GetClientRect(window,&client);Surface image(client.right,client.bottom);renderQuotaPopup(*this,image);BitBlt(dc,0,0,image.width,image.height,image.dc,0,0,SRCCOPY);}
void QuotaPopup::capture(const fs::path &file){RECT r{};GetClientRect(window,&r);Surface image(r.right,r.bottom);renderQuotaPopup(*this,image,false);for(int i=0;i<image.width*image.height;++i)image.pixels[i*4+3]=255;image.save(file);}
LRESULT CALLBACK QuotaPopup::procedure(HWND w,UINT msg,WPARAM wp,LPARAM lp){auto self=reinterpret_cast<QuotaPopup *>(GetWindowLongPtrW(w,GWLP_USERDATA));if(msg==WM_NCCREATE){self=static_cast<QuotaPopup *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(w,msg,wp,lp);
    switch(msg){case WM_NCCALCSIZE:return 0;case WM_ERASEBKGND:return 1;case WM_PAINT:{PAINTSTRUCT p{};auto dc=BeginPaint(w,&p);self->draw(dc);EndPaint(w,&p);return 0;}
    case WM_SHOWWINDOW:if(!wp)KillTimer(w,1);break;
    case WM_PRINTCLIENT:self->draw(reinterpret_cast<HDC>(wp));return 0;
    case WM_TIMER:if(IsWindowVisible(w)){
        POINT pointer{};GetCursorPos(&pointer);RECT bounds{};GetWindowRect(w,&bounds);auto now=GetTickCount64();
        if(PtInRect(&bounds,pointer))self->lastInside=now;
        else if(now-self->lastInside>=1500){ShowWindow(w,SW_HIDE);KillTimer(w,1);return 0;}
        if(now-self->lastPaint>=1000){self->lastPaint=now;self->app->refreshPopupUsage();InvalidateRect(w,nullptr,FALSE);}
        if(self->view.todayTokens.started>=0){
            InvalidateRect(w,nullptr,FALSE);
            if(!self->view.todayTokens.running(now))self->view.todayTokens.started=-1;
        }
    }return 0;
    case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(w,&p);RECT r{};GetClientRect(w,&r);return p.y<int(45*self->dpi/96.f)&&p.x<r.right-int(48*self->dpi/96.f)?HTCAPTION:HTCLIENT;}
    case WM_LBUTTONUP:{float d=self->dpi/96.f;for(const auto &h:self->view.hits)if(h.rect.Contains(GET_X_LPARAM(lp)/d,GET_Y_LPARAM(lp)/d)){if(h.action==5)ShowWindow(w,SW_HIDE);else if(h.action==3){ShowWindow(w,SW_HIDE);self->app->showSettings();self->app->settingsWindow->selectPage(4);}else {quotaCardAction(self->view,h.action,w,self->app->state);self->reveal();}InvalidateRect(w,nullptr,FALSE);break;}return 0;}
    case WM_MOUSEMOVE:{TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};TrackMouseEvent(&track);float d=self->dpi/96.f;if(quotaCardHover(self->view,GET_X_LPARAM(lp)/d,GET_Y_LPARAM(lp)/d))InvalidateRect(w,nullptr,FALSE);return 0;}
    case WM_MOUSELEAVE:if(quotaCardHover(self->view,-1,-1))InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_MOUSEWHEEL:self->view.extraScroll=std::max(0,self->view.extraScroll+(GET_WHEEL_DELTA_WPARAM(wp)<0?1:-1));InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE){ShowWindow(w,SW_HIDE);return 0;}if(wp==VK_LEFT||wp==VK_RIGHT){self->view.history=wp==VK_RIGHT;self->reveal();InvalidateRect(w,nullptr,FALSE);return 0;}break;
    case WM_CLOSE:ShowWindow(w,SW_HIDE);return 0;
    case WM_DWMCOMPOSITIONCHANGED:case WM_THEMECHANGED:case WM_SETTINGCHANGE:self->backdrop=configureBackdrop(w,self->settings.darkAppearance);InvalidateRect(w,nullptr,FALSE);return 0;
    case WM_NCDESTROY:self->window=nullptr;SetWindowLongPtrW(w,GWLP_USERDATA,0);break;}
    return DefWindowProcW(w,msg,wp,lp);
}

void Application::refreshPopupUsage() {
    pollPopupUsage();
    if(testing||preview){if(!popupUsage.available)popupUsage=demoUsageReport(utcNow());return;}
    if(popupUsageTask.valid()||GetTickCount64()<popupUsageNextScan)return;
    popupUsageCancel=false;UsageScanOptions options;options.home=codexHome();
    options.cache=dataDirectory()/L"codex-popup-usage-cache.json";
    popupUsageNextScan=GetTickCount64()+120000;
    try{popupUsageTask=std::async(std::launch::async,[this,options]{return scanCodexUsage(options,popupUsageCancel);});}
    catch(...){popupUsage.error=L"无法启动本地用量读取";}
}
void Application::pollPopupUsage() {
    if(!popupUsageTask.valid()||popupUsageTask.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    try{popupUsage=popupUsageTask.get();popupUsageNextScan=GetTickCount64()+(popupUsage.pending?1500:120000);}
    catch(...){popupUsage.error=L"无法读取本地用量";}
    for(auto &overlay:overlays)if(overlay->kind==WidgetKind::Codex&&IsWindowVisible(overlay->details))overlay->showDetails();
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
    {
        TokenRoll roll;auto day=localDay(utcNow());
        roll.update(std::nullopt,day,true,1000);
        check(!roll.running(1000)&&roll.text.empty(),"unavailable Token totals do not animate a fabricated zero");
        roll.update(48580000,day,true,1100);
        check(roll.text==L"48.58M"&&roll.running(1100)&&roll.position(0,1100)==0,"today Token reveal starts rolling digits toward the actual compact total");
        auto started=roll.started;roll.update(48580001,day,true,1250);
        check(roll.started==started,"refreshes with unchanged displayed digits do not restart the roll");
        double position=roll.position(0,1400);roll.update(49580000,day,true,1400);
        check(std::abs(roll.position(0,1400)-position)<1e-9,"a new total retargets from the fractional visible digit position");
        check(!roll.running(3000)&&int(std::round(roll.position(0,3000)))%10==4,"rolling settles exactly on the target without a continuing timer");
        roll.update(47580000,day,true,3100);
        check(roll.columns[1].to<roll.columns[1].from,"corrected lower totals roll in the reverse direction");
        roll.update(47580000,day,false,3200);
        check(!roll.running(3200)&&roll.position(1,3200)==7,"disabling animation immediately shows the actual total");
        for(auto tokens:{int64_t(0),int64_t(999),int64_t(1000),int64_t(999999),int64_t(1000000),INT64_MAX}){
            roll.update(tokens,day,true,4000);bool exact=roll.text==compactTokens(tokens);
            for(size_t i=0;i<roll.text.size();++i)if(roll.text[i]>=L'0'&&roll.text[i]<=L'9')exact=exact&&((int(std::round(roll.position(i,6000)))%10+10)%10)==roll.text[i]-L'0';
            check(exact,"digit and K/M/B unit transitions preserve the formatted total, including large counts");
        }
        roll.update(123,shiftLocalDay(day,1),true,7000);
        check(roll.position(0,7000)==0&&roll.text==L"123","a new local calendar day resets the reveal state");
        roll.update(std::nullopt,day,true,7100);check(roll.text.empty()&&!roll.running(7100),"missing data clears an in-progress roll");
        // Native rendered frames, deliberately synthetic; the suffix and label
        // remain fixed while individual digits move inside the number bounds.
        for(bool dark:{false,true})for(int dpi:{96,144,192}){
            float scale=dpi/96.f;std::vector<BYTE> first,last;
            TokenRoll preview;preview.update(48580000,day,true,0);
            for(int elapsed:{0,200,400,600,900}){
                Surface image(int(220*scale),int(80*scale));Graphics g(image.bitmap.get());const auto &c=glassPalette(dark);g.Clear(c.mid);g.ScaleTransform(scale,scale);
                drawUiText(g,L"今日 Token · 模拟数据",RectF(12,3,200,22),12,c.secondary);
                drawRollingTokens(g,RectF(12,28,190,36),preview,elapsed,c.ink);g.Flush(FlushIntentionSync);
                if(elapsed==0)first.assign(image.pixels,image.pixels+image.width*image.height*4);
                if(elapsed==900)last.assign(image.pixels,image.pixels+image.width*image.height*4);
                image.save(dir/(L"token-roll-"+std::wstring(dark?L"dark-":L"light-")+std::to_wstring(dpi)+L"dpi-"+std::to_wstring(elapsed)+L"ms-simulated.png"));
            }
            check(first!=last,"native Token digits animate in both themes and at 100/150/200 percent DPI");
        }
    }
    Application app(false,true);app.settings=Settings{};app.state.active=true;app.state.updated=utcNow();app.state.error.clear();app.state.q.plan="plus";app.state.q.accountKey=privateKey("demo-account");app.state.q.hour=WindowQuota{68,utcNow()+1800,300};app.state.q.week=WindowQuota{42,utcNow()+86400*2,10080};
    app.state.q.extraWindows={{"spark:primary:300","Codex Spark · 5 小时","spark",{91,utcNow()+1800,300}},{"codex:monthly:43200","月度额度","codex",{64,utcNow()+86400*16,43200}}};
    for(const auto &w:allQuotaWindows(app.state.q)){int64_t start=w.window.resets-int64_t(w.window.minutes)*60;for(int i=0;i<9;++i){auto at=start+(app.state.updated-start)*i/8;app.quotaHistory.samples.push_back({app.state.q.accountKey,app.state.q.plan,w.id,w.label,at,w.window.resets,w.window.minutes,100-(100-w.window.remaining)*i/8});}}
    if(motionEnabled()){
        auto demo=app.state;demo.q.extraWindows.clear();demo.q.hour->remaining=73;demo.q.week->remaining=4;
        auto usage=demoUsageReport(utcNow());auto today=localDay(utcNow());
        usage.entries.erase(std::remove_if(usage.entries.begin(),usage.entries.end(),[&](const auto &entry){return localDay(entry.at)==today;}),usage.entries.end());
        UsageEntry entry;entry.at=utcNow();entry.tokens={48468000,47550000,0,112000,0};entry.requests=282;usage.entries.push_back(entry);
        QuotaCardView view;
        for(int elapsed=0;elapsed<=900;elapsed+=30){
            Surface image(550,875);Graphics g(image.bitmap.get());g.Clear(glassPalette(false).mid);g.ScaleTransform(1.25f,1.25f);g.SetSmoothingMode(SmoothingModeAntiAlias);
            paintQuotaCard(g,440,700,demo,app.settings,app.quotaHistory,view,true,&usage,elapsed);g.Flush(FlushIntentionSync);
            image.save(dir/(L"token-roll-card-"+std::to_wstring(elapsed)+L"ms-simulated.png"));
        }
    }
    app.showSettings();auto &ui=*app.settingsWindow;ui.usageReport=demoUsageReport(utcNow());ui.selectPage(4);ui.usageView.dirty=true;
    check(IsWindowVisible(GetDlgItem(ui.content,UsageExport))&&IsWindowVisible(GetDlgItem(ui.content,UsageImport)),"usage page exposes CSV export and restore buttons");
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
    ui.usageCommand(UsageQuota);check(ui.quotaPopup&&IsWindowVisible(ui.quotaPopup->window),"native quota popup opens from analytics");
    {
        auto &popup=*ui.quotaPopup;RECT r{};GetClientRect(popup.window,&r);Surface image(r.right,r.bottom);
        SendMessageW(popup.window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT);
        check(!motionEnabled()||popup.view.todayTokens.running(GetTickCount64()),"analytics quota popup starts the today Token animation when revealed");
        if(motionEnabled()){
            popup.view.todayTokens.started=GetTickCount64()-350;
            SendMessageW(popup.window,WM_TIMER,1,0);
            check(popup.view.todayTokens.running(GetTickCount64()),"native popup timer keeps repainting while Token digits roll");
            popup.view.todayTokens.started=GetTickCount64()-2000;SendMessageW(popup.window,WM_TIMER,1,0);
            check(!popup.view.todayTokens.running(GetTickCount64()),"native popup stops animation repaint requests after the final frame");
        }
        ShowWindow(popup.window,SW_HIDE);popup.reveal();
        SendMessageW(popup.window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(image.dc),PRF_CLIENT);
        check(!motionEnabled()||popup.view.todayTokens.running(GetTickCount64()),"reopening the analytics quota popup replays the Token reveal");
    }
    ui.quotaPopup->capture(dir/L"quota-current-simulated.png");
    auto extras=app.state.q.extraWindows;app.state.q.extraWindows.clear();ui.quotaPopup->reveal();
    ui.quotaPopup->capture(dir/L"quota-standard-simulated.png");
    RECT normal{};GetWindowRect(ui.quotaPopup->window,&normal);
    SetWindowPos(ui.quotaPopup->window,nullptr,0,0,int(440*ui.quotaPopup->dpi/96.f),int(440*ui.quotaPopup->dpi/96.f),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    ui.quotaPopup->capture(dir/L"quota-compact-simulated.png");
    ui.quotaPopup->reveal();app.state.q.extraWindows=extras;
    check(std::none_of(ui.quotaPopup->view.hits.begin(),ui.quotaPopup->view.hits.end(),[](const auto &hit){return hit.action==2;})&&std::any_of(ui.quotaPopup->view.hits.begin(),ui.quotaPopup->view.hits.end(),[](const auto &hit){return hit.action==4;}),"current quota card includes history picker without a separate history tab");
    auto dayTarget=std::find_if(ui.quotaPopup->view.hits.begin(),ui.quotaPopup->view.hits.end(),[](const auto &hit){return hit.action==6;});
    check(dayTarget!=ui.quotaPopup->view.hits.end(),"daily popup curve exposes date hover regions");
    auto target=*dayTarget;float popupDpi=ui.quotaPopup->dpi/96.f;
    SendMessageW(ui.quotaPopup->window,WM_MOUSEMOVE,0,MAKELPARAM(int((target.rect.X+2)*popupDpi),int((target.rect.Y+5)*popupDpi)));
    check(ui.quotaPopup->view.hoveredDay==target.value&&target.key.find("Token")!=std::string::npos,"native popup mouse move selects a date with exact Token tooltip text");
    ui.quotaPopup->capture(dir/L"quota-hover-simulated.png");
    SendMessageW(ui.quotaPopup->window,WM_MOUSELEAVE,0,0);check(ui.quotaPopup->view.hoveredDay==-1,"leaving daily popup curve clears the tooltip");
    auto savedReport=ui.usageReport;auto savedView=ui.usageView;int populatedHeight=ui.pageHeight;
    ui.usageReport={};ui.usageView.dirty=true;ui.layout();
    check(ui.pageHeight<populatedHeight&&!IsWindowVisible(GetDlgItem(ui.content,UsageNext)),"empty usage page shrinks to content and hides redundant pagination");
    ui.scrollTo(INT_MAX);ui.capture(dir/L"usage-empty-bottom-simulated.png");ui.usageReport=savedReport;ui.usageView=savedView;ui.usageView.dirty=true;ui.layout();ui.scrollTo(0);
    auto plotHit=std::find_if(ui.usageView.hits.begin(),ui.usageView.hits.end(),[](const auto &hit){return hit.action==20;});
    ui.capture(dir/L"usage-before-hover-simulated.png");
    plotHit=std::find_if(ui.usageView.hits.begin(),ui.usageView.hits.end(),[](const auto &hit){return hit.action==20;});
    check(plotHit!=ui.usageView.hits.end(),"settings chart retains hover regions after dynamic layout");
    if(plotHit!=ui.usageView.hits.end()){auto rect=plotHit->rect;ui.usageHover(rect.X+2,rect.Y+5);check(ui.usageView.hovered>=0,"settings usage curve responds to date hover");ui.capture(dir/L"usage-hover-simulated.png");ui.usageHover(-1,-1);}
    ui.usageCommand(UsageDay);ui.usageCommand(UsageSeven);ui.usageCommand(UsageModels);ui.capture(dir/L"usage-daily-before-hover-simulated.png");
    auto dailyHit=std::find_if(ui.usageView.hits.begin(),ui.usageView.hits.end(),[](const auto &hit){return hit.action==20;});
    check(dailyHit!=ui.usageView.hits.end()&&!ui.usageView.hourly,"settings daily chart exposes per-day hover regions");
    auto dailyRect=dailyHit->rect;ui.usageHover(dailyRect.X+2,dailyRect.Y+5);ui.capture(dir/L"usage-daily-hover-simulated.png");ui.usageHover(-1,-1);
    ui.scrollTo(INT_MAX);ui.capture(dir/L"usage-populated-bottom-simulated.png");ui.scrollTo(0);
    auto usagePixels=[&]{Surface image(440,560);Graphics g(image.bitmap.get());g.Clear(Color::White);QuotaCardView v;paintQuotaCard(g,440,560,app.state,app.settings,app.quotaHistory,v,true,&app.popupUsage);g.Flush(FlushIntentionSync);return std::vector<BYTE>(image.pixels,image.pixels+440*560*4);};
    auto populated=usagePixels();auto savedUsage=app.popupUsage;app.popupUsage={};
    check(populated!=usagePixels(),"quota card renders local Token data and daily trend instead of the unavailable state");
    ui.quotaPopup->capture(dir/L"quota-empty-simulated.png");app.popupUsage=savedUsage;
    ui.quotaPopup->settings.darkAppearance=true;ui.quotaPopup->capture(dir/L"quota-current-dark-simulated.png");ui.quotaPopup->settings.darkAppearance=false;
    ShowWindow(ui.window,SW_HIDE);
    {
        auto monitor=monitors().front();Overlay overlay(&app,monitor,app.settings,app.state);KillTimer(overlay.window,1);
        overlay.showDetails();
        check(!motionEnabled()||overlay.codexCard.todayTokens.running(GetTickCount64()),"taskbar hover card starts the today Token animation");
        auto start=overlay.codexCard.todayTokens.started;overlay.showDetails();
        check(overlay.codexCard.todayTokens.started==start,"redrawing an open hover card does not restart its Token animation");
        if(motionEnabled()){
            std::vector<BYTE> first(overlay.detailImage->pixels,overlay.detailImage->pixels+overlay.detailImage->width*overlay.detailImage->height*4);
            overlay.codexCard.todayTokens.started=GetTickCount64()-350;SendMessageW(overlay.details,WM_TIMER,1,0);
            std::vector<BYTE> middle(overlay.detailImage->pixels,overlay.detailImage->pixels+overlay.detailImage->width*overlay.detailImage->height*4);
            check(first!=middle,"native hover animation timer presents a different rendered digit frame");
            overlay.codexCard.todayTokens.started=GetTickCount64()-2000;SendMessageW(overlay.details,WM_TIMER,1,0);
            check(!overlay.codexCard.todayTokens.running(GetTickCount64()),"native hover animation timer settles on the final total");
        }
        overlay.hover=true;POINT outside{monitor.work.left-100,monitor.work.top-100};
        overlay.lastInside=GetTickCount64()-1000;overlay.tick(&outside);
        check(IsWindowVisible(overlay.details),"quota hover remains visible during the 1.5 second leave grace period");
        RECT card{};GetWindowRect(overlay.details,&card);POINT inside{card.left+10,card.top+10};overlay.tick(&inside);
        check(GetTickCount64()-overlay.lastInside<100,"returning to quota details resets the leave timer");
        overlay.lastInside=GetTickCount64()-1600;overlay.tick(&outside);
        check(!IsWindowVisible(overlay.details),"quota hover closes after the leave grace period");
        check(!overlay.codexCard.todayTokens.value,"hiding the hover card clears its animation state");
        overlay.settings.preferences[overlay.settings.styleIndex()].animate=false;overlay.showDetails();
        check(overlay.codexCard.todayTokens.value&&!overlay.codexCard.todayTokens.running(GetTickCount64()),"existing style animation switch disables today Token rolling in the hover card");
    }
    ShowWindow(ui.window,SW_SHOW);
    ui.quotaPopup->view.history=true;ui.quotaPopup->reveal();ui.quotaPopup->capture(dir/L"quota-history-simulated.png");check(!app.quotaHistory.series(app.state.q,"hour").empty(),"native quota history uses recorded owned samples");
    ui.draft.darkAppearance=true;ui.applyAppearance();ui.usageCommand(UsageSeven);ui.usageCommand(UsageProjects);ui.usageCommand(UsageCost);ui.usageCommand(UsageDay);ui.scrollTo(0);ui.capture(dir/L"usage-dark-simulated.png");
    check(ui.backdrop==BackdropMode::Acrylic||ui.backdrop==BackdropMode::Blur||ui.backdrop==BackdropMode::Solid,"native analytics uses the existing system backdrop with fallback");
    log<<count<<" analytics UI checks passed.\n";return count;
}
} // namespace quota
