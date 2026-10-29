#pragma once
#include <windows.h>
#include <shellapi.h>
#include "PortableCrypto.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <array>
#include <set>
#include <algorithm>
#include <cstdio>
#include "UpdateLock.hpp"

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
fs::path utf8(const std::string& value) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size())); }
void safe(const fs::path& path) {
    auto current = fs::absolute(path).lexically_normal();
    while (!current.empty()) {
        const auto attributes = GetFileAttributesW(current.c_str());
        require(attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Update path contains a reparse point");
        const auto parent = current.parent_path(); if (parent == current) break; current = parent;
    }
}
fs::path resolve(const fs::path& root, const std::string& name) {
    require(!name.empty() && name.size() <= 220 && name.back() != '/', "Invalid update file");
    require(name == "RuToolInstaller.exe" || name == "README.md" || name == "Installer.release.json" || name.starts_with("app/") || name.starts_with("updater/"), "Unexpected update file");
    for (const auto c : name) require(static_cast<unsigned char>(c) >= 32 && c != '\\' && c != ':' && c != '<' && c != '>' && c != '"' && c != '|' && c != '?' && c != '*', "Invalid update path");
    const auto relative = utf8(name);
    for (const auto& part : relative) {
        const auto text = part.wstring();
        require(!text.empty() && text != L"." && text != L".." && text.back() != L'.' && text.back() != L' ', "Unsafe update path");
        auto stem = part.stem().wstring(); CharUpperBuffW(stem.data(), static_cast<DWORD>(stem.size()));
        require(stem != L"CON" && stem != L"PRN" && stem != L"AUX" && stem != L"NUL" && stem != L"CLOCK$" && !(stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3] >= L'0' && stem[3] <= L'9'), "Reserved update path");
    }
    auto path = fs::absolute(root).lexically_normal() / relative; path.make_preferred(); safe(path); return path;
}
Json readJson(const fs::path& path) {
    safe(path); require(fs::file_size(path) <= 1024 * 1024, "Update metadata too large");
    std::ifstream file(path, std::ios::binary); require(file.good(), "Update metadata unavailable"); return Json::parse(file);
}
void writeJson(const fs::path& path, const Json& object) {
    safe(path); const auto temporary = fs::path(path.wstring() + L".next"); safe(temporary);
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << object.dump(); file.flush(); require(file.good(), "Cannot write update journal"); }
    const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "Cannot open update journal for flush");
    const bool flushed = FlushFileBuffers(handle) != 0; CloseHandle(handle);
    require(flushed, "Cannot flush update journal");
    require(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0, "Cannot commit update journal");
}
std::string hash(const fs::path& path) {
    safe(path); std::ifstream file(path, std::ios::binary); require(file.good(), "Update file unavailable");
    rutool::crypto::Sha256 algorithm;
    std::array<unsigned char, 65536> buffer{};
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()); const auto count = file.gcount();
        if (count) require(algorithm.update({buffer.data(), static_cast<std::size_t>(count)}), "Hash failed");
    }
    require(file.eof(), "Update file read failed");
    std::array<unsigned char, 32> digest{}; require(algorithm.finish(digest), "Hash failed");
    constexpr char hex[] = "0123456789abcdef"; std::string result;
    for (const auto b : digest) { result += hex[b >> 4]; result += hex[b & 15]; } return result;
}
void copyFile(const fs::path& source, const fs::path& target) {
    safe(source); safe(target); fs::create_directories(target.parent_path());
    const auto temporary = fs::path(target.wstring() + L".lite-next"); safe(temporary);
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing);
    const auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "Cannot open update file for flush");
    const bool flushed = FlushFileBuffers(handle) != 0; CloseHandle(handle);
    require(flushed, "Cannot flush update file");
    require(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0, "Cannot replace update file");
}
bool matches(const fs::path& path, const Json& item, const char* bytes, const char* digest) {
    return fs::is_regular_file(path) && item.contains(bytes) && item.contains(digest) &&
        item.at(bytes).is_number_unsigned() && item.at(digest).is_string() &&
        fs::file_size(path) == item.at(bytes).get<std::uint64_t>() && hash(path) == item.at(digest).get<std::string>();
}
void validateJournal(const fs::path& root, const Json& metadata) {
    require(metadata.is_object() && metadata.contains("Files") && metadata.at("Files").is_array() && metadata.at("Files").size() <= 901, "Invalid update journal; snapshot preserved");
    std::set<std::wstring> names;
    for (const auto& item : metadata.at("Files")) {
        const auto name = item.at("Path").get<std::string>(); resolve(root, name);
        auto folded = utf8(name).wstring(); CharLowerBuffW(folded.data(), static_cast<DWORD>(folded.size()));
        require(names.insert(folded).second && item.at("Existed").is_boolean(), "Invalid or duplicate update journal entry");
    }
}
void clean(const fs::path& journal, const Json& metadata) {
    validateJournal(journal.parent_path(), metadata);
    std::set<fs::path> allowed{journal / L"journal.json", journal / L"journal.json.next"};
    for (const auto& item : metadata.at("Files")) {
        const auto snapshot = resolve(journal / L"previous", item.at("Path").get<std::string>());
        allowed.insert(snapshot); allowed.insert(fs::path(snapshot.wstring() + L".lite-next"));
    }
    // Validate the complete tree before deleting anything, including leftover snapshot copies.
    std::vector<fs::path> folders;
    for (const auto& entry : fs::recursive_directory_iterator(journal)) {
        safe(entry.path());
        if (entry.is_directory()) {
            const auto prefix = entry.path().wstring() + L"\\";
            require(std::ranges::any_of(allowed, [&](const auto& path) { return path.wstring().starts_with(prefix); }), "Unknown directory in update journal; preserved");
            folders.push_back(entry.path());
        } else require(entry.is_regular_file() && allowed.contains(entry.path()), "Unknown file in update journal; preserved");
    }
    for (const auto& item : metadata.at("Files")) {
        const auto name = item.at("Path").get<std::string>(); const auto snapshot = resolve(journal / L"previous", name);
        fs::remove(snapshot); fs::remove(fs::path(snapshot.wstring() + L".lite-next"));
        fs::remove(fs::path(resolve(journal.parent_path(), name).wstring() + L".lite-next"));
    }
    std::ranges::sort(folders, [](const auto& a, const auto& b) { return a.native().size() > b.native().size(); });
    for (const auto& folder : folders) fs::remove(folder);
    fs::remove(journal / L"journal.json.next"); fs::remove(journal / L"journal.json"); fs::remove(journal);
}
void recoverUnlocked(const fs::path& root) {
    const auto journal = root / L".rutool-lite-update"; safe(journal);
    if (!fs::exists(journal)) return;
    if (!fs::exists(journal / L"journal.json")) {
        for (const auto& entry : fs::directory_iterator(journal)) {
            safe(entry.path()); require(entry.is_regular_file() && entry.path().filename() == L"journal.json.next", "Incomplete journal contains unknown files; preserved");
        }
        fs::remove(journal / L"journal.json.next"); fs::remove(journal); return;
    }
    auto metadata = readJson(journal / L"journal.json"); validateJournal(root, metadata);
    const auto phase = metadata.at("State").get<std::string>();
    require(phase == "PREPARING" || phase == "COMMITTING" || phase == "COMMITTED" || phase == "ROLLED_BACK", "Unknown update journal state; snapshot preserved");
    if (phase == "COMMITTING") {
        // A legacy journal has no trusted snapshot digest. Never guess or promote its contents.
        require(metadata.value("FormatVersion", 0) == 2, "Legacy recovery has no snapshot hashes; preserve the journal and restore from a verified archive");
        for (const auto& item : metadata.at("Files")) {
            const auto name = item.at("Path").get<std::string>();
            if (item.at("Existed").get<bool>()) require(matches(resolve(journal / L"previous", name), item, "BeforeBytes", "BeforeSha256"), "Previous update snapshot is damaged; no files restored");
            const auto target = resolve(root, name);
            require(!fs::exists(target) || matches(target, item, "AfterBytes", "AfterSha256") || (item.at("Existed").get<bool>() && matches(target, item, "BeforeBytes", "BeforeSha256")), "Application changed after interrupted update; snapshot preserved");
        }
        for (const auto& item : metadata.at("Files")) {
            const auto name = item.at("Path").get<std::string>();
            const auto target = resolve(root, name);
            if (item.at("Existed").get<bool>()) copyFile(resolve(journal / L"previous", name), target); else fs::remove(target);
            require(item.at("Existed").get<bool>() ? matches(target, item, "BeforeBytes", "BeforeSha256") : !fs::exists(target), "Restored update file verification failed");
        }
        metadata["State"] = "ROLLED_BACK"; writeJson(journal / L"journal.json", metadata);
    }
    clean(journal, metadata);
}
void recover(const fs::path& root) { const auto normalized = fs::absolute(root).lexically_normal().make_preferred(); UpdateLock lock(normalized); recoverUnlocked(normalized); }
template<class AfterReplace>
void apply(const fs::path& root, const fs::path& stage, const Json& files, AfterReplace afterReplace) {
    require(fs::absolute(root).lexically_normal() != fs::absolute(stage).lexically_normal(), "Stage equals application root");
    require(files.is_array() && !files.empty() && files.size() <= 901, "Invalid update file list");
    UpdateLock lock(root); recoverUnlocked(root); std::set<std::wstring> names; Json entries = Json::array();
    for (const auto& item : files) {
        const auto name = item.at("Path").get<std::string>();
        const auto source = resolve(stage, name), target = resolve(root, name);
        auto folded = utf8(name).wstring(); CharLowerBuffW(folded.data(), static_cast<DWORD>(folded.size())); require(names.insert(folded).second, "Duplicate update file");
        require(fs::file_size(source) == item.at("Bytes").get<std::uint64_t>() && hash(source) == item.at("Sha256").get<std::string>(), "Downloaded update file is damaged");
        if (name == "app/config/installer-keys.json" && fs::exists(target)) continue;
        Json entry{{"Path", name}, {"Existed", fs::exists(target)}, {"AfterBytes", item.at("Bytes")}, {"AfterSha256", item.at("Sha256")}};
        if (fs::exists(target)) { entry["BeforeBytes"] = fs::file_size(target); entry["BeforeSha256"] = hash(target); }
        entries.push_back(std::move(entry));
    }
    std::uint64_t required = 64ULL * 1024 * 1024, largest{};
    for (const auto& item : entries) {
        if (item.at("Existed").get<bool>()) required += item.at("BeforeBytes").get<std::uint64_t>();
        const auto bytes = item.at("AfterBytes").get<std::uint64_t>(); if (bytes > largest) largest = bytes;
    }
    ULARGE_INTEGER available{};
    require(GetDiskFreeSpaceExW(root.c_str(), &available, nullptr, nullptr) != 0 && available.QuadPart >= required + largest, "Insufficient disk space for update snapshot and replacement");
    const auto journal = root / L".rutool-lite-update"; fs::create_directory(journal);
    Json metadata{{"FormatVersion", 2}, {"State", "PREPARING"}, {"Files", entries}}; writeJson(journal / L"journal.json", metadata);
    for (const auto& item : entries) if (item.at("Existed").get<bool>()) {
        const auto snapshot = resolve(journal / L"previous", item.at("Path")); copyFile(resolve(root, item.at("Path")), snapshot);
        require(matches(snapshot, item, "BeforeBytes", "BeforeSha256"), "Previous file changed while preparing update");
    }
    metadata["State"] = "COMMITTING"; writeJson(journal / L"journal.json", metadata);
    try {
        for (const auto& item : entries) {
            const auto name = item.at("Path").get<std::string>(); const auto source = resolve(stage, name), target = resolve(root, name);
            require(matches(source, item, "AfterBytes", "AfterSha256"), "Staged file changed before replacement");
            require(item.at("Existed").get<bool>() ? matches(target, item, "BeforeBytes", "BeforeSha256") : !fs::exists(target), "Application changed before replacement");
            copyFile(source, target); require(matches(target, item, "AfterBytes", "AfterSha256"), "Updated file verification failed");
            afterReplace();
        }
        metadata["State"] = "COMMITTED"; writeJson(journal / L"journal.json", metadata);
    } catch (...) { recoverUnlocked(root); throw; }
    clean(journal, metadata);
}
void waitForParent(const Json& plan, const fs::path& root) {
    const auto process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, plan.at("ProcessId").get<DWORD>());
    if (!process) { require(GetLastError() == ERROR_INVALID_PARAMETER, "Parent process inaccessible"); return; }
    struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{process};
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(process, &created, &exited, &kernel, &user) != 0, "Parent identity unavailable");
    require(((static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime) + 504911232000000000ULL == plan.at("ProcessStartedUtcTicks").get<std::uint64_t>(), "Parent process changed");
    wchar_t image[32768]{}; DWORD length = 32768;
    require(QueryFullProcessImageNameW(process, 0, image, &length) && CompareStringOrdinal(image, static_cast<int>(length), (root / L"RuToolInstaller.exe").c_str(), -1, TRUE) == CSTR_EQUAL, "Parent belongs to another installation");
    require(WaitForSingleObject(process, 60000) == WAIT_OBJECT_0, "Close Installer before updating");
}
void resultLog(const std::string& message) noexcept {
    try {
        wchar_t local[32768]{}; const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (!size || size >= 32768) return;
        const auto directory = fs::path(local) / L"RuToolInstaller"; safe(directory); fs::create_directories(directory);
        const auto path = directory / L"native-update-result.txt"; safe(path);
        std::ofstream file(path, std::ios::binary | std::ios::trunc); file << message << '\n';
    } catch (...) { }
}
}
