#include "overlay/user_data.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>

namespace pet::win {
namespace {

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

}  // namespace

namespace {

std::wstring local_appdata_dir() {
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &base)) && base) {
        dir = std::wstring(base) + L"\\CyberDog";
        CoTaskMemFree(base);
    }
    return dir;
}

bool dir_writable(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);   // 已存在返回失败，无妨
    const std::wstring probe = dir + L"\\.write-test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// 2.0 起数据放在程序旁边的 CyberDog-data\，方便清理（作者要求）；程序目录不可写（比如放在 Program Files）时退回 %LOCALAPPDATA%\CyberDog。
// 第一次切过去时把老位置（改名前叫 Jdog）的存档和备忘搬过来。结果只算一次。
const std::wstring& data_dir_w() {
    static std::wstring dir;
    if (!dir.empty()) return dir;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exeDir = exe;
    const size_t slash = exeDir.find_last_of(L"\\/");
    exeDir = slash == std::wstring::npos ? L"." : exeDir.substr(0, slash);
    const std::wstring beside = exeDir + L"\\CyberDog-data";
    const std::wstring legacy = local_appdata_dir();
    if (dir_writable(beside)) dir = beside;
    else if (!legacy.empty() && dir_writable(legacy)) dir = legacy;
    else dir = L".";

    // 迁移：新目录里还没有存档时，从老位置搬：exe 旁的 Jdog-data\、%LOCALAPPDATA%\Jdog\、%LOCALAPPDATA%\CyberDog\。
    if (GetFileAttributesW((dir + L"\\cyberdog.ini").c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t* base = nullptr;
        std::wstring oldAppData;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
            oldAppData = std::wstring(base) + L"\\Jdog";
            CoTaskMemFree(base);
        }
        const std::wstring candidates[] = {exeDir + L"\\Jdog-data", oldAppData, legacy};
        for (const std::wstring& from : candidates) {
            if (from.empty() || from == dir) continue;
            const std::wstring oldIni = from + L"\\jdog.ini", newIni = from + L"\\cyberdog.ini";
            const bool hasOld = GetFileAttributesW(oldIni.c_str()) != INVALID_FILE_ATTRIBUTES;
            const bool hasNew = GetFileAttributesW(newIni.c_str()) != INVALID_FILE_ATTRIBUTES;
            if (!hasOld && !hasNew) continue;
            CopyFileW((hasNew ? newIni : oldIni).c_str(), (dir + L"\\cyberdog.ini").c_str(), TRUE);
            CopyFileW((from + L"\\plugin.memo.txt").c_str(), (dir + L"\\plugin.memo.txt").c_str(), TRUE);
            break;
        }
    }
    return dir;
}

}  // namespace

std::string data_dir() { return narrow(data_dir_w().c_str()); }

std::string save_path() { return narrow((data_dir_w() + L"\\cyberdog.ini").c_str()); }

std::string plugin_data_path(const char* pluginId) {
    const std::string base = save_path();
    const size_t slash = base.find_last_of("\\/");
    std::string id;
    for (const char* p = pluginId; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
        id += ok ? c : '_';
    }
    return base.substr(0, slash + 1) + id + ".txt";
}

bool read_text_file(const std::string& path, std::string& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, widen(path).c_str(), L"rb") != 0 || !f) return false;
    char buf[4096];
    out.clear();
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool write_text_file(const std::string& path, const std::string& text) {
    const std::wstring wpath = widen(path);
    const std::wstring tmp = wpath + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace pet::win
