#pragma once
#include <string>
#include <array>
#include <cwctype>

namespace penumbra::setup {
struct CompletionOptions { bool run=true; };
inline bool Version(const std::wstring& text,std::array<unsigned,3>& parts) {
    size_t pos=0;
    for(size_t i=0;i<parts.size();++i) {
        if(pos==text.size()||text[pos]<L'0'||text[pos]>L'9') return false;
        unsigned value=0,limit=i==2?65535:255;
        while(pos<text.size()&&text[pos]>=L'0'&&text[pos]<=L'9') {
            value=value*10+static_cast<unsigned>(text[pos++]-L'0');
            if(value>limit) return false;
        }
        parts[i]=value;
        if(i<2&&(pos==text.size()||text[pos++]!=L'.')) return false;
    }
    return pos==text.size();
}
inline int CompareVersions(const std::wstring& left,const std::wstring& right) {
    std::array<unsigned,3> a{},b{};
    if(!Version(left,a)||!Version(right,b)) return 0;
    return a<b?-1:(a>b?1:0);
}
// MSI properties are quoted; reject quotes, device/UNC paths, dot segments,
// reserved DOS names and paths whose installed payload would exceed MAX_PATH.
inline bool NormalizeDirectory(const std::wstring& input,std::wstring& path,size_t limit=200) {
    path=input;
    while(!path.empty()&&iswspace(path.front())) path.erase(0,1);
    while(!path.empty()&&iswspace(path.back())) path.pop_back();
    for(auto& ch:path) if(ch==L'/') ch=L'\\';
    while(path.size()>3&&path.back()==L'\\') path.pop_back();
    if(path.size()<4||path.size()>limit||!((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))||path[1]!=L':'||path[2]!=L'\\') return false;
    for(size_t start=3;start<path.size();) {
        size_t end=path.find(L'\\',start);
        if(end==std::wstring::npos) end=path.size();
        std::wstring segment=path.substr(start,end-start);
        if(segment.empty()||segment.back()==L'.'||segment.back()==L' ') return false;
        for(auto ch:segment) if(ch<32||std::wstring(L"<>:\"|?*").find(ch)!=std::wstring::npos) return false;
        auto base=segment.substr(0,segment.find(L'.'));
        for(auto& ch:base) ch=static_cast<wchar_t>(towupper(ch));
        if(base==L"CON"||base==L"PRN"||base==L"AUX"||base==L"NUL"||
            (base.size()==4&&(base.substr(0,3)==L"COM"||base.substr(0,3)==L"LPT")&&
            ((base[3]>=L'1'&&base[3]<=L'9')||base[3]==L'¹'||base[3]==L'²'||base[3]==L'³'))) return false;
        start=end+1;
    }
    return true;
}
inline std::wstring RendererCommand(std::wstring directory) {
    if(!directory.empty()&&directory.back()!=L'\\') directory+=L'\\';
    return L"\""+directory+L"app\\Nocturne.Renderer.exe\"";
}
}
