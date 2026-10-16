#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Exceptions.h"
#include "Array.h"
#include "Map.h"
#include "Native.h"
#include "Object.h"
#include "Table.h"
#include "Value.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// Helps define a generic signature for Throwable, Exception and Error.
#define SIG_EXCEPTION(name, doc) \
    KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE(name, \
        KTN_SHIKI_TYPE(TYPE_CONSTRUCTOR), \
        KTN_PARAM("message", "String", KTN_PARAM_POSITIONAL), \
        KTN_DOCS(doc)))

/// For exceptions (i.e. TypeError, KeyError, etc...) that have a default error message.
#define EXPECT_OPTIONAL_MESSAGE() do { \
        if (ARGUMENT_COUNT >= 2 && IsSupplied(ARG(1))) \
            EXPECT_ARG_STRING(1); \
    } while (0)

/// If a message was provided, stores it in the exception instance.
#define STORE_MESSAGE(self) do { \
        if (ARGUMENT_COUNT >= 2 && IsSupplied(ARG(1))) \
            TableSet(vm, &(self)->properties, KTN_NAME(vm, KTN_NAME_MESSAGE), ARG(1)); \
    } while (0)

/// Helps assert that a named parameter was provided with the desired value.
#define EXPECT_NAMED(paramName, checkMacro, typeName) do { \
        KTN_Value namedValue_ = NAMED(paramName); \
        if (IsSupplied(namedValue_) && !checkMacro(namedValue_)) { \
            char contextBuffer_[64]; \
            snprintf(contextBuffer_, sizeof(contextBuffer_), KTN_CTX_NAMED_ARGUMENT, paramName); \
            KTN_ThrowTypeError(vm, (typeName), namedValue_, contextBuffer_); \
            RETURN_ERROR(NULL_VALUE); \
        } \
    } while (0)

/// Helper typed macros.
#define EXPECT_NAMED_STRING(paramName) EXPECT_NAMED(paramName, IS_STRING, "String")
#define EXPECT_NAMED_INT(paramName)    EXPECT_NAMED(paramName, IS_INT,    "Int")

/// Finds the named parameter and stores it with the same well-known name on the table.
#define STORE_NAMED(self, nameId, paramName) do { \
        KTN_Value namedValue_ = NAMED(paramName); \
        if (IsSupplied(namedValue_)) \
            TableSet(vm, &(self)->properties, KTN_NAME(vm, nameId), namedValue_); \
    } while (0)

/// Finds the named parameter and stores it in the instance with the given name.
#define STORE_NAMED_AS(self, key, paramName) do { \
        KTN_Value namedValue_ = NAMED(paramName); \
        if (IsSupplied(namedValue_)) \
            TableSet(vm, &(self)->properties, STRING_COPY_AUTO(key), namedValue_); \
    } while (0)

static const char* const ExceptionTypeNames[KTN_EXC_COUNT] = {
    [KTN_EXC_THROWABLE]         = "Throwable",
    [KTN_EXC_EXCEPTION]         = "Exception",
    [KTN_EXC_RUNTIME_EXCEPTION] = "RuntimeException",
    [KTN_EXC_FORMAT_EXCEPTION]  = "FormatException",
    [KTN_EXC_IO_EXCEPTION]      = "IOException",
    [KTN_EXC_ERROR]             = "Error",
    [KTN_EXC_RUNTIME_ERROR]     = "RuntimeError",
    [KTN_EXC_ASSERTION_ERROR]   = "AssertionError",
    [KTN_EXC_ACCESS_ERROR]      = "AccessError",
    [KTN_EXC_PROPERTY_ERROR]    = "PropertyError",
    [KTN_EXC_TYPE_ERROR]        = "TypeError",
    [KTN_EXC_OPERAND_ERROR]     = "OperandError",
    [KTN_EXC_ARGUMENT_ERROR]    = "ArgumentError",
    [KTN_EXC_RANGE_ERROR]       = "RangeError",
    [KTN_EXC_INDEX_ERROR]       = "IndexError",
    [KTN_EXC_KEY_ERROR]         = "KeyError",
};

/// Returns the proper exception name. This way, you can ensure that the name is correct.
#define EXCEPTION_NAME(name) KTN_ExceptionTypeName(KTN_EXC_##name)

