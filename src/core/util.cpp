#include "util.h"
#include <windows.h>
#include <shlobj.h>
#include <cmath>
#include <cstdio>
#include <cwctype>

namespace rdj {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::wstring knownFolder(REFKNOWNFOLDERID id, const wchar_t* sub) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
    if (p) CoTaskMemFree(p);
    if (out.empty()) out = L".";
    out += L"\\";
    out += sub;
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}

std::wstring appDataDir() {
    // RDJ_DATA_DIR permite aislar los datos (pruebas automáticas).
    wchar_t over[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"RDJ_DATA_DIR", over, MAX_PATH);
    if (n > 0 && n < MAX_PATH) { CreateDirectoryW(over, nullptr); return over; }
    return knownFolder(FOLDERID_RoamingAppData, L"RamiroDJ");   // se mantiene al cambiar de nombre (0.10.6): misma biblioteca y ajustes
}
std::wstring recordingsDir() { return knownFolder(FOLDERID_Music, L"DJ Elver Galarga - Grabaciones"); }

std::string foldForSearch(const std::string& utf8) {
    std::wstring w = fromUtf8(utf8);
    // Descomponer (NFD) y quitar marcas diacríticas.
    const int n = NormalizeString(NormalizationD, w.c_str(), (int)w.size(), nullptr, 0);
    std::wstring d;
    if (n > 0) {
        d.resize(n);
        const int m = NormalizeString(NormalizationD, w.c_str(), (int)w.size(), d.data(), n);
        d.resize(m > 0 ? m : 0);
    } else d = w;
    std::wstring o;
    o.reserve(d.size());
    for (wchar_t c : d) {
        if (c >= 0x0300 && c <= 0x036F) continue;
        o.push_back((wchar_t)std::towlower(c));
    }
    return toUtf8(o);
}

std::vector<std::string> searchTerms(const std::string& query) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : foldForSearch(query)) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::wstring fileNameOf(const std::wstring& path) {
    const size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring extensionLower(const std::wstring& path) {
    const size_t p = path.find_last_of(L'.');
    if (p == std::wstring::npos || path.find_first_of(L"\\/", p) != std::wstring::npos) return L"";
    std::wstring e = path.substr(p);
    for (auto& c : e) c = (wchar_t)std::towlower(c);
    return e;
}

bool isSupportedAudioExt(const std::wstring& e) {
    return e == L".mp3" || e == L".wav" || e == L".flac" || e == L".ogg";
}

std::string formatTime(double sec, bool tenths) {
    if (!(sec >= 0)) sec = 0;
    const int m = (int)(sec / 60);
    const double s = sec - m * 60;
    char b[32];
    if (tenths) std::snprintf(b, sizeof b, "%d:%04.1f", m, std::floor(s * 10) / 10);
    else std::snprintf(b, sizeof b, "%d:%02d", m, (int)s);
    return b;
}

} // namespace rdj
