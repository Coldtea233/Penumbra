#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

static int ShowError(const wchar_t* message, DWORD error = 0) {
    std::wstring text(message);
    if (error) text += L"\n\nWindows 错误码：" + std::to_wstring(error);
    MessageBoxW(nullptr, text.c_str(), L"Penumbra 半影", MB_OK | MB_ICONERROR);
    return 1;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size())
        return ShowError(L"无法确定程序所在目录。", GetLastError());
    std::wstring self(buffer.data(), length);
    auto separator = self.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return ShowError(L"无法确定程序所在目录。");
    std::wstring directory = self.substr(0, separator + 1) + L"app";
    std::wstring executable = directory + L"\\Penumbra.exe";
    DWORD attributes = GetFileAttributesW(executable.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return ShowError(L"找不到 app\\Penumbra.exe。\n请完整解压发行包，并保留启动程序旁的 app 文件夹。");
    std::wstring command = L"\"" + executable + L"\"";
    if (arguments && *arguments) command += L" " + std::wstring(arguments);
    if (command.size() >= 32767) return ShowError(L"启动参数过长。");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, directory.c_str(), &startup, &process))
        return ShowError(L"无法启动 Penumbra。请确认发行包已完整解压。", GetLastError());
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    // No resident launcher: the independent UI/backend own their lifetimes.
    return 0;
}
