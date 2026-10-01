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
constexpr Payload payloads[]={{101,L"WavoFilter.exe"},{102,L"WavoFilterAPO-v3.dll"},{103,L"install.ps1"},{104,L"installer-registry.ps1"},{105,L"LICENSE"},{106,L"RNNOISE-LICENSE.txt"},{107,L"SPEEX-LICENSE.txt"},{108,L"setup-install.ps1"},{109,L"LEEME.txt"}};
struct Device {std::wstring id,name,guid;};
std::wstring property(IPropertyStore* store,const PROPERTYKEY& key){PROPVARIANT p;PropVariantInit(&p);std::wstring text;if(SUCCEEDED(store->GetValue(key,&p))&&p.vt==VT_LPWSTR&&p.pwszVal)text=p.pwszVal;PropVariantClear(&p);return text;}
std::vector<Device> microphones(){
    std::vector<Device> devices;IMMDeviceEnumerator* enumerator=nullptr;IMMDeviceCollection* collection=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator))))throw std::runtime_error("No se pudieron consultar los microfonos de Windows.");
    if(SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture,DEVICE_STATE_ACTIVE,&collection))){
        UINT count=0;collection->GetCount(&count);
        for(UINT i=0;i<count;++i){IMMDevice* device=nullptr;IPropertyStore* store=nullptr;LPWSTR id=nullptr;
            if(SUCCEEDED(collection->Item(i,&device))){if(SUCCEEDED(device->GetId(&id))&&SUCCEEDED(device->OpenPropertyStore(STGM_READ,&store)))devices.push_back({id,property(store,PKEY_Device_FriendlyName),property(store,PKEY_AudioEndpoint_GUID)});if(store)store->Release();if(id)CoTaskMemFree(id);device->Release();}
        }collection->Release();
    }enumerator->Release();return devices;
}
bool admin(){SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID sid=nullptr;BOOL member=FALSE;if(AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(nullptr,sid,&member);FreeSid(sid);}return member!=FALSE;}
std::wstring unique(){GUID id{};wchar_t text[40]{};if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,text,40))throw std::runtime_error("No se pudo crear una carpeta temporal unica.");return text;}
void extract(const std::wstring& directory){
    for(const auto& entry:payloads){auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(entry.id),RT_RCDATA);auto loaded=resource?LoadResource(nullptr,resource):nullptr;const auto size=resource?SizeofResource(nullptr,resource):0;const auto* data=loaded?LockResource(loaded):nullptr;
        if(!data||!size)throw std::runtime_error("El instalador no contiene todos sus componentes.");
        HANDLE file=CreateFileW((directory+L"\\"+entry.name).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("No se pudo extraer un componente.");DWORD written=0;const bool ok=WriteFile(file,data,size,&written,nullptr)!=0;CloseHandle(file);if(!ok||written!=size)throw std::runtime_error("La extraccion de un componente quedo incompleta.");
    }
}
int elevate(const std::wstring& arguments){
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);launch.fMask=SEE_MASK_NOCLOSEPROCESS;launch.lpVerb=L"runas";launch.lpFile=path;launch.lpParameters=arguments.c_str();launch.nShow=SW_SHOWNORMAL;
    std::wcout<<L"Windows solicitará permiso de administrador para instalar el efecto.\n"<<std::flush;
    if(!ShellExecuteExW(&launch)){std::wcout<<L"No se inició la instalación. Si cancelaste el permiso, no se modificó el micrófono.\n";return 1;}
    const auto console=GetConsoleWindow();if(console)ShowWindow(console,SW_HIDE);
    WaitForSingleObject(launch.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(launch.hProcess,&code);CloseHandle(launch.hProcess);return static_cast<int>(code);
}
void pause(){std::wcout<<L"\nPulsa Enter para cerrar.\n"<<std::flush;std::wstring ignored;std::getline(std::wcin,ignored);}
}
int wmain(int argc,wchar_t** argv){
    _setmode(_fileno(stdout),_O_U16TEXT);_setmode(_fileno(stderr),_O_U16TEXT);SetConsoleTitleW(L"Wavo Filter — instalación");
    bool noPause=false,selfTest=false,list=false;std::wstring requested;
    for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--no-pause")noPause=true;else if(arg==L"--self-test")selfTest=true;else if(arg==L"--list-devices")list=true;else if(arg==L"--endpoint"&&i+1<argc){GUID guid{};if(FAILED(CLSIDFromString(argv[++i],&guid))){std::wcerr<<L"Identificador de micrófono inválido.\n";return 2;}wchar_t text[40]{};StringFromGUID2(guid,text,40);requested=text;}else{std::wcerr<<L"Opción no reconocida.\n";return 2;}}
    SYSTEM_INFO systemInfo{};GetNativeSystemInfo(&systemInfo);
    if(!selfTest&&systemInfo.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64){std::wcerr<<L"Este instalador requiere Windows para procesadores x64 (Intel/AMD).\n";if(!noPause&&!list)pause();return 2;}
    if(!selfTest&&!list&&!admin())return elevate((requested.empty()?L"":L"--endpoint "+requested+L" ")+(noPause?L"--no-pause":L""));
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=1;std::wstring staging;bool ownsStaging=false;
    try {
        if(selfTest){
            wchar_t temp[32768]{};if(!GetTempPathW(32768,temp))throw std::runtime_error("No hay carpeta temporal.");staging=std::wstring(temp)+L"WavoFilter-package-test-"+unique();if(!CreateDirectoryW(staging.c_str(),nullptr))throw std::runtime_error("No se pudo crear el directorio de prueba.");ownsStaging=true;extract(staging);
            for(const auto& entry:payloads){const auto path=staging+L"\\"+entry.name;if(std::filesystem::file_size(path)==0)throw std::runtime_error("Recurso vacio.");if(entry.id<=102){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);char magic[2]{};DWORD bytes=0;const auto ok=ReadFile(file,magic,2,&bytes,nullptr);CloseHandle(file);if(!ok||bytes!=2||magic[0]!='M'||magic[1]!='Z')throw std::runtime_error("Binario empaquetado invalido.");}}
            std::wcout<<L"PASS paquete: 9 recursos extraídos; EXE/DLL válidos; sin instalar ni modificar el micrófono.\n";result=0;
        }else{
            auto devices=microphones();if(devices.empty())throw std::runtime_error("No hay microfonos habilitados y conectados. Conecta uno y vuelve a ejecutar el instalador.");
            if(list){for(const auto& d:devices)std::wcout<<d.name<<L" | "<<d.guid<<L"\n";result=0;}
            else{
                std::wcout<<L"WAVO FILTER 0.3.0 — reducción de ruido del micrófono\n\n"
                    L"Se instalará RNNoise en Windows para el micrófono elegido.\n"
                    L"El modelo viene incluido: no necesitas Clownfish, VST ni conexión a internet.\n"
                    L"La primera instalación activa el filtro. Al reiniciar conserva su estado.\n"
                    L"Funciona sin abrir el programa. El acceso del escritorio permite controlarlo.\n"
                    L"Un clic en su icono activa/desactiva. Salir lo desactiva.\n"
                    L"Abrir el programa después NO lo activa por sí solo.\n"
                    L"Durante la instalación se comprueba la captura sin guardar tu voz.\n"
                    L"Cierra las aplicaciones que estén usando el micrófono.\n\n";
                size_t choice=0;
                if(!requested.empty()){bool found=false;for(size_t i=0;i<devices.size();++i)if(_wcsicmp(devices[i].guid.c_str(),requested.c_str())==0){choice=i;found=true;break;}if(!found)throw std::runtime_error("El microfono solicitado no esta habilitado o conectado.");}
                else if(devices.size()>1){
                    for(size_t i=0;i<devices.size();++i)std::wcout<<i+1<<L". "<<devices[i].name<<L"\n";
                    for(;;){std::wcout<<L"\nElige el número del micrófono (0 cancela): "<<std::flush;std::wstring input;if(!std::getline(std::wcin,input))throw std::runtime_error("Seleccion cancelada.");try{size_t used=0;const auto number=std::stoul(input,&used);if(used!=input.size()||number>devices.size())continue;if(!number)throw std::runtime_error("Seleccion cancelada.");choice=number-1;break;}catch(const std::invalid_argument&){}catch(const std::out_of_range&){}}
                }
                const auto& selected=devices[choice];std::wcout<<L"\nMicrófono: "<<selected.name<<L"\n[1/5] Comprobando que Windows pueda abrir el audio…\n"<<std::flush;
                std::wstring report;bool compatible=false;if(FAILED(checkAudio(selected.id,report,false,&compatible))){std::wcout<<report;throw std::runtime_error("No se puede abrir este microfono. Revisa sus permisos o cierra otras aplicaciones.");}
                if(!compatible){std::wcout<<report;throw std::runtime_error("Formato no compatible. Se admiten 1 a 8 canales, 8 a 192 kHz, en el motor flotante de Windows. No se ha instalado el filtro.");}
                std::wcout<<report<<L"\n[2/5] Preparando los componentes incluidos…\n"<<std::flush;
                PWSTR programFiles=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&programFiles)))throw std::runtime_error("No hay carpeta Program Files.");staging=std::wstring(programFiles)+L"\\WavoFilter-Setup-"+unique();CoTaskMemFree(programFiles);
                PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr))throw std::runtime_error("No se pudo proteger la carpeta temporal.");SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};const auto created=CreateDirectoryW(staging.c_str(),&attributes);LocalFree(descriptor);if(!created)throw std::runtime_error("No se pudo crear la carpeta de instalacion.");ownsStaging=true;extract(staging);
                wchar_t system[32768]{};GetSystemDirectoryW(system,32768);const auto powershell=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
                auto command=L"\""+powershell+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+staging+L"\\setup-install.ps1\" -EndpointGuid "+selected.guid;
                STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
                if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,staging.c_str(),&startup,&process))throw std::runtime_error("No se pudo iniciar la instalacion.");WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);result=static_cast<int>(code);
            }
        }
    }catch(const std::exception& error){const std::string text=error.what();std::wcerr<<L"\nNo se completó la instalación: "<<std::wstring(text.begin(),text.end())<<L"\n";result=1;}
    // Only this process's newly created, unpredictable directory is removed.
    if(ownsStaging){std::error_code error;std::filesystem::remove_all(staging,error);if(error)std::wcerr<<L"No se pudo limpiar la carpeta temporal: "<<staging<<L"\n";}
    CoUninitialize();if(!noPause&&!selfTest&&!list)pause();return result;
}
