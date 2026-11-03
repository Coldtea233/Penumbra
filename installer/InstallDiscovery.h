#pragma once
#include <windows.h>
#include <msi.h>
#include <shlobj.h>
#include "install_logic.h"

namespace penumbra::setup {
inline constexpr wchar_t UpgradeCode[]=L"{0CB1971B-FE53-4E66-9699-C87884CB2D4E}";
struct InstalledProduct { std::wstring code,version,directory; };
inline std::wstring DefaultDirectory() {
    PWSTR path=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DONT_VERIFY,nullptr,&path))) return {};
    std::wstring result=std::wstring(path)+L"\\Programs\\Penumbra";
    CoTaskMemFree(path);return result;
}
inline std::wstring ProductInfo(const wchar_t* code,MSIINSTALLCONTEXT context,const wchar_t* property) {
    DWORD count=0;
    UINT result=MsiGetProductInfoExW(code,nullptr,context,property,nullptr,&count);
    if(result!=ERROR_MORE_DATA&&result!=ERROR_SUCCESS) return {};
    std::wstring text(count+1,L'\0');DWORD capacity=count+1;
    if(MsiGetProductInfoExW(code,nullptr,context,property,text.data(),&capacity)!=ERROR_SUCCESS) return {};
    text.resize(capacity);return text;
}
inline std::wstring LegacyDirectory() {
    wchar_t value[32768]{};DWORD size=sizeof(value);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Penumbra\\Installer\\Components",L"C_053F7A3C3440F2B8A0C00EA0",RRF_RT_REG_SZ,nullptr,value,&size)!=ERROR_SUCCESS) return {};
    std::wstring path=value;auto slash=path.find_last_of(L"\\/");
    if(slash==std::wstring::npos||_wcsicmp(path.substr(slash+1).c_str(),L"Penumbra.exe")) return {};
    return path.substr(0,slash);
}
inline InstalledProduct FindInstalled() {
    InstalledProduct best;
    wchar_t code[39]{};
    for(DWORD index=0;MsiEnumRelatedProductsW(UpgradeCode,0,index,code)==ERROR_SUCCESS;++index) {
        for(auto context:{MSIINSTALLCONTEXT_USERUNMANAGED,MSIINSTALLCONTEXT_USERMANAGED}) {
            auto version=ProductInfo(code,context,INSTALLPROPERTY_VERSIONSTRING);
            std::array<unsigned,3> parts{};
            if(!Version(version,parts)) continue; // Ignore machine/other-user products.
            if(!best.code.empty()&&CompareVersions(version,best.version)<=0) continue;
            auto directory=ProductInfo(code,context,INSTALLPROPERTY_INSTALLLOCATION);
            if(directory.empty()) directory=LegacyDirectory();
            if(directory.empty()) directory=DefaultDirectory();
            if(directory.size()>3&&directory.back()==L'\\') directory.pop_back();
            best={code,version,directory};
        }
    }
    return best;
}
inline bool ApplicationRunning() {
    for(const auto* name:{L"Local\\Penumbra.Settings",L"Local\\Penumbra.Settings.Beta0.6",L"Local\\VignFinder.Settings.v3",L"Local\\Nocturne.Renderer.v1"}) {
        HANDLE mutex=OpenMutexW(SYNCHRONIZE,FALSE,name);
        if(mutex) { CloseHandle(mutex);return true; }
        if(GetLastError()==ERROR_ACCESS_DENIED) return true;
    }
    return false;
}
}