/// Returns the name of the exception given the type, as stored.
const char* KTN_ExceptionTypeName(KTN_ExceptionType type) {
    assert(type >= 0 && type < KTN_EXC_COUNT);

    return ExceptionTypeNames[type];
}

/// Returns the exception type given the name. Returns true if the type was found, returns false if not.
static bool ExceptionTypeFromName(const char* name, KTN_ExceptionType* out) {
    if (name == NULL) return false;

    for (int i = 0; i < KTN_EXC_COUNT; i++) {
        if (strcmp(ExceptionTypeNames[i], name) == 0) {
            *out = (KTN_ExceptionType)i;
            return true;
        }
    }

    return false;
}

/// Finds the registered class for the type, or NULL if it has not been registered (yet).
static KTN_ObjKata* LookupExceptionClass(KTN_VM* vm, KTN_ExceptionType type) {
    if (type == KTN_EXC_THROWABLE)
        return vm->exceptionClass;

    KTN_Value exceptionClass;
    if (TableGet(&vm->globals, STRING_COPY_AUTO(KTN_ExceptionTypeName(type)), &exceptionClass) && IS_CLASS(exceptionClass))
        return AS_CLASS(exceptionClass);

    return NULL;
}

/// Returns the base exception class for the given type.
static KTN_ObjKata* ExceptionClass(KTN_VM* vm, KTN_ExceptionType type) {
    KTN_ObjKata* kata = LookupExceptionClass(vm, type);

    return (kata != NULL) ? kata : vm->exceptionClass;
}

/// Creates an exception instance of the given type.
KTN_ObjInstance* KTN_ExceptionCreate(KTN_VM* vm, KTN_ExceptionType type, KTN_ObjString* message) {
    KTN_ObjInstance* instance = InstanceNew(vm, ExceptionClass(vm, type));
    Push(vm, OBJECT_VALUE(instance));
    TableSet(vm, &instance->properties, KTN_NAME(vm, KTN_NAME_MESSAGE), OBJECT_VALUE(message));
    Pop(vm);
    return instance;
}

/// Helper method that gives you the string name of a value type.
const char* KTN_ValueTypeName(KTN_Value value) {
    if (IS_INT(value))         return "Int";
    if (IS_DOUBLE(value))      return "Float";
    if (IS_BOOL(value))        return "Bool";
    if (IS_NULL(value))        return "Null";
    if (IS_STRING(value))      return "String";
    if (IS_INSTANCE(value))    return AS_INSTANCE(value)->kata->className->chars;
    if (IS_CLASS(value))       return "Kata";
    if (IS_ARRAY(value))       return "Array";
    if (IS_MAP(value))         return "Map";
    if (IS_CLOSURE(value) || IS_FUNCTION(value)) return "Shiki";
    return "unknown";
}

