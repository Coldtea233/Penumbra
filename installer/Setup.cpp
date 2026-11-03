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
#include <shobjidl.h>
#include <string>
#include "SetupConfig.h"
#include "InstallDiscovery.h"

namespace {
using namespace penumbra::setup;
bool quiet=false;
void Inform(const wchar_t* title,const std::wstring& message,bool error=false) {
    if(quiet) return;
    TaskDialog(nullptr,GetModuleHandleW(nullptr),L"Penumbra 安装程序",title,message.c_str(),TDCBF_OK_BUTTON,
        error?TD_ERROR_ICON:TD_INFORMATION_ICON,nullptr);
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
struct InstallDialog {
    InstalledProduct installed;
    std::wstring directory;
    bool repair=false;
    CompletionOptions options;
};
void SetDialogIcon(HWND window) {
    auto icon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
    SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));
    SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon));
}
bool LaunchInstalledApplication(HWND owner,const InstallDialog& dialog) {
    if(!dialog.options.run) return true;
    std::wstring application=dialog.directory+L"\\Penumbra.exe";
    SHELLEXECUTEINFOW launch{};launch.cbSize=sizeof(launch);launch.fMask=SEE_MASK_FLAG_NO_UI;launch.hwnd=owner;
    launch.lpVerb=L"open";launch.lpFile=application.c_str();launch.lpDirectory=dialog.directory.c_str();launch.nShow=SW_SHOWNORMAL;
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool started=ShellExecuteExW(&launch)!=FALSE;
    if(SUCCEEDED(com)) CoUninitialize();
    if(started) return true;
    if(!quiet) MessageBoxW(owner,L"软件已安装，但未能自动打开。可以从安装目录的 Penumbra.exe 启动。",L"未能运行 Penumbra",MB_OK|MB_ICONWARNING);
    return false;
}
INT_PTR CALLBACK CompletionProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* dialog=reinterpret_cast<InstallDialog*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        dialog=reinterpret_cast<InstallDialog*>(lParam);SetWindowLongPtrW(window,DWLP_USER,lParam);
        SetDlgItemTextW(window,1101,(L"Penumbra "+std::wstring(DisplayVersion)+L" 已安装").c_str());
        SetDlgItemTextW(window,1102,(L"安装位置："+dialog->directory).c_str());
        CheckDlgButton(window,1110,dialog->options.run?BST_CHECKED:BST_UNCHECKED);
        SetDialogIcon(window);return TRUE;
    }
    if(dialog&&(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(wParam)==IDOK))) {
        dialog->options.run=IsDlgButtonChecked(window,1110)==BST_CHECKED;
        EnableWindow(GetDlgItem(window,IDOK),FALSE);
        LaunchInstalledApplication(window,*dialog);EndDialog(window,IDOK);
        return TRUE;
    }
    return FALSE;
}
bool ValidateDirectory(HWND owner,std::wstring& directory) {
    std::wstring normalized;
    if(!NormalizeDirectory(directory,normalized,InstallPathLimit)) {
        std::wstring message=L"请选择本地磁盘中的有效文件夹（如 D:\\Apps\\Penumbra）。\n不能使用磁盘根目录、网络路径或特殊字符；安装路径最多为 "
            +std::to_wstring(InstallPathLimit)+L" 个字符。";
        if(!quiet) MessageBoxW(owner,message.c_str(),L"请检查安装位置",MB_OK|MB_ICONWARNING);
        return false;
    }
    DWORD attributes=GetFileAttributesW(normalized.c_str());
    std::wstring root=normalized.substr(0,3);
    UINT drive=GetDriveTypeW(root.c_str());
    if((attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY))||
        (drive!=DRIVE_FIXED&&drive!=DRIVE_REMOVABLE&&drive!=DRIVE_RAMDISK)) {
        if(!quiet) MessageBoxW(owner,L"安装位置必须位于可用的本地磁盘，且不能与现有文件同名。",L"安装位置不可用",MB_OK|MB_ICONWARNING);
        return false;
    }
    directory=normalized;return true;
}
void BrowseDirectory(HWND owner) {
    IFileOpenDialog* picker=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))) return;
    DWORD options=0;picker->GetOptions(&options);
    picker->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
    picker->SetTitle(L"选择 Penumbra 的安装文件夹");picker->SetOkButtonLabel(L"选择此文件夹");
    wchar_t text[1024]{};GetDlgItemTextW(owner,1004,text,1024);
    IShellItem* folder=nullptr;
    if(SUCCEEDED(SHCreateItemFromParsingName(text,nullptr,IID_PPV_ARGS(&folder)))) {picker->SetFolder(folder);folder->Release();}
    if(SUCCEEDED(picker->Show(owner))&&SUCCEEDED(picker->GetResult(&folder))) {
        PWSTR path=nullptr;
        if(SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH,&path))) {SetDlgItemTextW(owner,1004,path);CoTaskMemFree(path);}
        folder->Release();
    }
    picker->Release();
}
INT_PTR CALLBACK DialogProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* dialog=reinterpret_cast<InstallDialog*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        dialog=reinterpret_cast<InstallDialog*>(lParam);
        SetWindowLongPtrW(window,DWLP_USER,lParam);
        std::wstring title=L"Penumbra ";title+=DisplayVersion;SetDlgItemTextW(window,1001,title.c_str());
        std::wstring description=dialog->installed.code.empty()?L"为当前用户安装":L"已安装 "+dialog->installed.version+L" · "+(dialog->repair?L"重新安装，保留配置":L"覆盖更新，保留配置");
        SetDlgItemTextW(window,1002,description.c_str());SetDlgItemTextW(window,1004,dialog->directory.c_str());
        SendDlgItemMessageW(window,1004,EM_SETLIMITTEXT,InstallPathLimit,0);
        SetDlgItemTextW(window,IDOK,dialog->repair?L"重新安装":(dialog->installed.code.empty()?L"安装":L"更新"));
        if(dialog->repair) {
            EnableWindow(GetDlgItem(window,1004),FALSE);EnableWindow(GetDlgItem(window,1005),FALSE);
            SetDlgItemTextW(window,1006,L"同版本重装沿用现有目录；迁移目录请使用更新版本。");
        }
        SetDialogIcon(window);
        return TRUE;
    }
    if(message==WM_CLOSE) {EndDialog(window,IDCANCEL);return TRUE;}
    if(message==WM_COMMAND&&dialog) {
        if(LOWORD(wParam)==IDCANCEL) {EndDialog(window,IDCANCEL);return TRUE;}
        if(LOWORD(wParam)==1005) {BrowseDirectory(window);return TRUE;}
        if(LOWORD(wParam)==IDOK) {
            wchar_t text[1024]{};GetDlgItemTextW(window,1004,text,1024);
            std::wstring directory=text;
            if(ValidateDirectory(window,directory)) {dialog->directory=directory;EndDialog(window,IDOK);}
            return TRUE;
        }
    }
    return FALSE;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    int count=0; LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args) return ERROR_INVALID_PARAMETER;
    std::wstring extractPath,logPath,requestedDirectory;
    CompletionOptions completion;
    bool valid=true;
    for(int i=1;i<count;i++) {
        if(!_wcsicmp(args[i],L"/quiet")) quiet=true;
        else if(!_wcsicmp(args[i],L"/extract")&&i+1<count) extractPath=args[++i];
        else if(!_wcsicmp(args[i],L"/log")&&i+1<count) logPath=args[++i];
        else if(!_wcsicmp(args[i],L"/dir")&&i+1<count) requestedDirectory=args[++i];
        else if(!_wcsicmp(args[i],L"/norun")) completion.run=false;
        else valid=false;
    }
    LocalFree(args);
    if(!valid) { Inform(L"参数无效",L"支持 /quiet、/dir <安装目录>、/log <日志路径>、/norun 或 /extract <新的 MSI 文件路径>。",true); return ERROR_INVALID_PARAMETER; }
    InstallDialog dialog{};
    dialog.options=completion;
    if(extractPath.empty()) {
        dialog.installed=FindInstalled();
        if(!dialog.installed.code.empty()&&CompareVersions(dialog.installed.version,ProductVersion)>0) {
            Inform(L"已安装更新版本",L"当前已安装 "+dialog.installed.version+L"，不能用此安装程序降级。个人配置未改动。",true);
            return ERROR_PRODUCT_VERSION;
        }
        dialog.repair=_wcsicmp(dialog.installed.code.c_str(),ProductCode)==0;
        if(!dialog.installed.code.empty()&&!dialog.repair&&CompareVersions(dialog.installed.version,ProductVersion)==0) {
            Inform(L"已安装相同版本",L"请使用原安装程序重装，或安装更新版本。",true);return ERROR_PRODUCT_VERSION;
        }
        dialog.directory=dialog.installed.code.empty()?DefaultDirectory():dialog.installed.directory;
        if(!requestedDirectory.empty()) {
            if(!ValidateDirectory(nullptr,requestedDirectory)) return ERROR_INVALID_PARAMETER;
            if(dialog.repair&&_wcsicmp(requestedDirectory.c_str(),dialog.directory.c_str())) {
                Inform(L"无法移动同版本安装",L"同版本重装必须沿用现有安装目录。",true);return ERROR_INVALID_PARAMETER;
            }
            dialog.directory=requestedDirectory;
        }
        if(!quiet) {
            HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            INT_PTR result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(202),nullptr,DialogProcedure,reinterpret_cast<LPARAM>(&dialog));
            if(SUCCEEDED(com)) CoUninitialize();
            if(result!=IDOK) return result==-1?ERROR_INSTALL_FAILURE:ERROR_INSTALL_USEREXIT;
        }
        if(!ValidateDirectory(nullptr,dialog.directory)) return ERROR_INVALID_PARAMETER;
        if(ApplicationRunning()) {
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
    MsiSetInternalUI(quiet?INSTALLUILEVEL_NONE:INSTALLUILEVEL_BASIC,nullptr);
    std::wstring properties=L"REBOOT=ReallySuppress INSTALLFOLDER=\""+dialog.directory+L"\" ADDLOCAL=Main";
    if(dialog.repair) properties+=L" REINSTALL=ALL REINSTALLMODE=vamus";
    UINT result=MsiInstallProductW(payload.path.c_str(),properties.c_str());
    if(result==ERROR_SUCCESS||result==ERROR_SUCCESS_REBOOT_REQUIRED) {
        // Quiet deployment never launches an interactive process. Normal setup
        // presents only the optional run checkbox after installation succeeds.
        if(!quiet) {
            INT_PTR finished=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(203),nullptr,CompletionProcedure,reinterpret_cast<LPARAM>(&dialog));
            if(finished==-1) Inform(L"安装完成",L"软件已安装。完成页无法打开，请从安装目录启动 Penumbra。个人配置保持不变。",true);
        }
    } else if(result!=ERROR_INSTALL_USEREXIT) {
        Inform(L"安装未完成",L"Windows Installer 返回错误 "+std::to_wstring(result)+L"。请确认旧程序已退出，且没有安装相同或更新版本。可用 /log 参数记录安装日志。",true);
    }
    return static_cast<int>(result);
}
