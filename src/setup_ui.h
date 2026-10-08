#pragma once
#include <commctrl.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>

namespace micfilter {
struct SetupInput {std::wstring name,status;bool available=true,checked=false;};
// A native, keyboard-accessible setup surface. Installation stays on the existing elevated path.
class SetupUi {
    HWND window_=nullptr,list_=nullptr,button_=nullptr,profile_=nullptr,profileLabel_=nullptr,title_=nullptr,subtitle_=nullptr,note_=nullptr,progress_=nullptr;
    HFONT font_=nullptr,heading_=nullptr;
    HBRUSH background_=CreateSolidBrush(RGB(247,250,252));
    bool accepted_=false,busy_=false,finished_=false;
    int scale_=96;
    std::wstring progressPath_;
    int px(int value)const{return MulDiv(value,scale_,96);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<SetupUi*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<SetupUi*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
        switch(msg){
        case WM_SIZE:self->layout();return 0;
        case WM_DPICHANGED:{self->scale_=HIWORD(wp);self->fonts();const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
        case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(40,57,72));return reinterpret_cast<LRESULT>(self->background_);}
        case WM_ERASEBKGND:{RECT r;GetClientRect(hwnd,&r);FillRect(reinterpret_cast<HDC>(wp),&r,self->background_);return 1;}
        case WM_NOTIFY:{auto* header=reinterpret_cast<NMHDR*>(lp);
            if(header->hwndFrom==self->list_&&header->code==LVN_ITEMCHANGING){
                auto* change=reinterpret_cast<NMLISTVIEW*>(lp);
                if(change->iItem>=0&&static_cast<size_t>(change->iItem)<self->inputs.size()&&!self->inputs[change->iItem].available&&
                   (change->uNewState&LVIS_STATEIMAGEMASK)!=(change->uOldState&LVIS_STATEIMAGEMASK))return TRUE;
            }
            if(header->hwndFrom==self->list_&&header->code==LVN_ITEMCHANGED){
                self->refreshButton();auto* change=reinterpret_cast<NMLISTVIEW*>(lp);
                if(self->note_&&change->iItem>=0&&static_cast<size_t>(change->iItem)<self->inputs.size()&&(change->uNewState&LVIS_SELECTED))
                    SetWindowTextW(self->note_,self->inputs[change->iItem].available?L"Your existing microphones and settings are kept.\nNo recordings. No account. Works offline.":L"This input has other audio effects. They will be kept.\nChoose another input, or remove the other app's effect and run setup again.");
            }
            return 0;}
        case WM_COMMAND:
            if(LOWORD(wp)==1){if(self->finished_)DestroyWindow(hwnd);else{self->accepted_=true;ShowWindow(hwnd,SW_HIDE);}}return 0;
        case WM_TIMER:
            if(!self->progressPath_.empty()){
                const auto text=readText(self->progressPath_);
                if(!text.empty())SetWindowTextW(self->note_,text.c_str());
            }return 0;
        case WM_CLOSE:if(!self->busy_)DestroyWindow(hwnd);return 0;
        case WM_DESTROY:self->window_=nullptr;return 0;
        }return DefWindowProcW(hwnd,msg,wp,lp);
    }
    void fonts(){
        HFONT previous=font_,previousHeading=heading_;
        font_=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        heading_=CreateFontW(-px(29),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        if(window_)EnumChildWindows(window_,[](HWND child,LPARAM font)->BOOL{SendMessageW(child,WM_SETFONT,static_cast<WPARAM>(font),TRUE);return TRUE;},reinterpret_cast<LPARAM>(font_));
        if(title_)SendMessageW(title_,WM_SETFONT,reinterpret_cast<WPARAM>(heading_),TRUE);
        if(previous)DeleteObject(previous);if(previousHeading)DeleteObject(previousHeading);
    }
    HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int id=0){
        auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);return h;
    }
    void layout(){
        if(!title_)return;
        RECT r{};GetClientRect(window_,&r);const int width=r.right,height=r.bottom,margin=px(32);
        MoveWindow(title_,margin,px(28),width-2*margin,px(42),TRUE);
        MoveWindow(subtitle_,margin,px(80),width-2*margin,px(52),TRUE);
        MoveWindow(list_,margin,px(146),width-2*margin,std::max(px(100),height-px(364)),TRUE);
        ListView_SetColumnWidth(list_,0,(width-2*margin)*55/100);ListView_SetColumnWidth(list_,1,(width-2*margin)*44/100);
        MoveWindow(profile_,margin,height-px(172),width-2*margin,px(160),TRUE);
        MoveWindow(profileLabel_,margin,height-px(202),width-2*margin,px(24),TRUE);
        MoveWindow(note_,margin,height-px(132),width-2*margin,px(54),TRUE);
        if(finished_)MoveWindow(note_,margin,px(156),width-2*margin,height-px(250),TRUE);
        MoveWindow(progress_,margin,height-px(64),width-px(248),px(12),TRUE);
        MoveWindow(button_,width-margin-px(170),height-px(64),px(170),px(38),TRUE);
    }
    void refreshButton(){
        if(!button_||busy_||finished_)return;
        bool checked=false;for(size_t i=0;i<inputs.size();++i)if(inputs[i].available&&ListView_GetCheckState(list_,static_cast<int>(i)))checked=true;
        EnableWindow(button_,checked);
    }