/// Builds a stacktrace object of the VM at the time. PrintStacktrace will use this in the future.
KTN_Value BuildStackTraceObject(KTN_VM* vm, KTN_ObjInstance* stackTraceInstance) {
    KTN_ObjArray* frames = ArrayNew(vm, vm->frameCount);
    Push(vm, OBJECT_VALUE(frames));

    // We go through each frame, gather the info, and store it in a map.
    for (int frameIndex = vm->frameCount - 1; frameIndex >= 0; frameIndex--) {
        KTN_CallFrame* frame = &vm->frames[frameIndex];
        KTN_ObjShiki* function = frame->closure->function;
        size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);

        int line = KTN_ChunkGetLine(&function->chunk, (int)instruction);
        char* content = KTN_ChunkGetSource(&function->chunk, (int)instruction);

        KTN_ObjMap* frameMap = MapNew(vm, 4);
        Push(vm, OBJECT_VALUE(frameMap));

        Push(vm, STRING_VALUE("line"));
        MapSet(vm, frameMap, Peek(vm, 0), INT_VALUE(line));
        Pop(vm);

        Push(vm, STRING_VALUE("source"));
        MapSet(vm, frameMap, Peek(vm, 0), content ? STRING_VALUE(content) : NULL_VALUE);
        Pop(vm);

        Push(vm, STRING_VALUE("functionName"));
        KTN_Value functionNameValue = (function->name != NULL)
            ? STRING_VALUE(function->name->chars)
            : STRING_VALUE((function->type == TYPE_LAMBDA) ? "<anonymous>" : "<script>");
        MapSet(vm, frameMap, Peek(vm, 0), functionNameValue);
        Pop(vm);

        Push(vm, STRING_VALUE("file"));
        KTN_Value fileValue = (function->module && function->module->file)
            ? STRING_VALUE(function->module->file)
            : NULL_VALUE;
        MapSet(vm, frameMap, Peek(vm, 0), fileValue);
        Pop(vm);

        KTN_ArraySet(vm, frames, INT_VALUE(frames->items.count), OBJECT_VALUE(frameMap));
        Pop(vm);
    }

    // If we had already created an instance, we simply assign the list to it.
    // Then, we return it again.
    if (stackTraceInstance != NULL) {
        TableSet(vm, &stackTraceInstance->properties, STRING_COPY_AUTO("frames"), Peek(vm, 0));
        Pop(vm);
        return OBJECT_VALUE(stackTraceInstance);
    }

    // Otherwise, we create a new instance and return it too.
    if (vm->stackTraceClass != NULL) {
        KTN_ObjInstance* traceInstance = InstanceNew(vm, vm->stackTraceClass);
        Push(vm, OBJECT_VALUE(traceInstance));
        TableSet(vm, &traceInstance->properties, STRING_COPY_AUTO("frames"), Peek(vm, 1));
        KTN_Value result = Peek(vm, 0);
        PopN(vm, 2);
        return result;
    }

    // Returns the list directly. This should not happen.
    return Pop(vm);
}

/// Formats a message into an ObjString.
static KTN_ObjString* FormatMessage(KTN_VM* vm, const char* format, va_list args) {
    char* text = NULL;
    int length = vasprintf(&text, format, args);

    if (length < 0) {
        static const char fallback[] = "Could not format the exception message.";

        return STRING_COPY(vm, fallback, (int)(sizeof(fallback) - 1));
    }

    return STRING_TAKE(vm, text, length);
}

static KTN_ObjInstance* BeginExceptionVM(KTN_VM* vm, KTN_ExceptionType type, const char* format, va_list args) {
    KTN_ObjString* message = FormatMessage(vm, format, args);

    Push(vm, OBJECT_VALUE(message));
    KTN_ObjInstance* instance = KTN_ExceptionCreate(vm, type, message);
    Pop(vm);

    Push(vm, OBJECT_VALUE(instance));
    return instance;
}

static KTN_ObjInstance* BeginException(KTN_VM* vm, KTN_ExceptionType type, const char* format, ...) KTN_PRINTF_LIKE(3, 4);
static KTN_ObjInstance* BeginException(KTN_VM* vm, KTN_ExceptionType type, const char* format, ...) {
    va_list args;
    va_start(args, format);
    KTN_ObjInstance* instance = BeginExceptionVM(vm, type, format, args);
    va_end(args);

    return instance;
}

static bool FinishThrow(KTN_VM* vm, KTN_ObjInstance* instance) {
    Pop(vm);
    return KTN_ThrowValue(vm, instance, true);
}

#define SET_WELL_KNOWN(vm, instance, nameId, value) \
        TableSet((vm), &(instance)->properties, KTN_NAME((vm), nameId), (value)); \

static void SetNamed(KTN_VM* vm, KTN_ObjInstance* instance, const char* name, KTN_Value value) {
    Push(vm, value);
    KTN_ObjString* key = STRING_COPY_AUTO(name);
    Push(vm, OBJECT_VALUE(key));
    TableSet(vm, &instance->properties, key, value);
    PopN(vm, 2);
}

static void FormatValueBrief(KTN_Value value, char* out, size_t capacity) {
    if (IS_STRING(value)) {
        const char* chars = AS_CSTRING(value);
        size_t length = strlen(chars);
        size_t shown = (length > 40) ? 40 : length;

        // Do not cut a UTF-8 sequence in half.
        while (shown > 0 && shown < length && ((unsigned char)chars[shown] & 0xC0) == 0x80)
            shown--;

        snprintf(out, capacity, "\"%.*s%s\"", (int)shown, chars, (shown < length) ? "..." : "");
    } else if (IS_INT(value)) {
        snprintf(out, capacity, "%lld", (long long)AS_INT(value));
    } else if (IS_DOUBLE(value)) {
        snprintf(out, capacity, "%g", AS_DOUBLE(value));
    } else if (IS_BOOL(value)) {
        snprintf(out, capacity, "%s", AS_BOOL(value) ? "true" : "false");
    } else if (IS_NULL(value)) {
        snprintf(out, capacity, "null");
    } else {
        snprintf(out, capacity, "<%s>", KTN_ValueTypeName(value));
    }
}

