#ifndef KATANE_OBJECT_H
#define KATANE_OBJECT_H

#include "Common.h"
#include "Value.h"
#include "Chunk.h"
#include "Table.h"
#include "HashMap.h"

typedef struct KTN_ObjTypeDescriptor KTN_ObjTypeDescriptor;
typedef struct KTN_ObjKata KTN_ObjKata;
typedef struct KTN_ObjSignature KTN_ObjSignature;

#define OBJECT_TYPE(value)     (AS_OBJECT(value)->type)

#define IS_STRING(value)            IsObjectType(value, OBJ_STRING)
#define IS_ARRAY(value)             IsObjectType(value, OBJ_ARRAY)
#define IS_MAP(value)               IsObjectType(value, OBJ_MAP)
#define IS_ENUM(value)              IsObjectType(value, OBJ_ENUM)
#define IS_ENUM_VARIANT(value)      IsObjectType(value, OBJ_ENUM_VARIANT)
#define IS_NATIVE(value)            IsObjectType(value, OBJ_NATIVE)
#define IS_FUNCTION(value)          IsObjectType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)           IsObjectType(value, OBJ_CLOSURE)
#define IS_CLASS(value)             IsObjectType(value, OBJ_CLASS)
#define IS_INSTANCE(value)          IsObjectType(value, OBJ_INSTANCE)
#define IS_ACCESSOR(value)          IsObjectType(value, OBJ_ACCESSOR)
#define IS_BOUND_METHOD(value)      IsObjectType(value, OBJ_BOUND_METHOD)
#define IS_MODULE(value)            IsObjectType(value, OBJ_MODULE)
#define IS_TYPE_DESCRIPTOR(value)   IsObjectType(value, OBJ_TYPE_DESCRIPTOR)
#define IS_SIGNATURE(value)         IsObjectType(value, OBJ_SIGNATURE)

#define AS_STRING(value)            ((KTN_ObjString*)AS_OBJECT(value))
#define AS_CSTRING(value)           (((KTN_ObjString*)AS_OBJECT(value))->chars)
#define AS_ARRAY(value)             ((KTN_ObjArray*)AS_OBJECT(value))
#define AS_MAP(value)               ((KTN_ObjMap*)AS_OBJECT(value))
#define AS_ENUM(value)              ((KTN_ObjEnum*)AS_OBJECT(value))
#define AS_ENUM_VARIANT(value)      ((KTN_ObjEnumVariant*)AS_OBJECT(value))
#define AS_NATIVE(value)            (((KTN_ObjNative*)AS_OBJECT(value)))
#define AS_FUNCTION(value)          ((KTN_ObjShiki*)AS_OBJECT(value))
#define AS_CLOSURE(value)           ((KTN_ObjClosure*)AS_OBJECT(value))
#define AS_CLASS(value)             ((KTN_ObjKata*)AS_OBJECT(value))
#define AS_INSTANCE(value)          ((KTN_ObjInstance*)AS_OBJECT(value))
#define AS_ACCESSOR(value)          ((KTN_ObjAccessor*)AS_OBJECT(value))
#define AS_BOUND_METHOD(value)      ((KTN_ObjBoundMethod*)AS_OBJECT(value))
#define AS_MODULE(value)            ((KTN_ObjModule*)AS_OBJECT(value))
#define AS_TYPE_DESCRIPTOR(value)   ((KTN_ObjTypeDescriptor*)AS_OBJECT(value))
#define AS_SIGNATURE(value)         ((KTN_ObjSignature*)AS_OBJECT(value))

typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_MAP,
    OBJ_ENUM,
    OBJ_ENUM_VARIANT,

    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,

    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_ACCESSOR,
    
    OBJ_TYPE_DESCRIPTOR,
    OBJ_SIGNATURE,

    OBJ_MODULE
} KTN_ObjectType;

struct KTN_Object {
    KTN_ObjectType type;
    bool isMarked;
    struct KTN_Object* next;
};

#define KTN_NAME(vm, id) KTN_GetWellKnownName((vm), (id))
#define KTN_NAMEC(vm, id) (KTN_GetWellKnownName((vm), ((id)))->chars)

typedef enum {
    KTN_NAME_INIT,
    KTN_NAME_TO_STRING,
    KTN_NAME_EQUALS,
    KTN_NAME_HASH_CODE,
    KTN_NAME_COMPARE,
    KTN_NAME_DISPOSE,
    KTN_NAME_SUPPRESSED_ERRORS,
    KTN_NAME_MESSAGE,
    KTN_NAME_STACK_TRACE,
    KTN_NAME_FRAMES,
    KTN_NAME_EXPECTED,
    KTN_NAME_ACTUAL,
    KTN_NAME_VALUE,
    KTN_NAME_VALUES,
    KTN_NAME_NAME,
    KTN_NAME_TYPE,
    KTN_NAME_ORDINAL,
    KTN_NAME_MINIMUM,
    KTN_NAME_MAXIMUM,
    KTN_NAME_ARGUMENT_NAME,
    KTN_NAME_OPERATION,
    KTN_NAME_TARGET,
    KTN_NAME_PROPERTY_NAME,
    KTN_NAME_KEY,
    KTN_NAME_INDEX,
    KTN_NAME_MODULE_NAME,
    KTN_NAME_ITERATOR,
    KTN_NAME_MOVE_NEXT,
    KTN_NAME_CURRENT,
    KTN_NAME_KEYS,
    KTN_NAME_ENTRIES,
    KTN_NAME_CHARS,
    KTN_NAME_BYTES,
    KTN_NAME_RUNES,
    KTN_NAME_NEXT,
    KTN_NAME_DONE,
    KTN_NAME_COUNT
} KTN_WellKnownName;

