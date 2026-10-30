// Native bootstrap: remains usable when Qt or a runtime DLL cannot be loaded.
#include <cwchar>
#include <string>
#include <windows.h>
namespace {
std::wstring logFile;
void log(const std::wstring &message) {
    HANDLE file = CreateFileW(logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    int size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), int(message.size()), nullptr, 0,
                                   nullptr, nullptr);
    std::string text(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), int(message.size()), text.data(), size,
                        nullptr, nullptr);
    text += "\r\n";
    DWORD written;
    WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr);
    CloseHandle(file);
}
void failure(const std::wstring &message, bool quiet) {
    log(message);
    if (!quiet)
        MessageBoxW(nullptr,
                    (message + L"\n\nLogs: " + logFile +
                     L"\nTry 'BT Karaoke Studio - Safe Mode' from the Start menu.")
                        .c_str(),
                    L"BT Karaoke could not start", MB_OK | MB_ICONERROR);
}
} // namespace
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR args, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    wchar_t data[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", data, 32768);
    if (n == 0 || n >= 32768)
        GetTempPathW(32768, data);
    std::wstring root = data;
    for (const auto *part : {L"\\BTStudio", L"\\BTKaraoke", L"\\logs"}) {
        root += part;
        CreateDirectoryW(root.c_str(), nullptr);
    }
    logFile = root + L"\\launcher.log";
    log(L"BT Karaoke launcher 0.1.1 starting");
    bool quiet = wcsstr(args, L"--smoke-test") || wcsstr(args, L"--startup-test");
    wchar_t path[32768]{};
    n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768) {
        failure(L"Cannot resolve application path", quiet);
        return 1;
    }
    std::wstring dir = path;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    std::wstring child = dir + L"\\BTKaraokeApp.exe";
    if (GetFileAttributesW(child.c_str()) == INVALID_FILE_ATTRIBUTES) {
        failure(L"BTKaraokeApp.exe is missing. Reinstall BT Karaoke Studio.", quiet);
        return 2;
    }
    std::wstring command = L"\"" + child + L"\" " + args;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
    bool inherit = out && err && out != INVALID_HANDLE_VALUE && err != INVALID_HANDLE_VALUE;
    if (inherit) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = out;
        startup.hStdError = err;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, inherit, 0, nullptr,
                        dir.c_str(), &startup, &process)) {
        failure(L"CreateProcess failed, Windows error " + std::to_wstring(GetLastError()), quiet);
        return 3;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    log(L"Application exit code: " + std::to_wstring(code));
    if (code != 0) {
        wchar_t hex[32];
        wsprintfW(hex, L"0x%08X", code);
        std::wstring message = L"Application exited with " + std::wstring(hex) + L".";
        if (code == 0xC0000135)
            message +=
                L" A required DLL could not be loaded. Please reinstall the complete package.";
        else if (code == 0xC000001D)
            message +=
                L" This processor does not support an instruction required by the application.";
        failure(message, quiet);
    }
    return int(code);
}