public:
    std::vector<SetupInput> inputs;
    explicit SetupUi(std::vector<SetupInput> items,bool visible=true):inputs(std::move(items)){
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&common);
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        scale_=GetDpiForSystem();fonts();
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"MicFilter.Setup.Wizard";cls.lpfnWndProc=procedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(cls.hInstance,MAKEINTRESOURCEW(1));RegisterClassW(&cls);
        const int width=px(740),height=px(620);
        window_=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"MicFilter Setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            (GetSystemMetrics(SM_CXSCREEN)-width)/2,(GetSystemMetrics(SM_CYSCREEN)-height)/2,width,height,nullptr,nullptr,cls.hInstance,this);
        if(!window_)throw std::runtime_error("Could not open the setup window.");
        scale_=GetDpiForWindow(window_);fonts();
        title_=control(L"STATIC",L"A clearer voice starts here",0);SendMessageW(title_,WM_SETFONT,reinterpret_cast<WPARAM>(heading_),TRUE);
        subtitle_=control(L"STATIC",L"Choose one or more microphones. We will take care of setup.\nYou can add more later by running this installer again.",0);
        list_=control(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_NOSORTHEADER|LVS_SHOWSELALWAYS);
        SetWindowTheme(list_,L"Explorer",nullptr);ListView_SetExtendedListViewStyle(list_,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=px(370);column.pszText=const_cast<wchar_t*>(L"Microphone");ListView_InsertColumn(list_,0,&column);
        column.cx=px(270);column.pszText=const_cast<wchar_t*>(L"Ready to use?");ListView_InsertColumn(list_,1,&column);
        for(size_t i=0;i<inputs.size();++i){
            LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=inputs[i].name.data();ListView_InsertItem(list_,&item);
            ListView_SetItemText(list_,item.iItem,1,inputs[i].status.data());ListView_SetCheckState(list_,item.iItem,inputs[i].available&&inputs[i].checked);
        }
        profileLabel_=control(L"STATIC",L"Choose your voice sound (optional)",0);
        profile_=control(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
        for(const auto* name:{L"Voice sound: keep my current choice",L"Natural - your voice as captured",L"Clear - gentle clarity for everyday calls",L"Broadcast - warm speech, even volume",L"Deep - body and clarity for lower voices"})SendMessageW(profile_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(profile_,CB_SETCURSEL,0,0);
        note_=control(L"STATIC",L"Your existing microphones and settings are kept.\nNo recordings. No account. Works offline.",0);
        progress_=control(PROGRESS_CLASSW,L"",PBS_MARQUEE);ShowWindow(progress_,SW_HIDE);
        button_=control(L"BUTTON",L"Install selected",WS_TABSTOP|BS_DEFPUSHBUTTON,1);
        refreshButton();layout();if(visible)ShowWindow(window_,SW_SHOW);UpdateWindow(window_);
    }
    ~SetupUi(){if(window_)DestroyWindow(window_);DeleteObject(font_);DeleteObject(heading_);DeleteObject(background_);}
    static std::wstring readText(const std::wstring& path){
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(f==INVALID_HANDLE_VALUE)return L"";
        char data[8192]{};DWORD count=0;ReadFile(f,data,sizeof(data),&count,nullptr);CloseHandle(f);
        int n=MultiByteToWideChar(CP_UTF8,0,data,count,nullptr,0);std::wstring text(n,0);MultiByteToWideChar(CP_UTF8,0,data,count,text.data(),n);return text;
    }
    void pump(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(!window_||!IsDialogMessageW(window_,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}}
    void hide(){ShowWindow(window_,SW_HIDE);}
    bool selfTest(){
        if(!window_||!list_||!profile_||!button_||ListView_GetItemCount(list_)!=3)return false;
        for(int i=0;i<3;++i)ListView_SetCheckState(list_,i,FALSE);
        if(IsWindowEnabled(button_))return false;
        ListView_SetCheckState(list_,0,TRUE);ListView_SetCheckState(list_,1,TRUE);ListView_SetCheckState(list_,2,TRUE);
        if(selection()!=std::vector<size_t>{0,1}||!IsWindowEnabled(button_))return false;
        SendMessageW(profile_,CB_SETCURSEL,4,0);if(preset()!=3)return false;
        SendMessageW(profile_,CB_SETCURSEL,0,0);return preset()==-1;
    }
    bool savePreview(const std::wstring& path){
        RECT r{};GetWindowRect(window_,&r);const int width=r.right-r.left,height=r.bottom-r.top;
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};
        info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};void* data=nullptr;
        HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&data,nullptr,0);auto old=SelectObject(dc,bitmap);
        SendMessageW(window_,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND|PRF_CHILDREN);
        const bool painted=true;
        BITMAPFILEHEADER fileHeader{0x4d42,static_cast<DWORD>(sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+width*height*4),0,0,sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)};
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);DWORD written=0;
        const bool saved=painted&&file!=INVALID_HANDLE_VALUE&&WriteFile(file,&fileHeader,sizeof(fileHeader),&written,nullptr)&&WriteFile(file,&info.bmiHeader,sizeof(info.bmiHeader),&written,nullptr)&&WriteFile(file,data,width*height*4,&written,nullptr);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return saved;
    }
    bool choose(){while(window_&&!accepted_){MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);pump();}return accepted_;}
    std::vector<size_t> selection()const{std::vector<size_t> result;for(size_t i=0;i<inputs.size();++i)if(inputs[i].available&&ListView_GetCheckState(list_,static_cast<int>(i)))result.push_back(i);return result;}
    int preset()const{return static_cast<int>(SendMessageW(profile_,CB_GETCURSEL,0,0))-1;}
    void begin(const std::wstring& path=L""){
        busy_=true;progressPath_=path;ShowWindow(window_,SW_SHOW);
        SetWindowTextW(title_,L"Getting your microphones ready");
        SetWindowTextW(subtitle_,L"Windows may ask for administrator permission.\nSetup will check audio on each selected microphone.");
        SetWindowTextW(note_,L"Installing safely. Please keep this window open.");EnableWindow(list_,FALSE);EnableWindow(profile_,FALSE);EnableWindow(button_,FALSE);
        ShowWindow(progress_,SW_SHOW);SendMessageW(progress_,PBM_SETMARQUEE,TRUE,30);SetTimer(window_,1,200,nullptr);
    }
    void finish(bool success,const std::wstring& summary){
        busy_=false;finished_=true;KillTimer(window_,1);ShowWindow(window_,SW_SHOW);
        SetWindowTextW(title_,success?L"Your voice, with less noise":L"Let's get this sorted");
        SetWindowTextW(subtitle_,success?L"Setup complete. Your microphone settings are saved.":L"Setup could not finish. The message below explains the next step.");
        ShowWindow(list_,SW_HIDE);ShowWindow(profile_,SW_HIDE);ShowWindow(profileLabel_,SW_HIDE);ShowWindow(progress_,SW_HIDE);
        RECT r{};GetClientRect(window_,&r);MoveWindow(note_,px(32),px(156),r.right-px(64),r.bottom-px(250),TRUE);SetWindowTextW(note_,summary.c_str());
        SetWindowTextW(button_,L"Close");EnableWindow(button_,TRUE);SetFocus(button_);
        while(window_){MsgWaitForMultipleObjects(0,nullptr,FALSE,100,QS_ALLINPUT);pump();}
    }
    void wait(HANDLE process){while(WaitForSingleObject(process,0)==WAIT_TIMEOUT){MsgWaitForMultipleObjects(1,&process,FALSE,100,QS_ALLINPUT);pump();}}
};
}
