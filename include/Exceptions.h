#ifndef KATANE_EXCEPTIONS_H
#define KATANE_EXCEPTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include "Object.h"

// Super cool thing I learned: the format attribute gives you printf-like correction (mismatched type formatters and parameters).
// Deprecated was also used mainly cuz I have a few functions that are used *everywhere* that I'll be removing but they are not within the current scope of the project.
#if defined(__GNUC__) || defined(__clang__)
    #define KTN_PRINTF_LIKE(formatIndex, firstArgIndex) __attribute__((format(printf, formatIndex, firstArgIndex)))
    #define KTN_DEPRECATED(reason)                      __attribute__((deprecated(reason)))
#else
    #define KTN_PRINTF_LIKE(formatIndex, firstArgIndex)
    #define KTN_DEPRECATED(reason)
#endif

/// Runtime error helper macro. [message] must be one of the messages defined in Messages.h
#define KATANE_RUNTIME_ERROR(message, ...) \
    KTN_ThrowRuntimeError(vm, KTN_MESSAGE(vm, message), ##__VA_ARGS__)

/// AccessError helper macro. [message] must be one of the messages defined in Messages.h
#define KATANE_ACCESS_ERROR(message, target) \
    KTN_ThrowAccessError(vm, KTN_MESSAGE(vm, message), (target))

/// PropertyError helper macro. [message] must be one of the messages defined in Messages.h
#define KATANE_PROPERTY_ERROR(message, target, property) \
    KTN_ThrowPropertyError(vm, KTN_MESSAGE(vm, message), (target), (property))

/// ArgumentError helper macro. [message] must be one of the messages defined in Messages.h
#define KATANE_ARGUMENT_ERROR(argumentName, message, ...) \
    KTN_ThrowArgumentError(vm, (argumentName), KTN_MESSAGE(vm, message), ##__VA_ARGS__)

/// IOException helper macro. [message] must be one of the messages defined in Messages.h
#define KATANE_IO_EXCEPTION(path, message, ...) \
    KTN_ThrowIOException(vm, (path), KTN_MESSAGE(vm, message), ##__VA_ARGS__)

/// Represents the valid built-in exception types. 
///
/// This is mainly to limit the number of strings and avoid typo-errors for builtins.
typedef enum {
    KTN_EXC_THROWABLE,
    KTN_EXC_EXCEPTION,
    KTN_EXC_RUNTIME_EXCEPTION,
    KTN_EXC_FORMAT_EXCEPTION,
    KTN_EXC_IO_EXCEPTION,
    KTN_EXC_ERROR,
    KTN_EXC_RUNTIME_ERROR,
    KTN_EXC_ASSERTION_ERROR,
    KTN_EXC_ACCESS_ERROR,
    KTN_EXC_PROPERTY_ERROR,
    KTN_EXC_TYPE_ERROR,
    KTN_EXC_OPERAND_ERROR,
    KTN_EXC_ARGUMENT_ERROR,
    KTN_EXC_RANGE_ERROR,
    KTN_EXC_INDEX_ERROR,
    KTN_EXC_KEY_ERROR,
    KTN_EXC_COUNT
} KTN_ExceptionType; // Why couldn't enums be name-spaced ~_~

/// Returns the string name of the given exception type.
const char* KTN_ExceptionTypeName(KTN_ExceptionType type);
KTN_Value BuildStackTraceObject(KTN_VM* vm, KTN_ObjInstance* stackTraceInstance);
KTN_ObjInstance* KTN_ExceptionCreate(KTN_VM* vm, KTN_ExceptionType type, KTN_ObjString* message);
const char* KTN_ValueTypeName(KTN_Value value);
void KTN_InitializeExceptions(KTN_VM* vm, KTN_ObjModule* module);
bool KTN_ThrowValue(KTN_VM* vm, KTN_ObjInstance* exception, bool buildTrace);

/// (Errors and Exceptions)
/// The following functions all throw their corresponding errors / exceptions.
/// They all return a boolean value which indicates whether or not the error was caught.

/// Generic runtime error. Should only be used on an error that legitimately belongs to the runtime or as a placeholder.
bool KTN_ThrowRuntimeError(KTN_VM* vm, const char* format, ...) KTN_PRINTF_LIKE(2, 3);
/// Throws an AssertionError. Thrown when an assertion fails.
bool KTN_ThrowAssertionError(KTN_VM* vm, KTN_ObjString* message);
/// Throws an AccessError exception. Meant to indicate when an action is attempting to access something it cannot.
/// I mostly use it to ban reassignment or modification of final variables.
bool KTN_ThrowAccessError(KTN_VM* vm, const char* format, const char* target);
/// Throws a PropertyError. Meant to indicate errors related to an instance's properties and methods, such as 
/// non-existence or lack of permissions (private / hidden).
bool KTN_ThrowPropertyError(KTN_VM* vm, const char* format, const char* target, const char* property);
/// Throws an ArgumentError. Meant to indicate errors regarding the arguments passed to a function or method.
bool KTN_ThrowArgumentError(KTN_VM* vm, const char* argumentName, const char* format, ...) KTN_PRINTF_LIKE(3, 4);
/// Throws an IOException. Generic exception related to I/O based operations.
/// In the future, I plan for other exceptions to expand on them, such as FileSystemException.
bool KTN_ThrowIOException(KTN_VM* vm, const char* path, const char* format, ...) KTN_PRINTF_LIKE(3, 4);
/// Throws a TypeError. Meant to be used when a value has a different type from what was expected.
///
/// [expected] is the string name of the expected value. [actual] is the value received (type gets extracted automatically).
/// [context] gives more information on what the type error was.
///
/// Example: ``` katane
/// >>> mochi a: Int = "String";
/// Ohoho~! Someone messed up~! Time for a stacktrace (most recent call last)~!
/// In <repl>:
///       1 | <script> | mochi a: Int = "String";
/// 
/// TypeError: Global variable type error: expected Int, got String.
/// In <script>:
///       1 | mochi a: Int = "String";
/// ```
bool KTN_ThrowTypeError(KTN_VM* vm, const char* expected, KTN_Value actual, const char* context);
bool KTN_ThrowOperandError(KTN_VM* vm, const char* operation, KTN_Value left, KTN_Value right);
bool KTN_ThrowUnaryOperandError(KTN_VM* vm, const char* operation, KTN_Value operand);
bool KTN_ThrowIndexError(KTN_VM* vm, int64_t index, int64_t length);
bool KTN_ThrowInvalidIndex(KTN_VM* vm, KTN_Value index, int64_t length);
bool KTN_ThrowKeyError(KTN_VM* vm, KTN_Value key);

/// (Legacy) The following are left because I'm pretty sure somewhere in the code base there are still 
/// leftover places where these are called. The old system was pretty bad because it created exception objects that
/// were not really initialized.

KTN_DEPRECATED("Use the typed KTN_Throw* functions / KATANE_* macros!")
bool KTN_RuntimeError(KTN_VM* vm, const char* format, ...) KTN_PRINTF_LIKE(2, 3);

KTN_DEPRECATED("Use the typed KTN_Throw* functions / KATANE_* macros!")
bool KTN_ThrowException(KTN_VM* vm, const char* type, bool isAssert, const char* format, ...) KTN_PRINTF_LIKE(4, 5);

#endif
