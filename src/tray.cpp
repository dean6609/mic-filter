#include "shared.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <filesystem>
#include "audio_check.h"
#include "apo_sdk.h"

namespace {
constexpr UINT trayMessage=WM_APP+1,refreshMessage=WM_APP+2;
constexpr UINT idToggle=100,idInstall=101,idRemove=102,idExit=103,idInfo=104,idStartup=105;
constexpr UINT idReference=110,idGentle=111,idNoGate=112,idWetFull=113,idWetPartial=114;
constexpr UINT idInstallerLog=115,idUninstall=116;
constexpr UINT idVoice=120; // idVoice+preset, presets 0..3
constexpr UINT idAddMicrophones=130,idDevice=1000;
struct Device{std::wstring id,name,guid;};
std::vector<Device> devices;
Device selected;
micfilter::StateMapping mapping;
micfilter::StateMapping endpointTelemetry;
micfilter::OptionsMapping options;
HWND window=nullptr;
NOTIFYICONDATAW tray{};
HICON icons[3]{};
std::wstring status;
std::wstring configuredGuid;
UINT taskbarCreated=0;
HANDLE installerProcess=nullptr;
bool installerRemoving=false;
std::wstring installerResultPath,lastInstallerLog,installerEndpointGuid;
HWND existingTray(){auto current=FindWindowW(micfilter::kWindowClass,nullptr);return current?current:FindWindowW(L"WavoFilter.Native.Tray.v1",nullptr);}
void printUtf8(const std::wstring& text){const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string bytes(count,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),bytes.data(),count,nullptr,nullptr);DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);}
void checkCom(){
    class Outer final:public IUnknown {
        ULONG references_=1;
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
        ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}
        ULONG STDMETHODCALLTYPE Release() override{return --references_;}
    } outer;
    IUnknown* object=nullptr;
    const auto normal=CoCreateInstance(micfilter::kClsid,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUnknown),reinterpret_cast<void**>(&object));if(object)object->Release();object=nullptr;
    const auto aggregate=CoCreateInstance(micfilter::kClsid,&outer,CLSCTX_INPROC_SERVER,__uuidof(IUnknown),reinterpret_cast<void**>(&object));
    std::wostringstream report;report<<L"Windows COM normal HRESULT=0x"<<std::hex<<static_cast<unsigned long>(normal)<<L"\nWindows COM aggregate HRESULT=0x"<<static_cast<unsigned long>(aggregate)<<L"\n";
    if(object){IAudioProcessingObject* apo=nullptr;const auto queried=object->QueryInterface(__uuidof(IAudioProcessingObject),reinterpret_cast<void**>(&apo));report<<L"Windows COM APO interface HRESULT=0x"<<static_cast<unsigned long>(queried)<<L"\n";if(apo)apo->Release();object->Release();}
    printUtf8(report.str());
}
std::wstring executableDirectory(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);std::wstring s=p;return s.substr(0,s.find_last_of(L"\\/"));}
std::wstring logDirectory(){wchar_t path[32768]{};const auto count=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);return count&&count<32768?std::wstring(path)+L"\\MicFilter\\logs":L"";}
std::wstring latestInstallerLog(){
    const auto directory=logDirectory();if(directory.empty())return L"";
    WIN32_FIND_DATAW entry{};HANDLE files=FindFirstFileW((directory+L"\\installer-*.log").c_str(),&entry);if(files==INVALID_HANDLE_VALUE)return L"";
    FILETIME latest{};std::wstring result;
    do{if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&CompareFileTime(&entry.ftLastWriteTime,&latest)>0){latest=entry.ftLastWriteTime;result=directory+L"\\"+entry.cFileName;}}while(FindNextFileW(files,&entry));
    FindClose(files);return result;
}
std::wstring readResult(const std::wstring& path){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return L"";
    LARGE_INTEGER length{};if(!GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>65536){CloseHandle(file);return L"";}
    std::string bytes(static_cast<size_t>(length.QuadPart),'\0');DWORD count=0;const bool ok=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)!=0;CloseHandle(file);if(!ok)return L"";
    const int characters=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),count,nullptr,0);if(!characters)return L"";
    std::wstring result(characters,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),count,result.data(),characters);return result;
}
std::wstring prop(IPropertyStore* store,const PROPERTYKEY& key){PROPVARIANT value;PropVariantInit(&value);std::wstring result;if(SUCCEEDED(store->GetValue(key,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)result=value.pwszVal;PropVariantClear(&value);return result;}
void enumerate(){
    const auto previous=selected.guid;devices.clear();selected={};
    wchar_t configured[128]{};DWORD configuredBytes=sizeof(configured);
    const auto configuration=L"SOFTWARE\\Classes\\CLSID\\"+std::wstring(micfilter::kClsidText);
    configuredGuid=RegGetValueW(HKEY_LOCAL_MACHINE,configuration.c_str(),L"EndpointGuid",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,configured,&configuredBytes)==ERROR_SUCCESS?configured:L"";
    IMMDeviceEnumerator* enumerator=nullptr;IMMDeviceCollection* collection=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))))return;
    if(SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection))){
        UINT count=0;collection->GetCount(&count);
        for(UINT i=0;i<count;++i){IMMDevice* device=nullptr;IPropertyStore* store=nullptr;LPWSTR id=nullptr;
            if(SUCCEEDED(collection->Item(i,&device))){
                if(SUCCEEDED(device->GetId(&id))&&SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store))){
                    Device d{id,prop(store,PKEY_Device_FriendlyName),prop(store,PKEY_AudioEndpoint_GUID)};
                    devices.push_back(d);
                    if(!configuredGuid.empty()){if(_wcsicmp(d.guid.c_str(),configuredGuid.c_str())==0)selected=d;}
                }
                if(store)store->Release();if(id)CoTaskMemFree(id);device->Release();
            }
        }collection->Release();
    }enumerator->Release();
    // Prefer the user's tray selection, then any connected input actually attached to our APO.
    for(const auto& d:devices){
        const auto path=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\"+d.guid+L"\\FxProperties";
        wchar_t value[128]{};DWORD bytes=sizeof(value);
        if(RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes)==ERROR_SUCCESS&&_wcsicmp(value,micfilter::kClsidText)==0){
            if(selected.id.empty()||_wcsicmp(d.guid.c_str(),previous.c_str())==0)selected=d;
        }
    }
    if(configuredGuid.empty()&&devices.size()==1)selected=devices.front();
    endpointTelemetry.close();if(!selected.guid.empty())endpointTelemetry.openEndpoint(selected.guid);
}
bool installed(const Device& device){
    if(device.guid.empty())return false;
    auto path=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\"+device.guid+L"\\FxProperties";
    wchar_t value[128]{};DWORD bytes=sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes)==ERROR_SUCCESS&&_wcsicmp(value,micfilter::kClsidText)==0;
}
bool installed(){return installed(selected);}
bool componentizedEndpoint(){
    if(selected.guid.empty())return false;
    const auto path=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\"+selected.guid+L"\\FxProperties";
    wchar_t value[1024]{};DWORD bytes=sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"{9e6136e0-57ab-4949-b57a-3627be142855},100",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes)==ERROR_SUCCESS&&value[0];
}
std::wstring telemetryText(micfilter::SharedState* p){
    if(!p||!micfilter::read(p->producerPid))return L"Effect activity: not observed.\nEffect format: not checked.\n";
    std::wstring text=micfilter::recentAudio(p)?L"Effect activity: recent.\nActive format: ":L"Effect activity: stopped; historical data.\nLast format: ";
    text+=std::to_wstring(micfilter::read(p->sampleRate))+L" Hz / "+std::to_wstring(micfilter::read(p->channels))+L" channels\nEffect callbacks: "+std::to_wstring(micfilter::read(p->callbacks))+L"\nFiltered frames: "+std::to_wstring(micfilter::read(p->processedFrames))+L"\n";
    return text;
}
HICON createIcon(COLORREF color){
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,32,32),old=static_cast<HBITMAP>(SelectObject(dc,bitmap));
    RECT area{0,0,32,32};FillRect(dc,&area,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HBRUSH brush=CreateSolidBrush(color);HGDIOBJ oldBrush=SelectObject(dc,brush);HPEN pen=CreatePen(PS_SOLID,2,color);HGDIOBJ oldPen=SelectObject(dc,pen);
    RoundRect(dc,11,3,22,22,10,10);SelectObject(dc,GetStockObject(NULL_BRUSH));Arc(dc,7,10,26,27,7,15,26,15);
    MoveToEx(dc,16,25,nullptr);LineTo(dc,16,30);MoveToEx(dc,10,30,nullptr);LineTo(dc,23,30);
    SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);DeleteObject(brush);SelectObject(dc,old);
    BYTE maskBits[128]{};HBITMAP mask=CreateBitmap(32,32,1,1,maskBits);ICONINFO info{TRUE,0,0,mask,bitmap};HICON icon=CreateIconIndirect(&info);
    DeleteObject(mask);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return icon;
}
void update(){
    if(!mapping.get())mapping.open();
    auto* state=mapping.get();int icon=2;
    if(installerProcess)status=installerRemoving?L"Restoring configuration...":L"Installing effect...";
    else if(selected.id.empty())status=L"Selected microphone disconnected";
    else if(!installed()){status=L"Original audio; effect not installed";icon=1;}
    else if(!state)status=L"Controls unavailable; original audio";
    else if(!micfilter::read(state->enabled)){status=L"Original audio";icon=1;}
    else if(micfilter::recentAudio(endpointTelemetry.get())){
        if(micfilter::read(endpointTelemetry.get()->formatSupported)){status=L"Filter confirmed: RNNoise v1.21";icon=0;}
        else status=L"Unsupported format; original audio";
    }else status=micfilter::read(state->producerPid)?L"Filter enabled; waiting for microphone use":L"Effect installed; processing not confirmed";
    tray.hIcon=icons[icon];auto tip=L"MicFilter - "+status;wcsncpy_s(tray.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void toggle(){if(!installed()||!mapping.get()){MessageBoxW(window,L"Run MicFilter-Setup.exe to install the effect on a microphone first.",L"MicFilter",MB_OK|MB_ICONINFORMATION);return;}auto* p=mapping.get();InterlockedExchange(&p->enabled,!micfilter::read(p->enabled));update();}
void installer(bool remove){
    if(installerProcess)return;
    if(selected.guid.empty()){MessageBoxW(window,L"Connect the selected microphone to install or remove its effect.",L"MicFilter",MB_OK);return;}
    const auto dir=executableDirectory();
    const auto logs=logDirectory();
    const int made=logs.empty()?ERROR_PATH_NOT_FOUND:SHCreateDirectoryExW(window,logs.c_str(),nullptr);
    if(made!=ERROR_SUCCESS&&made!=ERROR_ALREADY_EXISTS&&made!=ERROR_FILE_EXISTS){MessageBoxW(window,L"Could not create the folder for the installation report.",L"MicFilter",MB_OK|MB_ICONERROR);return;}
    GUID attempt{};wchar_t attemptText[40]{};if(FAILED(CoCreateGuid(&attempt))||!StringFromGUID2(attempt,attemptText,40))return;
    installerResultPath=logs+L"\\installer-"+attemptText+L".txt";
    installerEndpointGuid=selected.guid;
    lastInstallerLog=logs+L"\\installer-"+attemptText+L".log";
    // GUID comes from PKEY_AudioEndpoint_GUID. It is validated again by the script.
    const auto args=L"-NoProfile -ExecutionPolicy Bypass -File \""+dir+L"\\install.ps1\" -Action "+(remove?L"Remove":L"Install")+L" -EndpointGuid \""+selected.guid+L"\" -SourceDir \""+dir+L"\" -ResultPath \""+installerResultPath+L"\"";
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;info.hwnd=window;info.lpVerb=L"runas";
    wchar_t systemDirectory[32768]{};GetSystemDirectoryW(systemDirectory,32768);
    const auto powershell=std::wstring(systemDirectory)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    info.lpFile=powershell.c_str();info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(ShellExecuteExW(&info)){installerProcess=info.hProcess;installerRemoving=remove;update();}
    else{const auto error=GetLastError();const auto message=error==ERROR_CANCELLED?L"Installation cancelled at the Windows administrator prompt.":L"Could not start the installer. Windows error: "+std::to_wstring(error);MessageBoxW(window,message.c_str(),L"MicFilter",MB_OK|MB_ICONINFORMATION);}
}
void installerFinished(){
    if(!installerProcess)return;
    DWORD code=STILL_ACTIVE;if(!GetExitCodeProcess(installerProcess,&code)||code==STILL_ACTIVE)return;
    CloseHandle(installerProcess);installerProcess=nullptr;
    enumerate();update();
    const bool targetInstalled=installed(Device{L"",L"",installerEndpointGuid});
    const bool success=code==0&&(installerRemoving?!targetInstalled:targetInstalled);
    auto message=readResult(installerResultPath);
    if(message.empty())message=L"The installer exited with code "+std::to_wstring(code)+L" and did not produce a report.\nCheck that all application files are present.\n\nExpected log: "+lastInstallerLog;
    else if(code==0&&!success)message=L"The installer finished, but the microphone change was not confirmed.\n\n"+message;
    MessageBoxW(window,message.c_str(),success?L"MicFilter - operation completed":L"MicFilter - installation failed",MB_OK|(success?MB_ICONINFORMATION:MB_ICONERROR));
}
// Starts the elevated uninstaller (install.ps1 -Action Uninstall). It waits for this process to
// exit before deleting the program folder, and shows its own result message.
bool uninstall(HWND owner){
    if(MessageBoxW(owner,L"Remove MicFilter from this computer?\n\nMicrophones return to their previous configuration. MicFilter files, shortcuts and settings are deleted.",L"Uninstall MicFilter",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return false;
    const auto args=L"-NoProfile -ExecutionPolicy Bypass -File \""+executableDirectory()+L"\\install.ps1\" -Action Uninstall -ShowResult -WaitForPid "+std::to_wstring(GetCurrentProcessId());
    wchar_t systemDirectory[32768]{};GetSystemDirectoryW(systemDirectory,32768);
    const auto powershell=std::wstring(systemDirectory)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.hwnd=owner;info.lpVerb=L"runas";info.lpFile=powershell.c_str();info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)){
        const auto error=GetLastError();
        if(error!=ERROR_CANCELLED){const auto message=L"Could not start the uninstaller. Windows error: "+std::to_wstring(error);MessageBoxW(owner,message.c_str(),L"MicFilter",MB_OK|MB_ICONERROR);}
        return false;
    }
    // Per-user data. The elevated script runs as the approving administrator, who may be another account.
    RegDeleteKeyValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"MicFilter");
    RegDeleteKeyValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"WavoFilter");
    const auto logs=logDirectory();
    if(!logs.empty()){std::error_code ignored;std::filesystem::remove_all(std::filesystem::path(logs).parent_path(),ignored);}
    return true;
}
void openInstallerLog(){
    const auto log=latestInstallerLog();if(log.empty()){MessageBoxW(window,L"No installation logs are available yet.",L"MicFilter",MB_OK);return;}
    const auto argument=L"\""+log+L"\"";ShellExecuteW(window,L"open",L"notepad.exe",argument.c_str(),nullptr,SW_SHOWNORMAL);
}
bool startupEnabled(){DWORD type=0,size=0;return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"MicFilter",RRF_RT_REG_SZ,&type,nullptr,&size)==ERROR_SUCCESS;}
void startup(){
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;
    if(startupEnabled())RegDeleteValueW(key,L"MicFilter");
    else{wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring value=L"\""+std::wstring(path)+L"\"";RegSetValueExW(key,L"MicFilter",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*2));}
    RegCloseKey(key);
}
void details(){
    update();std::wstring text=L"Status: "+status+L"\nMicrophone: "+(selected.name.empty()?L"Microphone not detected":selected.name)+L"\nEngine: RNNoise model from Werman v1.21\n";
    text+=telemetryText(endpointTelemetry.get());
    text+=L"\nVoice and enable/disable controls apply to every installed microphone.\nActivity above belongs only to the microphone selected in the menu.\n";
    if(!installed())text+=L"\nThe effect is not installed. Run MicFilter-Setup.exe to select a microphone.";
    else text+=L"\nOne click: enable/disable. Right-click: options.\nThe filter keeps its setting after reboot and works without this icon.\nExit disables the filter; opening the app does not enable it.\nOpen an app that uses the microphone to check effect activity.";
    const auto log=latestInstallerLog();if(!log.empty())text+=L"\n\nLatest installation log:\n"+log;
    MessageBoxW(window,text.c_str(),L"MicFilter - diagnostics",MB_OK|MB_ICONINFORMATION);
}
void heading(HMENU m,const wchar_t* text){AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING|MF_GRAYED,0,text);}
void choice(HMENU m,bool enabled,bool checked,UINT id,const wchar_t* text){AppendMenuW(m,MF_STRING|(enabled?0:MF_GRAYED)|(checked?MF_CHECKED:0),id,text);}
void menu(){
    update();auto* state=mapping.get();if(!options.get())options.open();auto* option=options.get();
    HMENU m=CreatePopupMenu();const bool ready=installed()&&state;
    AppendMenuW(m,MF_STRING|MF_DISABLED,0,status.c_str());AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED),idToggle,ready&&micfilter::read(state->enabled)?L"Disable filter (original audio)":L"Enable filter");
    const LONG voice=option?micfilter::read(option->voicePreset):0;
    heading(m,L"Voice sound");
    choice(m,ready&&option,voice==0,idVoice+0,L"    Natural (as captured)");
    choice(m,ready&&option,voice==1,idVoice+1,L"    Clear (brighter, less boomy)");
    choice(m,ready&&option,voice==2,idVoice+2,L"    Broadcast (polished, even volume)");
    choice(m,ready&&option,voice==3,idVoice+3,L"    Deep (body and clarity for lower voices)");
    const LONG threshold=state?micfilter::read(state->thresholdPermille):850;
    heading(m,L"Silence between words");
    choice(m,ready,threshold==0,idNoGate,L"    Off: noise removal only (recommended)");
    choice(m,ready,threshold==600,idGentle,L"    Balanced: silence pauses, 60% voice detection");
    choice(m,ready,threshold==850,idReference,L"    Strict: silence pauses, 85% voice detection");
    const LONG wet=state?micfilter::read(state->wetPermille):1000;
    heading(m,L"Original microphone sound");
    choice(m,ready,wet==1000,idWetFull,L"    0%: cleanest");
    choice(m,ready,wet==850,idWetPartial,L"    15%: more natural, some noise returns");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,idInfo,L"Diagnostics");
    AppendMenuW(m,MF_STRING,idInstallerLog,L"View installation log");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    HMENU inputs=CreatePopupMenu();
    for(size_t i=0;i<devices.size();++i)if(installed(devices[i]))choice(inputs,true,devices[i].guid==selected.guid,idDevice+static_cast<UINT>(i),devices[i].name.c_str());
    if(GetMenuItemCount(inputs)==0)AppendMenuW(inputs,MF_STRING|MF_GRAYED,0,L"No connected filtered microphones");
    AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(inputs),L"Microphone activity and removal");
    AppendMenuW(m,MF_STRING,idAddMicrophones,L"Add or update microphones...");
    AppendMenuW(m,MF_STRING|(installed()&&!installerProcess?0:MF_GRAYED),idRemove,L"Remove effect and restore configuration...");
    AppendMenuW(m,MF_STRING|(installerProcess?MF_GRAYED:0),idUninstall,L"Uninstall MicFilter...");
    // The filter works without the tray app; this only controls the icon.
    AppendMenuW(m,MF_STRING|(startupEnabled()?MF_CHECKED:0),idStartup,L"Show this icon when Windows starts");
    AppendMenuW(m,MF_STRING|(installerProcess?MF_GRAYED:0),idExit,L"Exit and disable filter");
    POINT point{};GetCursorPos(&point);SetForegroundWindow(window);const auto command=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(m);
    if(command)PostMessageW(window,WM_COMMAND,command,0);PostMessageW(window,WM_NULL,0,0);Shell_NotifyIconW(NIM_SETFOCUS,&tray);
}
class Notifications final:public IMMNotificationClient {
    volatile LONG refs_=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p) override{if(!p)return E_POINTER;*p=nullptr;if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IMMNotificationClient))return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT changed(){PostMessageW(window,refreshMessage,0,0);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow,ERole,LPCWSTR)override{return changed();}
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY)override{return changed();}
};
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==taskbarCreated){Shell_NotifyIconW(NIM_ADD,&tray);Shell_NotifyIconW(NIM_SETVERSION,&tray);update();return 0;}
    switch(msg){
    case trayMessage:{const auto event=LOWORD(lp);if(event==NIN_SELECT||event==NIN_KEYSELECT)toggle();else if(event==WM_CONTEXTMENU)menu();return 0;}
    case WM_TIMER:installerFinished();update();return 0;
    case refreshMessage:enumerate();update();return 0;
    case micfilter::kControlMessage:toggle();return 0;
    case WM_COMMAND:{auto* p=mapping.get();
        if(LOWORD(wp)>=idDevice&&LOWORD(wp)<idDevice+devices.size()){
            selected=devices[LOWORD(wp)-idDevice];endpointTelemetry.close();endpointTelemetry.openEndpoint(selected.guid);update();return 0;
        }
        switch(LOWORD(wp)){
        case idToggle:toggle();break;case idInfo:details();break;case idInstallerLog:openInstallerLog();break;case idInstall:installer(false);break;case idRemove:installer(true);break;
        case idUninstall:if(!installerProcess&&uninstall(hwnd))DestroyWindow(hwnd);break;
        case idStartup:startup();break;case idReference:if(p)InterlockedExchange(&p->thresholdPermille,850);break;
        case idGentle:if(p)InterlockedExchange(&p->thresholdPermille,600);break;case idNoGate:if(p)InterlockedExchange(&p->thresholdPermille,0);break;
        case idWetFull:if(p)InterlockedExchange(&p->wetPermille,1000);break;case idWetPartial:if(p)InterlockedExchange(&p->wetPermille,850);break;
        case idVoice+0:case idVoice+1:case idVoice+2:case idVoice+3:if(auto* o=options.get())InterlockedExchange(&o->voicePreset,static_cast<LONG>(LOWORD(wp)-idVoice));break;
        case idAddMicrophones:MessageBoxW(hwnd,L"Run your downloaded MicFilter-Setup.exe again.\n\nType microphone numbers to check or uncheck them, then press Enter to continue. Already installed inputs update safely; your saved controls and other microphones are kept.",L"Add microphones",MB_OK|MB_ICONINFORMATION);break;
        case idExit:DestroyWindow(hwnd);break;}update();return 0;}
    case WM_CLOSE:if(installerProcess)return 0;DestroyWindow(hwnd);return 0;
    case WM_DESTROY:if(auto* p=mapping.get())InterlockedExchange(&p->enabled,0);Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    // Windows PowerShell 5.1 must build its own module path; one inherited from PowerShell 7 breaks cmdlet loading.
    SetEnvironmentVariableW(L"PSModulePath",nullptr);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);enumerate();
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1){std::wstring command=argv[1];mapping.open();int result=0;
        if(argc>2){
            if(argc!=4||std::wstring(argv[2])!=L"--endpoint"){LocalFree(argv);CoUninitialize();return 2;}
            GUID guid{};if(FAILED(CLSIDFromString(argv[3],&guid))){LocalFree(argv);CoUninitialize();return 2;}
            selected={};for(const auto& d:devices)if(_wcsicmp(d.guid.c_str(),argv[3])==0)selected=d;
            endpointTelemetry.close();if(!selected.guid.empty())endpointTelemetry.openEndpoint(selected.guid);
        }
        if(command==L"--devices"||command==L"--status"){
            std::wostringstream output;for(const auto& d:devices)output<<d.name<<L" | "<<d.guid<<L"\n";
            output<<L"Selected microphone detected: "<<(!selected.id.empty())<<L"\nMicFilter APO installed: "<<installed()<<L"\n";
            output<<L"Microsoft Voice Clarity association: "<<componentizedEndpoint()<<L"\n";
            if(auto* p=mapping.get())output<<L"Enabled: "<<micfilter::read(p->enabled)<<L"\n";
            output<<telemetryText(endpointTelemetry.get());
            printUtf8(output.str());
        }else if(command==L"--check-com"){
            checkCom();
        }else if(command==L"--check-audio"||command==L"--probe-audio"){
            if(selected.id.empty()){printUtf8(L"Selected microphone not detected.\n");result=2;}
            else {std::wstring report;result=FAILED(checkAudio(selected.id,report,command==L"--probe-audio"))?1:0;printUtf8(report);}
        }else if(command==L"--quit"){
            if(auto existing=existingTray())PostMessageW(existing,WM_CLOSE,0,0);
        }else if(command==L"--uninstall"){
            // Used by Settings > Apps. A running tray handles it so it can close itself first.
            if(auto existing=existingTray())PostMessageW(existing,WM_COMMAND,idUninstall,0);
            else result=uninstall(nullptr)?0:1;
        }else if(command==L"--install"){
            if(auto existing=existingTray())PostMessageW(existing,WM_COMMAND,idInstall,0);
            else result=2;
        }else if(command==L"--enable"||command==L"--disable"||command==L"--toggle"){
            auto* p=mapping.get();if(!p||!installed())result=2;
            else InterlockedExchange(&p->enabled,command==L"--enable"?1:command==L"--disable"?0:!micfilter::read(p->enabled));
        }else result=2;
        LocalFree(argv);CoUninitialize();return result;
    }LocalFree(argv);
    HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\MicFilter.Tray.v1");
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto existing=existingTray())PostMessageW(existing,micfilter::kControlMessage,0,0);CloseHandle(singleton);CoUninitialize();return 0;}
    icons[0]=createIcon(RGB(75,210,125));icons[1]=createIcon(RGB(160,165,175));icons[2]=createIcon(RGB(240,180,65));
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=micfilter::kWindowClass;RegisterClassW(&cls);
    window=CreateWindowExW(0,micfilter::kWindowClass,L"MicFilter",0,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!window){CoUninitialize();return 1;}
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;
    tray.uCallbackMessage=trayMessage;tray.hIcon=icons[2];tray.uVersion=NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_ADD,&tray);Shell_NotifyIconW(NIM_SETVERSION,&tray);SetTimer(window,1,1000,nullptr);update();
    IMMDeviceEnumerator* notifier=nullptr;auto* callback=new Notifications;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&notifier))))notifier->RegisterEndpointNotificationCallback(callback);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    if(notifier){notifier->UnregisterEndpointNotificationCallback(callback);notifier->Release();}callback->Release();
    for(auto icon:icons)DestroyIcon(icon);if(singleton)CloseHandle(singleton);CoUninitialize();return 0;
}
