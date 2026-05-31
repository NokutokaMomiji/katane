/**
 * Platform.c
 * Nokutoka Momiji
 * 
 * Small wrappers around platform-specific behavior regarding the filesystem
 * and dynamic libraries.
 * 
 * It's amazing how something like loading dynamic libraries has to be so
 * freaking platform dependent.
 */

#include "Platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

KTN_DLHandle KTN_DLOpen(const char* path) {
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

KTN_DLSym KTN_DLGetSymbol(KTN_DLHandle handle, const char* symbol) {
#ifdef _WIN32
    return GetProcAddress(handle, symbol);
#else
    return dlsym(handle, symbol);
#endif
}

void KTN_DLClose(KTN_DLHandle handle) {
    if (!handle) 
        return;

#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

bool KTN_FileExists(const char* path) {
    struct KTN_STAT stat;
    return (KTN_STAT(path, &stat) == 0);
}

char* KTN_FileRead(const char* path, size_t* size) {
    if (!size) return NULL;

    FILE* file = fopen(path, "rb");

    if (!file) {
        *size = 0;
        return NULL;
    }

    fseek(file, 0, SEEK_END);

    long length = ftell(file);
    if (length < 0) {
        *size = 0;
        fclose(file);
        return NULL;
    }

    rewind(file);

    char* buffer = (char*)malloc(length + 1);

    if (!buffer) {
        *size = 0;
        fclose(file);
        return NULL;
    }

    size_t readBytes = fread(buffer, 1, length, file);
    fclose(file);

    if (readBytes != length) {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';

    *size = readBytes;
    return buffer;
}

bool KTN_FileWrite(const char* path, const void* data, size_t size) {
    char tempPath[1024];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE* file = fopen(tempPath, "wb");
    if (!file)
        return false;

    size_t written = fwrite(data, 1, size, file);
    int error = ferror(file);
    fclose(file);

    if (written != size || error) {
        remove(tempPath);
        return false;
    }

    if (rename(tempPath, path) != 0) {
        remove(tempPath);
        return false;
    }

    return true;
}

int KTN_IsDirectory(const char* path) {
    struct KTN_STAT stat;

    if (KTN_STAT(path, &stat) != 0)
        return 0;

#ifdef _WIN32
    return (stat.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(stat.st_mode);
#endif
}

#undef _WIN32

/// @brief Returns a new temporary path (without trailing slash)
/// @param buffer Buffer where to store the path.
/// @param size Size of the buffer
void KTN_GetTempPath(char* buffer, size_t size) {
    if (size == 0) return;

#ifdef _WIN32
    DWORD length = GetTempPathA((DWORD)size, buffer);
    if (length == 0 || length >= size) {
        strncpy(buffer, ".", size);
        buffer[size - 1] = '\0';
        return;
    }

    // Remove the backslash. I hate them.
    size_t last = strlen(buffer);
    if (last > 0 && (buffer[last - 1] == '\\' || buffer[last - 1] == '/')) {
        buffer[last - 1] = '\0';
    }
#else
    const char* tempDirectory = getenv("TMPDIR");

    if (!tempDirectory)
        tempDirectory = "/tmp";

    snprintf(buffer, size, "%s", tempDirectory);

    // Remove the backslash.
    size_t last = strlen(buffer);
    if (last > 0 && buffer[last - 1] == '/') {
        buffer[last - 1] = '\0';
    }
#endif
}