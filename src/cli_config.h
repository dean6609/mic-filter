#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace micfilter::cli {
// Choices remembered between launches. Plain "key=value" text in the user's own folder.
struct Config {
    std::string microphone;      // device name, only to preselect it next time
    unsigned voice=0;            // 0 Natural, 1 Clear, 2 Broadcast, 3 Deep
    unsigned silencePermille=0;  // 0 Off, 600 Balanced, 850 Strict
    unsigned wetPermille=1000;   // 1000 = 0% original sound, 850 = 15%
};
inline std::filesystem::path configDirectory(){
    const char* home=std::getenv("HOME");
#if defined(__APPLE__)
    return std::filesystem::path(home?home:".")/"Library"/"Application Support"/"MicFilter";
#else
    const char* xdg=std::getenv("XDG_CONFIG_HOME");
    if(xdg&&*xdg)return std::filesystem::path(xdg)/"micfilter";
    return std::filesystem::path(home?home:".")/".config"/"micfilter";
#endif
}
inline unsigned boundedNumber(const std::string& text,unsigned fallback,unsigned maximum){
    try{const unsigned long value=std::stoul(text);return value<=maximum?static_cast<unsigned>(value):fallback;}
    catch(...){return fallback;}
}
inline Config loadConfig(const std::filesystem::path& directory=configDirectory()){
    Config config;std::ifstream file(directory/"config");std::string line;
    while(std::getline(file,line)){
        const auto split=line.find('=');if(split==std::string::npos)continue;
        const auto key=line.substr(0,split),value=line.substr(split+1);
        if(key=="microphone")config.microphone=value;
        else if(key=="voice")config.voice=boundedNumber(value,0,3);
        else if(key=="silence"){const auto v=boundedNumber(value,0,1000);config.silencePermille=(v==600||v==850)?v:0;}
        else if(key=="original")config.wetPermille=boundedNumber(value,0,1000)==150?850:1000;
    }
    return config;
}
inline bool saveConfig(const Config& config,const std::filesystem::path& directory=configDirectory()){
    std::error_code error;std::filesystem::create_directories(directory,error);
    std::ofstream file(directory/"config",std::ios::trunc);if(!file)return false;
    file<<"microphone="<<config.microphone<<"\nvoice="<<config.voice<<"\nsilence="<<config.silencePermille
        <<"\noriginal="<<(config.wetPermille==850?150:0)<<"\n";
    return static_cast<bool>(file);
}
}
