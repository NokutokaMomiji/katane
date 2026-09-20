#include "Exceptions.h"
#include "Array.h"
#include "Map.h"
#include "Native.h"
#include "Primitives.h"

static KTN_ObjKata* ExceptionGet(KTN_VM* vm, const char* name) {
    if (name == NULL) {
        return vm->exceptionClass;
    }

    KTN_Value exceptionClass;
    if (TableGet(&vm->globals, STRING_COPY(name), &exceptionClass) && IS_CLASS(exceptionClass)) {
        return AS_CLASS(exceptionClass);
    }

    return vm->exceptionClass;
}

KTN_ObjInstance* KTN_ExceptionCreate(KTN_VM* vm, const char* type, KTN_ObjString* message) {
    KTN_ObjInstance* instance = InstanceNew(vm, ExceptionGet(vm, type));
    Push(vm, OBJECT_VALUE(instance));
    TableSet(vm, &instance->properties, KTN_NAME(vm, KTN_NAME_MESSAGE), OBJECT_VALUE(message));
    Pop(vm);
    return instance;
}

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

KTN_Value BuildStackTraceObject(KTN_VM* vm, KTN_ObjInstance* stackTraceInstance) {
    KTN_ObjArray* frames = ArrayNew(vm);
    Push(vm, OBJECT_VALUE(frames));

    for (int frameIndex = vm->frameCount - 1; frameIndex >= 0; frameIndex--) {
        KTN_CallFrame* frame = &vm->frames[frameIndex];
        KTN_ObjShiki* function = frame->closure->function;
        size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);

        int line = KTN_ChunkGetLine(&function->chunk, (int)instruction);
        char* content = KTN_ChunkGetSource(&function->chunk, (int)instruction);

        KTN_ObjMap* frameMap = MapNew(vm);
        Push(vm, OBJECT_VALUE(frameMap));

        Push(vm, STRING_VALUE("line"));
        MapSet(vm, frameMap, Peek(vm, 0), INT_VALUE(line));
        Pop(vm);

        Push(vm, STRING_VALUE("source"));
        MapSet(vm, frameMap, Peek(vm, 0), content ? STRING_VALUE(content) : NULL_VALUE);
        Pop(vm);

        Push(vm, STRING_VALUE("functionName"));
        KTN_Value functionNameValue = (function->name != NULL) ? STRING_VALUE(function->name->chars) : STRING_VALUE((function->type == TYPE_LAMBDA) ? "<anonymous>" : "<script>");
        MapSet(vm, frameMap, Peek(vm, 0), functionNameValue);
        Pop(vm);

        Push(vm, STRING_VALUE("file"));
        KTN_Value fileValue = (function->module && function->module->file) ? STRING_VALUE(function->module->file) : NULL_VALUE;
        MapSet(vm, frameMap, Peek(vm, 0), fileValue);
        Pop(vm);

        KTN_ArraySet(vm, frames, INT_VALUE(frames->items.count), OBJECT_VALUE(frameMap));
        Pop(vm);
    }

    if (stackTraceInstance != NULL) {
        TableSet(vm, &stackTraceInstance->properties, STRING_COPY("frames"), Peek(vm, 0));
        Pop(vm);
        return OBJECT_VALUE(stackTraceInstance);
    }

    if (vm->stackTraceClass != NULL) {
        KTN_ObjInstance* traceInstance = InstanceNew(vm, vm->stackTraceClass);
        Push(vm, OBJECT_VALUE(traceInstance));
        TableSet(vm, &traceInstance->properties, STRING_COPY("frames"), Peek(vm, 1));
        KTN_Value result = Peek(vm, 0);
        PopN(vm, 2);
        return result;
    }

    return Pop(vm);
}

// Exception(message)
static KTN_NativeResult ExceptionConstructorNative(KTN_VM* vm, KTN_CallArgs arguments) {
    EXPECT_ARGC(1);
    EXPECT_ARG_STRING(0);

    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1))) {
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    }

    RETURN_VALUE(ARG(0));
}

// StackTrace()
static KTN_NativeResult StackTraceConstructorNative(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    (void)BuildStackTraceObject(vm, self);
    RETURN_VALUE(ARG(0));
}

// TypeError(message, expected, actual)
static KTN_NativeResult TypeErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    EXPECT_ARGC(4);
    EXPECT_ARG_STRING(1);

    KTN_ObjInstance* self = AS_INSTANCE(THIS);

    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));

    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("expected"), ARG(2));

    if (ARGUMENT_COUNT >= 4)
        TableSet(vm, &self->properties, STRING_COPY("actual"), ARG(3));

    RETURN_VALUE(ARG(0));
}

