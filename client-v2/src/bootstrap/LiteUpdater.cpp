#include "LiteUpdateCore.hpp"

int main() {
    try {
        int count{}; auto* args = CommandLineToArgvW(GetCommandLineW(), &count); require(args != nullptr, "Invalid arguments");
        struct Cleanup { wchar_t** value; ~Cleanup() { LocalFree(value); } } cleanup{args};
        if (count == 3 && std::wstring(args[1]) == L"--recover-only") { recover(fs::absolute(args[2])); return 0; }
        require(count == 3 && std::wstring(args[1]) == L"--plan", "Expected update plan");
        const auto plan = readJson(args[2]); auto root = fs::absolute(utf8(plan.at("Root"))).lexically_normal(); root.make_preferred();
        waitForParent(plan, root);
        const auto mutex = CreateMutexW(nullptr, TRUE, (L"Local\\RuToolLiteUpdate-" + std::to_wstring(std::hash<std::wstring>{}(root.wstring()))).c_str());
        require(mutex != nullptr, "Update lock unavailable"); struct Mutex { HANDLE h; ~Mutex() { ReleaseMutex(h); CloseHandle(h); } } locked{mutex};
        require(GetLastError() != ERROR_ALREADY_EXISTS, "Another update is running");
        HANDLE lease = INVALID_HANDLE_VALUE;
        {
            const auto cache = fs::path(args[2]).parent_path(); safe(cache);
            lease = CreateFileW((cache / L".lease").c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            require(lease != INVALID_HANDLE_VALUE, "Update cache is busy");
        }
        struct Lease { HANDLE h; ~Lease() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } } cacheLease{lease};
        apply(root, utf8(plan.at("Stage")), plan.at("Files"), [] {});
        resultLog("OK: Lightweight update committed.");
        if (!plan.value("Restart", true)) return 0;
        const auto program = root / L"RuToolInstaller.exe"; std::wstring command = L"\"" + program.wstring() + L"\"";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
        require(CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &child) != 0, "Cannot restart Installer");
        CloseHandle(child.hProcess); CloseHandle(child.hThread); return 0;
    } catch (const std::exception& error) { resultLog(std::string("ERROR: ") + error.what()); std::fprintf(stderr, "UPDATE: %s\n", error.what()); return 1; }
}
