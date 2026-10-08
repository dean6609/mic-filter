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
#include <algorithm>
#include "audio_check.h"
#include "shared.h"
#include "setup_selection.h"

namespace {
constexpr wchar_t kVersion[]=L"0.6.0";
constexpr WORD kGreen=FOREGROUND_GREEN|FOREGROUND_INTENSITY;
constexpr WORD kYellow=FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
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
    for(int slotIndex:{1,5,6,7}){
        const auto name=L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},"+std::to_wstring(slotIndex);
        if(!hasValue(name))continue;
        wchar_t effect[128]{};DWORD effectBytes=sizeof(effect);
        // Microsoft's discovery-only proxy reports hardware effects; it does not filter samples.
        const auto read=RegGetValueW(key,nullptr,name.c_str(),RRF_RT_REG_SZ,nullptr,effect,&effectBytes);
        const bool discoveryOnly=slotIndex==7&&read==ERROR_SUCCESS&&_wcsicmp(effect,L"{889C03C8-ABAD-4004-BF0A-BC7BB825E166}")==0;
        const bool ownedStream=slotIndex==5&&read==ERROR_SUCCESS&&_wcsicmp(effect,micfilter::kClsidText)==0;
        if(ownedStream){device.installed=true;device.status=L"Already installed - safe to update";}
        else if(!discoveryOnly){device.available=false;device.status=L"Uses manufacturer audio enhancements";}
    }
    // A driver association alone is not proof of a conflicting processing APO.
    // Leave it intact and verify actual capture/our own processing after installation.
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
    WaitForSingleObject(launch.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(launch.hProcess,&code);CloseHandle(launch.hProcess);
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
    if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,staging.c_str(),&startup,&process))throw std::runtime_error("Could not start the installation script.");
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);return static_cast<int>(code);
}
void pause(){detail(L"\n  Press Enter to close.\n");std::wstring ignored;std::getline(std::wcin,ignored);}
void verifyIcons(const std::wstring& path){
    const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!module)throw std::runtime_error("Could not read icon resources.");
    bool valid=true;
    for(int size:{16,24,32,48,64,128,256}){
        auto icon=static_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(1),IMAGE_ICON,size,size,0));
        if(!icon)valid=false;else DestroyIcon(icon);
    }
    FreeLibrary(module);if(!valid)throw std::runtime_error("An embedded icon size could not be loaded by Windows.");
}
}
namespace {
std::vector<size_t> chooseMicrophones(const std::vector<Device>& devices){
    if(devices.size()==1&&devices.front().available){
        ok(L"Only one microphone found - selected automatically: "+devices.front().name);
        return {0};
    }
    std::vector<bool> available,checked;
    const auto ready=std::count_if(devices.begin(),devices.end(),[](const auto& device){return device.available;});
    for(const auto& device:devices){available.push_back(device.available);checked.push_back(device.available&&(device.installed||ready==1));}
    micfilter::SetupSelection selection(available,checked);
    for(;;){
        colored(kWhite,L"  Choose your microphones\n\n");
        for(size_t i=0;i<devices.size();++i){
            const auto& device=devices[i];
            colored(!device.available?kGray:selection.checked(i)?kGreen:kWhite,
                std::wstring(L"    ")+(!device.available?L"[-] ":selection.checked(i)?L"[x] ":L"[ ] ")+std::to_wstring(i+1)+L". "+device.name+L"\n");
            if(!device.available)colored(kYellow,L"        "+device.status+L". Its effects will be kept.\n");
            else if(device.installed)detail(L"        Already installed - safe to update.\n");
        }
        if(!ready)throw std::runtime_error("These microphones use other audio effects. Connect another microphone, or remove the other app's effect from an input and run setup again.");
        detail(L"\n  Type numbers to check or uncheck (example: 1,2).\n  A selects all available inputs. Enter continues with [x]. 0 cancels.\n");
        std::wcout<<L"\n  Your choice: "<<std::flush;
        std::wstring answer;if(!std::getline(std::wcin,answer))throw std::runtime_error("Cancelled. Nothing was changed.");
        switch(selection.apply(answer)){
        case micfilter::SelectionResult::Continue:return selection.selected();
        case micfilter::SelectionResult::Cancel:throw std::runtime_error("Cancelled. Nothing was changed.");
        case micfilter::SelectionResult::Empty:colored(kYellow,L"  Check at least one microphone before continuing.\n");break;
        case micfilter::SelectionResult::Unavailable:colored(kYellow,L"  That input has another audio effect. Choose an available microphone.\n");break;
        case micfilter::SelectionResult::Invalid:colored(kYellow,L"  Use the shown numbers, A, Enter or 0. Your checks were kept.\n");break;
        case micfilter::SelectionResult::Changed:break;
        }
        std::wcout<<L"\n";
    }
}
}
int wmain(int argc,wchar_t** argv){
    SetEnvironmentVariableW(L"PSModulePath",nullptr);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG{
        wchar_t code[16]{};swprintf(code,16,L"0x%08lX",e&&e->ExceptionRecord?e->ExceptionRecord->ExceptionCode:0UL);
        logLine(std::wstring(L"Setup closed unexpectedly, exception ")+code);return EXCEPTION_CONTINUE_SEARCH;});
    logLine(std::wstring(L"Started: ")+GetCommandLineW()+(admin()?L" (administrator)":L""));
    _setmode(_fileno(stdout),_O_U16TEXT);_setmode(_fileno(stderr),_O_U16TEXT);SetConsoleTitleW(L"MicFilter - installation");
    bool noPause=false,selfTest=false,testConsole=false,testSingle=false,testBlocked=false,list=false,checkInputs=false,uninstall=false;std::vector<std::wstring> requested;int preset=-1;
    for(int i=1;i<argc;++i){
        const std::wstring arg=argv[i];
        if(arg==L"--no-pause")noPause=true;else if(arg==L"--self-test")selfTest=true;else if(arg==L"--list-devices")list=true;else if(arg==L"--uninstall")uninstall=true;
        else if(arg==L"--check-inputs")list=checkInputs=true;
        else if(arg==L"--test-console")selfTest=testConsole=true;
        else if(arg==L"--test-console-single")selfTest=testConsole=testSingle=true;
        else if(arg==L"--test-console-blocked")selfTest=testConsole=testSingle=testBlocked=true;
        else if(arg==L"--preset"&&i+1<argc){const std::wstring value=argv[++i];if(value.size()!=1||value[0]<L'0'||value[0]>L'3'){std::wcerr<<L"Invalid voice choice.\n";return 2;}preset=value[0]-L'0';}
        else if(arg==L"--endpoint"&&i+1<argc){GUID guid{};if(FAILED(CLSIDFromString(argv[++i],&guid))){std::wcerr<<L"Invalid microphone identifier.\n";return 2;}wchar_t text[40]{};StringFromGUID2(guid,text,40);if(std::find(requested.begin(),requested.end(),text)==requested.end())requested.push_back(text);}
        else{std::wcerr<<L"Unknown option.\n";return 2;}
    }
    SYSTEM_INFO systemInfo{};GetNativeSystemInfo(&systemInfo);
    if(!selfTest&&systemInfo.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64){std::wcerr<<L"This installer requires Windows on an x64 processor (Intel/AMD).\n";if(!noPause&&!list)pause();return 2;}
    if(!selfTest&&!list&&!admin()){
        std::wstring args=uninstall?L"--uninstall ":L"";for(const auto& guid:requested)args+=L"--endpoint "+guid+L" ";if(preset>=0)args+=L"--preset "+std::to_wstring(preset)+L" ";if(noPause)args+=L"--no-pause";
        const int code=elevate(args);if(code==-1&&!noPause)pause();return code==-1?1:code;
    }
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;std::wstring staging;bool ownsStaging=false;
    HANDLE installationMutex=nullptr;
    try{
        if(selfTest){
            if(!micfilter::selectionSelfTest())throw std::runtime_error("Microphone checklist parsing failed.");
            HICON appIcon=nullptr;wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            if(ExtractIconExW(executable,0,&appIcon,nullptr,1)!=1||!appIcon)throw std::runtime_error("The installer icon is missing.");DestroyIcon(appIcon);
            verifyIcons(executable);
            wchar_t temp[32768]{};if(!GetTempPathW(32768,temp))throw std::runtime_error("No temporary folder is available.");
            staging=std::wstring(temp)+L"MicFilter-package-test-"+unique();if(!CreateDirectoryW(staging.c_str(),nullptr))throw std::runtime_error("Could not create the test directory.");ownsStaging=true;extract(staging);
            for(const auto& entry:payloads){const auto path=staging+L"\\"+entry.name;if(std::filesystem::file_size(path)==0)throw std::runtime_error("Empty resource.");if(entry.id<=102){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char magic[2]{};DWORD bytes=0;const auto ok=ReadFile(file,magic,2,&bytes,nullptr);CloseHandle(file);if(!ok||bytes!=2||magic[0]!='M'||magic[1]!='Z')throw std::runtime_error("Invalid embedded binary.");}}
            HICON applicationIcon=nullptr;
            if(ExtractIconExW((staging+L"\\MicFilter.exe").c_str(),0,&applicationIcon,nullptr,1)!=1||!applicationIcon)throw std::runtime_error("The application icon is missing.");DestroyIcon(applicationIcon);
            verifyIcons(staging+L"\\MicFilter.exe");
            std::wcout<<L"PASS package: 9 resources, EXE/DLL/icons, persistent multi-input checklist; no microphone changes.\n";result=0;
            if(testConsole){
                std::vector<Device> fixture{{L"",L"Desktop microphone",L"",L"Ready to install",true,false},{L"",L"Headset microphone",L"",L"Ready to install",true,false},{L"",L"Other microphone",L"",L"Uses another audio effect",false,false}};
                if(testSingle)fixture.resize(1);
                if(testBlocked){fixture.front().available=false;fixture.front().status=L"Uses another audio effect";}
                const auto choices=chooseMicrophones(fixture);
                std::wcout<<L"TEST console selection=";
                for(size_t i=0;i<choices.size();++i){if(i)std::wcout<<L",";std::wcout<<choices[i]+1;}
                std::wcout<<L" voice=default\n";
            }
        }else{
            if(!list){
                installationMutex=CreateMutexW(nullptr,FALSE,L"Global\\MicFilter.Setup.Installation");
                if(!installationMutex)throw std::runtime_error("Windows could not reserve setup. Close other setup windows and try again.");
                const auto acquired=WaitForSingleObject(installationMutex,0);
                if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED){CloseHandle(installationMutex);installationMutex=nullptr;throw std::runtime_error("MicFilter setup is already running. Finish the other setup window and try again.");}
            }
            if(uninstall){
                colored(kWhite,std::wstring(L"\n  MicFilter ")+kVersion+L" - uninstall\n\n");
                staging=createStaging();ownsStaging=true;extract(staging);result=runScript(staging,L"install.ps1\" -Action Uninstall -Quiet -ShowResult");
                if(result==0)ok(L"MicFilter was removed.");else failed(L"Uninstall did not complete. The message window explains why.");
            }else{
                auto devices=microphones();for(auto& device:devices)availability(device);
                if(list){
                    if(checkInputs){const auto ready=std::count_if(devices.begin(),devices.end(),[](const auto& device){return device.available;});std::wcout<<L"Ready inputs="<<ready<<L" Unavailable inputs="<<devices.size()-ready<<L"\n";}
                    else for(const auto& d:devices)std::wcout<<d.name<<L" | "<<d.guid<<L"\n";
                    result=0;
                }
                else{
                    colored(kWhite,std::wstring(L"\n  MicFilter ")+kVersion+L"\n");
                    detail(L"  Less background noise. Nothing is recorded or uploaded.\n\n");
                    if(devices.empty())throw std::runtime_error("No microphone found. Connect one, enable it in Windows Settings and run setup again.");
                    std::vector<size_t> choices;
                    if(requested.empty())choices=chooseMicrophones(devices);
                    else for(const auto& guid:requested){bool found=false;for(size_t i=0;i<devices.size();++i)if(_wcsicmp(devices[i].guid.c_str(),guid.c_str())==0){choices.push_back(i);found=true;break;}if(!found)throw std::runtime_error("A selected microphone is no longer connected. Reconnect it and run setup again.");}
                    std::wstring endpointList;
                    for(auto choice:choices){
                        const auto& selected=devices[choice];if(!selected.available)throw std::runtime_error("A selected microphone uses another audio effect. Its configuration was kept. Choose an available input.");
                        std::wstring report;bool compatible=false;
                        if(FAILED(checkAudio(selected.id,report,false,&compatible))){logLine(report);throw std::runtime_error("Windows could not open a selected microphone. Close audio apps, check microphone permissions in Windows Settings and try again.");}
                        if(!compatible){logLine(report);throw std::runtime_error("A selected microphone uses an unsupported format (1-8 channels, 8-192 kHz). Nothing was changed.");}
                        ok(L"Microphone ready: "+selected.name);
                        if(!endpointList.empty())endpointList+=L",";endpointList+=selected.guid;
                    }
                    staging=createStaging();ownsStaging=true;extract(staging);
                    std::wstring args=L"setup-install.ps1\" -EndpointList \""+endpointList+L"\"";if(preset>=0)args+=L" -VoicePreset "+std::to_wstring(preset);
                    result=runScript(staging,args);
                }
            }
        }
    }catch(const std::exception& error){const std::string text=error.what();std::wcout<<L"\n";failed(std::wstring(text.begin(),text.end()));result=1;}
    catch(...){std::wcout<<L"\n";failed(L"Setup stopped because of an unexpected error. Nothing else was changed.");result=1;}
    logLine(L"Finished with code "+std::to_wstring(result));
    if(installationMutex){ReleaseMutex(installationMutex);CloseHandle(installationMutex);}
    // Only this process's newly created, unpredictable staging/test directory is removed.
    if(ownsStaging){std::error_code error;std::filesystem::remove_all(staging,error);if(error)std::wcerr<<L"Could not clean up the staging folder: "<<staging<<L"\n";}
    CoUninitialize();if(!noPause&&!selfTest&&!list)pause();return result;
}
