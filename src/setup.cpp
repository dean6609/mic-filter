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
#include "audio_check.h"

namespace {
constexpr wchar_t kVersion[]=L"0.5.3";
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
struct Device {std::wstring id,name,guid;};
std::wstring property(IPropertyStore* store,const PROPERTYKEY& key){PROPVARIANT p;PropVariantInit(&p);std::wstring text;if(SUCCEEDED(store->GetValue(key,&p))&&p.vt==VT_LPWSTR&&p.pwszVal)text=p.pwszVal;PropVariantClear(&p);return text;}
std::vector<Device> microphones(){
    std::vector<Device> devices;IMMDeviceEnumerator* enumerator=nullptr;IMMDeviceCollection* collection=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))))throw std::runtime_error("Could not enumerate Windows microphones.");
    if(SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection))){
        UINT count=0;collection->GetCount(&count);
        for(UINT i=0;i<count;++i){IMMDevice* device=nullptr;IPropertyStore* store=nullptr;LPWSTR id=nullptr;
            if(SUCCEEDED(collection->Item(i,&device))){if(SUCCEEDED(device->GetId(&id))&&SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store)))devices.push_back({id,property(store,PKEY_Device_FriendlyName),property(store,PKEY_AudioEndpoint_GUID)});if(store)store->Release();if(id)CoTaskMemFree(id);device->Release();}
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
    if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,staging.c_str(),&startup,&process))throw std::runtime_error("Could not start the installation script.");WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);return static_cast<int>(code);
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
    bool noPause=false,selfTest=false,list=false,uninstall=false;std::wstring requested;
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--no-pause")noPause=true;else if(arg==L"--self-test")selfTest=true;else if(arg==L"--list-devices")list=true;else if(arg==L"--uninstall")uninstall=true;else if(arg==L"--endpoint"&&i+1<argc){GUID guid{};if(FAILED(CLSIDFromString(argv[++i],&guid))){std::wcerr<<L"Invalid microphone identifier.\n";return 2;}wchar_t text[40]{};StringFromGUID2(guid,text,40);requested=text;}else{std::wcerr<<L"Unknown option.\n";return 2;}}
    SYSTEM_INFO systemInfo{};GetNativeSystemInfo(&systemInfo);
    if(!selfTest&&systemInfo.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64){std::wcerr<<L"This installer requires Windows on an x64 processor (Intel/AMD).\n";if(!noPause&&!list)pause();return 2;}
    if(!selfTest&&!list&&!admin()){
        const int code=elevate((uninstall?L"--uninstall ":L"")+(requested.empty()?L"":L"--endpoint "+requested+L" ")+(noPause?L"--no-pause":L""));
        // The administrator window pauses on its own; pause here only when it never started.
        if(code==-1&&!noPause)pause();
        return code==-1?1:code;
    }
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;std::wstring staging;bool ownsStaging=false;
    try {
        if(selfTest){
            wchar_t temp[32768]{};if(!GetTempPathW(32768,temp))throw std::runtime_error("No temporary folder is available.");staging=std::wstring(temp)+L"MicFilter-package-test-"+unique();if(!CreateDirectoryW(staging.c_str(),nullptr))throw std::runtime_error("Could not create the test directory.");ownsStaging=true;extract(staging);
            for(const auto& entry:payloads){const auto path=staging+L"\\"+entry.name;if(std::filesystem::file_size(path)==0)throw std::runtime_error("Empty resource.");if(entry.id<=102){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char magic[2]{};DWORD bytes=0;const auto ok=ReadFile(file,magic,2,&bytes,nullptr);CloseHandle(file);if(!ok||bytes!=2||magic[0]!='M'||magic[1]!='Z')throw std::runtime_error("Invalid embedded binary.");}}
            std::wcout<<L"PASS package: 9 resources extracted; valid EXE/DLL; no microphone installation or changes.\n";result=0;
        }else if(uninstall){
            colored(kWhite,std::wstring(L"\n  MicFilter ")+kVersion+L" - uninstall\n\n");
            detail(L"  Removing MicFilter and restoring your microphone...\n");
            staging=createStaging();ownsStaging=true;extract(staging);
            result=runScript(staging,L"install.ps1\" -Action Uninstall -Quiet -ShowResult");
            if(result==0)ok(L"MicFilter was removed.");else failed(L"Uninstall did not complete. The message window explains why.");
        }else{
            auto devices=microphones();if(devices.empty())throw std::runtime_error("No microphone found. Connect one, make sure it is enabled in Windows, and run setup again.");
            if(list){for(const auto& d:devices)std::wcout<<d.name<<L" | "<<d.guid<<L"\n";result=0;}
            else{
                colored(kWhite,std::wstring(L"\n  MicFilter ")+kVersion+L"\n");
                detail(L"  Noise suppression for your microphone. Nothing is recorded or uploaded.\n\n");
                size_t choice=0;
                if(!requested.empty()){bool found=false;for(size_t i=0;i<devices.size();++i)if(_wcsicmp(devices[i].guid.c_str(),requested.c_str())==0){choice=i;found=true;break;}if(!found)throw std::runtime_error("The requested microphone is not enabled and connected.");}
                else if(devices.size()>1){
                    std::wcout<<L"  Which microphone should MicFilter clean up?\n\n";
                    for(size_t i=0;i<devices.size();++i)std::wcout<<L"    "<<i+1<<L". "<<devices[i].name<<L"\n";
                    for(;;){std::wcout<<L"\n  Type its number and press Enter (0 cancels): "<<std::flush;std::wstring input;if(!std::getline(std::wcin,input))throw std::runtime_error("Cancelled. Nothing was changed.");try{size_t used=0;const auto number=std::stoul(input,&used);if(used!=input.size()||number>devices.size())continue;if(!number)throw std::runtime_error("Cancelled. Nothing was changed.");choice=number-1;break;}catch(const std::invalid_argument&){}catch(const std::out_of_range&){}}
                }
                const auto& selected=devices[choice];std::wcout<<L"\n  Microphone: "<<selected.name<<L"\n\n"<<std::flush;
                std::wstring report;bool compatible=false;
                if(FAILED(checkAudio(selected.id,report,false,&compatible))){detail(report);throw std::runtime_error("Windows could not open this microphone. Close apps that are using it, check microphone permissions in Settings > Privacy, and try again.");}
                if(!compatible){detail(report);throw std::runtime_error("This microphone uses an audio format MicFilter does not support (1-8 channels, 8-192 kHz). Nothing was changed.");}
                ok(L"Microphone ready");
                staging=createStaging();ownsStaging=true;extract(staging);
                result=runScript(staging,L"setup-install.ps1\" -EndpointGuid "+selected.guid);
            }
        }
    }catch(const std::exception& error){const std::string text=error.what();std::wcout<<L"\n";failed(std::wstring(text.begin(),text.end()));result=1;}
    catch(...){std::wcout<<L"\n";failed(L"Setup stopped because of an unexpected error. Nothing else was changed.");result=1;}
    logLine(L"Finished with code "+std::to_wstring(result));
    // Only this process's newly created, unpredictable directory is removed.
    if(ownsStaging){std::error_code error;std::filesystem::remove_all(staging,error);if(error)std::wcerr<<L"Could not clean up the staging folder: "<<staging<<L"\n";}
    CoUninitialize();if(!noPause&&!selfTest&&!list)pause();return result;
}
