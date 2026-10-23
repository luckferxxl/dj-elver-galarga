#pragma once
#ifndef _WIN32
#include <ctime>
#include <cwchar>
#include <strings.h>
#include <cstdio>
#include <filesystem>
#define _stricmp strcasecmp
#define _wcsicmp wcscasecmp
inline int localtime_s(std::tm* out,const std::time_t* in) { return localtime_r(in,out)?0:1; }
inline FILE* _wfopen(const wchar_t* path,const wchar_t* mode) { return fopen(std::filesystem::path(path).string().c_str(),std::filesystem::path(mode).string().c_str()); }
#endif
