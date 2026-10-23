#include "winutil.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <algorithm>

namespace rdj::win {

namespace {
template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

std::vector<std::wstring> runOpen(void* hwnd, bool folders, bool multi, const COMDLG_FILTERSPEC* filters, UINT nf) {
    std::vector<std::wstring> out;
    Com<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folders) opts |= FOS_PICKFOLDERS;
    if (multi) opts |= FOS_ALLOWMULTISELECT;
    dlg->SetOptions(opts);
    if (filters && nf) dlg->SetFileTypes(nf, filters);
    if (FAILED(dlg->Show((HWND)hwnd))) return out;
    Com<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return out;
    DWORD n = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        Com<IShellItem> it;
        if (FAILED(items->GetItemAt(i, &it))) continue;
        PWSTR s = nullptr;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &s)) && s) { out.push_back(s); CoTaskMemFree(s); }
    }
    return out;
}
} // namespace

std::vector<std::wstring> pickAudioFiles(void* hwnd, bool multi) {
    const COMDLG_FILTERSPEC f[] = {{L"Audio (MP3, WAV, FLAC, OGG)", L"*.mp3;*.wav;*.flac;*.ogg"}, {L"Todos los archivos", L"*.*"}};
    return runOpen(hwnd, false, multi, f, 2);
}

std::wstring pickImage(void* hwnd) {
    const COMDLG_FILTERSPEC f[] = {{L"Imágenes (JPEG, PNG, WebP)", L"*.jpg;*.jpeg;*.png;*.webp;*.bmp"}};
    auto r = runOpen(hwnd, false, false, f, 1);
    return r.empty() ? L"" : r[0];
}

std::wstring pickFolder(void* hwnd) {
    auto r = runOpen(hwnd, true, false, nullptr, 0);
    return r.empty() ? L"" : r[0];
}

std::wstring pickFileToOpen(void* hwnd, const wchar_t* filterName, const wchar_t* filterSpec) {
    const COMDLG_FILTERSPEC f[] = {{filterName, filterSpec}};
    auto r = runOpen(hwnd, false, false, f, 1);
    return r.empty() ? L"" : r[0];
}

std::wstring pickPlaylistToOpen(void* hwnd) {
    const COMDLG_FILTERSPEC f[] = {{L"Listas M3U", L"*.m3u;*.m3u8"}};
    auto r = runOpen(hwnd, false, false, f, 1);
    return r.empty() ? L"" : r[0];
}

std::wstring pickSaveFile(void* hwnd, const std::wstring& defaultName, const wchar_t* filterName, const wchar_t* filterSpec, const wchar_t* defExt) {
    Com<IFileSaveDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return L"";
    const COMDLG_FILTERSPEC f[] = {{filterName, filterSpec}};
    dlg->SetFileTypes(1, f);
    dlg->SetFileName(defaultName.c_str());
    dlg->SetDefaultExtension(defExt);
    if (FAILED(dlg->Show((HWND)hwnd))) return L"";
    Com<IShellItem> it;
    if (FAILED(dlg->GetResult(&it))) return L"";
    PWSTR s = nullptr;
    std::wstring out;
    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &s)) && s) { out = s; CoTaskMemFree(s); }
    return out;
}

void revealInExplorer(const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (pidl) { SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); ILFree(pidl); }
}

void openFolder(const std::wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

bool confirm(void* hwnd, const std::wstring& text, const std::wstring& title) {
    return MessageBoxW((HWND)hwnd, text.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES;
}

std::vector<DriveInfo> drives() {
    std::vector<DriveInfo> out;
    const DWORD mask = GetLogicalDrives();
    // Evita el cuadro "No hay disco en la unidad" con lectores vacíos.
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        DriveInfo d;
        d.root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
        const UINT t = GetDriveTypeW(d.root.c_str());
        if (t == DRIVE_NO_ROOT_DIR || t == DRIVE_UNKNOWN) continue;
        d.type = t == DRIVE_REMOVABLE ? 1 : t == DRIVE_REMOTE ? 2 : t == DRIVE_CDROM ? 3 : 0;
        wchar_t name[MAX_PATH] = L"";
        if (d.type != 2 && d.type != 3) GetVolumeInformationW(d.root.c_str(), name, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
        std::wstring label = name[0] ? name : (d.type == 1 ? L"Unidad extraíble" : d.type == 2 ? L"Unidad de red" : d.type == 3 ? L"Unidad de CD/DVD" : L"Disco local");
        label += L" (" + d.root.substr(0, 2) + L")";
        const int n = WideCharToMultiByte(CP_UTF8, 0, label.c_str(), (int)label.size(), nullptr, 0, nullptr, nullptr);
        d.label.resize(n);
        WideCharToMultiByte(CP_UTF8, 0, label.c_str(), (int)label.size(), d.label.data(), n, nullptr, nullptr);
        out.push_back(d);
    }
    SetErrorMode(oldMode);
    return out;
}

std::wstring userFolder(int which) {
    const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_Music, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Videos};
    if (which < 0 || which > 4) return L"";
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(*ids[which], 0, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return folderExists(out) ? out : L"";
}

bool folderExists(const std::wstring& dir) {
    if (dir.empty()) return false;
    const DWORD a = GetFileAttributesW(dir.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<std::wstring> subfolders(const std::wstring& dir) {
    std::vector<std::wstring> out;
    const std::wstring base = (!dir.empty() && dir.back() == L'\\') ? dir : dir + L"\\";
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L".." || n[0] == L'$') continue;
            out.push_back(base + n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetErrorMode(oldMode);
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return out;
}

} // namespace rdj::win
