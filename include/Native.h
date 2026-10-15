#ifndef KATANE_NATIVE_H
#define KATANE_NATIVE_H

#include "Value.h"
#include "Memory.h"
#include "Object.h"
#include "Utilities.h"
#include "VM.h"
#include "Exceptions.h"
#include "Messages.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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
#define RETURN_STRING(value) return (KTN_NativeResult){true, STRING_VALUE(value)}
#define RETURN_VALUE(value)  return (KTN_NativeResult){true, value}
#define RETURN_ERROR(value)  return (KTN_NativeResult){false, value}

#define ARGUMENT_COUNT      (arguments.positionalCount)
#define ARG(i)              (arguments.positional[i])
#define THIS                (arguments.positional[0])
#define VARIADIC_COUNT      (arguments.variadicCount)
#define VARIADIC(i)         (arguments.variadic[i])
#define NAMED_COUNT         (arguments.namedCount)
#define NAMED(name)         (NativeCallArgsFindNamed(&arguments, name))

#define KTN_ITEM(...)           ((KTN_NativeSignatureItem){ __VA_ARGS__ })
#define KTN_SIG_NAME(_name)     KTN_ITEM(.item = KTN_ITEM_NAME, .name = (_name))
#define KTN_DOCS(_docs)         KTN_ITEM(.item = KTN_ITEM_DOCS, .name = (_docs))
#define KTN_RETURNS(_type)      KTN_ITEM(.item = KTN_ITEM_RETURNS, .type = (_type))
#define KTN_SHIKI_TYPE(_type)   KTN_ITEM(.item = KTN_ITEM_SHIKI_TYPE, .shikiType = (_type))

#define KTN_PARAM(_name, _type, _kind) KTN_ITEM( \
    .item = KTN_ITEM_PARAMETER,     \
    .name = (_name),                \
    .type = (_type),                \
    .kind = (_kind),                \
    .defaultValue = EMPTY_VALUE,    \
    .defaultIsImmutable = true)

#define KTN_PARAM_DEFAULT(_name, _type, _kind, _default) KTN_ITEM( \
    .item = KTN_ITEM_PARAMETER,         \
    .name = (_name),                    \
    .type = (_type),                    \
    .kind = (_kind),                    \
    .defaultValue = (_default),         \
    .defaultIsImmutable = false)

#define KTN_END KTN_ITEM(.item = KTN_ITEM_END)

#define KTN_SIGNATURE(_name, ...) \
    ((const KTN_NativeSignatureItem[]){ \
        KTN_SIG_NAME(_name), \
        __VA_ARGS__, \
        KTN_END \
    })

#define EXPECT_ARGC(count) do {                             \
        if (arguments.positionalCount != (count)) {         \
            KATANE_ARGUMENT_ERROR(                          \
                NULL,                                       \
                KTN_MSG_ARGC_EXACT,                         \
                ((count)),                                  \
                ((count) == 1) ? "argument" : "arguments",  \
                arguments.positionalCount                   \
            );                                              \
            RETURN_ERROR(NULL_VALUE);                       \
        }                                                   \
    } while (0);                                            \

#define EXPECT_MAX_ARGC(count) do {                             \
        if (arguments.positionalCount > (count)) {              \
            KATANE_ARGUMENT_ERROR(                              \
                NULL,                                           \
                KTN_MSG_ARGC_MAX,                               \
                ((count)),                                      \
                ((count) == 1) ? "argument" : "arguments",      \
                arguments.positionalCount                       \
            );                                                  \
            RETURN_ERROR(NULL_VALUE);                           \
        }                                                       \
    } while (0)

#define EXPECT_MIN_ARGC(count) do {                             \
        if (arguments.positionalCount < (count)) {              \
            KATANE_ARGUMENT_ERROR(                              \
                NULL,                                           \
                KTN_MSG_ARGC_MIN,                               \
                ((count)),                                      \
                ((count) == 1) ? "argument" : "arguments",      \
                arguments.positionalCount                       \
            );                                                  \
            RETURN_ERROR(NULL_VALUE);                           \
        }                                                       \
    } while (0)

