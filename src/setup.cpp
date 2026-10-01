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
    std::wcout<<L"Windows will request administrator permission to install the effect.\n"<<std::flush;
    if(!ShellExecuteExW(&launch)){std::wcout<<L"Installation did not start. Cancelling the permission prompt leaves the microphone unchanged.\n";return 1;}
    const auto console=GetConsoleWindow();if(console)ShowWindow(console,SW_HIDE);
    WaitForSingleObject(launch.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(launch.hProcess,&code);CloseHandle(launch.hProcess);return static_cast<int>(code);
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
void pause(){std::wcout<<L"\nPress Enter to close.\n"<<std::flush;std::wstring ignored;std::getline(std::wcin,ignored);}
}
int wmain(int argc,wchar_t** argv){
    // Windows PowerShell 5.1 must build its own module path; one inherited from PowerShell 7 breaks cmdlet loading.
    SetEnvironmentVariableW(L"PSModulePath",nullptr);
    _setmode(_fileno(stdout),_O_U16TEXT);_setmode(_fileno(stderr),_O_U16TEXT);SetConsoleTitleW(L"MicFilter - installation");
    bool noPause=false,selfTest=false,list=false,uninstall=false;std::wstring requested;
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--no-pause")noPause=true;else if(arg==L"--self-test")selfTest=true;else if(arg==L"--list-devices")list=true;else if(arg==L"--uninstall")uninstall=true;else if(arg==L"--endpoint"&&i+1<argc){GUID guid{};if(FAILED(CLSIDFromString(argv[++i],&guid))){std::wcerr<<L"Invalid microphone identifier.\n";return 2;}wchar_t text[40]{};StringFromGUID2(guid,text,40);requested=text;}else{std::wcerr<<L"Unknown option.\n";return 2;}}
    SYSTEM_INFO systemInfo{};GetNativeSystemInfo(&systemInfo);
    if(!selfTest&&systemInfo.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64){std::wcerr<<L"This installer requires Windows on an x64 processor (Intel/AMD).\n";if(!noPause&&!list)pause();return 2;}
    if(!selfTest&&!list&&!admin())return elevate((uninstall?L"--uninstall ":L"")+(requested.empty()?L"":L"--endpoint "+requested+L" ")+(noPause?L"--no-pause":L""));
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;std::wstring staging;bool ownsStaging=false;
    try {
        if(selfTest){
            wchar_t temp[32768]{};if(!GetTempPathW(32768,temp))throw std::runtime_error("No temporary folder is available.");staging=std::wstring(temp)+L"MicFilter-package-test-"+unique();if(!CreateDirectoryW(staging.c_str(),nullptr))throw std::runtime_error("Could not create the test directory.");ownsStaging=true;extract(staging);
            for(const auto& entry:payloads){const auto path=staging+L"\\"+entry.name;if(std::filesystem::file_size(path)==0)throw std::runtime_error("Empty resource.");if(entry.id<=102){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char magic[2]{};DWORD bytes=0;const auto ok=ReadFile(file,magic,2,&bytes,nullptr);CloseHandle(file);if(!ok||bytes!=2||magic[0]!='M'||magic[1]!='Z')throw std::runtime_error("Invalid embedded binary.");}}
            std::wcout<<L"PASS package: 9 resources extracted; valid EXE/DLL; no microphone installation or changes.\n";result=0;
        }else if(uninstall){
            std::wcout<<L"MICFILTER 0.5.2 - uninstall\n\nRestoring microphones and deleting MicFilter files...\n"<<std::flush;
            staging=createStaging();ownsStaging=true;extract(staging);
            result=runScript(staging,L"install.ps1\" -Action Uninstall");
            std::wcout<<(result==0?L"\nMicFilter was removed. Restart Windows if a message above mentions files in use.\n":L"\nUninstall did not complete. See the messages above.\n");
        }else{
            auto devices=microphones();if(devices.empty())throw std::runtime_error("No enabled, connected microphones found. Connect one and run the installer again.");
            if(list){for(const auto& d:devices)std::wcout<<d.name<<L" | "<<d.guid<<L"\n";result=0;}
            else{
                std::wcout<<L"MICFILTER 0.5.2 - native microphone noise suppression\n\n"
                    L"RNNoise will be installed in Windows for the selected microphone.\n"
                    L"The model is included: no audio host or internet connection is needed.\n"
                    L"The first installation enables the filter. Rebooting preserves its state.\n"
                    L"It works without opening the app. Use the desktop shortcut to control it.\n"
                    L"Click the tray icon to enable/disable. Exit disables the filter.\n"
                    L"Opening the app later does NOT enable the filter by itself.\n"
                    L"Installation checks capture without saving your voice.\n"
                    L"Close applications currently using the microphone.\n\n";
                size_t choice=0;
                if(!requested.empty()){bool found=false;for(size_t i=0;i<devices.size();++i)if(_wcsicmp(devices[i].guid.c_str(),requested.c_str())==0){choice=i;found=true;break;}if(!found)throw std::runtime_error("The requested microphone is not enabled and connected.");}
                else if(devices.size()>1){
                    for(size_t i=0;i<devices.size();++i)std::wcout<<i+1<<L". "<<devices[i].name<<L"\n";
                    for(;;){std::wcout<<L"\nChoose the microphone number (0 cancels): "<<std::flush;std::wstring input;if(!std::getline(std::wcin,input))throw std::runtime_error("Selection cancelled.");try{size_t used=0;const auto number=std::stoul(input,&used);if(used!=input.size()||number>devices.size())continue;if(!number)throw std::runtime_error("Selection cancelled.");choice=number-1;break;}catch(const std::invalid_argument&){}catch(const std::out_of_range&){}}
                }
                const auto& selected=devices[choice];std::wcout<<L"\nMicrophone: "<<selected.name<<L"\n[1/5] Checking Windows audio capture...\n"<<std::flush;
                std::wstring report;bool compatible=false;if(FAILED(checkAudio(selected.id,report,false,&compatible))){std::wcout<<report;throw std::runtime_error("Cannot open this microphone. Check permissions or close other applications.");}
                if(!compatible){std::wcout<<report;throw std::runtime_error("Unsupported format. Supports 1-8 channels at 8-192 kHz in the Windows float engine. The filter was not installed.");}
                std::wcout<<report<<L"\n[2/5] Preparing embedded components...\n"<<std::flush;
                staging=createStaging();ownsStaging=true;extract(staging);
                result=runScript(staging,L"setup-install.ps1\" -EndpointGuid "+selected.guid);
            }
        }
    }catch(const std::exception& error){const std::string text=error.what();std::wcerr<<L"\nInstallation did not complete: "<<std::wstring(text.begin(),text.end())<<L"\n";result=1;}
    // Only this process's newly created, unpredictable directory is removed.
    if(ownsStaging){std::error_code error;std::filesystem::remove_all(staging,error);if(error)std::wcerr<<L"Could not clean up the staging folder: "<<staging<<L"\n";}
    CoUninitialize();if(!noPause&&!selfTest&&!list)pause();return result;
}