typedef struct {
    KTN_ObjString* values[KTN_NAME_COUNT];
} KTN_WellKnownNames;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
    TYPE_CONSTRUCTOR,
    TYPE_LAMBDA,
    TYPE_GETTER,
    TYPE_SETTER,
    TYPE_ENTRY
} KTN_ShikiType;

typedef struct KTN_ObjModule {
    KTN_Object object;
    KTN_Table values;

    char* name;
    char* file;

    void* preloader;
    void* unloader;
    void* handle;

    struct KTN_ObjModule* parent;
    bool imported;
    bool isMain;
} KTN_ObjModule;

typedef struct {
    KTN_Object object;
    KTN_ShikiType type;
    int arity;
    int upvalueCount;
    int32_t returnTypeDescriptor;
    KTN_Chunk chunk;
    KTN_ObjString* name;
    KTN_ObjModule* module;
    KTN_ObjString* docs;
    KTN_ObjSignature* signature;
} KTN_ObjShiki;

typedef struct {
    bool success;
    KTN_Value value;  
} KTN_NativeResult;

typedef struct {
    KTN_Value* positional;
    KTN_Value* variadic;
    KTN_Value* named;
    int positionalCount;
    int variadicCount;
    int namedCount;
} KTN_CallArgs;

typedef KTN_NativeResult (*NativeFnEx)(KTN_VM* vm, KTN_CallArgs arguments);

typedef struct {
    KTN_Object object;
    NativeFnEx function;
    KTN_ShikiType type;
    const char* name;
    const char* signature;
    const char* docs;
} KTN_ObjNative;

typedef enum {
    KTN_PARAM_POSITIONAL,
    KTN_PARAM_VARIADIC,
    KTN_PARAM_NAMED
} KTN_ParameterKind;

/// Compiler-safe version of a signature parameter. It later gets converted to a [KTN_SignatureParameter]
typedef struct {
    const char* start;
    int length;
    KTN_ObjTypeDescriptor* type;
    bool hasDefaultValue;
    bool isNamed;
    KTN_ParameterKind kind;
    KTN_Value defaultValue;
    bool defaultIsImmutable;
} KTN_SignatureParameterSpec;

/// Represents a shiki parameter, including its default data. It gets used for recreating the signature as well as
/// enforcing parameter conditions.
typedef struct {
    KTN_ObjString* name;
    KTN_ObjTypeDescriptor* type;
    bool hasDefaultValue;
    bool isNamed;
    KTN_ParameterKind kind;
    KTN_Value defaultValue;
    bool defaultIsImmutable;
} KTN_SignatureParameter;

/// Represents the signature of a shiki. This is used for recreating the signature as well as for calling the function.
typedef struct KTN_ObjSignature {
    KTN_Object object;
    KTN_ObjString* display;
    KTN_ObjString* name;
    KTN_ObjTypeDescriptor* returnType;
    KTN_SignatureParameter* parameters;
    int parameterCount;
    KTN_ShikiType shikiType;
} KTN_ObjSignature;

struct KTN_ObjString {
    KTN_Object object;
    int length;
    int charLength;
    char* chars;
    uint32_t hash;
};

typedef struct {
    KTN_Object object;
    KTN_ValueArray items;
} KTN_ObjArray;

typedef struct {
    KTN_Object object;
    KTN_HashMap map;
} KTN_ObjMap;

typedef struct KTN_ObjEnum {
    KTN_Object object;
    KTN_ObjString* name;
    KTN_Table variants;
    KTN_ObjArray* variantList;
    KTN_Table methods;
    KTN_Table getters;
    KTN_ObjString* docs;
} KTN_ObjEnum;

typedef struct KTN_ObjEnumVariant {
    KTN_Object object;
    KTN_ObjEnum* owner;
    KTN_ObjString* name;
    int32_t ordinal;
    KTN_Value value;
} KTN_ObjEnumVariant;

typedef struct KTN_ObjUpvalue {
    KTN_Object object;
    KTN_Value* location;
    KTN_Value closed;
    struct KTN_ObjUpvalue* next;
} KTN_ObjUpvalue;

typedef struct {
    KTN_Object object;
    KTN_ObjShiki* function;
    KTN_ObjKata* owner;
    KTN_ObjUpvalue** upvalues;
    int upvalueCount;
} KTN_ObjClosure;

typedef struct {
    KTN_ObjString* name;
    KTN_ObjTypeDescriptor* constraint;
} KTN_TypeParameter;

