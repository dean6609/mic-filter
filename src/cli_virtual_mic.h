#pragma once
#include "audio_backend.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// The virtual microphone other apps select. Linux creates it in PipeWire/PulseAudio for as
// long as MicFilter runs and removes it on exit. macOS has no built-in equivalent, so an
// installed loopback driver (BlackHole) is used and never modified.
namespace micfilter::cli {
struct VirtualMic {
    bool ready=false;
    std::string error;            // plain-language reason and what to do
    std::string appName;          // the name to choose inside other apps
    bool madeDefault=false;       // apps that follow the system default need no change
    ma_device_id playback{};      // where the filtered audio is played
    std::vector<std::string> modules;
    std::string previousDefault;
};
inline std::string lowered(std::string text){
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return text;
}
inline bool contains(const std::string& text,const char* part){return lowered(text).find(part)!=std::string::npos;}

#if defined(__APPLE__)
inline bool loopbackName(const std::string& name){return contains(name,"blackhole");}
// Inputs that would feed MicFilter its own output.
inline bool ownOrLoopback(const ma_device_info& device){return loopbackName(device.name);}
inline VirtualMic openVirtualMic(ma_context& context){
    VirtualMic mic;ma_device_info* playback=nullptr;ma_uint32 count=0;
    if(ma_context_get_devices(&context,&playback,&count,nullptr,nullptr)==MA_SUCCESS)
        for(ma_uint32 i=0;i<count;++i)if(loopbackName(playback[i].name)){mic.ready=true;mic.appName=playback[i].name;mic.playback=playback[i].id;break;}
    if(!mic.ready)mic.error=
        "macOS needs a virtual audio device to pass the clean sound to your apps.\n"
        "  Install the free BlackHole 2ch driver, then open MicFilter again:\n\n"
        "      brew install blackhole-2ch\n\n"
        "  or download it from https://existential.audio/blackhole/";
    return mic;
}
inline void closeVirtualMic(VirtualMic&){}
#else
inline constexpr char kSinkName[]="micfilter_sink";
inline constexpr char kSourceName[]="micfilter_mic";
// Runs a fixed command and returns what it printed. Never receives untrusted text.
inline std::string commandOutput(const std::string& command,bool* succeeded=nullptr){
    std::string output;if(succeeded)*succeeded=false;
    FILE* pipe=popen((command+" 2>/dev/null").c_str(),"r");if(!pipe)return output;
    char chunk[512];size_t count=0;
    while((count=std::fread(chunk,1,sizeof(chunk),pipe))>0)output.append(chunk,count);
    const int status=pclose(pipe);if(succeeded)*succeeded=status==0;
    while(!output.empty()&&std::isspace(static_cast<unsigned char>(output.back())))output.pop_back();
    return output;
}
inline bool moduleNumber(const std::string& text){return !text.empty()&&text.size()<12&&std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isdigit(c)!=0;});}
// Source names come from the sound server; only plain ones are ever passed back to a command.
inline bool plainName(const std::string& text){
    return !text.empty()&&text.size()<200&&std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isalnum(c)||std::strchr("._-:+@",c);});
}
// A previous run that was killed can leave its devices behind; remove exactly those.
inline void removeLeftovers(){
    std::istringstream lines(commandOutput("pactl list short modules"));std::string line;std::vector<std::string> sources,sinks;
    while(std::getline(lines,line)){
        const auto id=line.substr(0,line.find('\t'));if(!moduleNumber(id))continue;
        if(line.find(std::string("source_name=")+kSourceName)!=std::string::npos)sources.push_back(id);
        else if(line.find(std::string("sink_name=")+kSinkName)!=std::string::npos)sinks.push_back(id);
    }
    for(const auto& id:sources)commandOutput("pactl unload-module "+id);
    for(const auto& id:sinks)commandOutput("pactl unload-module "+id);
}
inline bool ownOrLoopback(const ma_device_info& device){
    const std::string id=device.id.pulse;
    return id==kSourceName||id.find(kSinkName)!=std::string::npos||(id.size()>8&&id.compare(id.size()-8,8,".monitor")==0);
}
inline VirtualMic openVirtualMic(ma_context&){
    VirtualMic mic;bool worked=false;
    commandOutput("pactl info",&worked);
    if(!worked){
        mic.error=
            "MicFilter needs PipeWire or PulseAudio and its 'pactl' tool.\n"
            "  Install it and open MicFilter again:\n\n"
            "      Debian/Ubuntu:  sudo apt install pulseaudio-utils\n"
            "      Fedora:         sudo dnf install pulseaudio-utils\n"
            "      Arch:           sudo pacman -S libpulse";
        return mic;
    }
    removeLeftovers();
    const auto previous=commandOutput("pactl get-default-source");
    if(plainName(previous)&&previous!=kSourceName)mic.previousDefault=previous;
    const auto sink=commandOutput(std::string("pactl load-module module-null-sink sink_name=")+kSinkName+
        " rate=48000 channels=1 channel_map=mono sink_properties=device.description=MicFilter-Output",&worked);
    if(!worked||!moduleNumber(sink)){mic.error="The sound server could not create the MicFilter output. Restart your session and try again.";return mic;}
    mic.modules.push_back(sink);
    const auto source=commandOutput(std::string("pactl load-module module-remap-source master=")+kSinkName+".monitor source_name="+kSourceName+
        " channels=1 channel_map=mono source_properties=device.description=MicFilter-Microphone",&worked);
    if(!worked||!moduleNumber(source)){
        commandOutput("pactl unload-module "+sink);mic.modules.clear();
        mic.error="The sound server could not create the MicFilter microphone. Restart your session and try again.";return mic;
    }
    mic.modules.push_back(source);
    commandOutput(std::string("pactl set-default-source ")+kSourceName,&mic.madeDefault);
    std::snprintf(mic.playback.pulse,sizeof(mic.playback.pulse),"%s",kSinkName);
    mic.appName="MicFilter-Microphone";mic.ready=true;
    return mic;
}
// Puts the default microphone back and removes only the devices this run created.
inline void closeVirtualMic(VirtualMic& mic){
    if(!mic.previousDefault.empty())commandOutput("pactl set-default-source "+mic.previousDefault);
    for(auto id=mic.modules.rbegin();id!=mic.modules.rend();++id)commandOutput("pactl unload-module "+*id);
    mic.modules.clear();mic.ready=false;
}
#endif
}
