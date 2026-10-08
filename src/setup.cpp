#include <windows.h>
#include <initguid.h>
#include <sddl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include "audio_check.h"
#include "shared.h"
#include "setup_ui.h"

namespace {
constexpr wchar_t kVersion[]=L"0.6.0";
micfilter::SetupUi* gui=nullptr;
constexpr WORD kGreen=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
constexpr WORD kRed=FOREGROUND_RED|FOREGROUND_INTENSITY,kGray=FOREGROUND_INTENSITY,kWhite=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE|FOREGROUND_INTENSITY;
// Everything shown is also appended to %TEMP%\MicFilter-Setup.log, so a message that scrolled
// away or a window that closed can still be read.
void logLine(const std::wstring& text){
    wchar_t temp[MAX_PATH+1]{};if(!GetTempPathW(MAX_PATH+1,temp))return;
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t stamp[64]{};
    swprintf(stamp,64,L"%04u-%02u-%02u %02u:%02u:%02u [%lu] ",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    const std::wstring line=stamp+text+L"\r\n";
    const int bytes=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);std::string utf8(bytes,'\0');
    WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),utf8.data(),bytes,nullptr,nullptr);
    HANDLE file=CreateFileW((std::wstring(temp)+L"MicFilter-Setup.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;DWORD written=0;WriteFile(file,utf8.data(),static_cast<DWORD>(utf8.size()),&written,nullptr);CloseHandle(file);
}
// Console output is for people: short colored lines. setup-install.ps1 uses the same layout.
void colored(WORD color,const std::wstring& text){
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);CONSOLE_SCREEN_BUFFER_INFO info{};const bool console=GetConsoleScreenBufferInfo(out,&info)!=0;
    std::wcout<<std::flush;if(console)SetConsoleTextAttribute(out,color);std::wcout<<text<<std::flush;if(console)SetConsoleTextAttribute(out,info.wAttributes);
}
void step(const wchar_t* mark,WORD color,const std::wstring& text){colored(color,mark);std::wcout<<text<<L"\n"<<std::flush;logLine(mark+text);}
void ok(const std::wstring& text){step(L"  OK  ",kGreen,text);}
void failed(const std::wstring& text){step(L"  X   ",kRed,text);}
void detail(const std::wstring& text){colored(kGray,text);logLine(text);}
struct Payload {WORD id;const wchar_t* name;};
constexpr Payload payloads[]={{101,L"MicFilter.exe"},{102,L"MicFilterAPO.dll"},{103,L"install.ps1"},{104,L"installer-registry.ps1"},{105,L"LICENSE"},{106,L"RNNOISE-LICENSE.txt"},{107,L"SPEEX-LICENSE.txt"},{108,L"setup-install.ps1"},{109,L"GETTING_STARTED.txt"}};
struct Device {std::wstring id,name,guid,status;bool available=true,installed=false;};
void availability(Device& device){
    const auto path=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\"+device.guid+L"\\FxProperties";
    HKEY key=nullptr;const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key);
    device.status=L"Ready to install";
    if(opened==ERROR_FILE_NOT_FOUND)return;
    if(opened!=ERROR_SUCCESS){device.available=false;device.status=L"Windows could not check this input";return;}
    auto hasValue=[&](const std::wstring& name){DWORD bytes=0;return RegQueryValueExW(key,name.c_str(),nullptr,nullptr,nullptr,&bytes)==ERROR_SUCCESS&&bytes>2;};
    wchar_t value[128]{};DWORD bytes=sizeof(value);
    const auto slot=L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},2";
    if(hasValue(slot)){
        const auto read=RegGetValueW(key,nullptr,slot,RRF_RT_REG_SZ,nullptr,value,&bytes);
        const bool ours=read==ERROR_SUCCESS&&(_wcsicmp(value,micfilter::kClsidText)==0||_wcsicmp(value,L"{54F530A1-D045-4C70-8999-11CF13E0DDAF}")==0||_wcsicmp(value,L"{6C78EB4F-8AE4-4461-BE4A-989C7C14C7B2}")==0);
        if(ours){device.installed=true;device.status=L"Already installed - safe to update";}
        else{device.available=false;device.status=L"Another audio app manages this input";}
    }
    for(int slotIndex:{1,5,6,7})if(hasValue(L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},"+std::to_wstring(slotIndex))){device.available=false;device.status=L"Uses manufacturer audio enhancements";}
    if(hasValue(L"{9e6136e0-57ab-4949-b57a-3627be142855},100")){device.available=false;device.status=L"Uses driver-managed audio effects";}
    RegCloseKey(key);
}
std::wstring property(IPropertyStore* store,const PROPERTYKEY& key){PROPVARIANT p;PropVariantInit(&p);std::wstring text;if(SUCCEEDED(store->GetValue(key,&p))&&p.vt==VT_LPWSTR&&p.pwszVal)text=p.pwszVal;PropVariantClear(&p);return text;}
std::vector<Device> microphones(){
    std::vector<Device> devices;IMMDeviceEnumerator* enumerator=nullptr;IMMDeviceCollection* collection=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))))throw std::runtime_error("Could not enumerate Windows microphones.");
    if(SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection))){
        UINT count=0;collection->GetCount(&count);
        for(UINT i=0;i<count;++i){IMMDevice* device=nullptr;IPropertyStore* store=nullptr;LPWSTR id=nullptr;
            if(SUCCEEDED(collection->Item(i,&device))){if(SUCCEEDED(device->GetId(&id))&&SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store)))devices.push_back({id,property(store,PKEY_Device_FriendlyName),property(store,PKEY_AudioEndpoint_GUID),L""});if(store)store->Release();if(id)CoTaskMemFree(id);device->Release();}
        }collection->Release();
    }enumerator->Release();return devices;
}
bool admin(){SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID sid=nullptr;BOOL member=FALSE;if(AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(nullptr,sid,&member);FreeSid(sid);}return member!=FALSE;}
std::wstring unique(){GUID id{};wchar_t text[40]{};if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,text,40))throw std::runtime_error("Could not create a unique staging folder.");return text;}
void extract(const std::wstring& directory){
    for(const auto& entry:payloads){auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(entry.id),RT_RCDATA);auto loaded=resource?LoadResource(nullptr,resource):nullptr;const auto size=resource?SizeofResource(nullptr,resource):0;const auto* data=loaded?LockResource(loaded):nullptr;
        if(!data||!size)throw std::runtime_error("The installer is missing embedded components.");
        HANDLE file=CreateFileW((directory+L"\\"+entry.name).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not extract a component.");DWORD written=0;const bool ok=WriteFile(file,data,size,&written,nullptr)!=0;CloseHandle(file);if(!ok||written!=size)throw std::runtime_error("Component extraction was incomplete.");
    }
}
int elevate(const std::wstring& arguments){
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);launch.fMask=SEE_MASK_NOCLOSEPROCESS;launch.lpVerb=L"runas";launch.lpFile=path;launch.lpParameters=arguments.c_str();launch.nShow=SW_SHOWNORMAL;
    std::wcout<<L"  Windows will ask for administrator permission.\n"<<std::flush;
    if(!ShellExecuteExW(&launch)){
        const auto error=GetLastError();
        failed(error==ERROR_CANCELLED?L"Setup needs administrator permission to install. Nothing was changed.":L"Setup could not start (Windows error "+std::to_wstring(error)+L"). Nothing was changed.");
        return -1;
    }
    // Hide the console only when setup owns it (double-click), never a terminal the user launched it from.
    DWORD processes[4]{};const auto console=GetConsoleWindow();if(console&&GetConsoleProcessList(processes,4)<=1)ShowWindow(console,SW_HIDE);
    if(gui)gui->wait(launch.hProcess);else WaitForSingleObject(launch.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(launch.hProcess,&code);CloseHandle(launch.hProcess);
    logLine(L"Administrator setup exited with code "+std::to_wstring(code));
    return static_cast<int>(code);
}
// Administrator-only folder under Program Files for the extracted scripts.
std::wstring createStaging(){
    PWSTR programFiles=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&programFiles)))throw std::runtime_error("Program Files is unavailable.");const auto staging=std::wstring(programFiles)+L"\\MicFilter-Setup-"+unique();CoTaskMemFree(programFiles);
    PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr))throw std::runtime_error("Could not secure the staging folder.");SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};const auto created=CreateDirectoryW(staging.c_str(),&attributes);LocalFree(descriptor);if(!created)throw std::runtime_error("Could not create the staging folder.");
    return staging;
}
// Runs "<staging>\<scriptAndArguments>" in this console and returns its exit code.
int runScript(const std::wstring& staging,const std::wstring& scriptAndArguments){
    wchar_t system[32768]{};GetSystemDirectoryW(system,32768);const auto powershell=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    auto command=L"\""+powershell+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+staging+L"\\"+scriptAndArguments;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,TRUE,gui?CREATE_NO_WINDOW:0,nullptr,staging.c_str(),&startup,&process))throw std::runtime_error("Could not start the installation script.");
    if(gui)gui->wait(process.hProcess);else WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);return static_cast<int>(code);
}
void pause(){detail(L"\n  Press Enter to close.\n");std::wstring ignored;std::getline(std::wcin,ignored);}
}
int wmain(int argc,wchar_t** argv){
    // Windows PowerShell 5.1 must build its own module path; one inherited from PowerShell 7 breaks cmdlet loading.
    SetEnvironmentVariableW(L"PSModulePath",nullptr);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG{
        wchar_t code[16]{};swprintf(code,16,L"0x%08lX",e&&e->ExceptionRecord?e->ExceptionRecord->ExceptionCode:0UL);
        logLine(std::wstring(L"Setup closed unexpectedly, exception ")+code);return EXCEPTION_CONTINUE_SEARCH;});
    {std::wstring command=GetCommandLineW();logLine(L"Started: "+command+(admin()?L" (administrator)":L""));}
    _setmode(_fileno(stdout),_O_U16TEXT);_setmode(_fileno(stderr),_O_U16TEXT);SetConsoleTitleW(L"MicFilter - installation");
    bool noPause=false,selfTest=false,list=false,uninstall=false,guiRun=false,preview=false;std::vector<std::wstring> requested;int preset=-1;
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--no-pause")noPause=true;else if(arg==L"--gui-run")guiRun=true;else if(arg==L"--self-test")selfTest=true;else if(arg==L"--preview-ui"){selfTest=true;preview=true;}else if(arg==L"--list-devices")list=true;else if(arg==L"--uninstall")uninstall=true;
        else if(arg==L"--preset"&&i+1<argc){const std::wstring value=argv[++i];if(value.size()!=1||value[0]<L'0'||value[0]>L'3')return 2;preset=value[0]-L'0';}
        else if(arg==L"--endpoint"&&i+1<argc){GUID guid{};if(FAILED(CLSIDFromString(argv[++i],&guid))){std::wcerr<<L"Invalid microphone identifier.\n";return 2;}wchar_t text[40]{};StringFromGUID2(guid,text,40);if(std::find(requested.begin(),requested.end(),text)==requested.end())requested.push_back(text);}
        else{std::wcerr<<L"Unknown option.\n";return 2;}}
    const bool graphical=guiRun||(!selfTest&&!list&&!uninstall&&requested.empty());
    SYSTEM_INFO systemInfo{};GetNativeSystemInfo(&systemInfo);
    if(!selfTest&&systemInfo.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64){
        if(graphical)MessageBoxW(nullptr,L"MicFilter requires Windows on an Intel or AMD x64 processor. This installer cannot install an audio effect on Windows ARM.",L"MicFilter Setup",MB_OK|MB_ICONINFORMATION);
        else{std::wcerr<<L"This installer requires Windows on an x64 processor (Intel/AMD).\n";if(!noPause&&!list)pause();}return 2;
    }
    if(!graphical&&!selfTest&&!list&&!admin()){
        std::wstring args=uninstall?L"--uninstall ":L"";for(const auto& guid:requested)args+=L"--endpoint "+guid+L" ";if(preset>=0)args+=L"--preset "+std::to_wstring(preset)+L" ";if(noPause)args+=L"--no-pause";
        const int code=elevate(args);
        // The administrator window pauses on its own; pause here only when it never started.
        if(code==-1&&!noPause)pause();
        return code==-1?1:code;
    }
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;std::wstring staging,summary;bool ownsStaging=false;
    std::unique_ptr<micfilter::SetupUi> wizard;
    HANDLE installationMutex=nullptr;
    try {
        if(selfTest){
            micfilter::SetupUi testUi({{L"Desktop microphone",L"Already installed - safe to update",true,true},{L"USB headset microphone",L"Ready to install",true,false},{L"Laptop microphone",L"Uses manufacturer audio enhancements",false,false}},false);
            if(!testUi.selfTest())throw std::runtime_error("Setup selection/profile controls failed.");
            HICON appIcon=nullptr;wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            if(ExtractIconExW(executable,0,&appIcon,nullptr,1)!=1||!appIcon)throw std::runtime_error("The installer icon is missing.");DestroyIcon(appIcon);
            if(preview){const auto path=std::filesystem::path(executable).parent_path()/L"MicFilter-Setup-preview.bmp";if(!testUi.savePreview(path.wstring()))throw std::runtime_error("Could not save the setup preview.");result=0;CoUninitialize();return result;}
            wchar_t temp[32768]{};if(!GetTempPathW(32768,temp))throw std::runtime_error("No temporary folder is available.");staging=std::wstring(temp)+L"MicFilter-package-test-"+unique();if(!CreateDirectoryW(staging.c_str(),nullptr))throw std::runtime_error("Could not create the test directory.");ownsStaging=true;extract(staging);
            for(const auto& entry:payloads){const auto path=staging+L"\\"+entry.name;if(std::filesystem::file_size(path)==0)throw std::runtime_error("Empty resource.");if(entry.id<=102){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char magic[2]{};DWORD bytes=0;const auto ok=ReadFile(file,magic,2,&bytes,nullptr);CloseHandle(file);if(!ok||bytes!=2||magic[0]!='M'||magic[1]!='Z')throw std::runtime_error("Invalid embedded binary.");}}
            std::wcout<<L"PASS package: 9 resources extracted; valid EXE/DLL; no microphone installation or changes.\n";result=0;
            HICON applicationIcon=nullptr;
            if(ExtractIconExW((staging+L"\\MicFilter.exe").c_str(),0,&applicationIcon,nullptr,1)!=1||!applicationIcon)throw std::runtime_error("The application icon is missing.");DestroyIcon(applicationIcon);
        }else if(uninstall){
            colored(kWhite,std::wstring(L"\n  MicFilter ")+kVersion+L" - uninstall\n\n");
            detail(L"  Removing MicFilter and restoring your microphone...\n");
            staging=createStaging();ownsStaging=true;extract(staging);
            result=runScript(staging,L"install.ps1\" -Action Uninstall -Quiet -ShowResult");
            if(result==0)ok(L"MicFilter was removed.");else failed(L"Uninstall did not complete. The message window explains why.");
        }else{
            auto devices=microphones();for(auto& device:devices)availability(device);
            if(list){for(const auto& d:devices)std::wcout<<d.name<<L" | "<<d.guid<<L"\n";result=0;}
            else{
                colored(kWhite,std::wstring(L"\n  MicFilter ")+kVersion+L"\n");
                detail(L"  Noise suppression for your microphone. Nothing is recorded or uploaded.\n\n");
                std::vector<size_t> choices;
                if(graphical){
                    std::vector<micfilter::SetupInput> items;
                    for(const auto& device:devices){
                        const bool checked=guiRun?std::any_of(requested.begin(),requested.end(),[&](const auto& guid){return _wcsicmp(guid.c_str(),device.guid.c_str())==0;}):device.installed||(devices.size()==1&&device.available);
                        items.push_back({device.name,device.status,device.available,checked});
                    }
                    wizard=std::make_unique<micfilter::SetupUi>(items);gui=wizard.get();
                    if(!guiRun){
                        if(!wizard->choose()){gui=nullptr;CoUninitialize();return 0;}
                        choices=wizard->selection();preset=wizard->preset();
                        for(auto index:choices)requested.push_back(devices[index].guid);
                        if(!admin()){
                            wizard->begin();std::wstring args=L"--gui-run ";for(const auto& guid:requested)args+=L"--endpoint "+guid+L" ";if(preset>=0)args+=L"--preset "+std::to_wstring(preset);
                            wizard->hide();const int code=elevate(args);
                            if(code<0)wizard->finish(false,L"Windows administrator permission was cancelled. Nothing was changed. Run setup again when you are ready.");
                            gui=nullptr;CoUninitialize();return code<0?1:code;
                        }
                    }
                }
                if(choices.empty()&&!requested.empty()){
                    for(const auto& guid:requested){bool found=false;for(size_t i=0;i<devices.size();++i)if(_wcsicmp(devices[i].guid.c_str(),guid.c_str())==0){choices.push_back(i);found=true;break;}if(!found)throw std::runtime_error("A selected microphone is no longer connected. Reconnect it and run setup again.");}
                }
                else if(!graphical&&devices.size()>1){
                    std::wcout<<L"  Which microphone should MicFilter clean up?\n\n";
                    for(size_t i=0;i<devices.size();++i)std::wcout<<L"    "<<i+1<<L". "<<devices[i].name<<L"\n";
                    for(;;){std::wcout<<L"\n  Type its number and press Enter (0 cancels): "<<std::flush;std::wstring input;if(!std::getline(std::wcin,input))throw std::runtime_error("Cancelled. Nothing was changed.");try{size_t used=0;const auto number=std::stoul(input,&used);if(used!=input.size()||number>devices.size())continue;if(!number)throw std::runtime_error("Cancelled. Nothing was changed.");choices.push_back(number-1);break;}catch(const std::invalid_argument&){}catch(const std::out_of_range&){}}
                }
                if(choices.empty()&&devices.size()==1)choices.push_back(0);
                if(choices.empty())throw std::runtime_error("No microphone selected. Connect a microphone, enable it in Windows Settings, and run setup again.");
                installationMutex=CreateMutexW(nullptr,FALSE,L"Global\\MicFilter.Setup.Installation");
                if(!installationMutex)throw std::runtime_error("Windows could not reserve setup. Close other setup windows and try again.");
                const auto acquired=WaitForSingleObject(installationMutex,0);
                if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){CloseHandle(installationMutex);installationMutex=nullptr;throw std::runtime_error("MicFilter setup is already running. Finish the other setup window and try again.");}
                if(wizard)wizard->begin();
                std::wstring endpointList;
                for(auto choice:choices){
                const auto& selected=devices[choice];
                if(!selected.available)throw std::runtime_error("A selected microphone uses another audio effect. Its configuration was kept. Run setup and choose an available input.");
                std::wcout<<L"\n  Microphone: "<<selected.name<<L"\n\n"<<std::flush;
                std::wstring report;bool compatible=false;
                if(FAILED(checkAudio(selected.id,report,false,&compatible))){detail(report);throw std::runtime_error("Windows could not open this microphone. Close apps that are using it, check microphone permissions in Settings > Privacy, and try again.");}
                if(!compatible){detail(report);throw std::runtime_error("This microphone uses an audio format MicFilter does not support (1-8 channels, 8-192 kHz). Nothing was changed.");}
                ok(L"Microphone ready");
                if(!endpointList.empty())endpointList+=L",";endpointList+=selected.guid;
                }
                staging=createStaging();ownsStaging=true;extract(staging);
                wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
                if(!CopyFileW(executable,(staging+L"\\MicFilter-Setup.exe").c_str(),TRUE))throw std::runtime_error("Could not prepare the microphone setup tool.");
                std::wstring args=L"setup-install.ps1\" -EndpointList \""+endpointList+L"\"";
                if(preset>=0)args+=L" -VoicePreset "+std::to_wstring(preset);
                if(wizard){wizard->begin(staging+L"\\progress.txt");args+=L" -ProgressPath \""+staging+L"\\progress.txt\" -SummaryPath \""+staging+L"\\summary.txt\"";}
                result=runScript(staging,args);
                if(wizard)summary=micfilter::SetupUi::readText(staging+L"\\summary.txt");
            }
        }
    }catch(const std::exception& error){const std::string text=error.what();summary=std::wstring(text.begin(),text.end());std::wcout<<L"\n";failed(summary);result=1;}
    catch(...){std::wcout<<L"\n";failed(L"Setup stopped because of an unexpected error. Nothing else was changed.");result=1;}
    logLine(L"Finished with code "+std::to_wstring(result));
    if(installationMutex){ReleaseMutex(installationMutex);CloseHandle(installationMutex);}
    // Only this process's newly created, unpredictable directory is removed.
    if(ownsStaging){std::error_code error;std::filesystem::remove_all(staging,error);if(error)std::wcerr<<L"Could not clean up the staging folder: "<<staging<<L"\n";}
    if(wizard)wizard->finish(result==0,summary.empty()?L"Setup stopped before it could produce a report. Run setup again.":summary);
    else if(graphical&&!summary.empty())MessageBoxW(nullptr,summary.c_str(),L"MicFilter Setup",MB_OK|MB_ICONERROR);
    gui=nullptr;CoUninitialize();if(!graphical&&!noPause&&!selfTest&&!list)pause();return result;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 1;
    // CLI diagnostics still write to redirected handles; double-click setup opens only the wizard.
    if(argc>1&&GetStdHandle(STD_OUTPUT_HANDLE)==nullptr)AttachConsole(ATTACH_PARENT_PROCESS);
    const int result=wmain(argc,argv);LocalFree(argv);return result;
}
