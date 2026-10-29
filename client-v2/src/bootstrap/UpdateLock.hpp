#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cstdint>

// Shared by normal updates, crash recovery and launcher startup checks.
class UpdateLock final {
public:
    explicit UpdateLock(const std::filesystem::path& root) {
        auto key = std::filesystem::absolute(root).lexically_normal().make_preferred().wstring();
        CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
        std::uint64_t value = 14695981039346656037ULL;
        for (const auto c : key) { value ^= static_cast<std::uint16_t>(c); value *= 1099511628211ULL; }
        handle_ = CreateMutexW(nullptr, FALSE, (L"Local\\RuToolUpdate-v2-" + std::to_wstring(value)).c_str());
        if (!handle_) throw std::runtime_error("Update lock unavailable");
        const auto waited = WaitForSingleObject(handle_, 60000);
        if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
            CloseHandle(handle_); handle_ = nullptr;
            throw std::runtime_error("Another update or recovery is running; try again after it finishes");
        }
    }
    ~UpdateLock() { if (handle_) { ReleaseMutex(handle_); CloseHandle(handle_); } }
    UpdateLock(const UpdateLock&) = delete;
    UpdateLock& operator=(const UpdateLock&) = delete;
private:
    HANDLE handle_{};
};
