#pragma once
#include "service.hpp"
#include "display.hpp"
#include <cmath>
namespace quiet {
inline COLORREF Ink=RGB(16,26,34),Muted=RGB(51,65,75),Accent=RGB(0,105,95),Paper=RGB(248,250,251),Line=RGB(185,198,204);
inline int pixels(HWND w,int logical){return MulDiv(logical,static_cast<int>(GetDpiForWindow(w)),96);}
inline int logical(HWND w,int physical){return MulDiv(physical,96,static_cast<int>(GetDpiForWindow(w)));}
inline RECT logicalClient(HWND w){RECT r{};GetClientRect(w,&r);r.right=logical(w,r.right);r.bottom=logical(w,r.bottom);return r;}
inline void logicalDrawing(HDC dc,HWND w){int dpi=static_cast<int>(GetDpiForWindow(w));SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,96,96,nullptr);SetViewportExtEx(dc,dpi,dpi,nullptr);}
inline void invalidateLogical(HWND w,RECT r){r={pixels(w,r.left),pixels(w,r.top),pixels(w,r.right),pixels(w,r.bottom)};InvalidateRect(w,&r,FALSE);}
struct CanvasScroll {
    int minWidth,minHeight,x=0,y=0;
    RECT content(HWND w)const{auto r=logicalClient(w);r.right=std::max<int>(r.right,minWidth);r.bottom=std::max<int>(r.bottom,minHeight);return r;}
    void update(HWND w){auto page=logicalClient(w),area=content(w);int oldX=x,oldY=y;x=std::clamp(x,0,std::max<int>(0,area.right-page.right));y=std::clamp(y,0,std::max<int>(0,area.bottom-page.bottom));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,area.right-1,static_cast<UINT>(page.right),x,0};SetScrollInfo(w,SB_HORZ,&info,TRUE);info.nMax=area.bottom-1;info.nPage=page.bottom;info.nPos=y;SetScrollInfo(w,SB_VERT,&info,TRUE);if(oldX!=x||oldY!=y)ScrollWindowEx(w,pixels(w,oldX)-pixels(w,x),pixels(w,oldY)-pixels(w,y),nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE);}
    void move(HWND w,bool horizontal,int action){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(w,horizontal?SB_HORZ:SB_VERT,&info);int old=horizontal?x:y,next=old;switch(action){case SB_LINEUP:next-=32;break;case SB_LINEDOWN:next+=32;break;case SB_PAGEUP:next-=std::max<int>(32,static_cast<int>(info.nPage)-32);break;case SB_PAGEDOWN:next+=std::max<int>(32,static_cast<int>(info.nPage)-32);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;case SB_TOP:next=0;break;case SB_BOTTOM:next=info.nMax;break;default:return;}next=std::clamp(next,0,std::max<int>(0,info.nMax-static_cast<int>(info.nPage)+1));if(horizontal)x=next;else y=next;SetScrollPos(w,horizontal?SB_HORZ:SB_VERT,next,TRUE);ScrollWindowEx(w,horizontal?(pixels(w,old)-pixels(w,next)):0,horizontal?0:(pixels(w,old)-pixels(w,next)),nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE);InvalidateRect(w,nullptr,FALSE);}
    void drawing(HDC dc,HWND w)const{logicalDrawing(dc,w);SetViewportOrgEx(dc,-pixels(w,x),-pixels(w,y),nullptr);}
    void invalidate(HWND w,RECT r)const{r.left-=x;r.right-=x;r.top-=y;r.bottom-=y;invalidateLogical(w,r);}
};
inline HINSTANCE appInstance=nullptr;
inline constexpr wchar_t ClientWindowClass[]=L"TPFCQuietWindow.C";
inline HFONT font(int px,bool bold=false){return CreateFontW(-px,0,0,0,bold?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");}
inline void label(HDC dc,int x,int y,int width,int height,const std::wstring&s,int size=15,COLORREF color=RGB(26,42,52),bool bold=false){HFONT f=font(size,bold);auto old=SelectObject(dc,f);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{x,y,x+width,y+height};DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);DeleteObject(f);}
inline void fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
inline void rule(HDC dc,int x,int y,int end){HPEN p=CreatePen(PS_SOLID,1,Line);auto old=SelectObject(dc,p);MoveToEx(dc,x,y,nullptr);LineTo(dc,end,y);SelectObject(dc,old);DeleteObject(p);}
inline std::wstring number(int value,const wchar_t*unit=L""){return value<0?L"--":std::to_wstring(value)+unit;}
inline int pngEncoder(CLSID&out){UINT n=0,bytes=0;Gdiplus::GetImageEncodersSize(&n,&bytes);std::vector<BYTE>b(bytes);auto info=reinterpret_cast<Gdiplus::ImageCodecInfo*>(b.data());Gdiplus::GetImageEncoders(n,bytes,info);for(UINT i=0;i<n;++i)if(!wcscmp(info[i].MimeType,L"image/png")){out=info[i].Clsid;return 0;}return -1;}
class Window {
    Engine*engine=nullptr;bool client=false,demo=false,smoke=false;State state;Config config;HWND hwnd=nullptr,settings=nullptr;HFONT controlFont=nullptr,settingsFont=nullptr;UINT settingsDpi=96;
    NOTIFYICONDATAW tray{},tray2{};HICON icon=nullptr,icon2=nullptr;bool secondAdded=false;HPOWERNOTIFY lidNotify=nullptr;UINT taskbarCreated=0;int tab=0;uint64_t started=0;std::wstring preview;int dragPoint=-1;bool dragExit=false;
    std::vector<HWND> buttons;std::wstring clientError;DisplayCache displayCache;uint64_t smartPending=0;
    struct SmartButtonState {const wchar_t*caption;std::wstring hint;bool selected,enabled;};
    SmartButtonState smartButtonState()const{
        bool connected=clientError.empty()&&state.alive!=0;
        if(connected&&state.mode==static_cast<int>(Mode::Smart)&&!state.controlPaused)return {L"Smart 已启用",L"Smart正在自动调节风扇；点击BIOS可退出。",true,true};
        if(connected&&state.resumePending&&state.fault)return {L"Smart 等待恢复",state.reason,false,false};
        if(connected&&state.ready&&smartPending>GetTickCount64())return {L"Smart 启用中…",L"已发送Smart请求，正在等待后台确认。",false,false};
        if(connected&&state.ready)return {L"启用 Smart",L"按当前曲线自动调节风扇，高温保护优先。",false,true};
        std::wstring reason=!connected?L"后台服务未连接或引擎未运行。":state.controlPaused?L"当前处于暂停状态，风扇由BIOS接管。":state.resumePending?L"正在等待唤醒后的有效采样。":!state.verified?L"尚未完成本机风扇档位验证。":!namedLive()?L"CPU/GPU温度数据暂不可用。":!state.healthy?L"EC温度采集暂不可用。":L"正在等待连续有效采样。";
        return {L"Smart 暂不可用",L"Smart暂不可用："+reason,false,false};
    }
    void updateSmartButton(){
        if(buttons.size()<7)return;auto view=smartButtonState();if(view.selected||!state.ready||!clientError.empty())smartPending=0;
        wchar_t current[64]{};GetWindowTextW(buttons[6],current,64);if(wcscmp(current,view.caption))SetWindowTextW(buttons[6],view.caption);
        SendMessageW(buttons[6],BM_SETCHECK,view.selected?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(buttons[6],view.enabled);
    }
    void enableSmart(){auto view=smartButtonState();if(view.selected||!view.enabled)return;smartPending=GetTickCount64()+5000;command(2,2);updateSmartButton();InvalidateRect(hwnd,nullptr,FALSE);}
    void quit(){DestroyWindow(hwnd);}
    CanvasScroll mainView{1060,740},settingsView{670,720};
    bool hasTray()const{return !demo&&!smoke;}
    bool namedLive()const{return vendorFresh(state.vendor,GetTickCount64());}
    const VendorSample*displayReading()const{return displayCache.reading(state.vendor,GetTickCount64());}
    int shownTemperature()const{auto sample=displayReading();return sample?sample->cpu:state.cpu;}
    std::wstring currentMode()const{return !clientError.empty()?L"后台未连接":state.mode==0&&state.resumePending&&state.fault?L"BIOS代管（待恢复）":modeName(static_cast<Mode>(state.mode));}
    GUID iconGuid(bool second=false)const{uint64_t hash=14695981039346656037ULL;for(wchar_t c:exePath()){hash^=static_cast<uint64_t>(towlower(c));hash*=1099511628211ULL;}GUID id{static_cast<DWORD>(hash),static_cast<WORD>(hash>>32),static_cast<WORD>(hash>>48),{0x88,0x17,0x54,0x50,0x46,0x43,static_cast<BYTE>(QUIET_STAGE),static_cast<BYTE>(second)}};return id;}
    State get(){if(engine)return engine->snapshot();State s;Request r;if(remote(r,s)){clientError.clear();config.cpu=s.cpuSensor;config.mask=s.controlMask;config.period=s.period;config.manualMinutes=s.manualMinutes;config.manualExit=s.manualExit;for(int i=0;i<12;++i)config.offset[i]=s.offset[i];config.profile=static_cast<Profile>(s.profile);config.display=s.display;config.autoSmart=s.autoSmart!=0;config.closedLidWork=s.closedLidWork!=0;for(int i=0;i<4;++i){config.curve.enter[i+1]=s.enter[i];config.curve.leave[i+1]=s.leave[i];}return s;}clientError=L"后台服务未连接，数据不可用";s.fault=1;s.alive=0;for(int&i:s.raw)i=-1;textcopy(s.reason,200,clientError);return s;}
    void command(int kind,int value=0,const Config*cfg=nullptr){
        if(engine){Command c;c.kind=kind;c.value=value;if(cfg)c.config=*cfg;engine->submit(c);}
        else{Request r;r.command=kind;r.value=value;if(cfg){for(int i=0;i<4;++i){r.enter[i]=cfg->curve.enter[i+1];r.leave[i]=cfg->curve.leave[i+1];}r.display=cfg->display;r.autoSmart=cfg->autoSmart;r.closedLidWork=cfg->closedLidWork;}State s;if(!remote(r,s))MessageBoxW(hwnd,L"后台服务未连接，操作未执行。",L"TPFC Quiet",MB_ICONWARNING);}
    }
    HWND button(const wchar_t*text,int id,int x,int y,int w,int h,HWND parent=nullptr){HWND owner=parent?parent:hwnd;HWND b=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,pixels(owner,x),pixels(owner,y),pixels(owner,w),pixels(owner,h),owner,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),appInstance,nullptr);SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(parent?settingsFont:controlFont),TRUE);return b;}
    void layout(){if(buttons.size()!=13)return;RECT r=mainView.content(hwnd);int w=r.right;auto move=[&](HWND child,int x,int y,int width,int height,BOOL repaint){MoveWindow(child,pixels(hwnd,x-mainView.x),pixels(hwnd,y-mainView.y),pixels(hwnd,width),pixels(hwnd,height),repaint);};move(buttons[0],w-122,28,88,32,TRUE);move(buttons[1],32,102,132,34,TRUE);move(buttons[2],174,102,100,34,TRUE);move(buttons[3],284,102,132,34,TRUE);move(buttons[4],w-390,102,94,34,TRUE);move(buttons[5],w-286,102,94,34,TRUE);move(buttons[6],w-182,102,150,34,TRUE);move(buttons[7],32,285,84,30,TRUE);move(buttons[8],126,285,84,30,TRUE);move(buttons[9],220,285,84,30,TRUE);int bottom=r.bottom-51;move(buttons[10],32,bottom,156,32,TRUE);move(buttons[11],198,bottom,130,32,TRUE);move(buttons[12],w-242,bottom,210,32,TRUE);}
    HICON makeIcon(bool second=false){
        HDC dc=GetDC(nullptr);HDC m=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,32,32);auto old=SelectObject(m,bmp);fill(m,{0,0,32,32},RGB(245,247,246));
        bool fresh=state.sampleAt&&GetTickCount64()-state.sampleAt<static_cast<uint64_t>(state.period)*3&&state.healthy;
        auto cached=displayReading();bool control=second||state.display==1;int temp=control?state.filtered:shownTemperature();bool available=control?fresh:(cached||fresh);
        std::wstring prefix=control?L"T":cached?L"C":L"E";std::wstring s=available?prefix+number(temp):L"--";HFONT f=font(s.size()>3?14:17,true);auto of=SelectObject(m,f);SetBkMode(m,TRANSPARENT);int hottest=cached?std::max<int>({state.cpu,cached->coreMax,cached->gpu}):state.cpu;SetTextColor(m,state.fault||hottest>=90?RGB(175,45,35):hottest>=80?RGB(174,105,24):Accent);RECT r{0,0,32,25};DrawTextW(m,s.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(m,of);DeleteObject(f);
        COLORREF bar=state.fault||!clientError.empty()?RGB(175,45,35):state.mode==2?Accent:state.mode==3||state.mode==4?RGB(174,105,24):RGB(116,126,132);fill(m,{3,28,29,31},bar);
        HBITMAP mask=CreateBitmap(32,32,1,1,nullptr);ICONINFO i{TRUE,0,0,mask,bmp};HICON out=CreateIconIndirect(&i);DeleteObject(mask);SelectObject(m,old);DeleteObject(bmp);DeleteDC(m);ReleaseDC(nullptr,dc);return out;
    }
    void updateTray(bool add=false){
        if(!hasTray())return;
        HICON next=makeIcon();if(icon)DestroyIcon(icon);icon=next;tray.hIcon=icon;
        auto cached=displayReading();std::wstring source=cached?(namedLive()?L"C CPU / 联想采集":L"C CPU最近有效读数"):L"E "+sensorName(state.cpuSensor)+L"（CPU来源不可用）";
        if(cached){auto age=cached->sourceAgeMs+static_cast<int>(GetTickCount64()-cached->at);source+=L" · "+std::to_wstring(age/1000)+L"秒前";}
        std::wstring controlSource=state.source>=0?sensorName(state.source,state.verified,state.cpuSensor):L"无有效测点";
        std::wstring named=cached?L"  核心最高 "+number(cached->coreMax,L"°C")+L"  GPU "+number(cached->gpu,L"°C"):L"";
        std::wstring tip=source+L" "+number(shownTemperature(),L"°C")+named+L"\n"+currentMode()+L" / "+profileName(static_cast<Profile>(state.profile))+L"\n控制 "+number(state.filtered,L"°C")+L" / "+controlSource+L"\n风扇 "+number(state.rpm[0])+L" / "+number(state.rpm[1])+L" RPM\n"+state.reason;
        textcopy(tray.szTip,128,tip);tray.guidItem=iconGuid();tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_GUID;Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&tray);if(add){tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}
        if(state.display==2){HICON next2=makeIcon(true);if(icon2)DestroyIcon(icon2);icon2=next2;tray2=tray;tray2.uID=2;tray2.guidItem=iconGuid(true);tray2.hIcon=icon2;textcopy(tray2.szTip,128,L"控制温度图标\n"+tip);Shell_NotifyIconW((add||!secondAdded)?NIM_ADD:NIM_MODIFY,&tray2);secondAdded=true;tray2.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray2);}
        else if(secondAdded){Shell_NotifyIconW(NIM_DELETE,&tray2);secondAdded=false;if(icon2){DestroyIcon(icon2);icon2=nullptr;}}
    }
    void graph(HDC dc,RECT r){
        fill(dc,r,RGB(255,255,255));label(dc,r.left+20,r.top+14,500,26,L"温度与风扇趋势  ·  最近30分钟",15,Ink,true);
        label(dc,r.right-240,r.top+16,220,20,L"绿色：控制温度    橙色：风扇1",15,Muted);
        RECT plot{r.left+48,r.top+55,r.right-54,r.bottom-32};
        for(int t=30;t<=90;t+=20){int y=plot.bottom-(t-30)*(plot.bottom-plot.top)/70;rule(dc,plot.left,y,plot.right);label(dc,r.left+14,y-9,32,20,std::to_wstring(t),15,Muted);label(dc,plot.right+8,y-9,40,20,std::to_wstring((t-30)*100),16,Muted);}
        label(dc,plot.left,plot.bottom+8,180,20,L"较早",16,Muted);label(dc,plot.right-36,plot.bottom+8,40,20,L"现在",16,Muted);
        if(state.historyCount<2){label(dc,plot.left+60,plot.top+35,550,50,L"等待有效采样。趋势只显示实际取得的数据。",16,Muted);return;}
        for(int channel=0;channel<2;++channel){HPEN pen=CreatePen(PS_SOLID,2,channel?RGB(186,112,32):Accent);auto old=SelectObject(dc,pen);bool connected=false;uint64_t first=state.history[0].at,last=state.history[state.historyCount-1].at;for(int i=0;i<state.historyCount;++i){auto&p=state.history[i];int value=channel?p.rpm1:p.control;if(value<0){connected=false;continue;}int x=plot.left+static_cast<int>((p.at-first)*(plot.right-plot.left)/std::max<uint64_t>(1,last-first));int temp=channel?30+value/100:value;int y=plot.bottom-(std::clamp(temp,30,100)-30)*(plot.bottom-plot.top)/70;if(connected)LineTo(dc,x,y);else MoveToEx(dc,x,y,nullptr);connected=true;}SelectObject(dc,old);DeleteObject(pen);}
    }
    void paint(HDC dc,RECT r){
        fill(dc,r,Paper);int w=r.right,h=r.bottom;fill(dc,{0,0,w,82},RGB(255,255,255));fill(dc,{0,0,6,82},Accent);
        label(dc,32,22,480,32,L"TPFC Quiet",26,Ink,true);label(dc,242,32,560,22,L"X1 Carbon  ·  "+version()+L"  /  "+(QUIET_STAGE==1?L"原曲线对照":profileName(static_cast<Profile>(state.profile)))+(state.demo?L"  演示数据":L""),15,Muted);
        auto reading=displayReading();bool live=reading!=nullptr;VendorSample displayed=live?*reading:VendorSample{};label(dc,32,151,215,23,live?L"CPU温度 / 联想采集":L"EC热区 / 0x78",16,Muted);
        label(dc,32,176,205,65,number(shownTemperature(),L"°C"),44,Ink,true);label(dc,32,239,210,20,live?L"核心最高 "+number(displayed.coreMax,L"°C"):L"CPU来源暂不可用",12,live&&displayed.coreMax>=90?RGB(175,45,35):Muted);
        label(dc,244,151,160,23,L"GPU / 联想采集",16,Muted);label(dc,244,182,160,50,number(live?displayed.gpu:-1,L"°C"),30,Ink,true);
        label(dc,410,151,190,23,L"控制参考温度",16,Muted);label(dc,410,182,185,50,number(state.filtered,L"°C"),30,Ink,true);label(dc,410,239,200,20,state.source>=0?sensorName(state.source):L"无有效测点",15,Muted);
        label(dc,618,151,210,23,L"风扇1 / 风扇2",16,Muted);label(dc,618,185,210,40,number(state.rpm[0])+L" / "+number(state.rpm[1]),24,Ink,true);label(dc,618,239,210,20,L"RPM · 实测转速",15,Muted);
        int bx=std::max<int>(844,w-185);label(dc,bx,151,175,23,currentMode(),16,state.fault?RGB(175,45,35):Accent,true);label(dc,bx,185,170,24,L"目标档位："+(!clientError.empty()?L"未知":state.requested==128?L"BIOS":number(state.requested)),16,Muted);label(dc,bx,214,170,24,L"读回："+number(state.level[0])+L" / "+number(state.level[1]),15,Muted);
        auto smartView=smartButtonState();rule(dc,32,266,w-32);label(dc,330,291,w-365,30,!smartView.selected&&!smartView.enabled?smartView.hint:std::wstring(state.reason),15,state.fault?RGB(175,45,35):Muted);
        RECT body{32,332,w-32,h-98};
        label(dc,44,h-83,w-90,26,L"Smart仅调节风扇；CPU性能由Windows和联想电源设置管理。",16,Muted);
        if(tab==0){graph(dc,body);}
        else if(tab==1){fill(dc,body,RGB(255,255,255));label(dc,50,342,w-100,24,L"部件温度来自联想采集；ACPI温区仅作参考，不冒充独立主板探头",14,Ink,true);label(dc,52,380,w-104,24,L"CPU "+number(live?displayed.cpu:-1,L"°C")+L"    核心最高 "+number(live?displayed.coreMax:-1,L"°C")+L"    GPU "+number(live?displayed.gpu:-1,L"°C"),15,Ink,true);label(dc,52,414,w-104,24,L"ACPI温区（参考） "+number(live?displayed.acpi:-1,L"°C")+L"    电池 "+number(live?displayed.battery:-1,L"°C")+L"    硬盘 "+number(live?displayed.storage:-1,L"°C"),15,Muted);for(int i=0;i<12;++i){int col=i/4,row=i%4,cw=(w-100)/3,x=52+col*cw,y=446+row*28;label(dc,x,y,cw-90,26,sensorName(i),15,Muted);label(dc,x+cw-86,y,78,26,number(state.raw[i],(state.controlMask&(1<<i))?L"°C":L""),15,Ink,true);}}
        else{fill(dc,body,RGB(255,255,255));label(dc,50,342,500,24,L"最近调速与模式事件",14,Ink,true);for(int i=0;i<std::min<int>(state.eventCount,static_cast<int>((body.bottom-380)/30));++i)label(dc,50,380+i*30,w-102,28,state.events[i],15,Muted);}
    }
    void savePreview(){
        if(preview.empty())return;RECT r;GetClientRect(hwnd,&r);HDC dc=GetDC(hwnd),mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(mem,bmp);int saved=SaveDC(mem);mainView.drawing(mem,hwnd);RECT body=mainView.content(hwnd);paint(mem,body);
        for(HWND b:buttons){RECT br;GetWindowRect(b,&br);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&br),2);br={logical(hwnd,br.left)+mainView.x,logical(hwnd,br.top)+mainView.y,logical(hwnd,br.right)+mainView.x,logical(hwnd,br.bottom)+mainView.y};wchar_t s[120];GetWindowTextW(b,s,120);fill(mem,br,RGB(255,255,255));FrameRect(mem,&br,static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));label(mem,br.left+10,br.top+7,br.right-br.left-12,br.bottom-br.top-4,s,16,Ink);}
        RestoreDC(mem,saved);Gdiplus::Bitmap image(bmp,nullptr);CLSID id;if(!pngEncoder(id))image.Save(preview.c_str(),&id,nullptr);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);ReleaseDC(hwnd,dc);
    }
    void settingsWindow(){
        if(settings){SetForegroundWindow(settings);return;}
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);int width=std::min<int>(pixels(hwnd,720),monitor.rcWork.right-monitor.rcWork.left-24),height=std::min<int>(pixels(hwnd,780),monitor.rcWork.bottom-monitor.rcWork.top-24);
        settings=CreateWindowExW(WS_EX_DLGMODALFRAME,L"TPFCQuietSettings",L"TPFC Quiet 设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VSCROLL|WS_HSCROLL,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,hwnd,nullptr,appInstance,this);
        ShowWindow(settings,SW_SHOW);EnableWindow(hwnd,FALSE);
    }
    void settingsCreate(HWND w){
        auto text=[&](const wchar_t*s,int x,int y,int width){HWND h=CreateWindowW(L"STATIC",s,WS_CHILD|WS_VISIBLE,pixels(w,x),pixels(w,y),pixels(w,width),pixels(w,28),w,nullptr,appInstance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);};
        text(L"日常使用",28,24,600);text(L"默认策略",28,70,160);
        HWND p=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,pixels(w,200),pixels(w,66),pixels(w,240),pixels(w,150),w,reinterpret_cast<HMENU>(301),appInstance,nullptr);SendMessageW(p,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);for(auto s:{L"静音办公（推荐）",L"均衡",L"散热优先"})SendMessageW(p,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));SendMessageW(p,CB_SETCURSEL,state.profile,0);
        if constexpr(QUIET_STAGE==1)EnableWindow(p,FALSE);
        text(L"托盘温度",28,116,160);HWND d=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,pixels(w,200),pixels(w,112),pixels(w,320),pixels(w,150),w,reinterpret_cast<HMENU>(302),appInstance,nullptr);SendMessageW(d,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);for(auto s:{L"C：CPU；来源中断时E：EC热区",L"T：控制参考温度",L"双图标：CPU来源与控制参考"})SendMessageW(d,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));SendMessageW(d,CB_SETCURSEL,state.display,0);
        HWND a=button(L"验证通过后启动时恢复Smart",303,28,159,410,28,w);SetWindowLongPtrW(a,GWL_STYLE,GetWindowLongPtrW(a,GWL_STYLE)&~BS_PUSHBUTTON|BS_AUTOCHECKBOX);SendMessageW(a,BM_SETCHECK,config.autoSmart?BST_CHECKED:BST_UNCHECKED,0);
        text(QUIET_STAGE==1?L"A版保留原Smart曲线，以下设置仅供查看":L"拖动圆点编辑曲线：实心升档，空心降档（°C）",28,211,620);
        for(int i=0;i<4;++i){int y=420+i*42;std::wstring name=std::to_wstring(config.curve.fan[i+1])+L"档";text(name.c_str(),28,y,100);for(int j=0;j<2;++j){std::wstring value=std::to_wstring(j?config.curve.leave[i+1]:config.curve.enter[i+1]);HWND e=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",value.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,pixels(w,200+j*130),pixels(w,y-3),pixels(w,100),pixels(w,28),w,reinterpret_cast<HMENU>(310+i*2+j),appInstance,nullptr);SendMessageW(e,WM_SETFONT,reinterpret_cast<WPARAM>(settingsFont),TRUE);if constexpr(QUIET_STAGE==1)EnableWindow(e,FALSE);}}
        HWND dock=button(L"合盖工作时继续Smart；睡眠时交还BIOS",304,28,573,610,27,w);SetWindowLongPtrW(dock,GWL_STYLE,GetWindowLongPtrW(dock,GWL_STYLE)&~BS_PUSHBUTTON|BS_AUTOCHECKBOX);SendMessageW(dock,BM_SETCHECK,config.closedLidWork?BST_CHECKED:BST_UNCHECKED,0);text(L"CPU/GPU后台采集；来源失效时交还BIOS。",28,607,350);button(L"打开 Lenovo Vantage",323,390,603,248,32,w);
        button(L"保存",320,434,658,96,32,w);button(L"取消",321,542,658,96,32,w);
        if constexpr(QUIET_STAGE==3)button(L"安装或更新后台",322,28,658,218,32,w);
    }
    LRESULT settingsProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
        if(m==WM_CREATE){settingsDpi=GetDpiForWindow(w);settingsFont=font(pixels(w,16));settingsView.x=settingsView.y=0;settingsCreate(w);settingsView.update(w);return 0;}
        if(m==WM_SIZE){settingsView.update(w);return 0;}
        if(m==WM_VSCROLL||m==WM_HSCROLL){settingsView.move(w,m==WM_HSCROLL,LOWORD(wp));return 0;}
        if(m==WM_MOUSEWHEEL){int direction=GET_WHEEL_DELTA_WPARAM(wp)>0?SB_LINEUP:SB_LINEDOWN;for(int i=0;i<3;++i)settingsView.move(w,false,direction);return 0;}
        if(m==WM_DPICHANGED){UINT next=HIWORD(wp);HFONT old=settingsFont;settingsFont=font(MulDiv(16,next,96));struct Resize{HWND window;UINT before,after;HFONT font;} resize{w,settingsDpi,next,settingsFont};
            EnumChildWindows(w,[](HWND child,LPARAM value)->BOOL{auto*p=reinterpret_cast<Resize*>(value);if(GetParent(child)!=p->window)return TRUE;RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,p->window,reinterpret_cast<POINT*>(&r),2);MoveWindow(child,MulDiv(r.left,p->after,p->before),MulDiv(r.top,p->after,p->before),MulDiv(r.right-r.left,p->after,p->before),MulDiv(r.bottom-r.top,p->after,p->before),TRUE);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(p->font),TRUE);return TRUE;},reinterpret_cast<LPARAM>(&resize));settingsDpi=next;if(old)DeleteObject(old);auto*r=reinterpret_cast<RECT*>(lp);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);InvalidateRect(w,nullptr,FALSE);return 0;}
        if(m==WM_DESTROY){if(settingsFont){DeleteObject(settingsFont);settingsFont=nullptr;}return 0;}
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);int saved=SaveDC(dc);settingsView.drawing(dc,w);RECT r{28,247,650,397};fill(dc,r,Paper);for(int t=40;t<=100;t+=10){int x=70+(t-40)*9;label(dc,x-12,374,36,20,std::to_wstring(t),11,Muted);}for(int i=0;i<4;++i){int y=350-i*27;rule(dc,70,y,610);label(dc,38,y-10,30,22,std::to_wstring(config.curve.fan[i+1]),12,Muted);for(int j=0;j<2;++j){BOOL ok;int v=GetDlgItemInt(w,310+i*2+j,&ok,FALSE);if(!ok)continue;int x=70+(std::clamp(v,40,100)-40)*9;HBRUSH b=CreateSolidBrush(j?Paper:Accent);auto old=SelectObject(dc,b);HPEN p=CreatePen(PS_SOLID,2,Accent);auto op=SelectObject(dc,p);Ellipse(dc,x-6,y-6,x+6,y+6);SelectObject(dc,old);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);}}RestoreDC(dc,saved);EndPaint(w,&ps);return 0;}
        if(m==WM_LBUTTONDOWN&&QUIET_STAGE>1){int x=logical(w,static_cast<short>(LOWORD(lp)))+settingsView.x,y=logical(w,static_cast<short>(HIWORD(lp)))+settingsView.y;for(int i=0;i<4;++i)for(int j=0;j<2;++j){BOOL ok;int value=GetDlgItemInt(w,310+i*2+j,&ok,FALSE);int px=70+(value-40)*9,py=350-i*27;if(ok&&abs(x-px)<12&&abs(y-py)<12){dragPoint=i;dragExit=j!=0;SetCapture(w);}}return 0;}
        if(m==WM_MOUSEMOVE&&dragPoint>=0&&(wp&MK_LBUTTON)){int x=logical(w,static_cast<short>(LOWORD(lp)))+settingsView.x;int value=std::clamp(40+(x-70)/9,40,dragExit?89:90);SetDlgItemInt(w,310+dragPoint*2+(dragExit?1:0),value,FALSE);RECT r{28,247,650,397};settingsView.invalidate(w,r);return 0;}
        if(m==WM_LBUTTONUP&&dragPoint>=0){dragPoint=-1;ReleaseCapture();return 0;}
        if(m==WM_COMMAND&&HIWORD(wp)==EN_CHANGE){RECT r{28,247,650,397};settingsView.invalidate(w,r);return 0;}
        if(m==WM_COMMAND){int id=LOWORD(wp);if(id==321){SendMessageW(w,WM_CLOSE,0,0);return 0;}if(id==320){Config c=config;c.profile=static_cast<Profile>(SendDlgItemMessageW(w,301,CB_GETCURSEL,0,0));c.display=static_cast<int>(SendDlgItemMessageW(w,302,CB_GETCURSEL,0,0));c.autoSmart=SendDlgItemMessageW(w,303,BM_GETCHECK,0,0)==BST_CHECKED;c.closedLidWork=SendDlgItemMessageW(w,304,BM_GETCHECK,0,0)==BST_CHECKED;bool ok=true;for(int i=0;i<4;++i)for(int j=0;j<2;++j){BOOL translated=FALSE;int v=GetDlgItemInt(w,310+i*2+j,&translated,FALSE);ok=ok&&translated;if(j)c.curve.leave[i+1]=v;else c.curve.enter[i+1]=v;}if(!ok||!c.valid()){MessageBoxW(w,L"曲线须递增，退出阈值低于进入阈值。最高档进入温度不得高于90°C。",L"设置未保存",MB_ICONWARNING);return 0;}command(6,static_cast<int>(c.profile),&c);config=c;SendMessageW(w,WM_CLOSE,0,0);return 0;}
        if(id==323){ShellExecuteW(w,L"open",L"shell:AppsFolder\\E046963F.LenovoCompanion_k1h2ywk1493x8!App",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if(id==322){if(engine)engine->stop();auto path=exePath();auto h=ShellExecuteW(w,L"runas",path.c_str(),L"--install",exeDir().c_str(),SW_HIDE);if(reinterpret_cast<INT_PTR>(h)<=32)MessageBoxW(w,L"未能启动后台安装，当前控制已停止。可重新打开程序恢复观察。",L"安装未完成",MB_ICONWARNING);else MessageBoxW(w,L"安装程序已启动。安装结果会单独显示；当前窗口将关闭。",L"后台安装",MB_OK);DestroyWindow(w);settings=nullptr;EnableWindow(hwnd,TRUE);DestroyWindow(hwnd);return 0;}}
        if(m==WM_CLOSE){EnableWindow(hwnd,TRUE);DestroyWindow(w);settings=nullptr;SetForegroundWindow(hwnd);return 0;}return DefWindowProcW(w,m,wp,lp);
    }
    void trayMenu(){HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,401,L"打开面板");AppendMenuW(m,MF_STRING,402,L"静音办公");AppendMenuW(m,MF_STRING,403,L"影子观察（BIOS控制）");AppendMenuW(m,MF_STRING,404,L"交还BIOS");HMENU manualMenu=CreatePopupMenu();for(int f:{0,1,2,4,7}){auto s=L"固定"+std::to_wstring(f)+L"档（最多10分钟）";AppendMenuW(manualMenu,MF_STRING|(state.ready&&f!=0?0:MF_GRAYED),450+f,s.c_str());}AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(manualMenu),L"限时手动");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,405,L"退出程序");SetForegroundWindow(hwnd);POINT p;GetCursorPos(&p);int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);DestroyMenu(m);if(id==401){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);}if(id==402)command(1,0);if(id==403)command(2,1);if(id==404)command(2,0);if(id==405)quit();if(id>=450&&id<=457){command(7,id-450);}}
    LRESULT proc(UINT m,WPARAM wp,LPARAM lp){
        if(m==taskbarCreated){updateTray(true);return 0;}
        if(m==WM_CREATE){controlFont=font(pixels(hwnd,16));buttons.push_back(button(L"设置",100,0,0,1,1));buttons.push_back(button(L"静音办公",101,0,0,1,1));buttons.push_back(button(L"均衡",102,0,0,1,1));buttons.push_back(button(L"散热优先",103,0,0,1,1));buttons.push_back(button(L"BIOS",104,0,0,1,1));buttons.push_back(button(L"影子观察",105,0,0,1,1));buttons.push_back(button(L"启用Smart",106,0,0,1,1));buttons.push_back(button(L"概览",110,0,0,1,1));buttons.push_back(button(L"测点",111,0,0,1,1));buttons.push_back(button(L"事件",112,0,0,1,1));buttons.push_back(button(L"刚才风扇很吵",120,0,0,1,1));buttons.push_back(button(L"导出诊断",121,0,0,1,1));buttons.push_back(button(L"停止控制并交还BIOS",122,0,0,1,1));
            SetWindowLongPtrW(buttons[6],GWL_STYLE,(GetWindowLongPtrW(buttons[6],GWL_STYLE)&~BS_TYPEMASK)|BS_CHECKBOX|BS_PUSHLIKE);
            if(client)ChangeWindowMessageFilterEx(hwnd,WM_APP+2,MSGFLT_ALLOW,nullptr);tray.cbSize=sizeof(tray);tray.hWnd=hwnd;tray.uID=1;tray.uCallbackMessage=WM_APP+1;state=get();displayCache.observe(state.vendor,GetTickCount64());updateSmartButton();updateTray(true);lidNotify=RegisterPowerSettingNotification(hwnd,&GUID_LIDSWITCH_STATE_CHANGE,DEVICE_NOTIFY_WINDOW_HANDLE);SetTimer(hwnd,1,1000,nullptr);started=GetTickCount64();if constexpr(QUIET_STAGE==1){for(int k=1;k<=3;++k)EnableWindow(buttons[k],FALSE);}return 0;}
        if(m==WM_GETMINMAXINFO){auto*p=reinterpret_cast<MINMAXINFO*>(lp);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);p->ptMinTrackSize={std::min<int>(pixels(hwnd,1060),monitor.rcWork.right-monitor.rcWork.left-16),std::min<int>(pixels(hwnd,740),monitor.rcWork.bottom-monitor.rcWork.top-16)};return 0;}
        if(m==WM_VSCROLL||m==WM_HSCROLL){mainView.move(hwnd,m==WM_HSCROLL,LOWORD(wp));return 0;}
        if(m==WM_MOUSEWHEEL){int direction=GET_WHEEL_DELTA_WPARAM(wp)>0?SB_LINEUP:SB_LINEDOWN;for(int i=0;i<3;++i)mainView.move(hwnd,false,direction);return 0;}
        if(m==WM_DPICHANGED){HFONT old=controlFont;controlFont=font(MulDiv(16,HIWORD(wp),96));for(HWND child:buttons)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont),TRUE);if(old)DeleteObject(old);auto*r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        if(m==WM_SIZE){if(wp==SIZE_MINIMIZED){if(hasTray())ShowWindow(hwnd,SW_HIDE);}else{mainView.update(hwnd);layout();}return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);HDC mem=CreateCompatibleDC(dc);HBITMAP b=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(mem,b);int saved=SaveDC(mem);mainView.drawing(mem,hwnd);RECT body=mainView.content(hwnd);paint(mem,body);RestoreDC(mem,saved);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(b);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
        if(m==WM_TIMER){state=get();displayCache.observe(state.vendor,GetTickCount64());updateSmartButton();updateTray();InvalidateRect(hwnd,nullptr,FALSE);if(smoke&&GetTickCount64()-started>5000){savePreview();std::ofstream report(dataDir()/L"ui-smoke.txt");report<<"window_created=1\ncontrols="<<buttons.size()<<"\nstate_demo="<<state.demo<<"\nversion="<<utf8(version())<<"\ndpi="<<GetDpiForWindow(hwnd)<<"\n";wchar_t smartCaption[64]{};GetWindowTextW(buttons[6],smartCaption,64);report<<"cpu_limit_controls="<<(GetDlgItem(hwnd,123)!=nullptr||GetDlgItem(hwnd,124)!=nullptr)<<"\n";report<<"smart_caption="<<utf8(smartCaption)<<"\nsmart_checked="<<SendMessageW(buttons[6],BM_GETCHECK,0,0)<<"\n";DestroyWindow(hwnd);}return 0;}
        if(m==WM_APP+2){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);return 0;}
        if(m==WM_APP+3){quit();return 0;}
        if(m==WM_APP+1){UINT n=LOWORD(lp);if(n==WM_CONTEXTMENU||n==WM_RBUTTONUP)trayMenu();else if(n==NIN_SELECT||n==WM_LBUTTONUP){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);}return 0;}
        if(m==WM_COMMAND){int id=LOWORD(wp);if(id==100)settingsWindow();if(id>=101&&id<=103)command(1,id-101);if(id==104||id==122)command(2,0);if(id==105)command(2,1);if(id==106)enableSmart();if(id>=110&&id<=112){tab=id-110;InvalidateRect(hwnd,nullptr,FALSE);}if(id==120){command(4);MessageBoxW(hwnd,L"已标记。诊断文件将包含前后5分钟记录；请让程序继续运行。",L"噪声事件",MB_OK);}if(id==121){command(5);ShellExecuteW(hwnd,L"open",dataDir(client).c_str(),nullptr,nullptr,SW_SHOW);}return 0;}
        if(m==WM_POWERBROADCAST&&engine){if(wp==PBT_APMSUSPEND)engine->power(true);if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND)engine->power(false);if(wp==PBT_POWERSETTINGCHANGE){auto*p=reinterpret_cast<POWERBROADCAST_SETTING*>(lp);if(p&&p->PowerSetting==GUID_LIDSWITCH_STATE_CHANGE&&p->DataLength>=sizeof(DWORD))engine->power(*reinterpret_cast<DWORD*>(p->Data)==0,true);}return TRUE;}
        if(m==WM_CLOSE){if(hasTray())ShowWindow(hwnd,SW_HIDE);else DestroyWindow(hwnd);return 0;}
        if(m==WM_QUERYENDSESSION){return TRUE;}
        if(m==WM_ENDSESSION){if(engine)engine->stop();return 0;}
        if(m==WM_DESTROY){KillTimer(hwnd,1);if(hasTray())Shell_NotifyIconW(NIM_DELETE,&tray);if(secondAdded)Shell_NotifyIconW(NIM_DELETE,&tray2);if(icon)DestroyIcon(icon);if(icon2)DestroyIcon(icon2);if(lidNotify)UnregisterPowerSettingNotification(lidNotify);if(controlFont)DeleteObject(controlFont);PostQuitMessage(0);return 0;}return DefWindowProcW(hwnd,m,wp,lp);
    }
    static LRESULT CALLBACK base(HWND w,UINT m,WPARAM wp,LPARAM lp){Window*p=reinterpret_cast<Window*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){p=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);p->hwnd=w;SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}return p?p->proc(m,wp,lp):DefWindowProcW(w,m,wp,lp);}
    static LRESULT CALLBACK settingsBase(HWND w,UINT m,WPARAM wp,LPARAM lp){Window*p=reinterpret_cast<Window*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){p=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}return p?p->settingsProc(w,m,wp,lp):DefWindowProcW(w,m,wp,lp);}
