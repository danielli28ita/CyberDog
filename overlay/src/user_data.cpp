#include "overlay/user_data.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>
#include <vector>

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

std::wstring g_dataDir;

bool file_exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool dir_exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool dir_writable(const std::wstring& dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring probe = dir + L"\\.write-test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

std::wstring exe_dir() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

std::wstring local_appdata_cyberdog() {
    wchar_t* base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &base)) && base) {
        dir = std::wstring(base) + L"\\CyberDog";
        CoTaskMemFree(base);
    }
    return dir;
}

bool has_dog_save(const std::wstring& dir) {
    return file_exists(dir + L"\\cyberdog.ini") || file_exists(dir + L"\\jdog.ini");
}

// 把 fromDir 里的狗数据拷到 toDir（覆盖同名）。至少要有 cyberdog.ini 或 jdog.ini。
bool copy_dog_files(const std::wstring& fromDir, const std::wstring& toDir) {
    if (!has_dog_save(fromDir)) return false;
    CreateDirectoryW(toDir.c_str(), nullptr);
    const std::wstring srcIni = file_exists(fromDir + L"\\cyberdog.ini")
                                    ? fromDir + L"\\cyberdog.ini"
                                    : fromDir + L"\\jdog.ini";
    if (!CopyFileW(srcIni.c_str(), (toDir + L"\\cyberdog.ini").c_str(), FALSE)) return false;

    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = fromDir + L"\\plugin.*.txt";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            CopyFileW((fromDir + L"\\" + fd.cFileName).c_str(),
                      (toDir + L"\\" + fd.cFileName).c_str(), FALSE);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // 旧名备忘录
    if (file_exists(fromDir + L"\\plugin.memo.txt")) {
        CopyFileW((fromDir + L"\\plugin.memo.txt").c_str(),
                  (toDir + L"\\plugin.memo.txt").c_str(), FALSE);
    }
    return true;
}

bool ensure_ini_name(const std::wstring& dir) {
    if (file_exists(dir + L"\\cyberdog.ini")) return true;
    if (file_exists(dir + L"\\jdog.ini")) {
        return CopyFileW((dir + L"\\jdog.ini").c_str(), (dir + L"\\cyberdog.ini").c_str(), FALSE) != 0;
    }
    return false;
}

// 选一个可写的数据根：优先程序旁 CyberDog-data，否则 %LOCALAPPDATA%\CyberDog。
bool pick_writable_root(std::wstring& out, std::wstring* note) {
    const std::wstring beside = exe_dir() + L"\\CyberDog-data";
    if (dir_writable(beside)) {
        out = beside;
        return true;
    }
    const std::wstring app = local_appdata_cyberdog();
    if (!app.empty() && dir_writable(app)) {
        out = app;
        if (note) {
            *note = L"程序目录不可写，数据将放在：\n" + app;
        }
        return true;
    }
    return false;
}

bool pick_import_folder(std::wstring& out) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = SUCCEEDED(hrCo);
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) {
        if (needUninit) CoUninitialize();
        return false;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"选择含有 cyberdog.ini（或 jdog.ini）的文件夹");
    hr = dlg->Show(nullptr);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                out = path;
                ok = true;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    if (needUninit) CoUninitialize();
    return ok;
}

std::wstring hint_old_locations() {
    std::wstring tip;
    const std::wstring candidates[] = {
        exe_dir() + L"\\Jdog-data",
        local_appdata_cyberdog(),
        [&]() {
            wchar_t* base = nullptr;
            std::wstring d;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) && base) {
                d = std::wstring(base) + L"\\Jdog";
                CoTaskMemFree(base);
            }
            return d;
        }(),
    };
    for (const auto& c : candidates) {
        if (!c.empty() && has_dog_save(c)) {
            tip += L"\n· ";
            tip += c;
        }
    }
    if (!tip.empty()) tip = L"\n\n检测到这些位置可能有旧数据（选「否」导入）：" + tip;
    return tip;
}

