#ifndef KATANE_NATIVE_H
#define KATANE_NATIVE_H

#include "Value.h"
#include "Memory.h"
#include "Object.h"
#include "Utilities.h"

#define GET_NATIVE(name)        NativeFn##name
#define GET_METHOD(name)        NativeMethod##name
#define GET_MODULE_METHOD(name) NativeModule##name

#define DECLARE_NATIVE(name)        KTN_NativeResult NativeFn##name(KTN_VM* vm, KTN_CallArgs arguments)
#define DECLARE_METHOD(name)        KTN_NativeResult NativeMethod##name(KTN_VM* vm, KTN_CallArgs arguments)
#define DECLARE_MODULE_METHOD(name) KTN_NativeResult NativeModule##name(KTN_VM* vm, KTN_CallArgs arguments)

#define RETURN_NULL          return (KTN_NativeResult){true, NULL_VALUE}
#define RETURN_BOOL(value)   return (KTN_NativeResult){true, BOOL_VALUE(value)}
#define RETURN_INT(value)    return (KTN_NativeResult){true, INT_VALUE(value)}
#define RETURN_NUMBER(value) return (KTN_NativeResult){true, DOUBLE_VALUE(value)}
#define RETURN_OBJECT(value) return (KTN_NativeResult){true, OBJECT_VALUE(value)}
#define RETURN_STRING(value) return (KTN_NativeResult){true, OBJECT_VALUE(StringCopy(vm, value, (int)strlen(value)))}
#define RETURN_VALUE(value)  return (KTN_NativeResult){true, value}
#define RETURN_ERROR(value)  return (KTN_NativeResult){false, value}

#define ARGUMENT_COUNT      (arguments.positionalCount)
#define ARG(i)              (arguments.positional[i])
#define THIS                (arguments.positional[0])
#define VARIADIC_COUNT      (arguments.variadicCount)
#define VARIADIC(i)         (arguments.variadic[i])

#define KTN_SIGNATURE(signature) signature 
#define KTN_DOC(docs) docs 

#define EXPECT_ARGC(count) do {                             \
        if (arguments.positionalCount != (count)) {         \
            ThrowException(                                 \
                vm,                                         \
                "ArgumentError",                            \
                false,                                      \
                "Expected %d %s but got %d.",               \
                ((count)),                                  \
                ((count) == 1) ? "argument" : "arguments",  \
                arguments.positionalCount                   \
            );                                              \
            RETURN_ERROR(NULL_VALUE);                       \
        }                                                   \
    } while (0);                                            \

#define EXPECT_MAX_ARGC(count) do {                             \
        if (arguments.positionalCount > (count)) {              \
            ThrowException(                                      \
                vm,                                             \
                "ArgumentError",                                \
                false,                                          \
                "Expected at most %d %s but got %d.",          \
                ((count)),                                      \
                ((count) == 1) ? "argument" : "arguments",      \
                arguments.positionalCount                       \
            );                                                  \
            RETURN_ERROR(NULL_VALUE);                           \
        }                                                       \
    } while (0)

#define EXPECT_MIN_ARGC(count) do {                             \
        if (arguments.positionalCount < (count)) {              \
            ThrowException(                                      \
                vm,                                             \
                "ArgumentError",                                \
                false,                                          \
                "Expected at least %d %s but got %d.",          \
                ((count)),                                      \
                ((count) == 1) ? "argument" : "arguments",      \
                arguments.positionalCount                       \
            );                                                  \
            RETURN_ERROR(NULL_VALUE);                           \
        }                                                       \
    } while (0)

#define EXPECT_ARG_TYPE(index, checkMacro, typeName) do {       \
        if (!(checkMacro(arguments.positional[(index)]))) {      \
            ThrowException(                                      \
                vm,                                             \
                "TypeError",                                    \
                false,                                          \
                "Argument %d: expected %s.",                    \
                (index),                                        \
                (typeName)                                      \
            );                                                  \
            RETURN_ERROR(NULL_VALUE);                           \
        }                                                       \
    } while (0)

#define EXPECT_ARG_INT(index)       EXPECT_ARG_TYPE(index, IS_INT,    "Int")
#define EXPECT_ARG_FLOAT(index)     EXPECT_ARG_TYPE(index, IS_DOUBLE, "Float")
#define EXPECT_ARG_STRING(index)    EXPECT_ARG_TYPE(index, IS_STRING, "String")
#define EXPECT_ARG_ARRAY(index)     EXPECT_ARG_TYPE(index, IS_ARRAY,  "Array")
#define EXPECT_ARG_BOOL(index)      EXPECT_ARG_TYPE(index, IS_BOOL,   "Bool")
#define EXPECT_ARG_ARRAY(index)     EXPECT_ARG_TYPE(index, IS_ARRAY,  "Array")
#define EXPECT_ARG_MAP(index)       EXPECT_ARG_TYPE(index, IS_MAP,    "Map")
#define EXPECT_ARG_KATA(index)      EXPECT_ARG_TYPE(index, IS_CLASS,  "Kata")

// Native class builder — allows defining classes with native constructors and methods
// without hand-writing bytecode.
typedef struct {
    KTN_VM* vm;
    KTN_ObjKata* builtClass;
} KTN_NativeKataBuilder;

// Begin building a native class. superclassName may be NULL (inherits nothing) or
// the name of any class already registered in vm->globals.
KTN_NativeKataBuilder* KTN_BeginNativeClass(KTN_VM* vm, const char* name, const char* superclassName);

// Finalize: register the class in vm->globals and return it.
KTN_ObjKata* KTN_EndNativeClass(KTN_NativeKataBuilder* builder);

// Add a native method. The function receives (vm, arguments) where arguments[0] is 'this'.
void KTN_AddNativeMethod(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs);

// Set a native constructor. Receives (vm, arguments) where arguments[0] is 'this'.
void KTN_SetNativeConstructor(KTN_NativeKataBuilder* builder, NativeFnEx function, const char* signature, const char* docs);

// Add a getter. Called by OP_GET_PROPERTY; arguments[0] is 'this'.
void KTN_AddNativeGetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs);

// Add a setter. Called by OP_SET_PROPERTY; arguments[0] is 'this', arguments[1] is the new value.
void KTN_AddNativeSetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs);

// Add a static method. Stored in kata->staticMethods.
void KTN_AddNativeStaticMethod(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs);

// Add a static getter. Stored in kata->staticMethods as an accessor with getter.
void KTN_AddNativeStaticGetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs);

// Add a static getter. Stored in kata->staticMethods as an accessor with setter.
void KTN_AddNativeStaticSetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs);

// Add a static constant property. Stored in kata->staticProperties.
void KTN_AddNativeStaticProperty(KTN_NativeKataBuilder* builder, const char* name, KTN_Value defaultValue);

// Add an instance property with a default value.
void KTN_AddNativeProperty(KTN_NativeKataBuilder* builder, const char* name, KTN_Value defaultValue);

// Set documentation for the class itself.
void KTN_SetClassDoc(KTN_NativeKataBuilder* builder, const char* docs);

// Mark a member as private (adds to kata->privateMembers).
void KTN_MarkNativePrivate(KTN_NativeKataBuilder* builder, const char* name);

// Mark the constructor as private.
void KTN_SetPrivateConstructor(KTN_NativeKataBuilder* builder);

// Set the optional generics flag (for Array, Map — allows call without type args).
void KTN_SetOptionalGenerics(KTN_NativeKataBuilder* builder);

// Add a named type parameter (for optional-generic classes).
void KTN_AddTypeParameter(KTN_NativeKataBuilder* builder, const char* parameterName);

#endif