public:
    Window(Engine*e,Config c,bool simulated,bool test,std::wstring image):engine(e),client(e==nullptr),demo(simulated),smoke(test),config(std::move(c)),preview(std::move(image)){}
    int run(bool hidden=false){
        Gdiplus::GdiplusStartupInput gi;ULONG_PTR token;Gdiplus::GdiplusStartup(&token,&gi,nullptr);taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        const wchar_t*windowClass=smoke?L"TPFCQuietWindow.Smoke":client?ClientWindowClass:L"TPFCQuietWindow";
        WNDCLASSW wc{};wc.hInstance=appInstance;wc.lpfnWndProc=base;wc.lpszClassName=windowClass;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(appInstance,MAKEINTRESOURCEW(101));RegisterClassW(&wc);wc.lpfnWndProc=settingsBase;wc.lpszClassName=L"TPFCQuietSettings";wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(POINT{},MONITOR_DEFAULTTOPRIMARY),&monitor);int dpi=static_cast<int>(GetDpiForSystem());int width=std::min<int>(MulDiv(1120,dpi,96),monitor.rcWork.right-monitor.rcWork.left-24),height=std::min<int>(MulDiv(800,dpi,96),monitor.rcWork.bottom-monitor.rcWork.top-24);
        HWND w=CreateWindowExW(WS_EX_APPWINDOW,windowClass,(L"TPFC Quiet "+version()+L" · 静音办公").c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,nullptr,nullptr,appInstance,this);
        if(!w){Gdiplus::GdiplusShutdown(token);return 1;}mainView.update(w);layout();if(!hidden)ShowWindow(w,SW_SHOW);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(settings&&IsDialogMessageW(settings,&msg))continue;if(IsDialogMessageW(w,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}Gdiplus::GdiplusShutdown(token);return 0;
    }
};
}