bool KTN_ThrowRuntimeError(KTN_VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    KTN_ObjInstance* exception = BeginExceptionVM(vm, KTN_EXC_RUNTIME_ERROR, format, args);
    va_end(args);

    return FinishThrow(vm, exception);
}

bool KTN_ThrowAssertionError(KTN_VM* vm, KTN_ObjString* message) {
    KTN_ObjInstance* exception;

    if (message != NULL) {
        Push(vm, OBJECT_VALUE(message));
        exception = KTN_ExceptionCreate(vm, KTN_EXC_ASSERTION_ERROR, message);
        Pop(vm);
        Push(vm, OBJECT_VALUE(exception));
    } else {
        exception = BeginException(vm, KTN_EXC_ASSERTION_ERROR, KTN_MESSAGE(vm, KTN_MSG_ASSERTION_FAILED));
    }

    return FinishThrow(vm, exception);
}

bool KTN_ThrowAccessError(KTN_VM* vm, const char* format, const char* target) {
    KTN_ObjInstance* exception = BeginException(vm, KTN_EXC_ACCESS_ERROR, format, target);

    if (target != NULL)
        SET_WELL_KNOWN(vm, exception, KTN_NAME_TARGET, STRING_VALUE(target));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowPropertyError(KTN_VM* vm, const char* format, const char* target, const char* property) {
    KTN_ObjInstance* exception = BeginException(vm, KTN_EXC_PROPERTY_ERROR, format, target, property);

    if (target != NULL)
        SET_WELL_KNOWN(vm, exception, KTN_NAME_TARGET, STRING_VALUE(target));
    if (property != NULL)
        SET_WELL_KNOWN(vm, exception, KTN_NAME_PROPERTY_NAME, STRING_VALUE(property));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowArgumentError(KTN_VM* vm, const char* argumentName, const char* format, ...) {
    va_list args;
    va_start(args, format);
    KTN_ObjInstance* exception = BeginExceptionVM(vm, KTN_EXC_ARGUMENT_ERROR, format, args);
    va_end(args);

    if (argumentName != NULL)
        SET_WELL_KNOWN(vm, exception, KTN_NAME_ARGUMENT_NAME, STRING_VALUE(argumentName));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowIOException(KTN_VM* vm, const char* path, const char* format, ...) {
    va_list args;
    va_start(args, format);
    KTN_ObjInstance* exception = BeginExceptionVM(vm, KTN_EXC_IO_EXCEPTION, format, args);
    va_end(args);

    if (path != NULL)
        SetNamed(vm, exception, "path", STRING_VALUE(path));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowTypeError(KTN_VM* vm, const char* expected, KTN_Value actual, const char* context) {
    KTN_ObjInstance* exception = BeginException(
        vm, KTN_EXC_TYPE_ERROR, KTN_MESSAGE(vm, KTN_MSG_TYPE_MISMATCH),
        context, expected, KTN_ValueTypeName(actual)
    );

    SET_WELL_KNOWN(vm, exception, KTN_NAME_EXPECTED, STRING_VALUE(expected));
    SET_WELL_KNOWN(vm, exception, KTN_NAME_ACTUAL, actual);

    return FinishThrow(vm, exception);
}

bool KTN_ThrowOperandError(KTN_VM* vm, const char* operation, KTN_Value left, KTN_Value right) {
    const char* leftType = KTN_ValueTypeName(left);
    const char* rightType = KTN_ValueTypeName(right);

    KTN_ObjInstance* exception = BeginException(
        vm, KTN_EXC_OPERAND_ERROR, KTN_MESSAGE(vm, KTN_MSG_OPERAND_BINARY),
        operation, leftType, rightType
    );

    SET_WELL_KNOWN(vm, exception, KTN_NAME_OPERATION, STRING_VALUE(operation));
    SetNamed(vm, exception, "left", STRING_VALUE(leftType));
    SetNamed(vm, exception, "right", STRING_VALUE(rightType));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowUnaryOperandError(KTN_VM* vm, const char* operation, KTN_Value operand) {
    const char* operandType = KTN_ValueTypeName(operand);

    KTN_ObjInstance* exception = BeginException(
        vm, KTN_EXC_OPERAND_ERROR, KTN_MESSAGE(vm, KTN_MSG_OPERAND_UNARY),
        operation, operandType
    );

    SET_WELL_KNOWN(vm, exception, KTN_NAME_OPERATION, STRING_VALUE(operation));
    SetNamed(vm, exception, "left", STRING_VALUE(operandType));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowIndexError(KTN_VM* vm, int64_t index, int64_t length) {
    KTN_ObjInstance* exception = BeginException(
        vm, KTN_EXC_INDEX_ERROR, KTN_MESSAGE(vm, KTN_MSG_INDEX_OUT_OF_RANGE),
        (long long)index, (long long)length
    );

    SET_WELL_KNOWN(vm, exception, KTN_NAME_INDEX, INT_VALUE((int32_t)index));
    SET_WELL_KNOWN(vm, exception, KTN_NAME_LENGTH, INT_VALUE((int32_t)length));
    SET_WELL_KNOWN(vm, exception, KTN_NAME_MINIMUM, INT_VALUE(0));
    SET_WELL_KNOWN(vm, exception, KTN_NAME_MAXIMUM, INT_VALUE((int32_t)(length - 1)));

    return FinishThrow(vm, exception);
}

bool KTN_ThrowInvalidIndex(KTN_VM* vm, KTN_Value index, int64_t length) {
    if (!IS_INT(index))
        return KTN_ThrowTypeError(vm, "Int", index, KTN_CTX_INDEX);

    return KTN_ThrowIndexError(vm, (int64_t)AS_INT(index), length);
}

bool KTN_ThrowKeyError(KTN_VM* vm, KTN_Value key) {
    char representation[96];
    FormatValueBrief(key, representation, sizeof(representation));

    KTN_ObjInstance* exception = BeginException(
        vm, KTN_EXC_KEY_ERROR, KTN_MESSAGE(vm, KTN_MSG_KEY_NOT_FOUND), representation
    );

    SET_WELL_KNOWN(vm, exception, KTN_NAME_KEY, key);

    return FinishThrow(vm, exception);
}

bool KTN_RuntimeError(KTN_VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    KTN_ObjInstance* exception = BeginExceptionVM(vm, KTN_EXC_RUNTIME_ERROR, format, args);
    va_end(args);

    return FinishThrow(vm, exception);
}

bool KTN_ThrowException(KTN_VM* vm, const char* type, bool isAssert, const char* format, ...) {
    KTN_ExceptionType exceptionType = KTN_EXC_RUNTIME_ERROR;

    if (isAssert)
        exceptionType = KTN_EXC_ASSERTION_ERROR;
    else if (!ExceptionTypeFromName(type, &exceptionType))
        exceptionType = KTN_EXC_RUNTIME_ERROR;

    va_list args;
    va_start(args, format);
    KTN_ObjInstance* exception = BeginExceptionVM(vm, exceptionType, format, args);
    va_end(args);

    return FinishThrow(vm, exception);
}

static inline bool IsSupplied(KTN_Value value) {
    return !IS_EMPTY(value) && !IS_NULL(value);
}

static KTN_NativeResult ExceptionConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_ARGC(2);
    EXPECT_ARG_STRING(1);

    TableSet(vm, &self->properties, KTN_NAME(vm, KTN_NAME_MESSAGE), ARG(1));

    RETURN_VALUE(THIS);
}

static KTN_NativeResult TypeErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("expected");

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_EXPECTED, "expected");
    STORE_NAMED(self, KTN_NAME_ACTUAL, "actual");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult OperandErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("operation");
    EXPECT_NAMED_STRING("left");
    EXPECT_NAMED_STRING("right");

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_OPERATION, "operation");
    STORE_NAMED_AS(self, "left", "left");
    STORE_NAMED_AS(self, "right", "right");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult ArgumentErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("name");

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_ARGUMENT_NAME, "name");
    STORE_NAMED(self, KTN_NAME_VALUE, "value");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult RangeErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_VALUE, "value");
    STORE_NAMED(self, KTN_NAME_MINIMUM, "minimum");
    STORE_NAMED(self, KTN_NAME_MAXIMUM, "maximum");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult IndexErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_INT("index");
    EXPECT_NAMED_INT("length");

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_INDEX, "index");

    KTN_Value length = NAMED("length");
    if (IsSupplied(length)) {
        TableSet(vm, &self->properties, KTN_NAME(vm, KTN_NAME_LENGTH), length);
        TableSet(vm, &self->properties, KTN_NAME(vm, KTN_NAME_MINIMUM), INT_VALUE(0));
        TableSet(vm, &self->properties, KTN_NAME(vm, KTN_NAME_MAXIMUM), INT_VALUE(AS_INT(length) - 1));
    }

    RETURN_VALUE(THIS);
}