typedef struct {
    KTN_Object object;
    KTN_ObjClosure* getter;
    KTN_ObjClosure* setter;
} KTN_ObjAccessor;

typedef struct KTN_ObjKata {
    KTN_Object object;
    KTN_ObjString* className;
    KTN_Value constructor;
    KTN_ValueArray methodNames;

    KTN_Table methods;
    KTN_Table properties;

    KTN_Table staticMethods;
    KTN_Table staticProperties;

    KTN_Table privateMembers;
    KTN_Table staticPrivateMembers;

    KTN_Table fieldTypes;
    KTN_Table staticFieldTypes;
    KTN_ObjClosure** overloads;

    KTN_ObjString* docs;
    KTN_Table innerDocs;

    KTN_TypeParameter* typeParameters;
    struct KTN_ObjKata* sokata; // Superclass.

    int typeParameterCount;
    uint16_t overloadMask;
    bool hasTypedFields;
    bool privateConstructor;
    bool optionalGenerics;
} KTN_ObjKata;

typedef struct {
    KTN_Object object;
    KTN_ObjKata* kata;
    KTN_ValueArray propertyNames;
    KTN_Table properties;
    KTN_ObjTypeDescriptor** typeArguments;
} KTN_ObjInstance;

typedef struct {
    KTN_Object object;
    KTN_Value receiver;
    KTN_ObjClosure* method;
    KTN_ObjKata* owner;
} KTN_ObjBoundMethod;

typedef enum {
    TD_NAMED,
    TD_UNION,
    TD_PARAMETER,
    TD_TYPE_VARIABLE
} KTN_TypeDescriptorType;

typedef struct KTN_ObjTypeDescriptor {
    KTN_Object object;
    KTN_TypeDescriptorType type;

    uint32_t hash;

    KTN_ObjString* name;
    KTN_ObjKata* resolved;

    struct KTN_ObjTypeDescriptor** arguments;
    uint8_t argumentCount;

    struct KTN_ObjTypeDescriptor* base;
} KTN_ObjTypeDescriptor;

KTN_ObjModule* ModuleNew(KTN_VM* vm, char* name, char* file, KTN_ObjModule* parent);

KTN_ObjClosure* ClosureNew(KTN_VM* vm, KTN_ObjShiki* function);
KTN_ObjShiki* ShikiNew(KTN_VM* vm, KTN_ObjModule* module, KTN_ShikiType type);
KTN_ObjNative* NativeNew(KTN_VM* vm, NativeFnEx function, const char* name, const char* signature, const char* docs);
KTN_ObjSignature* SignatureNew(KTN_VM* vm, KTN_ObjString* display, KTN_ObjString* name, KTN_ObjTypeDescriptor* returnType, const KTN_SignatureParameterSpec* parameters, int parameterCount, KTN_ShikiType shikiType);

KTN_ObjArray* ArrayNew(KTN_VM* vm);
KTN_ObjMap* MapNew(KTN_VM* vm);
KTN_ObjEnum* EnumNew(KTN_VM* vm);
KTN_ObjEnumVariant* EnumVariantNew(KTN_VM*);

KTN_ObjString* StringTake(KTN_VM* vm, char* chars, int length);
KTN_ObjString* StringCopy(KTN_VM* vm, const char* chars, int length);

KTN_ObjUpvalue* UpvalueNew(KTN_VM* vm, KTN_Value* slot);

KTN_ObjKata* KataNew(KTN_VM* vm, KTN_ObjString* name);
KTN_ObjInstance* InstanceNew(KTN_VM* vm, KTN_ObjKata* kataObject);
KTN_ObjBoundMethod* BoundMethodNew(KTN_VM* vm, KTN_Value receiver, KTN_ObjClosure* method, KTN_ObjKata* owner);
KTN_ObjAccessor* AccessorNew(KTN_VM* vm);

KTN_ObjTypeDescriptor* TypeDescriptorNew(KTN_VM* vm);

void KTN_WellKnownNamesInit(KTN_VM* vm, KTN_WellKnownNames* names);
void KTN_WellKnownNamesMark(KTN_VM* vm, KTN_WellKnownNames* names);
KTN_ObjString* KTN_GetWellKnownName(KTN_VM* vm, KTN_WellKnownName id);

void ObjectPrint(KTN_Value value);
void ObjectRepr(KTN_Value value);
KTN_ObjString* ObjectToString(KTN_VM* vm, KTN_Value value);

static inline bool IsObjectType(KTN_Value value, KTN_ObjectType type) {
    return (IS_OBJECT(value) && AS_OBJECT(value)->type == type);
}

// Walk the kata hierarchy to check if instanceKata is the targetKata or a subclass of it.
static inline bool IsInstanceOfKata(KTN_ObjKata* instanceKata, KTN_ObjKata* targetKata) {
    KTN_ObjKata* cursor = instanceKata;
    
    while (cursor != NULL) {
        if (cursor == targetKata)
            return true;
        cursor = cursor->sokata;
    }

    return false;
}

#endif
