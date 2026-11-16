// MicFilter for Linux and macOS: a terminal app. Pick a microphone and the cleaned sound is
// offered to other apps through a virtual microphone for as long as this window stays open.
#include "cli_config.h"
#include "cli_engine.h"
#include "cli_terminal.h"
#include "cli_virtual_mic.h"
#include "version.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace micfilter::cli;
volatile std::sig_atomic_t interrupted=0;
void onSignal(int){interrupted=1;}

struct Microphone {ma_device_id id;std::string name;bool isDefault;};
std::vector<Microphone> microphones(ma_context& context){
    std::vector<Microphone> result;ma_device_info* capture=nullptr;ma_uint32 count=0;
    if(ma_context_get_devices(&context,nullptr,nullptr,&capture,&count)!=MA_SUCCESS)return result;
    for(ma_uint32 i=0;i<count;++i)if(!ownOrLoopback(capture[i]))result.push_back({capture[i].id,capture[i].name,capture[i].isDefault!=0});
    return result;
}
const char* voiceName(unsigned preset){static const char* names[]={"Natural","Clear","Broadcast","Deep"};return names[preset<4?preset:0];}
const char* silenceName(unsigned permille){return permille==850?"Strict":permille==600?"Balanced":"Off";}

// Arrow keys on a terminal; typed numbers when input is piped. Returns -1 to quit.
int pickMicrophone(const std::vector<Microphone>& list,size_t current){
    RawTerminal raw;
    if(!raw.active()){
        for(size_t i=0;i<list.size();++i)std::printf("    %zu. %s%s\n",i+1,list[i].name.c_str(),list[i].isDefault?" (system default)":"");
        std::printf("\n  Microphone number (0 cancels): ");std::fflush(stdout);
        std::string answer;if(!std::getline(std::cin,answer))return -1;
        const auto number=boundedNumber(answer,0,static_cast<unsigned>(list.size()));
        return number?static_cast<int>(number)-1:-1;
    }
    unsigned drawn=0;
    for(;;){
        rewind(drawn);
        std::printf("  %s  %s\n\n",bold("Choose your microphone").c_str(),dim("↑/↓ move · Enter select · q quit").c_str());
        for(size_t i=0;i<list.size();++i){
            const auto label=list[i].name+(list[i].isDefault?dim("  system default"):"");
            if(i==current)std::printf("  %s %s\n",teal("❯ ●").c_str(),bold(label).c_str());
            else std::printf("    %s %s\n",dim("○").c_str(),label.c_str());
        }
        std::fflush(stdout);drawn=static_cast<unsigned>(list.size())+2;
        for(;;){
            if(interrupted)return -1;
            const auto key=readKey(200);
            if(key==Key::None||key==Key::Other)continue;
            if(key==Key::Quit)return -1;
            if(key==Key::Enter||key==Key::Space)return static_cast<int>(current);
            if(key==Key::Up)current=(current+list.size()-1)%list.size();
            if(key==Key::Down)current=(current+1)%list.size();
            break;
        }
    }
}

