#include "core/util.h"
#include <filesystem>
#include <locale>
#include <codecvt>
namespace rdj {
std::string toUtf8(const std::wstring& w) {return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(w);}
std::wstring fromUtf8(const std::string& s) {return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(s);}
std::wstring appDataDir() {return L"/tmp/rdj-cover-test-appdata";}
std::wstring fileNameOf(const std::wstring& p) {return std::filesystem::path(p).filename().wstring();}
bool isSupportedAudioExt(const std::wstring& e) {return e==L".mp3"||e==L".wav"||e==L".flac"||e==L".ogg";}
std::wstring extensionLower(const std::wstring& p) {auto s=std::filesystem::path(p).extension().wstring();for(auto& c:s)c=towlower(c);return s;}
}
