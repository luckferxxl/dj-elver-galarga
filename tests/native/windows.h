// Declarations ONLY for a Linux syntax check, never linked into the app.
#pragma once
#include "crt.h"
using DWORD=unsigned long;
constexpr DWORD INVALID_FILE_ATTRIBUTES=0xffffffff, FILE_ATTRIBUTE_DIRECTORY=16;
constexpr int THREAD_PRIORITY_BELOW_NORMAL=-1;
void* GetCurrentThread();
void SetThreadPriority(void*,int);
DWORD GetFileAttributesW(const wchar_t*);
bool DeleteFileW(const wchar_t*);
void Sleep(unsigned long);
