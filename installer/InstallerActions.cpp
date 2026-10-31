#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <string>

static constexpr const wchar_t* RunKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static std::wstring Property(MSIHANDLE install,const wchar_t* name) {
    DWORD count=0; wchar_t empty{};
    if(MsiGetPropertyW(install,name,&empty,&count)!=ERROR_MORE_DATA) return {};
    std::wstring value(count+1,L'\0');
    DWORD capacity=count+1;
    if(MsiGetPropertyW(install,name,value.data(),&capacity)!=ERROR_SUCCESS) return {};
    value.resize(capacity);return value;
}
static UINT Error(MSIHANDLE install,const wchar_t* text) {
    MSIHANDLE message=MsiCreateRecord(0);
    if(message) {MsiRecordSetStringW(message,0,text);MsiProcessMessage(install,INSTALLMESSAGE_ERROR,message);MsiCloseHandle(message);}
    return ERROR_INSTALL_FAILURE;
}
static bool Running(const wchar_t* name) {
    HANDLE mutex=OpenMutexW(SYNCHRONIZE,FALSE,name);
    if(mutex) {CloseHandle(mutex);return true;}
    return GetLastError()==ERROR_ACCESS_DENIED;
}
static std::wstring StartupValue() {
    HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,RunKey,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return {};
    wchar_t value[32768]{};DWORD bytes=sizeof(value),type=0;
    LONG result=RegQueryValueExW(key,L"Nocturne",nullptr,&type,(BYTE*)value,&bytes);RegCloseKey(key);
    if(result!=ERROR_SUCCESS||type!=REG_SZ||bytes<sizeof(wchar_t)||bytes>sizeof(value)) return {};
    value[32767]=0;return value;
}
extern "C" __declspec(dllexport) UINT __stdcall CheckEnvironment(MSIHANDLE install) {
    // MSI deliberately applies version-lie shims to DLL custom actions,
    // including RtlGetVersion. Read the native installation version instead.
    const wchar_t* versionKey=L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    DWORD major=0,bytes=sizeof(major);wchar_t build[32]{};
    LONG majorResult=RegGetValueW(HKEY_LOCAL_MACHINE,versionKey,L"CurrentMajorVersionNumber",RRF_RT_REG_DWORD,nullptr,&major,&bytes);
    bytes=sizeof(build);
    LONG buildResult=RegGetValueW(HKEY_LOCAL_MACHINE,versionKey,L"CurrentBuildNumber",RRF_RT_REG_SZ,nullptr,build,&bytes);
    if(majorResult!=ERROR_SUCCESS||buildResult!=ERROR_SUCCESS||major<10||wcstoul(build,nullptr,10)<19041)
        return Error(install,L"Penumbra 需要 Windows 10 2004（19041）或更新的系统。");
    if(Running(L"Local\\Nocturne.Renderer.v1")||Running(L"Local\\VignFinder.Settings.v3"))
        return Error(install,L"请先关闭 Penumbra 设置窗口，并从托盘退出暗角后台，然后重新安装或卸载。未应用的参数不会自动保存。");
    return ERROR_SUCCESS;
}
extern "C" __declspec(dllexport) UINT __stdcall PrepareStartupCleanup(MSIHANDLE install) {
    std::wstring directory=Property(install,L"INSTALLFOLDER");
    if(directory.empty()) return ERROR_SUCCESS;
    if(directory.back()!=L'\\') directory+=L'\\';
    std::wstring owned=L"\""+directory+L"app\\Nocturne.Renderer.exe\"";
    if(_wcsicmp(StartupValue().c_str(),owned.c_str())==0) {
        MsiSetPropertyW(install,L"RemoveOwnedStartup",owned.c_str());
        MsiSetPropertyW(install,L"RollbackOwnedStartup",owned.c_str());
    }
    return ERROR_SUCCESS;
}
extern "C" __declspec(dllexport) UINT __stdcall RemoveOwnedStartup(MSIHANDLE install) {
    std::wstring expected=Property(install,L"CustomActionData");
    if(!expected.empty()&&_wcsicmp(StartupValue().c_str(),expected.c_str())==0) {
        HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,RunKey,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS) {
            RegDeleteValueW(key,L"Nocturne");RegCloseKey(key);
        }
    }
    return ERROR_SUCCESS;
}
extern "C" __declspec(dllexport) UINT __stdcall RollbackOwnedStartup(MSIHANDLE install) {
    std::wstring expected=Property(install,L"CustomActionData");
    if(!expected.empty()&&StartupValue().empty()) {
        HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,RunKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS) {
            DWORD bytes=0;
            if(RegQueryValueExW(key,L"Nocturne",nullptr,nullptr,nullptr,&bytes)==ERROR_FILE_NOT_FOUND)
                RegSetValueExW(key,L"Nocturne",0,REG_SZ,(BYTE*)expected.c_str(),(DWORD)((expected.size()+1)*sizeof(wchar_t)));
            RegCloseKey(key);
        }
    }
    return ERROR_SUCCESS;
}