static KTN_NativeResult KeyErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_KEY, "key");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult AccessErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("target");

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_TARGET, "target");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult PropertyErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("target");
    EXPECT_NAMED_STRING("propertyName");

    STORE_MESSAGE(self);
    STORE_NAMED(self, KTN_NAME_TARGET, "target");
    STORE_NAMED(self, KTN_NAME_PROPERTY_NAME, "propertyName");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult FormatExceptionConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("source");
    EXPECT_NAMED_INT("offset");

    STORE_MESSAGE(self);
    STORE_NAMED_AS(self, "source", "source");
    STORE_NAMED_AS(self, "offset", "offset");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult IOExceptionConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_MAX_ARGC(2);
    EXPECT_OPTIONAL_MESSAGE();
    EXPECT_NAMED_STRING("path");

    STORE_MESSAGE(self);
    STORE_NAMED_AS(self, "path", "path");

    RETURN_VALUE(THIS);
}

static KTN_NativeResult StackTraceConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    EXPECT_ARGC(1);
    (void)BuildStackTraceObject(vm, self);
    RETURN_VALUE(THIS);
}

void KTN_InitializeExceptions(KTN_VM* vm, KTN_ObjModule* module) {
    (void)module;

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, "StackTrace", NULL);
        KTN_NativeKataSetDoc(builder, "Represents the given call stack at the moment of an error.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_FRAMES), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, StackTraceConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE(
                "StackTrace",
                KTN_SHIKI_TYPE(TYPE_CONSTRUCTOR),
                KTN_DOCS("Captures the current call stack.")
            )),
            false
        );
        vm->stackTraceClass = KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(THROWABLE), NULL);
        KTN_NativeKataSetDoc(builder, "Root of the throwable hierarchy. Never instantiated directly.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_MESSAGE), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_STACK_TRACE), NULL_VALUE, false, false);
        vm->exceptionClass = KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(EXCEPTION), EXCEPTION_NAME(THROWABLE));
        KTN_NativeKataSetDoc(builder,
            "Base for all exceptions. Exceptions represent expected, "
            "recoverable failures and are meant to be caught and handled."
        );
        KTN_NativeKataSetConstructor(builder, ExceptionConstructor,
            SIG_EXCEPTION("Exception", "Creates an Exception with the given message."),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(RUNTIME_EXCEPTION), EXCEPTION_NAME(EXCEPTION));
        KTN_NativeKataSetDoc(builder, "A general runtime error.");
        KTN_NativeKataSetConstructor(builder, ExceptionConstructor,
            SIG_EXCEPTION("RuntimeException", "Creates a RuntimeException with the given message."),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(FORMAT_EXCEPTION), EXCEPTION_NAME(EXCEPTION));
        KTN_NativeKataSetDoc(builder, "Raised when data cannot be parsed or formatted.");
        KTN_NativeKataAddProperty(builder, "source", NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, "offset", NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, FormatExceptionConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("FormatException",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("source", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("offset", "Int", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates a FormatException with an optional message, source, and offset.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(IO_EXCEPTION), EXCEPTION_NAME(EXCEPTION));
        KTN_NativeKataSetDoc(builder, "An I/O operation failed.");
        KTN_NativeKataAddProperty(builder, "path", NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, IOExceptionConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("IOException",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("path", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates an IOException with an optional message and path.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    // Errors.
    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(ERROR), EXCEPTION_NAME(THROWABLE));
        KTN_NativeKataSetDoc(builder,
            "Base for all errors. Errors indicate programming bugs in your "
            "code and are not meant to be caught.");
        KTN_NativeKataSetConstructor(builder, ExceptionConstructor,
            SIG_EXCEPTION("Error", "Creates an Error with the given message."),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(RUNTIME_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "A general failure detected while the program was running.");
        KTN_NativeKataSetConstructor(builder, ExceptionConstructor,
            SIG_EXCEPTION("RuntimeError", "Creates a RuntimeError with the given message."),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(ASSERTION_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "An assertion evaluated to false.");
        KTN_NativeKataSetConstructor(builder, ExceptionConstructor,
            SIG_EXCEPTION("AssertionError", "Creates an AssertionError with the given message."),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(ACCESS_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "A write to a constant or final binding was attempted.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_TARGET), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, AccessErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("AccessError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("target", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates an AccessError with an optional message and the name of the target.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(PROPERTY_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "A property, method, getter or setter does not exist or cannot be accessed.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_TARGET), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_PROPERTY_NAME), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, PropertyErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("PropertyError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("target", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("propertyName", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates a PropertyError with an optional message, target kata, and property name.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(TYPE_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "A value had an unexpected type.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_EXPECTED), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_ACTUAL), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, TypeErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("TypeError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("expected", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("actual", NULL, KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates a TypeError with an optional message, expected type, and actual value.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(OPERAND_ERROR), EXCEPTION_NAME(TYPE_ERROR));
        KTN_NativeKataSetDoc(builder,
            "An operator was applied to operand types it does not support. "
            "`left` and `right` hold the operand type names; unary operators only set `left`.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_OPERATION), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, "left", NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, "right", NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, OperandErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("OperandError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("operation", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("left", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("right", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates an OperandError with an optional message, operator, and operand type names.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(ARGUMENT_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "A function received an invalid argument.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_ARGUMENT_NAME), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_VALUE), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, ArgumentErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("ArgumentError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("name", "String", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("value", NULL, KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates an ArgumentError with an optional message, argument name, and value.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(RANGE_ERROR), EXCEPTION_NAME(ARGUMENT_ERROR));
        KTN_NativeKataSetDoc(builder, "A value was outside the allowed range.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_MINIMUM), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_MAXIMUM), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, RangeErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("RangeError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("value", NULL, KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("minimum", NULL, KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("maximum", NULL, KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates a RangeError with an optional message, value, minimum, and maximum.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(INDEX_ERROR), EXCEPTION_NAME(RANGE_ERROR));
        KTN_NativeKataSetDoc(builder, "An index was outside the bounds of a collection.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_INDEX), NULL_VALUE, false, false);
        KTN_NativeKataAddProperty(builder, "length", NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, IndexErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("IndexError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("index", "Int", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_PARAM_DEFAULT("length", "Int", KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates an IndexError with an optional message, index, and length.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

    {
        KTN_NativeKataBuilder* builder = KTN_NativeKataBegin(vm, EXCEPTION_NAME(KEY_ERROR), EXCEPTION_NAME(ERROR));
        KTN_NativeKataSetDoc(builder, "A required key was not found.");
        KTN_NativeKataAddProperty(builder, KTN_NAMEC(vm, KTN_NAME_KEY), NULL_VALUE, false, false);
        KTN_NativeKataSetConstructor(builder, KeyErrorConstructor,
            KTN_NativeBuiltinToSignature(vm, KTN_SIGNATURE("KeyError",
                KTN_PARAM_DEFAULT("message", "String", KTN_PARAM_POSITIONAL, NULL_VALUE),
                KTN_PARAM_DEFAULT("key", NULL, KTN_PARAM_NAMED, NULL_VALUE),
                KTN_DOCS("Creates a KeyError with an optional message and key.")
            )),
            false
        );
        KTN_NativeKataEnd(builder);
    }

#ifndef DEBUG_MODE
    for (int i = 0; i < KTN_EXC_COUNT; i++)
        assert(LookupExceptionClass(vm, (KTN_ExceptionType)i) != NULL);
#endif
}
