#pragma once
#include <string>
#include <cwctype>
namespace rdj {
inline std::wstring safeFileComponent(std::wstring s) {
    for (auto& c : s) if (c < 32 || std::wstring(L"<>:\"/\\|?*").find(c) != std::wstring::npos) c = L'_';
    if (s.size() > 100) s.resize(100);
    while (!s.empty() && (s.back() == L'.' || s.back() == L' ')) s.pop_back();
    if (s.empty()) s = L"Sin título";
    std::wstring base = s.substr(0, s.find(L'.'));
    for (auto& c : base) c = std::towupper(c);
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
        (base.size() == 4 && (base.substr(0,3) == L"COM" || base.substr(0,3) == L"LPT") && base[3] >= L'0' && base[3] <= L'9')) s = L"_" + s;
    return s;
}
}