enum class Outcome {Quit,ChangeMicrophone,Lost};
// The live view: status, level meters, current choices and the keys that change them.
Outcome session(Engine& engine,Config& config,const Microphone& microphone,const VirtualMic& virtualMic){
    RawTerminal raw;const bool live=raw.active()&&useColor();
    if(!live)std::printf("  Reducing noise on %s. Press Ctrl+C to stop.\n",microphone.name.c_str());
    float inLevel=0,outLevel=0;unsigned drawn=0;
    std::uint64_t lastFrames=0;auto lastAudio=std::chrono::steady_clock::now(),lastSound=lastAudio;
    for(;;){
        if(interrupted)return Outcome::Quit;
        if(engine.lost.load())return Outcome::Lost;
        const auto now=std::chrono::steady_clock::now();
        const auto frames=engine.capturedFrames.load();
        if(frames!=lastFrames){lastFrames=frames;lastAudio=now;}
        const float inPeak=engine.inputPeak.exchange(0),outPeak=engine.outputPeak.exchange(0);
        if(inPeak>0)lastSound=now;
        inLevel=std::max(inPeak,inLevel*0.85f);outLevel=std::max(outPeak,outLevel*0.85f);
        const bool stalled=now-lastAudio>std::chrono::seconds(3);
        if(now-lastAudio>std::chrono::seconds(8))return Outcome::Lost;
        if(live){
            rewind(drawn);
            const bool on=engine.enabled.load();
            const auto state=stalled?yellow("● Waiting for microphone audio..."):on?green("● Reducing noise"):dim("○ Noise reduction is off - apps hear your original microphone");
            std::printf("  %s\n  %s\n\n",state.c_str(),dim(microphone.name+"  →  "+virtualMic.appName).c_str());
            std::printf("  Microphone  %s %4.0f dB\n",meter(inLevel).c_str(),std::max(-99.0f,decibels(inLevel)));
            std::printf("  Clean voice %s %4.0f dB\n\n",meter(outLevel).c_str(),std::max(-99.0f,decibels(outLevel)));
            std::printf("  Voice sound: %s    Silence between words: %s    Original sound: %s\n\n",
                bold(voiceName(engine.voice.load())).c_str(),bold(silenceName(engine.silencePermille.load())).c_str(),bold(engine.wetPermille.load()==850?"15%":"0%").c_str());
            std::printf("  %s on/off   %s voice   %s silence   %s original   %s microphone   %s quit\n",
                teal("space").c_str(),teal("v").c_str(),teal("s").c_str(),teal("o").c_str(),teal("m").c_str(),teal("q").c_str());
            drawn=9;
#if defined(__APPLE__)
            if(!stalled&&now-lastSound>std::chrono::seconds(4)){std::printf("  %s\n",yellow("No sound yet. Allow your terminal in System Settings > Privacy & Security > Microphone.").c_str());++drawn;}
#else
            (void)lastSound;
#endif
            std::fflush(stdout);
        }
        switch(readKey(66)){
        case Key::Quit:return Outcome::Quit;
        case Key::Microphone:return Outcome::ChangeMicrophone;
        case Key::Space:engine.enabled=!engine.enabled.load();break;
        case Key::Voice:config.voice=(engine.voice.load()+1)%4;engine.voice=config.voice;saveConfig(config);break;
        case Key::Silence:config.silencePermille=engine.silencePermille.load()==0?600:engine.silencePermille.load()==600?850:0;engine.silencePermille=config.silencePermille;saveConfig(config);break;
        case Key::Original:config.wetPermille=engine.wetPermille.load()==1000?850:1000;engine.wetPermille=config.wetPermille;saveConfig(config);break;
        default:break;
        }
    }
}

// Opened by double-click there is no terminal to draw in; open one and run inside it.
#if !defined(__APPLE__)
std::string selfPath(){char path[4096]{};const auto length=readlink("/proc/self/exe",path,sizeof(path)-1);return length>0?std::string(path,static_cast<size_t>(length)):std::string();}
void reopenInTerminal(){
    if(isatty(STDIN_FILENO)||isatty(STDOUT_FILENO)||std::getenv("MICFILTER_IN_TERMINAL"))return;
    if(!std::getenv("DISPLAY")&&!std::getenv("WAYLAND_DISPLAY"))return;
    const auto self=selfPath();if(self.empty())return;
    setenv("MICFILTER_IN_TERMINAL","1",1);
    const char* launchers[][3]={{"x-terminal-emulator","-e",nullptr},{"gnome-terminal","--",nullptr},{"ptyxis","--",nullptr},{"kgx","-e",nullptr},{"konsole","-e",nullptr},
        {"xfce4-terminal","-x",nullptr},{"kitty",nullptr,nullptr},{"alacritty","-e",nullptr},{"foot",nullptr,nullptr},{"xterm","-e",nullptr}};
    for(const auto& launcher:launchers){
        const char* arguments[4]{launcher[0],launcher[1]?launcher[1]:self.c_str(),launcher[1]?self.c_str():nullptr,nullptr};
        execvp(launcher[0],const_cast<char* const*>(arguments)); // returns only when this terminal is missing
    }
}
// Adds MicFilter to the applications menu for this user; no administrator rights involved.
int installShortcut(bool remove){
    const char* home=std::getenv("HOME");if(!home){fail("Your home folder could not be found.");return 1;}
    const auto binary=std::filesystem::path(home)/".local"/"bin"/"micfilter";
    const auto shortcut=std::filesystem::path(home)/".local"/"share"/"applications"/"micfilter.desktop";
    std::error_code error;
    if(remove){
        std::filesystem::remove(shortcut,error);std::filesystem::remove(binary,error);std::filesystem::remove_all(configDirectory(),error);
        ok("MicFilter was removed from your applications menu. Nothing else on your system was changed.");return 0;
    }
    std::filesystem::create_directories(binary.parent_path(),error);std::filesystem::create_directories(shortcut.parent_path(),error);
    const auto self=selfPath();
    if(self.empty()){fail("MicFilter could not find its own file.");return 1;}
    if(std::filesystem::path(self)!=binary){
        std::filesystem::copy_file(self,binary,std::filesystem::copy_options::overwrite_existing,error);
        if(error){fail("MicFilter could not be copied to "+binary.string());return 1;}
    }
    std::ofstream file(shortcut,std::ios::trunc);
    file<<"[Desktop Entry]\nType=Application\nName=MicFilter\nComment=Less background noise from your microphone\nExec=\""<<binary.string()
        <<"\"\nTerminal=true\nIcon=audio-input-microphone\nCategories=AudioVideo;Audio;\n";
    if(!file){fail("The menu shortcut could not be written.");return 1;}
    ok("MicFilter is now in your applications menu.");note("Remove it any time with: micfilter --uninstall");return 0;
}
#endif

