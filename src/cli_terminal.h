#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace micfilter::cli {
enum class Key {None,Up,Down,Enter,Space,Quit,Voice,Silence,Original,Microphone,Other};

// Colors are used only on a real terminal and never when NO_COLOR is set.
inline bool useColor(){static const bool value=isatty(STDOUT_FILENO)&&!std::getenv("NO_COLOR");return value;}
inline std::string paint(const char* code,const std::string& text){return useColor()?std::string("\x1b[")+code+"m"+text+"\x1b[0m":text;}
inline std::string teal(const std::string& t){return paint("38;5;43",t);}
inline std::string green(const std::string& t){return paint("38;5;42",t);}
inline std::string yellow(const std::string& t){return paint("38;5;221",t);}
inline std::string red(const std::string& t){return paint("38;5;203",t);}
inline std::string dim(const std::string& t){return paint("38;5;245",t);}
inline std::string bold(const std::string& t){return paint("1",t);}

// Single-key input without Enter; the previous terminal mode always comes back.
class RawTerminal {
    termios saved_{};
    bool active_=false;
public:
    RawTerminal(){
        if(!isatty(STDIN_FILENO)||tcgetattr(STDIN_FILENO,&saved_)!=0)return;
        termios raw=saved_;raw.c_lflag&=~static_cast<tcflag_t>(ICANON|ECHO);raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=0;
        active_=tcsetattr(STDIN_FILENO,TCSANOW,&raw)==0;
        if(active_&&useColor())std::fputs("\x1b[?25l",stdout); // hide the cursor while drawing
    }
    ~RawTerminal(){
        if(!active_)return;
        tcsetattr(STDIN_FILENO,TCSANOW,&saved_);
        if(useColor())std::fputs("\x1b[?25h",stdout);
        std::fflush(stdout);
    }
    RawTerminal(const RawTerminal&)=delete;
    RawTerminal& operator=(const RawTerminal&)=delete;
    bool active()const{return active_;}
};
// Waits up to `milliseconds` for one key. Arrow keys arrive as ESC [ A/B.
inline Key readKey(int milliseconds){
    pollfd waiting{STDIN_FILENO,POLLIN,0};
    if(poll(&waiting,1,milliseconds)<=0)return Key::None;
    unsigned char bytes[8]{};const auto count=read(STDIN_FILENO,bytes,sizeof(bytes));
    if(count<=0)return Key::Quit; // the terminal went away
    if(bytes[0]==0x1b&&count>=3&&bytes[1]=='['){return bytes[2]=='A'?Key::Up:bytes[2]=='B'?Key::Down:Key::Other;}
    switch(bytes[0]){
    case '\r':case '\n':return Key::Enter;
    case ' ':return Key::Space;
    case 'q':case 'Q':case 3:case 0x1b:return Key::Quit;
    case 'k':case 'K':return Key::Up;
    case 'j':case 'J':return Key::Down;
    case 'v':case 'V':return Key::Voice;
    case 's':case 'S':return Key::Silence;
    case 'o':case 'O':return Key::Original;
    case 'm':case 'M':return Key::Microphone;
    default:return Key::Other;
    }
}
// Redraw a block of lines in place: move back to its first line and clear below.
inline void rewind(unsigned lines){if(lines&&useColor())std::printf("\x1b[%uA\x1b[J",lines);}

inline float decibels(float level){return level>0.000001f?20.0f*std::log10(level):-120.0f;}
// A 24-cell level meter covering -60..0 dBFS. Plain ASCII when colors are off (logs, pipes).
inline std::string meter(float level){
    constexpr int cells=24;
    const int filled=std::clamp(static_cast<int>(std::lround((decibels(level)+60.0f)/60.0f*cells)),0,cells);
    if(!useColor())return "["+std::string(static_cast<size_t>(filled),'#')+std::string(static_cast<size_t>(cells-filled),'.')+"]";
    std::string bar;
    for(int i=0;i<cells;++i){
        const char* color=i>=cells-2?"38;5;203":i>=cells-6?"38;5;221":"38;5;43";
        bar+=i<filled?paint(color,"█"):paint("38;5;238","░");
    }
    return bar;
}
inline void banner(const char* version){
    const std::string title=std::string("MicFilter ")+version,tagline="Your voice, without the background noise.";
    const size_t width=tagline.size()+4;
    if(!useColor()){std::printf("\n  %s\n  %s\n\n",title.c_str(),tagline.c_str());return;}
    std::string edge;for(size_t i=0;i<width;++i)edge+="─";
    std::printf("\n  %s\n",teal("╭"+edge+"╮").c_str());
    std::printf("  %s  %s%s%s\n",teal("│").c_str(),bold(title).c_str(),std::string(width-2-title.size(),' ').c_str(),teal("│").c_str());
    std::printf("  %s  %s  %s\n",teal("│").c_str(),dim(tagline).c_str(),teal("│").c_str());
    std::printf("  %s\n\n",teal("╰"+edge+"╯").c_str());
}
inline void ok(const std::string& text){std::printf("  %s %s\n",green(useColor()?"✓":"OK").c_str(),text.c_str());}
inline void warn(const std::string& text){std::printf("  %s %s\n",yellow("!").c_str(),text.c_str());}
inline void fail(const std::string& text){std::printf("  %s %s\n",red(useColor()?"✗":"X").c_str(),text.c_str());}
inline void note(const std::string& text){std::printf("  %s\n",dim(text).c_str());}
}
