#include <windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <shellapi.h>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <memory>
#include "UpdateLock.hpp"

int main() {
    try {
        int count{}; auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) throw std::runtime_error("Command line unavailable");
        struct Args { wchar_t** value; ~Args() { LocalFree(value); } } owned{arguments};
        wchar_t executable[32768]{}; const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768) throw std::runtime_error("Executable path unavailable");
        const auto root = std::filesystem::path(executable).parent_path();
        auto startupLock = std::make_unique<UpdateLock>(root);
        if (std::filesystem::exists(root / L".rutool-lite-update")) {
            startupLock.reset();
            wchar_t temporary[32768]{}; if (!GetTempPathW(32768, temporary)) throw std::runtime_error("Temporary folder unavailable");
            const auto helper = std::filesystem::path(temporary) / (L"RuToolLiteRecover-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
            if (!CopyFileW((root / L"updater/RuToolUpdater.exe").c_str(), helper.c_str(), FALSE)) throw std::runtime_error("Cannot prepare update recovery");
            std::wstring command = L"\"" + helper.wstring() + L"\" --recover-only \"" + root.wstring() + L"\"";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
            if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process)) throw std::runtime_error("Cannot start update recovery");
            const auto waited = WaitForSingleObject(process.hProcess, 300000); DWORD code{}; GetExitCodeProcess(process.hProcess, &code); CloseHandle(process.hProcess); CloseHandle(process.hThread); DeleteFileW(helper.c_str());
            if (waited != WAIT_OBJECT_0) throw std::runtime_error("Recovery is still running; wait before starting RuTool again");
            if (code) throw std::runtime_error("Update recovery failed; keep the update journal for recovery");
            startupLock = std::make_unique<UpdateLock>(root);
        }
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS)) throw std::runtime_error("Secure DLL search unavailable");
        const auto app = root / L"app"; auto directory = AddDllDirectory(app.c_str());
        if (!directory) throw std::runtime_error("Cannot configure DLL directory");
        struct Directory { DLL_DIRECTORY_COOKIE value; ~Directory() { RemoveDllDirectory(value); } } search{directory};
        const auto dll = app / L"RuToolInstaller.dll";
        const auto module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) throw std::runtime_error("Cannot load native client");
        struct Module { HMODULE value; ~Module() { FreeLibrary(value); } } loaded{module};
        using Entry = int (*)(int, char**); const auto address = GetProcAddress(module, "rutoolMain");
        if (!address) throw std::runtime_error("Native entry point unavailable");
        static_assert(sizeof(Entry) == sizeof(address)); Entry entry{}; std::memcpy(&entry, &address, sizeof(entry));
        startupLock.reset();
        std::vector<std::string> utf8; std::vector<char*> argv;
        for (int i = 0; i < count; ++i) {
            const auto length = WideCharToMultiByte(CP_UTF8, 0, arguments[i], -1, nullptr, 0, nullptr, nullptr);
            if (length <= 0) throw std::runtime_error("Invalid command line");
            std::string text(static_cast<size_t>(length), '\0'); WideCharToMultiByte(CP_UTF8, 0, arguments[i], -1, text.data(), length, nullptr, nullptr); text.pop_back(); utf8.push_back(std::move(text));
        }
        for (auto& text : utf8) argv.push_back(text.data());
        argv.push_back(nullptr);
        return entry(count, argv.data());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "START: %s\n", error.what());
        MessageBoxW(nullptr, L"Не удалось загрузить RuTool. Распакуйте ZIP целиком и проверьте наличие папки app.", L"RuTool", MB_OK | MB_ICONERROR);
        return 2;
    }
}