#define EXPECT_ARG_TYPE(index, checkMacro, typeName) do {       \
        if (!(checkMacro(arguments.positional[(index)]))) {      \
            char argumentContext_[32];                           \
            snprintf(argumentContext_, sizeof(argumentContext_), KTN_CTX_POSITIONAL_ARGUMENT, (int)(index)); \
            KTN_ThrowTypeError(                                  \
                vm,                                              \
                (typeName),                                      \
                arguments.positional[(index)],                   \
                argumentContext_                                 \
            );                                                   \
            RETURN_ERROR(NULL_VALUE);                            \
        }                                                        \
    } while (0)

#define EXPECT_ARG_INT(index)       EXPECT_ARG_TYPE(index, IS_INT,    "Int")
#define EXPECT_ARG_FLOAT(index)     EXPECT_ARG_TYPE(index, IS_DOUBLE, "Float")
#define EXPECT_ARG_STRING(index)    EXPECT_ARG_TYPE(index, IS_STRING, "String")
#define EXPECT_ARG_BOOL(index)      EXPECT_ARG_TYPE(index, IS_BOOL,   "Bool")
#define EXPECT_ARG_ARRAY(index)     EXPECT_ARG_TYPE(index, IS_ARRAY,  "Array")
#define EXPECT_ARG_MAP(index)       EXPECT_ARG_TYPE(index, IS_MAP,    "Map")
#define EXPECT_ARG_KATA(index)      EXPECT_ARG_TYPE(index, IS_CLASS,  "Kata")


/// Native kata builder. Allows you to write Katane katas from C code.
/// Katas can then be registered for use in the VM or in a module.
typedef struct {
    KTN_VM* vm;
    KTN_ObjKata* builtClass;
} KTN_NativeKataBuilder;

typedef enum {
    KTN_ITEM_END,
    KTN_ITEM_NAME,
    KTN_ITEM_DOCS,
    KTN_ITEM_RETURNS,
    KTN_ITEM_PARAMETER,
    KTN_ITEM_SHIKI_TYPE
} KTN_NativeItemType;

typedef struct {
    const char* name;
    const char* type;
    KTN_NativeItemType item;
    KTN_ParameterKind kind;
    KTN_Value defaultValue;
    bool defaultIsImmutable;
    KTN_ShikiType shikiType;
} KTN_NativeSignatureItem;

/// Creates a proper ObjSignature from a NativeSignature. 
/// You are not meant to use this directly. Use KTN_SIGNATURE instead.
KTN_ObjSignature* KTN_NativeBuiltinToSignature(KTN_VM* vm, const KTN_NativeSignatureItem* items);
KTN_NativeKataBuilder* KTN_NativeKataBegin(KTN_VM* vm, const char* name, const char* superclassName);
KTN_ObjKata* KTN_NativeKataEnd(KTN_NativeKataBuilder* builder);
void KTN_NativeKataAddMethod(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate, bool isStatic);
void KTN_NativeKataSetConstructor(KTN_NativeKataBuilder* builder, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate);
void KTN_NativeKataAddGetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate, bool isStatic);
void KTN_NativeKataAddSetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate, bool isStatic);
void KTN_NativeKataAddProperty(KTN_NativeKataBuilder* builder, const char* name, KTN_Value defaultValue, bool isPrivate, bool isStatic);
void KTN_NativeKataSetDoc(KTN_NativeKataBuilder* builder, const char* docs);
void KTN_NativeKataSetGenerics(KTN_NativeKataBuilder* builder);
void KTN_NativeKataAddType(KTN_NativeKataBuilder* builder, const char* parameterName);

static KTN_Value NativeCallArgsFindNamed(KTN_CallArgs* args, const char* name) {
    if (args->named == NULL) return EMPTY_VALUE;
    if (args->signature == NULL) return EMPTY_VALUE;

    KTN_Value* values = args->named;
    KTN_ObjSignature* signature = args->signature;

    int namedStart = signature->namedStart;
    int parameterCount = signature->parameterCount;
    
    uint32_t targetHash = StringHash(name, (int)strlen(name));
    for (int i = namedStart; i < parameterCount; i++) {
        KTN_ObjString* paramName = signature->parameters[i].name;

        if (paramName->hash != targetHash) continue;
        if (memcmp(paramName->chars, name, paramName->length) != 0) continue;
        
        return values[i - namedStart];
    }

    return EMPTY_VALUE;
}

#endif
