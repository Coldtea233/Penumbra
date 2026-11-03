// Real completion resource and production handler; shell launch is intercepted.
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <cstdio>
static unsigned launched=0,ended=0;
static std::wstring launchFile,launchDirectory;
static BOOL WINAPI Execute(LPSHELLEXECUTEINFOW launch) {
    ++launched;launchFile=launch->lpFile;launchDirectory=launch->lpDirectory;return TRUE;
}
static BOOL WINAPI Finish(HWND,INT_PTR) {++ended;return TRUE;}
#define ShellExecuteExW Execute
#define EndDialog Finish
#define wWinMain UnusedSetupEntry
#include "../installer/Setup.cpp"
#undef wWinMain
#undef ShellExecuteExW
#undef EndDialog
int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool result,const char* message){++checks;if(!result){++failures;std::printf("FAIL %s\n",message);}};
    quiet=true;HWND foreground=GetForegroundWindow();
    for(bool run:{false,true}) for(UINT command:{WM_COMMAND,WM_CLOSE}) {
        InstallDialog state;state.directory=L"D:\\测试目录\\Penumbra Folder";
        launched=ended=0;
        HWND dialog=CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(203),nullptr,CompletionProcedure,reinterpret_cast<LPARAM>(&state));
        check(dialog!=nullptr,"real completion resource loads");
        check(IsDlgButtonChecked(dialog,1110)==BST_CHECKED,"run defaults checked");
        check(!GetDlgItem(dialog,1111)&&!GetDlgItem(dialog,1112),"both shortcut controls are removed");
        CheckDlgButton(dialog,1110,run?BST_CHECKED:BST_UNCHECKED);
        SendMessageW(dialog,command,command==WM_COMMAND?IDOK:0,0);
        check(state.options.run==run,"run choice captured");
        check(launched==(run?1u:0u)&&ended==1,"run opt-out honored and dialog finishes once");
        if(run) check(launchFile==state.directory+L"\\Penumbra.exe"&&launchDirectory==state.directory,"correct Unicode installed app and directory");
        DestroyWindow(dialog);
    }
    check(GetForegroundWindow()==foreground,"foreground preserved");
    std::printf("{\"CompletionChecks\":%u,\"Failures\":%u,\"ShortcutControlsRemoved\":true,\"InstallOrLaunchPerformed\":false}\n",checks,failures);
    return failures?1:0;
}