// Engine and storage checks that need no sound card; used by automated builds.
int selfTest(){
    const auto directory=std::filesystem::temp_directory_path()/("micfilter-self-test-"+std::to_string(getpid()));
    Config saved;saved.microphone="Test = microphone";saved.voice=3;saved.silencePermille=600;saved.wetPermille=850;
    bool passed=saveConfig(saved,directory);const auto loaded=loadConfig(directory);
    passed=passed&&loaded.microphone==saved.microphone&&loaded.voice==3&&loaded.silencePermille==600&&loaded.wetPermille==850;
    {std::ofstream bad(directory/"config",std::ios::trunc);bad<<"voice=99\nsilence=7\noriginal=x\nnonsense\n";}
    const auto safe=loadConfig(directory);passed=passed&&safe.voice==0&&safe.silencePermille==0&&safe.wetPermille==1000;
    std::error_code error;std::filesystem::remove_all(directory,error);
    auto processor=std::make_unique<micfilter::RateProcessor>();passed=passed&&processor->initialize(1,48000);
    std::array<float,480> in{},out{};for(unsigned i=0;i<480;++i)in[i]=0.1f*std::sin(0.02f*static_cast<float>(i));
    for(unsigned block=0;block<20&&passed;++block){processor->process(in.data(),out.data(),480,false,{true,0,20,1,1});for(float x:out)passed=passed&&std::isfinite(x);}
    ma_pcm_rb ring;passed=passed&&ma_pcm_rb_init(ma_format_f32,1,960,nullptr,nullptr,&ring)==MA_SUCCESS;
    if(passed){
        ma_uint32 frames=480;void* buffer=nullptr;
        passed=ma_pcm_rb_acquire_write(&ring,&frames,&buffer)==MA_SUCCESS&&frames==480;
        if(passed){std::memcpy(buffer,in.data(),sizeof(in));ma_pcm_rb_commit_write(&ring,480);passed=ma_pcm_rb_available_read(&ring)==480;}
        ma_pcm_rb_uninit(&ring);
    }
    passed=passed&&decibels(1.0f)==0.0f&&decibels(0.0f)<-100.0f;
    std::printf(passed?"PASS terminal app: saved choices, safe handling of a damaged settings file, mono engine, audio ring; no microphone opened.\n":"FAIL terminal app self-test.\n");
    return passed?0:1;
}
void waitBeforeClosing(){
    if(!std::getenv("MICFILTER_IN_TERMINAL")||!isatty(STDIN_FILENO))return;
    note("\n  Press Enter to close.");std::fflush(stdout);std::string ignored;std::getline(std::cin,ignored);
}
int usage(){
    std::printf("MicFilter %s - microphone noise reduction\n\n"
        "  micfilter               choose a microphone and start\n"
        "  micfilter --list        show the available microphones\n"
        "  micfilter --device N    start with microphone N from --list\n"
#if !defined(__APPLE__)
        "  micfilter --install     add MicFilter to your applications menu\n"
        "  micfilter --uninstall   remove the menu entry and saved choices\n"
#endif
        "  micfilter --version\n",micfilter::kVersionText);
    return 0;
}
}