// ValueError(message, value)
static KTN_NativeResult ValueErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    EXPECT_ARGC(2);
    EXPECT_ARG_STRING(1);

    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("value"), ARG(2));
    RETURN_VALUE(ARG(0));
}

// RangeError(message, value, {minimum?, maximum?})
static KTN_NativeResult RangeErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    EXPECT_MAX_ARGC(4);
    EXPECT_ARG_STRING(1);

    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("value"), ARG(2));
    if (ARGUMENT_COUNT >= 4)
        TableSet(vm, &self->properties, STRING_COPY("minimum"), ARG(3));
    if (ARGUMENT_COUNT >= 5)
        TableSet(vm, &self->properties, STRING_COPY("maximum"), ARG(4));
    RETURN_VALUE(ARG(0));
}

// ArgumentError(message, argumentName, value)
static KTN_NativeResult ArgumentErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("argumentName"), ARG(2));
    if (ARGUMENT_COUNT >= 4)
        TableSet(vm, &self->properties, STRING_COPY("value"), ARG(3));
    RETURN_VALUE(ARG(0));
}

// NumericError(message, operation?)
static KTN_NativeResult NumericErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("operation"), ARG(2));
    RETURN_VALUE(ARG(0));
}

// AccessError(message, target)
static KTN_NativeResult AccessErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("target"), ARG(2));
    RETURN_VALUE(ARG(0));
}

// PropertyError(message, propertyName, target)
static KTN_NativeResult PropertyErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("propertyName"), ARG(2));
    if (ARGUMENT_COUNT >= 4)
        TableSet(vm, &self->properties, STRING_COPY("target"), ARG(3));
    RETURN_VALUE(ARG(0));
}

// UndefinedError(message, name)
static KTN_NativeResult UndefinedErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("name"), ARG(2));
    RETURN_VALUE(ARG(0));
}

// KeyError(message, key)
static KTN_NativeResult KeyErrorConstructor(KTN_VM* vm, KTN_CallArgs arguments) {
    KTN_ObjInstance* self = AS_INSTANCE(THIS);
    if (ARGUMENT_COUNT >= 2 && IS_STRING(ARG(1)))
        TableSet(vm, &self->properties, STRING_COPY("message"), ARG(1));
    if (ARGUMENT_COUNT >= 3)
        TableSet(vm, &self->properties, STRING_COPY("key"), ARG(2));
    RETURN_VALUE(ARG(0));
}

