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
#include "audio_check.h"
#include "apo_sdk.h"

namespace {
constexpr UINT trayMessage=WM_APP+1,refreshMessage=WM_APP+2;
constexpr UINT idToggle=100,idInstall=101,idRemove=102,idExit=103,idInfo=104,idStartup=105;
constexpr UINT idReference=110,idGentle=111,idNoGate=112,idWetFull=113,idWetPartial=114;
constexpr UINT idInstallerLog=115;
struct Device{std::wstring id,name,guid;};
std::vector<Device> devices;
Device selected;
wavo::StateMapping mapping;
HWND window=nullptr;
NOTIFYICONDATAW tray{};
HICON icons[3]{};
std::wstring status;
std::wstring configuredGuid;
UINT taskbarCreated=0;
HANDLE installerProcess=nullptr;
bool installerRemoving=false;
std::wstring installerResultPath,lastInstallerLog;
void printUtf8(const std::wstring& text){const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string bytes(count,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),bytes.data(),count,nullptr,nullptr);DWORD written=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);}
void checkCom(){
    class Outer final:public IUnknown {
        ULONG references_=1;
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
        ULONG STDMETHODCALLTYPE AddRef() override{return ++references_;}
        ULONG STDMETHODCALLTYPE Release() override{return --references_;}
    } outer;
    constexpr GUID diagnosticClsid={0x4a1f2290,0xfe8c,0x4f3a,{0x98,0x55,0xee,0xb7,0xbc,0x03,0x81,0x71}};
    IUnknown* object=nullptr;
    const auto normal=CoCreateInstance(diagnosticClsid,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IUnknown),reinterpret_cast<void**>(&object));if(object)object->Release();object=nullptr;
    const auto aggregate=CoCreateInstance(diagnosticClsid,&outer,CLSCTX_INPROC_SERVER,__uuidof(IUnknown),reinterpret_cast<void**>(&object));
    std::wostringstream report;report<<L"Windows COM normal HRESULT=0x"<<std::hex<<static_cast<unsigned long>(normal)<<L"\nWindows COM aggregate HRESULT=0x"<<static_cast<unsigned long>(aggregate)<<L"\n";
    if(object){IAudioProcessingObject* apo=nullptr;const auto queried=object->QueryInterface(__uuidof(IAudioProcessingObject),reinterpret_cast<void**>(&apo));report<<L"Windows COM APO interface HRESULT=0x"<<static_cast<unsigned long>(queried)<<L"\n";if(apo)apo->Release();object->Release();}
    printUtf8(report.str());
}
std::wstring executableDirectory(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);std::wstring s=p;return s.substr(0,s.find_last_of(L"\\/"));}
std::wstring logDirectory(){wchar_t path[32768]{};const auto count=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);return count&&count<32768?std::wstring(path)+L"\\WavoFilter\\logs":L"";}
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
    devices.clear();selected={};
    wchar_t configured[128]{};DWORD configuredBytes=sizeof(configured);
    const auto configuration=L"SOFTWARE\\Classes\\CLSID\\"+std::wstring(wavo::kClsidText);
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
                    else if(d.name.find(L"Wavo POD")!=std::wstring::npos)selected=d;
                }
                if(store)store->Release();if(id)CoTaskMemFree(id);device->Release();
            }
        }collection->Release();
    }enumerator->Release();
}
bool installed(){
    if(selected.guid.empty())return false;
    auto path=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\"+selected.guid+L"\\FxProperties";
    wchar_t value[128]{};DWORD bytes=sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes)==ERROR_SUCCESS&&_wcsicmp(value,wavo::kClsidText)==0;
}
bool componentizedEndpoint(){
    if(selected.guid.empty())return false;
    const auto path=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\"+selected.guid+L"\\FxProperties";
    wchar_t value[1024]{};DWORD bytes=sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"{9e6136e0-57ab-4949-b57a-3627be142855},100",RRF_RT_REG_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,value,&bytes)==ERROR_SUCCESS&&value[0];
}
std::wstring telemetryText(wavo::SharedState* p){
    if(!p||!wavo::read(p->producerPid))return L"Actividad del efecto: no observada.\nFormato del efecto: sin comprobar.\n";
    std::wstring text=wavo::recentAudio(p)?L"Actividad del efecto: reciente.\nFormato activo: ":L"Actividad del efecto: detenida; datos históricos.\nÚltimo formato: ";
    text+=std::to_wstring(wavo::read(p->sampleRate))+L" Hz / "+std::to_wstring(wavo::read(p->channels))+L" canales\nBloques procesados por el efecto: "+std::to_wstring(wavo::read(p->callbacks))+L"\nMuestras filtradas: "+std::to_wstring(wavo::read(p->processedFrames))+L"\n";
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
    if(installerProcess)status=installerRemoving?L"Restaurando configuración…":L"Instalando efecto…";
    else if(selected.id.empty())status=L"Micrófono seleccionado desconectado";
    else if(!installed()){status=L"Audio original; efecto propio sin instalar";icon=1;}
    else if(!state)status=L"Estado inaccesible; audio original";
    else if(!wavo::read(state->enabled)){status=L"Audio original";icon=1;}
    else if(wavo::recentAudio(state)){
        if(wavo::read(state->formatSupported)){status=L"Filtro confirmado: RNNoise v1.21";icon=0;}
        else status=L"Formato incompatible; audio original";
    }else status=wavo::read(state->producerPid)?L"Filtro activado; esperando uso del micrófono":L"Efecto asociado; procesamiento sin confirmar";
    tray.hIcon=icons[icon];auto tip=L"Wavo Filter - "+status;wcsncpy_s(tray.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void toggle(){if(!installed()||!mapping.get()){MessageBoxW(window,L"Primero instala el efecto en un micrófono ejecutando WavoFilter-Setup.exe.",L"Wavo Filter",MB_OK|MB_ICONINFORMATION);return;}auto* p=mapping.get();InterlockedExchange(&p->enabled,!wavo::read(p->enabled));update();}
void installer(bool remove){
    if(installerProcess)return;
    if(selected.guid.empty()){MessageBoxW(window,L"Conecta el micrófono seleccionado para instalar o retirar su efecto.",L"Wavo Filter",MB_OK);return;}
    const auto dir=executableDirectory();
    const auto logs=logDirectory();
    const int made=logs.empty()?ERROR_PATH_NOT_FOUND:SHCreateDirectoryExW(window,logs.c_str(),nullptr);
    if(made!=ERROR_SUCCESS&&made!=ERROR_ALREADY_EXISTS&&made!=ERROR_FILE_EXISTS){MessageBoxW(window,L"No se pudo crear la carpeta para guardar el resultado de la instalación.",L"Wavo Filter",MB_OK|MB_ICONERROR);return;}
    GUID attempt{};wchar_t attemptText[40]{};if(FAILED(CoCreateGuid(&attempt))||!StringFromGUID2(attempt,attemptText,40))return;
    installerResultPath=logs+L"\\installer-"+attemptText+L".txt";
    lastInstallerLog=logs+L"\\installer-"+attemptText+L".log";
    if(remove&&mapping.get())InterlockedExchange(&mapping.get()->enabled,0);
    // GUID comes from PKEY_AudioEndpoint_GUID. It is validated again by the script.
    const auto args=L"-NoProfile -ExecutionPolicy Bypass -File \""+dir+L"\\install.ps1\" -Action "+(remove?L"Remove":L"Install")+L" -EndpointGuid \""+selected.guid+L"\" -SourceDir \""+dir+L"\" -ResultPath \""+installerResultPath+L"\"";
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;info.hwnd=window;info.lpVerb=L"runas";
    wchar_t systemDirectory[32768]{};GetSystemDirectoryW(systemDirectory,32768);
    const auto powershell=std::wstring(systemDirectory)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    info.lpFile=powershell.c_str();info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(ShellExecuteExW(&info)){installerProcess=info.hProcess;installerRemoving=remove;update();}
    else{const auto error=GetLastError();const auto message=error==ERROR_CANCELLED?L"Instalación cancelada en el aviso de administrador de Windows.":L"No se pudo iniciar el instalador. Error de Windows: "+std::to_wstring(error);MessageBoxW(window,message.c_str(),L"Wavo Filter",MB_OK|MB_ICONINFORMATION);}
}
void installerFinished(){
    if(!installerProcess)return;
    DWORD code=STILL_ACTIVE;if(!GetExitCodeProcess(installerProcess,&code)||code==STILL_ACTIVE)return;
    CloseHandle(installerProcess);installerProcess=nullptr;
    enumerate();update();
    const bool success=code==0&&(installerRemoving?!installed():installed());
    if(success&&!installerRemoving){if(!mapping.get())mapping.open();if(auto* p=mapping.get())InterlockedExchange(&p->enabled,1);update();}
    auto message=readResult(installerResultPath);
    if(message.empty())message=L"El instalador terminó con código "+std::to_wstring(code)+L" y no generó un informe.\nComprueba que los archivos del programa estén completos.\n\nRegistro esperado: "+lastInstallerLog;
    else if(code==0&&!success)message=L"El instalador terminó, pero no se confirmó el cambio en el Wavo.\n\n"+message;
    MessageBoxW(window,message.c_str(),success?L"Wavo Filter - operación completada":L"Wavo Filter - instalación fallida",MB_OK|(success?MB_ICONINFORMATION:MB_ICONERROR));
}
void openInstallerLog(){
    const auto log=latestInstallerLog();if(log.empty()){MessageBoxW(window,L"Todavía no hay registros de instalación.",L"Wavo Filter",MB_OK);return;}
    const auto argument=L"\""+log+L"\"";ShellExecuteW(window,L"open",L"notepad.exe",argument.c_str(),nullptr,SW_SHOWNORMAL);
}
bool startupEnabled(){DWORD type=0,size=0;return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"WavoFilter",RRF_RT_REG_SZ,&type,nullptr,&size)==ERROR_SUCCESS;}
void startup(){
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return;
    if(startupEnabled())RegDeleteValueW(key,L"WavoFilter");
    else{wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring value=L"\""+std::wstring(path)+L"\"";RegSetValueExW(key,L"WavoFilter",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*2));}
    RegCloseKey(key);
}
void details(){
    update();std::wstring text=L"Estado: "+status+L"\nMicrófono: "+(selected.name.empty()?L"Micrófono no detectado":selected.name)+L"\nMotor: modelo RNNoise de Werman v1.21\n";
    text+=telemetryText(mapping.get());
    if(!installed())text+=L"\nEl efecto propio está retirado. Puedes instalar la versión corregida desde el menú.";
    else text+=L"\nUn clic: activar/desactivar. Clic derecho: opciones.\nEl filtro conserva su ajuste tras reiniciar y funciona sin este icono.\nSalir desactiva el filtro; abrir el programa no lo activa.\nAbre una aplicación que use el micrófono para comprobar la actividad del efecto.";
    const auto log=latestInstallerLog();if(!log.empty())text+=L"\n\nÚltimo registro de instalación:\n"+log;
    MessageBoxW(window,text.c_str(),L"Wavo Filter - diagnóstico",MB_OK|MB_ICONINFORMATION);
}
void menu(){
    update();auto* state=mapping.get();HMENU m=CreatePopupMenu();const bool ready=installed()&&state;
    AppendMenuW(m,MF_STRING|MF_DISABLED,0,status.c_str());AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED),idToggle,ready&&wavo::read(state->enabled)?L"Desactivar filtro (audio original)":L"Activar filtro");
    AppendMenuW(m,MF_STRING,idInfo,L"Diagnóstico");
    AppendMenuW(m,MF_STRING,idInstallerLog,L"Ver registro de instalación");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    const LONG threshold=state?wavo::read(state->thresholdPermille):850;
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED)|(threshold==850?MF_CHECKED:0),idReference,L"Perfil: referencia v1.21 (85%)");
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED)|(threshold==600?MF_CHECKED:0),idGentle,L"Perfil: detección más permisiva (60%)");
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED)|(threshold==0?MF_CHECKED:0),idNoGate,L"Perfil: RNNoise sin silenciamiento adicional");
    const LONG wet=state?wavo::read(state->wetPermille):1000;
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED)|(wet==1000?MF_CHECKED:0),idWetFull,L"Mezcla: 100% filtrado");
    AppendMenuW(m,MF_STRING|(ready?0:MF_GRAYED)|(wet==850?MF_CHECKED:0),idWetPartial,L"Mezcla: 85% filtrado / 15% original");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING|(selected.guid.empty()||installerProcess?MF_GRAYED:0),idInstall,L"Instalar efecto en micrófono seleccionado…");
    AppendMenuW(m,MF_STRING|(installed()&&!installerProcess?0:MF_GRAYED),idRemove,L"Retirar efecto y restaurar configuración…");
    AppendMenuW(m,MF_STRING|(startupEnabled()?MF_CHECKED:0),idStartup,L"Iniciar con Windows");
    AppendMenuW(m,MF_STRING|(installerProcess?MF_GRAYED:0),idExit,L"Salir (dejar audio original)");
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
    case wavo::kControlMessage:toggle();return 0;
    case WM_COMMAND:{auto* p=mapping.get();switch(LOWORD(wp)){
        case idToggle:toggle();break;case idInfo:details();break;case idInstallerLog:openInstallerLog();break;case idInstall:installer(false);break;case idRemove:installer(true);break;
        case idStartup:startup();break;case idReference:if(p)InterlockedExchange(&p->thresholdPermille,850);break;
        case idGentle:if(p)InterlockedExchange(&p->thresholdPermille,600);break;case idNoGate:if(p)InterlockedExchange(&p->thresholdPermille,0);break;
        case idWetFull:if(p)InterlockedExchange(&p->wetPermille,1000);break;case idWetPartial:if(p)InterlockedExchange(&p->wetPermille,850);break;
        case idExit:DestroyWindow(hwnd);break;}update();return 0;}
    case WM_CLOSE:if(installerProcess)return 0;DestroyWindow(hwnd);return 0;
    case WM_DESTROY:if(auto* p=mapping.get())InterlockedExchange(&p->enabled,0);Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);enumerate();
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1){std::wstring command=argv[1];mapping.open();int result=0;
        if(command==L"--devices"||command==L"--status"){
            std::wostringstream output;for(const auto& d:devices)output<<d.name<<L" | "<<d.guid<<L"\n";
            output<<L"Micrófono seleccionado detectado: "<<(!selected.id.empty())<<L"\nAPO propio instalado: "<<installed()<<L"\n";
            output<<L"Asociación a Voice Clarity de Microsoft: "<<componentizedEndpoint()<<L"\n";
            if(auto* p=mapping.get())output<<L"Activado: "<<wavo::read(p->enabled)<<L"\n";
            output<<telemetryText(mapping.get());
            printUtf8(output.str());
        }else if(command==L"--check-com"){
            checkCom();
        }else if(command==L"--check-audio"||command==L"--probe-audio"){
            if(selected.id.empty()){printUtf8(L"Micrófono seleccionado no detectado.\n");result=2;}
            else {std::wstring report;result=FAILED(checkAudio(selected.id,report,command==L"--probe-audio"))?1:0;printUtf8(report);}
        }else if(command==L"--quit"){
            if(auto existing=FindWindowW(wavo::kWindowClass,nullptr))PostMessageW(existing,WM_CLOSE,0,0);
        }else if(command==L"--install"){
            if(auto existing=FindWindowW(wavo::kWindowClass,nullptr))PostMessageW(existing,WM_COMMAND,idInstall,0);
            else result=2;
        }else if(command==L"--enable"||command==L"--disable"||command==L"--toggle"){
            auto* p=mapping.get();if(!p||!installed())result=2;
            else InterlockedExchange(&p->enabled,command==L"--enable"?1:command==L"--disable"?0:!wavo::read(p->enabled));
        }else result=2;
        LocalFree(argv);CoUninitialize();return result;
    }LocalFree(argv);
    HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\WavoFilter.Tray.v1");
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto existing=FindWindowW(wavo::kWindowClass,nullptr))PostMessageW(existing,wavo::kControlMessage,0,0);CloseHandle(singleton);CoUninitialize();return 0;}
    icons[0]=createIcon(RGB(75,210,125));icons[1]=createIcon(RGB(160,165,175));icons[2]=createIcon(RGB(240,180,65));
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=wavo::kWindowClass;RegisterClassW(&cls);
    window=CreateWindowExW(0,wavo::kWindowClass,L"Wavo Filter",0,0,0,0,0,nullptr,nullptr,instance,nullptr);
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
