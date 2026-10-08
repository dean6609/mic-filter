#pragma once
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

namespace micfilter {
enum class SelectionResult {Changed,Continue,Cancel,Invalid,Unavailable,Empty};
// Checked inputs are independent of the current prompt. Validate the entire answer before
// changing any checks, so a typo or an unavailable input never loses the user's selection.
class SetupSelection {
    std::vector<bool> available_,checked_;
public:
    SetupSelection(std::vector<bool> available,std::vector<bool> checked):available_(std::move(available)),checked_(std::move(checked)){}
    std::vector<size_t> selected()const{
        std::vector<size_t> result;for(size_t i=0;i<available_.size();++i)if(available_[i]&&checked_[i])result.push_back(i);return result;
    }
    bool checked(size_t i)const{return available_[i]&&checked_[i];}
    SelectionResult apply(std::wstring answer){
        std::transform(answer.begin(),answer.end(),answer.begin(),[](wchar_t c){return std::towlower(c);});
        const auto first=answer.find_first_not_of(L" \t\r\n");
        if(first==std::wstring::npos)return selected().empty()?SelectionResult::Empty:SelectionResult::Continue;
        answer=answer.substr(first,answer.find_last_not_of(L" \t\r\n")-first+1);
        if(answer==L"0")return SelectionResult::Cancel;
        if(answer==L"a"||answer==L"all"){checked_=available_;return SelectionResult::Changed;}
        std::replace(answer.begin(),answer.end(),L',',L' ');std::wistringstream tokens(answer);
        std::wstring token;std::vector<size_t> changes;
        while(tokens>>token){
            if(token.empty()||!std::all_of(token.begin(),token.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))return SelectionResult::Invalid;
            size_t number=0;try{number=std::stoul(token);}catch(...){return SelectionResult::Invalid;}
            if(number<1||number>available_.size())return SelectionResult::Invalid;
            if(!available_[number-1])return SelectionResult::Unavailable;
            if(std::find(changes.begin(),changes.end(),number-1)==changes.end())changes.push_back(number-1);
        }
        if(changes.empty())return SelectionResult::Invalid;
        for(auto i:changes)checked_[i]=!checked_[i];return SelectionResult::Changed;
    }
};
inline bool selectionSelfTest(){
    SetupSelection selection({true,true,false},{false,false,false});
    if(selection.apply(L"")!=SelectionResult::Empty)return false;
    if(selection.apply(L"1")!=SelectionResult::Changed||selection.selected()!=std::vector<size_t>{0})return false;
    if(selection.apply(L"2")!=SelectionResult::Changed||selection.selected()!=std::vector<size_t>{0,1})return false;
    if(selection.apply(L"1")!=SelectionResult::Changed||selection.selected()!=std::vector<size_t>{1})return false;
    if(selection.apply(L"1,3")!=SelectionResult::Unavailable||selection.selected()!=std::vector<size_t>{1})return false;
    for(const auto* bad:{L"1 nope",L"4",L"999999999999999999999999999",L"1.5",L","})
        if(selection.apply(bad)!=SelectionResult::Invalid||selection.selected()!=std::vector<size_t>{1})return false;
    if(selection.apply(L" A ")!=SelectionResult::Changed||selection.selected()!=std::vector<size_t>{0,1})return false;
    if(selection.apply(L"1,1")!=SelectionResult::Changed||selection.selected()!=std::vector<size_t>{1})return false;
    return selection.apply(L" ")==SelectionResult::Continue&&selection.apply(L"0")==SelectionResult::Cancel;
}
}