void KTN_InitializeExceptions(KTN_VM* vm, KTN_ObjModule* module) {
    (void)module;

    // Exception base kata
    {
        KTN_NativeKataBuilder* builder = KTN_BeginNativeClass(vm, "Exception", NULL);
        KTN_SetClassDoc(builder, "Base class for all exceptions.");
        KTN_AddNativeProperty(builder, KTN_NAMEC(vm, KTN_NAME_MESSAGE), NULL_VALUE);
        KTN_AddNativeProperty(builder, KTN_NAMEC(vm, KTN_NAME_STACK_TRACE), NULL_VALUE);
        KTN_AddNativeProperty(builder, KTN_NAMEC(vm, KTN_NAME_TYPE), NULL_VALUE);
        KTN_AddNativeProperty(builder, KTN_NAMEC(vm, KTN_NAME_SUPPRESSED_ERRORS), NULL_VALUE);
        KTN_SetNativeConstructor(builder, ExceptionConstructorNative, "Exception(message?)", "Creates a new Exception with optional message.");
        vm->exceptionClass = KTN_EndNativeClass(builder);
    }

    // StackTrace helper kata
    {
        KTN_NativeKataBuilder* builder = KTN_BeginNativeClass(vm, "StackTrace", NULL);
        KTN_SetClassDoc(builder, "Represents a stack trace captured at the point of error.");
        KTN_AddNativeProperty(builder, KTN_NAMEC(vm, KTN_NAME_FRAMES), NULL_VALUE);
        KTN_SetNativeConstructor(builder, StackTraceConstructorNative, "StackTrace()", "Captures the current call stack.");
        vm->stackTraceClass = KTN_EndNativeClass(builder);
    }

    KTN_InitializePrimitives(vm, module);

    // RuntimeError(message)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "RuntimeError", "Exception");
        KTN_SetClassDoc(b, "Runtime error occurred during execution.");
        KTN_SetNativeConstructor(b, ExceptionConstructorNative, "RuntimeError(message?)", "Creates a RuntimeError with optional message.");
        KTN_EndNativeClass(b);
    }

    // NotImplementedError(message)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "NotImplementedError", "Exception");
        KTN_SetClassDoc(b, "Operation not implemented.");
        KTN_SetNativeConstructor(b, ExceptionConstructorNative, "NotImplementedError(message?)", "Creates a NotImplementedError with optional message.");
        KTN_EndNativeClass(b);
    }

    // AssertionError(message)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "AssertionError", "Exception");
        KTN_SetClassDoc(b, "Assertion failed.");
        KTN_SetNativeConstructor(b, ExceptionConstructorNative, "AssertionError(message?)", "Creates an AssertionError with optional message.");
        KTN_EndNativeClass(b);
    }

    // StateError(message)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "StateError", "Exception");
        KTN_SetClassDoc(b, "Invalid state for operation.");
        KTN_SetNativeConstructor(b, ExceptionConstructorNative, "StateError(message?)", "Creates a StateError with optional message.");
        KTN_EndNativeClass(b);
    }

    // TypeError(message, expected?, actual?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "TypeError", "Exception");
        KTN_SetClassDoc(b, "Type mismatch error.");
        KTN_AddNativeProperty(b, "expected", NULL_VALUE);
        KTN_AddNativeProperty(b, "actual", NULL_VALUE);
        KTN_SetNativeConstructor(b, TypeErrorConstructor, "TypeError(message?, expected?, actual?)", "Creates a TypeError with optional message, expected type, and actual value.");
        KTN_EndNativeClass(b);
    }

    // ValueError(message, value?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "ValueError", "Exception");
        KTN_SetClassDoc(b, "Invalid value error.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_VALUE), NULL_VALUE);
        KTN_SetNativeConstructor(b, ValueErrorConstructor, "ValueError(message?, value?)", "Creates a ValueError with optional message and offending value.");
        KTN_EndNativeClass(b);
    }

    // RangeError(message, value?, minimum?, maximum?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "RangeError", "Exception");
        KTN_SetClassDoc(b, "Value out of range.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_VALUE), NULL_VALUE);
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_MINIMUM), NULL_VALUE);
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_MAXIMUM), NULL_VALUE);
        KTN_SetNativeConstructor(b, RangeErrorConstructor, "RangeError(message?, value?, minimum?, maximum?)", "Creates a RangeError with optional message, value, minimum, and maximum.");
        KTN_EndNativeClass(b);
    }

    // ArgumentError(message, argumentName?, value?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "ArgumentError", "Exception");
        KTN_SetClassDoc(b, "Invalid argument.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_ARGUMENT_NAME), NULL_VALUE);
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_VALUE), NULL_VALUE);
        KTN_SetNativeConstructor(b, ArgumentErrorConstructor, "ArgumentError(message?, argumentName?, value?)", "Creates an ArgumentError with optional message, argument name, and value.");
        KTN_EndNativeClass(b);
    }

    // NumericError(message, operation?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "NumericError", "Exception");
        KTN_SetClassDoc(b, "Numeric operation error.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_OPERATION), NULL_VALUE);
        KTN_SetNativeConstructor(b, NumericErrorConstructor, "NumericError(message?, operation?)", "Creates a NumericError with optional message and operation name.");
        KTN_EndNativeClass(b);
    }

    // AccessError(message, target?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "AccessError", "Exception");
        KTN_SetClassDoc(b, "Access denied or invalid.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_TARGET), NULL_VALUE);
        KTN_SetNativeConstructor(b, AccessErrorConstructor, "AccessError(message?, target?)", "Creates an AccessError with optional message and target.");
        KTN_EndNativeClass(b);
    }

    // PropertyError(message, propertyName?, target?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "PropertyError", "Exception");
        KTN_SetClassDoc(b, "Property access error.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_PROPERTY_NAME), NULL_VALUE);
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_TARGET), NULL_VALUE);
        KTN_SetNativeConstructor(b, PropertyErrorConstructor, "PropertyError(message?, propertyName?, target?)", "Creates a PropertyError with optional message, property name, and target.");
        KTN_EndNativeClass(b);
    }

    // UndefinedError(message, name?)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "UndefinedError", "Exception");
        KTN_SetClassDoc(b, "Undefined identifier.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_NAME), NULL_VALUE);
        KTN_SetNativeConstructor(b, UndefinedErrorConstructor, "UndefinedError(message?, name?)", "Creates an UndefinedError with optional message and name.");
        KTN_EndNativeClass(b);
    }

    // KeyError(message)
    {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, "KeyError", "Exception");
        KTN_SetClassDoc(b, "Key not found in collection.");
        KTN_AddNativeProperty(b, KTN_NAMEC(vm, KTN_NAME_KEY), NULL_VALUE);
        KTN_SetNativeConstructor(b, KeyErrorConstructor, "KeyError(message?)", "Creates a KeyError with optional message.");
        KTN_EndNativeClass(b);
    }
}
