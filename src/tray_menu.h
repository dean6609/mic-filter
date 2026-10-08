#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace micfilter {
struct InputMenuItem {std::wstring label;UINT command;bool selected=false;};
struct TrayMenuSettings {
    std::wstring status;
    bool ready=false,optionsReady=false,busy=false;
    LONG voice=0,threshold=0,wet=1000;
    std::vector<InputMenuItem> inputs;
};
inline void menuChoice(HMENU menu,bool enabled,bool checked,UINT command,const wchar_t* label){
    AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED)|(checked?MF_CHECKED:0),command,label);
}
inline HMENU createTrayMenu(const TrayMenuSettings& settings){
    auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|MF_DISABLED,0,settings.status.c_str());
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    auto voice=CreatePopupMenu();
    const wchar_t* voices[]={L"Natural (as captured)",L"Clear (gentle clarity)",L"Broadcast (even volume)",L"Deep (body and clarity for lower voices)"};
    for(UINT i=0;i<4;++i)menuChoice(voice,settings.ready&&settings.optionsReady,settings.voice==static_cast<LONG>(i),120+i,voices[i]);
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(voice),L"Voice sound");
    auto silence=CreatePopupMenu();
    menuChoice(silence,settings.ready,settings.threshold==0,112,L"Off (recommended)");
    menuChoice(silence,settings.ready,settings.threshold==600,111,L"Balanced (gentle pauses)");
    menuChoice(silence,settings.ready,settings.threshold==850,110,L"Strict (quieter pauses)");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(silence),L"Silence between words");
    auto original=CreatePopupMenu();
    menuChoice(original,settings.ready,settings.wet==1000,113,L"0% (cleanest)");
    menuChoice(original,settings.ready,settings.wet==850,114,L"15% (more natural)");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(original),L"Original microphone sound");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    auto inputs=CreatePopupMenu();
    for(const auto& input:settings.inputs)menuChoice(inputs,!settings.busy,input.selected,input.command,input.label.c_str());
    if(settings.inputs.empty())AppendMenuW(inputs,MF_STRING|MF_GRAYED,0,L"No microphone set up");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(inputs),L"Microphone activity");
    AppendMenuW(menu,MF_STRING|(settings.busy?MF_GRAYED:0),130,L"Add or update microphones...");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(settings.busy?MF_GRAYED:0),103,L"Exit and turn off noise reduction");
    return menu;
}
inline bool trayMenuSelfTest(){
    const auto menu=createTrayMenu({L"Noise reduction: Off",true,true,false,3,600,850,{{L"Test microphone",1000,true}}});
    if(!menu)return false;
    const auto voice=GetSubMenu(menu,2),silence=GetSubMenu(menu,3),original=GetSubMenu(menu,4),inputs=GetSubMenu(menu,6);
    bool valid=GetMenuItemCount(menu)==10&&voice&&silence&&original&&inputs&&GetMenuItemCount(voice)==4&&GetMenuItemCount(silence)==3&&GetMenuItemCount(original)==2&&GetMenuItemCount(inputs)==1;
    valid=valid&&(GetMenuState(voice,123,MF_BYCOMMAND)&MF_CHECKED)&&(GetMenuState(silence,111,MF_BYCOMMAND)&MF_CHECKED)&&(GetMenuState(original,114,MF_BYCOMMAND)&MF_CHECKED);
    for(UINT removed:{100,102,104,105,115,116})if(GetMenuState(menu,removed,MF_BYCOMMAND)!=static_cast<UINT>(-1))valid=false;
    DestroyMenu(menu);return valid;
}
}
