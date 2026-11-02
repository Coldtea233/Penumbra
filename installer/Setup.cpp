// Native EXE entry point. Its embedded Windows Installer payload retains
// transactional install/rollback, per-user registration and legacy MSI upgrades.
// No shell scripts, network downloads, elevation request or extra runtime.
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <msi.h>
#include <bcrypt.h>
#include <objbase.h>
#include <string>
#include "SetupConfig.h"

namespace {
bool quiet=false;
void Inform(const wchar_t* title,const std::wstring& message,bool error=false) {
    if(quiet) return;
    TaskDialog(nullptr,GetModuleHandleW(nullptr),L"Penumbra 安装程序",title,message.c_str(),TDCBF_OK_BUTTON,
        error?TD_ERROR_ICON:TD_INFORMATION_ICON,nullptr);
}
bool Busy(const wchar_t* name) {
    HANDLE mutex=OpenMutexW(SYNCHRONIZE,FALSE,name);
    if(mutex) { CloseHandle(mutex); return true; }
    return GetLastError()==ERROR_ACCESS_DENIED;
}
struct TempPayload {
    std::wstring directory,path;
    ~TempPayload() {
        // Remove only this instance's two explicit, privately-created paths.
        // Windows Installer keeps its own managed cache for later uninstall.
        if(!path.empty()) DeleteFileW(path.c_str());
        if(!directory.empty()) RemoveDirectoryW(directory.c_str());
    }
    bool Extract() {
        HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(201),RT_RCDATA);
        HGLOBAL handle=resource?LoadResource(nullptr,resource):nullptr;
        const auto* bytes=handle?static_cast<const BYTE*>(LockResource(handle)):nullptr;
        DWORD size=resource?SizeofResource(nullptr,resource):0;
        if(!bytes||!size) return false;
        BYTE hash[32]{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<BYTE*>(bytes),size,hash,sizeof(hash))<0) return false;
        std::wstring digest;
        constexpr wchar_t hex[]=L"0123456789ABCDEF";
        for(BYTE byte:hash) { digest+=hex[byte>>4]; digest+=hex[byte&15]; }
        if(digest!=PayloadSha256) return false;
        wchar_t temp[MAX_PATH+1]{};
        DWORD count=GetTempPathW(MAX_PATH,temp);
        if(!count||count>=MAX_PATH) return false;
        GUID guid{}; if(FAILED(CoCreateGuid(&guid))) return false;
        wchar_t unique[40]{}; StringFromGUID2(guid,unique,40);
        std::wstring candidate=std::wstring(temp)+L"PenumbraSetup-"+unique;
        if(!CreateDirectoryW(candidate.c_str(),nullptr)) return false;
        directory=candidate; path=directory+L"\\Penumbra.msi";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) return false;
        DWORD written=0; bool ok=WriteFile(file,bytes,size,&written,nullptr)&&written==size;
        CloseHandle(file); return ok;
    }
};
bool ConfirmInstall() {
    std::wstring title=L"安装 Penumbra "; title+=DisplayVersion;
    TASKDIALOG_BUTTON buttons[]={{100,L"安装"},{IDCANCEL,L"取消"}};
    TASKDIALOGCONFIG config{}; config.cbSize=sizeof(config);
    config.hInstance=GetModuleHandleW(nullptr); config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_USE_HICON_MAIN;
    HICON icon=static_cast<HICON>(LoadImageW(config.hInstance,MAKEINTRESOURCEW(101),IMAGE_ICON,48,48,LR_DEFAULTCOLOR));
    config.hMainIcon=icon; config.pszWindowTitle=L"Penumbra 安装程序"; config.pszMainInstruction=title.c_str();
    config.pszContent=L"为当前用户安装桌面暗角与模糊工具。\n\n安装位置：用户目录下的 AppData\\Local\\Programs\\Penumbra\n自动创建开始菜单入口，可在 Windows 设置中卸载。\n\n请先保存参数，关闭设置窗口并从托盘退出后台。升级或卸载不会删除个人配置；安装后不会自动运行或开启登录启动。";
    config.pButtons=buttons; config.cButtons=2; config.nDefaultButton=100;
    int clicked=IDCANCEL; HRESULT result=TaskDialogIndirect(&config,&clicked,nullptr,nullptr);
    if(icon) DestroyIcon(icon);
    return SUCCEEDED(result)&&clicked==100;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    int count=0; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args) return ERROR_INVALID_PARAMETER;
    std::wstring extractPath,logPath;
    bool valid=true;
    for(int i=1;i<count;i++) {
        if(!_wcsicmp(args[i],L"/quiet")) quiet=true;
        else if(!_wcsicmp(args[i],L"/extract")&&i+1<count) extractPath=args[++i];
        else if(!_wcsicmp(args[i],L"/log")&&i+1<count) logPath=args[++i];
        else valid=false;
    }
    LocalFree(args);
    if(!valid) { Inform(L"参数无效",L"支持 /quiet、/log <日志路径> 或 /extract <新的 MSI 文件路径>。",true); return ERROR_INVALID_PARAMETER; }
    if(extractPath.empty()) {
        if(!quiet&&!ConfirmInstall()) return ERROR_INSTALL_USEREXIT;
        if(Busy(L"Local\\Penumbra.Settings.Beta0.6")||Busy(L"Local\\VignFinder.Settings.v3")||Busy(L"Local\\Nocturne.Renderer.v1")) {
            Inform(L"Penumbra 正在运行",L"请先保存参数，关闭 Penumbra 设置窗口，并从托盘退出后台，再重新安装。程序不会强行关闭窗口或丢弃未保存参数。",true);
            return ERROR_INSTALL_ALREADY_RUNNING;
        }
    }
    TempPayload payload;
    if(!payload.Extract()) { Inform(L"无法准备安装文件",L"安装文件校验或临时目录写入失败，请重新下载完整安装程序。",true); return ERROR_INSTALL_PACKAGE_INVALID; }
    if(!extractPath.empty()) {
        if(!CopyFileW(payload.path.c_str(),extractPath.c_str(),TRUE)) return static_cast<int>(GetLastError());
        return ERROR_SUCCESS;
    }
    if(!logPath.empty()) {
        UINT result=MsiEnableLogW(INSTALLLOGMODE_VERBOSE|INSTALLLOGMODE_ERROR|INSTALLLOGMODE_WARNING|INSTALLLOGMODE_INFO,logPath.c_str(),0);
        if(result!=ERROR_SUCCESS) { Inform(L"日志文件不可写",L"请选择可写的日志文件路径。",true); return static_cast<int>(result); }
    }
    MsiSetInternalUI(quiet?INSTALLUILEVEL_NONE:static_cast<INSTALLUILEVEL>(INSTALLUILEVEL_BASIC|INSTALLUILEVEL_ENDDIALOG),nullptr);
    UINT result=MsiInstallProductW(payload.path.c_str(),L"REBOOT=ReallySuppress");
    if(result==ERROR_SUCCESS||result==ERROR_SUCCESS_REBOOT_REQUIRED) {
        Inform(L"安装完成",L"可从开始菜单打开 Penumbra。个人配置保持不变。\n\n如需卸载，请在 Windows 设置 → 应用中找到 Penumbra。");
    } else if(result!=ERROR_INSTALL_USEREXIT) {
        Inform(L"安装未完成",L"Windows Installer 返回错误 "+std::to_wstring(result)+L"。请确认旧程序已退出，且没有安装相同或更新版本。可用 /log 参数记录安装日志。",true);
    }
    return static_cast<int>(result);
}
