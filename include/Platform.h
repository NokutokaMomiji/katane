#ifndef KATANE_PLATFORM_H
#define KATANE_PLATFORM_H

#include "Common.h"
#include <stddef.h>

#ifdef _WIN32
    #include <windows.h>
    #include <sys/stat.h>
    #define KTN_STAT _stat
#else
    #include <dlfcn.h>
    #include <sys/stat.h>
    #define KTN_STAT stat
#endif

#ifdef _WIN32
    typedef HMODULE KTN_DLHandle;
    typedef FARPROC KTN_DLSym;
#else
    typedef void* KTN_DLHandle;
    typedef void* KTN_DLSym;
#endif

KTN_DLHandle KTN_DLOpen(const char* path);
KTN_DLSym KTN_DLGetSymbol(KTN_DLHandle handle, const char* symbol);
void KTN_DLClose(KTN_DLHandle handle);

char* KTN_FileRead(const char* path, size_t* size);
bool KTN_FileWrite(const char* path, const void* data, size_t size);
bool KTN_FileExists(const char* path);
int KTN_IsDirectory(const char* path);
void KTN_GetTempPath(char* buffer, size_t size);

#endif