int main(int argc,char** argv){
    bool list=false;int requested=-1;
    for(int i=1;i<argc;++i){
        const std::string argument=argv[i];
        if(argument=="--version"){std::printf("MicFilter %s\n",micfilter::kVersionText);return 0;}
        if(argument=="--help"||argument=="-h")return usage();
        if(argument=="--self-test")return selfTest();
#if !defined(__APPLE__)
        if(argument=="--install")return installShortcut(false);
        if(argument=="--uninstall")return installShortcut(true);
#endif
        if(argument=="--list")list=true;
        else if(argument=="--device"&&i+1<argc){requested=static_cast<int>(boundedNumber(argv[++i],0,999))-1;if(requested<0){std::fprintf(stderr,"Use a microphone number from --list.\n");return 2;}}
        else{std::fprintf(stderr,"Unknown option. Try --help.\n");return 2;}
    }
#if !defined(__APPLE__)
    if(!list)reopenInTerminal();
#endif
    std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);std::signal(SIGHUP,onSignal);
    if(!list)banner(micfilter::kVersionText);

    ma_context context;auto contextConfig=ma_context_config_init();contextConfig.pulse.pApplicationName="MicFilter";
#if defined(__APPLE__)
    const ma_backend backends[]={ma_backend_coreaudio};
#else
    const ma_backend backends[]={ma_backend_pulseaudio};
#endif
    if(ma_context_init(backends,1,&contextConfig,&context)!=MA_SUCCESS){
#if defined(__APPLE__)
        fail("macOS audio could not be opened.");
#else
        fail("No PipeWire or PulseAudio sound server was found. MicFilter needs one of them (with pipewire-pulse on PipeWire).");
#endif
        waitBeforeClosing();return 1;
    }
    if(list){
        const auto found=microphones(context);
        for(size_t i=0;i<found.size();++i)std::printf("%zu. %s%s\n",i+1,found[i].name.c_str(),found[i].isDefault?" (system default)":"");
        ma_context_uninit(&context);return found.empty()?1:0;
    }
    auto virtualMic=openVirtualMic(context);
    if(!virtualMic.ready){fail(virtualMic.error);ma_context_uninit(&context);waitBeforeClosing();return 1;}

    auto config=loadConfig();auto engine=std::make_unique<Engine>();int result=0;
    engine->voice=config.voice;engine->silencePermille=config.silencePermille;engine->wetPermille=config.wetPermille;
    for(bool first=true;!interrupted;first=false){
        const auto found=microphones(context);
        if(found.empty()){fail("No microphone found. Connect one and open MicFilter again.");result=1;break;}
        size_t preselected=0;
        for(size_t i=0;i<found.size();++i)if(found[i].isDefault)preselected=i;
        for(size_t i=0;i<found.size();++i)if(!config.microphone.empty()&&found[i].name==config.microphone)preselected=i;
        int choice=-1;
        if(first&&requested>=0){if(static_cast<size_t>(requested)>=found.size()){fail("That microphone number is not in --list.");result=2;break;}choice=requested;}
        else if(first&&found.size()==1){choice=0;ok("Only one microphone found - selected automatically.");}
        else choice=pickMicrophone(found,preselected);
        if(choice<0)break;
        const auto& microphone=found[static_cast<size_t>(choice)];
        std::string error;
        if(!engine->start(context,microphone.id,virtualMic.playback,error)){std::printf("\n");fail(error);if(found.size()==1||requested>=0){result=1;break;}std::printf("\n");continue;}
        config.microphone=microphone.name;saveConfig(config);
        std::printf("\n");ok("Microphone: "+microphone.name);
        ok(std::string("In your apps, choose \"")+virtualMic.appName+"\" as the microphone."+(virtualMic.madeDefault?" It is now the system default.":""));
        note("  Keep this window open. Nothing is recorded or uploaded.");std::printf("\n");
        const auto outcome=session(*engine,config,microphone,virtualMic);
        engine->stop();std::printf("\n");
        if(outcome==Outcome::Quit)break;
        if(outcome==Outcome::Lost){warn("The microphone stopped sending audio. Choose a microphone again.");std::printf("\n");requested=-1;}
    }
    engine->stop();closeVirtualMic(virtualMic);ma_context_uninit(&context);
    if(result==0)ok("MicFilter closed. Your microphone is back to normal.");
    if(result!=0)waitBeforeClosing();
    return result;
}