bool create_new_dog(std::wstring& outDir) {
    std::wstring note;
    if (!pick_writable_root(outDir, &note)) {
        MessageBoxW(nullptr, L"找不到可写的数据目录，无法新建。", L"CyberDog", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return false;
    }
    if (!note.empty()) {
        MessageBoxW(nullptr, note.c_str(), L"CyberDog", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
    CreateDirectoryW(outDir.c_str(), nullptr);
    // 空目录即可；主程序会写出第一份 cyberdog.ini。
    return true;
}

bool import_dog(std::wstring& outDir) {
    std::wstring from;
    if (!pick_import_folder(from)) return false;
    if (!has_dog_save(from)) {
        MessageBoxW(nullptr,
                    L"所选文件夹里没有 cyberdog.ini 或 jdog.ini。",
                    L"CyberDog", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return false;
    }
    std::wstring note;
    if (!pick_writable_root(outDir, &note)) {
        MessageBoxW(nullptr, L"找不到可写的数据目录，无法导入。", L"CyberDog", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return false;
    }
    if (!note.empty()) {
        MessageBoxW(nullptr, note.c_str(), L"CyberDog", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    }
    if (!copy_dog_files(from, outDir)) {
        MessageBoxW(nullptr, L"复制存档失败。", L"CyberDog", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return false;
    }
    return true;
}

}  // namespace

bool ensure_data_ready() {
    if (!g_dataDir.empty()) return true;

    wchar_t envBuf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"PET_DATA_DIR", envBuf, MAX_PATH) > 0) {
        CreateDirectoryW(envBuf, nullptr);
        if (!dir_writable(envBuf)) return false;
        g_dataDir = envBuf;
        ensure_ini_name(g_dataDir);
        return true;
    }

    const std::wstring beside = exe_dir() + L"\\CyberDog-data";
    if (has_dog_save(beside)) {
        g_dataDir = beside;
        ensure_ini_name(g_dataDir);
        return true;
    }

    if (GetEnvironmentVariableW(L"PET_NEW_DOG", envBuf, MAX_PATH) > 0) {
        return create_new_dog(g_dataDir);
    }
    if (GetEnvironmentVariableW(L"PET_IMPORT_DIR", envBuf, MAX_PATH) > 0) {
        std::wstring root;
        std::wstring note;
        if (!pick_writable_root(root, &note)) return false;
        if (!copy_dog_files(envBuf, root)) return false;
        g_dataDir = root;
        return true;
    }

    // 无控制台、自动化环境：没有同目录存档就失败，避免弹窗卡住。
    if (GetEnvironmentVariableW(L"PET_HEADLESS", envBuf, MAX_PATH) > 0) {
        std::printf("  [FAIL] 同目录无 CyberDog-data 存档，且设置了 PET_HEADLESS。\n");
        return false;
    }

    const std::wstring body =
        L"程序同目录下没有找到 CyberDog-data\\cyberdog.ini。\n\n"
        L"是(Y) = 新建一条狗\n"
        L"否(N) = 从其他文件夹导入已有数据\n"
        L"取消 = 退出" +
        hint_old_locations();

    for (;;) {
        const int r = MessageBoxW(nullptr, body.c_str(), L"CyberDog",
                                  MB_YESNOCANCEL | MB_ICONQUESTION | MB_TOPMOST);
        if (r == IDCANCEL) return false;
        if (r == IDYES) {
            if (create_new_dog(g_dataDir)) return true;
            continue;
        }
        // IDNO = 导入
        if (import_dog(g_dataDir)) return true;
        // 导入取消或失败则再问一次
    }
}

std::string data_dir() {
    if (g_dataDir.empty()) {
        // 未走 ensure 时尽量不崩：与旧行为接近，但不再静默迁移。
        const std::wstring beside = exe_dir() + L"\\CyberDog-data";
        if (has_dog_save(beside) || dir_writable(beside)) g_dataDir = beside;
        else g_dataDir = local_appdata_cyberdog();
        if (g_dataDir.empty()) g_dataDir = L".";
        ensure_ini_name(g_dataDir);
    }
    return narrow(g_dataDir.c_str());
}

std::string save_path() { return data_dir() + "\\cyberdog.ini"; }

std::string plugin_data_path(const char* pluginId) {
    const std::string base = save_path();
    const size_t slash = base.find_last_of("\\/");
    std::string id;
    for (const char* p = pluginId; *p; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_';
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
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace pet::win
