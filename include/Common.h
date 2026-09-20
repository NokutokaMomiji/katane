#ifndef KATANE_COMMON_H
#define KATANE_COMMON_H

#include <stdint.h>     //Includes integers with sepcified widths.
#include <stdbool.h>    //Includes boolean types and values.
#include <stddef.h>     //Includes standard type definitions (and NULL).

//#define DEBUG_TRACE_EXECUTION
//#define DEBUG_PRINT_CODE
//#define DEBUG_STRESS_GC
//define DEBUG_LOG_GC

#define COLOR_RED     "\x1b[91m"
#define COLOR_CYAN    "\x1b[96m"
#define COLOR_MAGENTA "\x1b[95m"
#define COLOR_GRAY    "\x1b[90m"
#define COLOR_CUSTOM  "\x1b[38;2;199;53;92m"
#define COLOR_RESET   "\x1b[0m"

#define UINT8_COUNT (UINT8_MAX + 1)
#define UINT16_COUNT (UINT16_MAX + 1)
#define STACK_MIN (MAX_FRAMES * UINT8_COUNT)
#define COPIED_STACK_MIN (STACK_MIN / 16)
#define THREADS_MIN        16

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#  define IS_UNIX
#endif

#define VERSION(x) #x
#define VERSION_STRING(name, major, minor, patch)  name " " VERSION(major) "." VERSION(minor) "." VERSION(patch)

#ifdef __clang__
#  define COMPILER        ("Clang " __clang_version__)
#elif defined(_MSC_VER)
#  define COMPILER        VERSION_STRING("MSC", _MSC_VER, 0, 0)
#elif defined(__MINGW64_VERSION_MAJOR)
#  define COMPILER        VERSION_STRING("MinGW64", __MINGW64_VERSION_MAJOR, __MINGW64_VERSION_MINOR, 0)
#elif defined(__MINGW32_MAJOR_VERSION)
#  define COMPILER        VERSION_STRING("MinGW32", __MINGW32_MAJOR_VERSION, __MINGW32_MINOR_VERSION, 0)
#elif defined(__GNUC__)
#  define COMPILER        VERSION_STRING("GCC", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#else
#  define COMPILER        "Unknown Compiler"
#endif

#if defined(__APPLE__) && defined(__MACH__)
#  define LIBRARY_FILE_EXTENSION ".dylib"
#  define COMPILED_OS "MacOS"
#elif defined(_WIN64)
#  define LIBRARY_FILE_EXTENSION ".dll"
#  define COMPILED_OS "Microsoft Windows (x64)"
#elif defined(_WIN32)
#  define LIBRARY_FILE_EXTENSION ".dll"
#  define COMPILED_OS "Microsoft Windows (x86)"
#elif defined(__linux__)
#  define LIBRARY_FILE_EXTENSION ".so"
#  define COMPILED_OS "GNU / Linux"
#else
#  define LIBRARY_FILE_EXTENSION ".so"
#  define COMPILED_OS "Unknown OS"
#endif

#define DEFAULT_GC_START   (1024 * 1024 * 10)
#define MINIMUM_GC_START   (1024 * 1024)

#if defined(_WIN32) && !defined(errno)
#  define errno           (GetLastError())
#endif

#if defined(__clang__) || defined(__GNUC__)
#  define KTN_MAYBE_UNUSED __attribute__((unused))
#else
#  define KTN_MAYBE_UNUSED
#endif

#define KATANE_COPYRIGHT   "Copyright (C) 2026 Nokutoka Momiji"
#endif
