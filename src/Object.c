#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Memory.h"
#include "Object.h"
#include "Table.h"
#include "TypeDescriptor.h"
#include "Utilities.h"
#include "Utf8.h"
#include "VM.h"
#include "Value.h"

#define VISITED_MAX 64

typedef struct {
    KTN_Object* items[VISITED_MAX];
    int count;
} VisitedSet;

static KTN_MAYBE_UNUSED const char* KTN_WellKnownNameText[KTN_NAME_COUNT] = {
    "init",
    "toString",
    "equals",
    "hashCode",
    "compare",
    "dispose",
    "suppressedErrors",
    "message",
    "stackTrace",
    "expected",
    "actual",
    "value",
    "values",
    "name",
    "ordinal",
    "minimum",
    "maximum",
    "argumentName",
    "operation",
    "target",
    "propertyName",
    "key",
    "index",
    "moduleName",
    "iterator",
    "moveNext",
    "current",
    "keys",
    "entries",
    "chars",
    "bytes",
    "runes",
    "next",
    "done"
};

static bool VisitedContains(VisitedSet* vs, KTN_Object* obj) {
    for (int i = 0; i < vs->count; i++)
        if (vs->items[i] == obj)
            return true;
    return false;
}

static void VisitedPush(VisitedSet* vs, KTN_Object* obj) {
    if (vs->count < VISITED_MAX)
        vs->items[vs->count++] = obj;
}

static void VisitedPop(VisitedSet* vs) {
    if (vs->count > 0)
        vs->count--;
}

static void ValueStringify(StringBuilder* sb, KTN_Value value, VisitedSet* visited);

static void FunctionStringify(StringBuilder* sb, KTN_ObjShiki* function) {
    if (function->name == NULL) {
        SBAppendCStr(sb, "<script>");
        return;
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "<function %s at 0x%p>", function->name->chars, (void*)function);
    SBAppendCStr(sb, buffer);
}

static void StringRepr(StringBuilder* sb, KTN_ObjString* string) {
    SBAppendCStr(sb, "\"");

    for (int i = 0; i < string->length;) {
        if (i >= 20) {
            SBAppendCStr(sb, "...");
            break;
        }

        Utf8Char chr = DecodeUtf8(string->chars, string->length, i);
        uint32_t codepoint = chr.codepoint;

        switch (codepoint) {
            case '"':
                SBAppendCStr(sb, "\\\"");
                i += chr.length;
                continue;
            case '\\':
                SBAppendCStr(sb, "\\\\");
                i += chr.length;
                continue;
            case '\n':
                SBAppendCStr(sb, "\\n");
                i += chr.length;
                continue;
            case '\r':
                SBAppendCStr(sb, "\\r");
                i += chr.length;
                continue;
            case '\t':
                SBAppendCStr(sb, "\\t");
                i += chr.length;
                continue;
            case '\a':
                SBAppendCStr(sb, "\\a");
                i += chr.length;
                continue;
            case '\b':
                SBAppendCStr(sb, "\\b");
                i += chr.length;
                continue;
            case '\f':
                SBAppendCStr(sb, "\\f");
                i += chr.length;
                continue;
            case '\v':
                SBAppendCStr(sb, "\\v");
                i += chr.length;
                continue;
            case '\0':
                SBAppendCStr(sb, "\\0");
                i += chr.length;
                continue;
            default:
                break;
        }

        if (codepoint >= 0x20 && codepoint < 0x7F) {
            SBAppend(sb, string->chars + i, chr.length);
            i += chr.length;
            continue;
        }

        char buffer[12];

        if (codepoint < 0x100) {
            snprintf(buffer, sizeof(buffer), "\\x%02x", codepoint);
            SBAppendCStr(sb, buffer);
            i += chr.length;
            continue;
        }

        if (codepoint < 0x10000) {
            snprintf(buffer, sizeof(buffer), "\\u%04x", codepoint);
            SBAppendCStr(sb, buffer);
            i += chr.length;
            continue;
        }

        snprintf(buffer, sizeof(buffer), "\\U%08x", codepoint);
        SBAppendCStr(sb, buffer);

        i += chr.length;
    }

    SBAppendCStr(sb, "\"");
}

static void ArrayStringify(StringBuilder* sb, KTN_ObjArray* array, VisitedSet* visited) {
    if (VisitedContains(visited, (KTN_Object*)array)) {
        SBAppendCStr(sb, "[...]");
        return;
    }

    VisitedPush(visited, (KTN_Object*)array);

    SBAppendCStr(sb, "[");

    for (int i = 0; i < array->items.count; i++) {
        KTN_Value value = array->items.values[i];

        if (IS_STRING(value))
            StringRepr(sb, AS_STRING(value));
        else
            ValueStringify(sb, value, visited);

        if (i != array->items.count - 1)
            SBAppendCStr(sb, ", ");
    }

    SBAppendCStr(sb, "]");

    VisitedPop(visited);
}

static void MapStringify(StringBuilder* sb, KTN_ObjMap* map, VisitedSet* visited) {
    if (VisitedContains(visited, (KTN_Object*)map)) {
        SBAppendCStr(sb, "{...}");
        return;
    }

    VisitedPush(visited, (KTN_Object*)map);

    SBAppendCStr(sb, "{");

    KTN_HashMap* hashMap = &map->map;

    int cursor = hashMap->orderHead;
    int i = 0;
    KTN_Value key, item;

    while (KTN_HashMapNextOrdered(hashMap, &cursor, &key, &item)) {
        if (IS_STRING(key))
            StringRepr(sb, AS_STRING(key));
        else
            ValueStringify(sb, key, visited);

        SBAppendCStr(sb, ": ");
        if (IS_STRING(item))
            StringRepr(sb, AS_STRING(item));
        else
            ValueStringify(sb, item, visited);

        if (i != hashMap->count - 1)
            SBAppendCStr(sb, ", ");

        i++;
    }

    SBAppendCStr(sb, "}");

    VisitedPop(visited);
}

static void ObjectRepresentation(StringBuilder* sb, KTN_Value value, VisitedSet* visited) {
    switch (OBJECT_TYPE(value)) {
        case OBJ_STRING:
            //SBAppend(sb, AS_CSTRING(value), AS_STRING(value)->length);
            StringRepr(sb, AS_STRING(value));
            break;

        case OBJ_ARRAY:
            ArrayStringify(sb, AS_ARRAY(value), visited);
            break;

        case OBJ_ENUM: {
            KTN_ObjEnum* _enum = AS_ENUM(value);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<enum \"%s\">",
                    (_enum->name != NULL) ? _enum->name->chars : "?");
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_ENUM_VARIANT: {
            KTN_ObjEnumVariant* variant = AS_ENUM_VARIANT(value);
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<enum variant \"%s\">",
                    (variant->name != NULL) ? variant->name->chars : "?");
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_MAP:
            MapStringify(sb, AS_MAP(value), visited);
            break;

        case OBJ_FUNCTION:
            FunctionStringify(sb, AS_FUNCTION(value));
            break;

        case OBJ_CLOSURE:
            FunctionStringify(sb, AS_CLOSURE(value)->function);
            break;

        case OBJ_BOUND_METHOD:
            FunctionStringify(sb, AS_BOUND_METHOD(value)->method->function);
            break;

        case OBJ_UPVALUE:
            SBAppendCStr(sb, "Upvalue");
            break;

        case OBJ_NATIVE: {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<native function \"%s\">",
                    AS_NATIVE(value)->name);
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_CLASS: {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<kata \"%s\">",
                    AS_CLASS(value)->className->chars);
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_INSTANCE: {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<%s instance at 0x%p>",
                    AS_INSTANCE(value)->kata->className->chars,
                    (void*)AS_INSTANCE(value));
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_MODULE: {
            char buffer[256];
            KTN_ObjModule* module = AS_MODULE(value);
            snprintf(buffer, sizeof(buffer), "<module \"%s\">",
                    module->name ? module->name : "?");
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_ACCESSOR:
            SBAppendCStr(sb, "<accessor>");
            break;

        case OBJ_TYPE_DESCRIPTOR: {
            char descriptorBuffer[256];
            KTN_TypeDescriptorFormat(AS_TYPE_DESCRIPTOR(value), descriptorBuffer, 256);

            char buffer[256 + 16];
            snprintf(buffer, sizeof(buffer), "<type %s>", descriptorBuffer);
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_SIGNATURE: {
            KTN_ObjSignature* signature = AS_SIGNATURE(value);
            if (signature->display != NULL)
                SBAppend(sb, signature->display->chars, signature->display->length);
            else
                SBAppendCStr(sb, "<signature>");
            break;
        }
    }
}

static void ObjectStringify(StringBuilder* sb, KTN_Value value, VisitedSet* visited) {
    switch (OBJECT_TYPE(value)) {
        case OBJ_STRING:
            SBAppend(sb, AS_CSTRING(value), AS_STRING(value)->length);
            break;

        case OBJ_ARRAY:
            ArrayStringify(sb, AS_ARRAY(value), visited);
            break;

        case OBJ_ENUM: {
            KTN_ObjEnum* _enum = AS_ENUM(value);
            if (_enum->name != NULL)
                SBAppend(sb, _enum->name->chars, _enum->name->length);
            else
                SBAppendCStr(sb, "<enum>");
            break;
        }

        case OBJ_ENUM_VARIANT: {
            KTN_ObjEnumVariant* variant = AS_ENUM_VARIANT(value);
            if (variant->name != NULL)
                SBAppend(sb, variant->name->chars, variant->name->length);
            else
                SBAppendCStr(sb, "<enum-variant>");
            break;
        }

        case OBJ_MAP:
            MapStringify(sb, AS_MAP(value), visited);
            break;

        case OBJ_FUNCTION:
            FunctionStringify(sb, AS_FUNCTION(value));
            break;

        case OBJ_CLOSURE:
            FunctionStringify(sb, AS_CLOSURE(value)->function);
            break;

        case OBJ_BOUND_METHOD:
            FunctionStringify(sb, AS_BOUND_METHOD(value)->method->function);
            break;

        case OBJ_UPVALUE:
            SBAppendCStr(sb, "Upvalue");
            break;

        case OBJ_NATIVE: {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<native function \"%s\">",
                    AS_NATIVE(value)->name);
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_CLASS: {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<kata \"%s\">",
                    AS_CLASS(value)->className->chars);
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_INSTANCE: {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "<%s instance at 0x%p>",
                    AS_INSTANCE(value)->kata->className->chars,
                    (void*)AS_INSTANCE(value));
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_MODULE: {
            char buffer[256];
            KTN_ObjModule* module = AS_MODULE(value);
            snprintf(buffer, sizeof(buffer), "<module \"%s\">",
                    module->name ? module->name : "?");
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_ACCESSOR:
            SBAppendCStr(sb, "<accessor>");
            break;

        case OBJ_TYPE_DESCRIPTOR: {
            char descriptorBuffer[256];
            KTN_TypeDescriptorFormat(AS_TYPE_DESCRIPTOR(value), descriptorBuffer, 256);

            char buffer[256 + 16];
            snprintf(buffer, sizeof(buffer), "<type %s>", descriptorBuffer);
            SBAppendCStr(sb, buffer);
            break;
        }

        case OBJ_SIGNATURE: {
            KTN_ObjSignature* signature = AS_SIGNATURE(value);
            if (signature->display != NULL)
                SBAppend(sb, signature->display->chars, signature->display->length);
            else
                SBAppendCStr(sb, "<signature>");
            break;
        }
    }
}

static void ValueRepr(StringBuilder* sb, KTN_Value value, VisitedSet* visited) {
#ifdef NAN_BOXING
    if (IS_EMPTY(value)) {
        SBAppendCStr(sb, "<empty>");
        return;
    }
    
    if (IS_BOOL(value)) {
        SBAppendCStr(sb, AS_BOOL(value) ? "true" : "false");
        return;
    }

    if (IS_NULL(value)) {
        SBAppendCStr(sb, "null");
        return;
    }

    if (IS_INT(value)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", AS_INT(value));
        SBAppendCStr(sb, buffer);
        return;
    }

    if (IS_DOUBLE(value)) {
        char buffer[32];
        double numberValue = AS_DOUBLE(value);

        for (int precision = 6; precision <= 17; precision++) {
        snprintf(buffer, sizeof(buffer), "%.*g", precision, numberValue);
        double parsed;
        if (sscanf(buffer, "%lf", &parsed) == 1 && parsed == numberValue)
            break;
        }

        SBAppendCStr(sb, buffer);
        return;
    }

    if (IS_OBJECT(value)) {
        ObjectRepresentation(sb, value, visited);
    }
#else
    switch (value.type) {
        case VALUE_BOOL:
            SBAppendCStr(sb, AS_BOOL(value) ? "true" : "false");
            break;

        case VALUE_NULL:
            SBAppendCStr(sb, "null");
            break;

        case VALUE_INT: {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", AS_INT(value));
            SBAppendCStr(sb, buffer);
            break;
        }

        case VALUE_NUMBER: {
            char buffer[32];
            double numberValue = AS_DOUBLE(value);

            for (int precision = 6; precision <= 17; precision++) {
            snprintf(buffer, sizeof(buffer), "%.*g", precision, numberValue);
            double parsed;
            if (sscanf(buffer, "%lf", &parsed) == 1 && parsed == numberValue)
                break;
            }

            SBAppendCStr(sb, buffer);
            break;
        }

        case VALUE_OBJECT:
            ObjectRepr(sb, value, visited);
            break;

        default:
            break;
    }
#endif
}

static void ValueStringify(StringBuilder* sb, KTN_Value value, VisitedSet* visited) {
#ifdef NAN_BOXING
    if (IS_EMPTY(value)) {
        SBAppendCStr(sb, "<empty>");
        return;
    }

    if (IS_BOOL(value)) {
        SBAppendCStr(sb, AS_BOOL(value) ? "true" : "false");
        return;
    }

    if (IS_NULL(value)) {
        SBAppendCStr(sb, "null");
        return;
    }

    if (IS_INT(value)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", AS_INT(value));
        SBAppendCStr(sb, buffer);
        return;
    }

    if (IS_DOUBLE(value)) {
        char buffer[32];
        double numberValue = AS_DOUBLE(value);

        for (int precision = 6; precision <= 17; precision++) {
        snprintf(buffer, sizeof(buffer), "%.*g", precision, numberValue);
        double parsed;
        if (sscanf(buffer, "%lf", &parsed) == 1 && parsed == numberValue)
            break;
        }

        SBAppendCStr(sb, buffer);
        return;
    }

    if (IS_OBJECT(value)) {
        ObjectStringify(sb, value, visited);
    }
#else
    switch (value.type) {
    case VALUE_BOOL:
        SBAppendCStr(sb, AS_BOOL(value) ? "true" : "false");
        break;

    case VALUE_NULL:
        SBAppendCStr(sb, "null");
        break;

    case VALUE_INT: {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d", AS_INT(value));
        SBAppendCStr(sb, buffer);
        break;
    }

    case VALUE_NUMBER: {
        char buffer[32];
        double numberValue = AS_DOUBLE(value);

        for (int precision = 6; precision <= 17; precision++) {
        snprintf(buffer, sizeof(buffer), "%.*g", precision, numberValue);
        double parsed;
        if (sscanf(buffer, "%lf", &parsed) == 1 && parsed == numberValue)
            break;
        }

        SBAppendCStr(sb, buffer);
        break;
    }

    case VALUE_OBJECT:
        ObjectStringify(sb, value, visited);
        break;

    default:
        break;
    }
#endif
}

#define ALLOCATE_OBJ(type, objectType) (type*)ObjectAllocate(vm, sizeof(type), objectType)

static KTN_Object* ObjectAllocate(KTN_VM* vm, size_t size, KTN_ObjectType objectType) {
    KTN_Object* object = (KTN_Object*)reallocate(vm, NULL, 0, size);
    object->type = objectType;
    object->isMarked = false;
    object->next = vm->objects;
    vm->objects = object;

#ifdef DEBUG_LOG_GC
    printf("[GC]: 0x%p allocate %zu for %d\n", (void*)object, size, objectType);
#endif

    return object;
}

KTN_ObjModule* ModuleNew(KTN_VM* vm, char* name, char* file, KTN_ObjModule* parent) {
    KTN_ObjModule* module = ALLOCATE_OBJ(KTN_ObjModule, OBJ_MODULE);

    TableInit(&module->values);

    module->name = name;
    module->file = file;
    module->parent = parent;

    module->preloader = NULL;
    module->unloader = NULL;
    module->handle = NULL;
    module->imported = false;

    module->isMain = false;

    return module;
}

KTN_ObjClosure* ClosureNew(KTN_VM* vm, KTN_ObjShiki* function) {
    KTN_ObjUpvalue** upvalues = ALLOCATE(KTN_ObjUpvalue*, function->upvalueCount);

    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    KTN_ObjClosure* closure = ALLOCATE_OBJ(KTN_ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->owner = NULL;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    
    return closure;
}

KTN_ObjShiki* ShikiNew(KTN_VM* vm, KTN_ObjModule* module, KTN_ShikiType type) {
    KTN_ObjShiki* newFunction = ALLOCATE_OBJ(KTN_ObjShiki, OBJ_FUNCTION);

    newFunction->name = NULL;
    newFunction->type = type;
    newFunction->arity = 0;
    newFunction->upvalueCount = 0;
    newFunction->module = module;
    newFunction->returnTypeDescriptor = -1;

    newFunction->docs = NULL;
    newFunction->signature = NULL;

    KTN_ChunkInit(&newFunction->chunk);

    return newFunction;
}

KTN_ObjNative* NativeNew(KTN_VM* vm, NativeFnEx function, const char* name, const char* signature, const char* docs) {
    KTN_ObjNative* newNative = ALLOCATE_OBJ(KTN_ObjNative, OBJ_NATIVE);
    
    newNative->name = name;
    newNative->type = TYPE_FUNCTION;
    newNative->function = function;
    newNative->signature = signature;
    newNative->docs = docs;

    return newNative;
}

KTN_ObjSignature* SignatureNew(KTN_VM* vm, KTN_ObjString* display, KTN_ObjString* name, KTN_ObjTypeDescriptor* returnType, const KTN_SignatureParameterSpec* parameters, int parameterCount, KTN_ShikiType shikiType) {
    KTN_ObjSignature* signature = ALLOCATE_OBJ(KTN_ObjSignature, OBJ_SIGNATURE);
    signature->display = display;
    signature->name = name;
    signature->returnType = returnType;
    signature->parameterCount = parameterCount;
    signature->shikiType = shikiType;
    signature->parameters = NULL;

    Push(vm, OBJECT_VALUE(signature));

    if (parameterCount > 0) {
        signature->parameters = ALLOCATE(KTN_SignatureParameter, parameterCount);

        for (int i = 0; i < parameterCount; i++) {
            signature->parameters[i].name = StringCopy(vm, parameters[i].start, parameters[i].length);
            signature->parameters[i].type = parameters[i].type;
            signature->parameters[i].hasDefaultValue = parameters[i].hasDefaultValue;
            signature->parameters[i].isNamed = parameters[i].isNamed;
            signature->parameters[i].defaultValue = parameters[i].defaultValue;
        }
    }

    Pop(vm);
    return signature;
}

KTN_ObjTypeDescriptor* TypeDescriptorNew(KTN_VM* vm) {
    KTN_ObjTypeDescriptor* newDescriptor = ALLOCATE_OBJ(KTN_ObjTypeDescriptor, OBJ_TYPE_DESCRIPTOR);

    newDescriptor->type = TD_NAMED;
    newDescriptor->hash = 0;
    newDescriptor->name = NULL;
    newDescriptor->resolved = NULL;
    newDescriptor->arguments = NULL;
    newDescriptor->argumentCount = 0;
    newDescriptor->base = NULL;

    return newDescriptor;
}

static KTN_ObjString* StringAllocate(KTN_VM* vm, char* chars, int length, uint32_t hash) {
    KTN_ObjString* string = ALLOCATE_OBJ(KTN_ObjString, OBJ_STRING);
    string->length = length;
    string->charLength = Utf8StrnCpLen(chars, length); // codepoint count
    string->chars = chars;
    string->hash = hash;

    Push(vm, OBJECT_VALUE(string));
    TableSet(vm, &vm->strings, string, NULL_VALUE);
    Pop(vm);

    return string;
}

//  FNV-1a Hashing.
static uint32_t StringHash(const char* key, int length) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }

    hash ^= hash >> 16;
    hash *= 0x21f0aaadU;
    hash ^= hash >> 15;
    hash *= 0x735a2d97U;
    hash ^= hash >> 15;

    return hash;
}

KTN_ObjString* StringTake(KTN_VM* vm, char* chars, int length) {
    uint32_t hash = StringHash(chars, length);
    KTN_ObjString* interned = TableFindString(&vm->strings, chars, length, hash);
    
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return StringAllocate(vm, chars, length, hash);
}

KTN_ObjString* StringCopy(KTN_VM* vm, const char* chars, int length) {
    uint32_t hash = StringHash(chars, length);
    KTN_ObjString* interned = TableFindString(&vm->strings, chars, length, hash);
    if (interned != NULL)
        return interned;

    char* heapChars = ALLOCATE(char, length + 1);

    // To make sure that ObjString does own its character array.
    memcpy(heapChars, chars, length);

    heapChars[length] = '\0'; // Because we allocated length + 1, length is the last index.
    return StringAllocate(vm, heapChars, length, hash);
}

KTN_ObjUpvalue* UpvalueNew(KTN_VM* vm, KTN_Value* slot) {
    KTN_ObjUpvalue* Upvalue = ALLOCATE_OBJ(KTN_ObjUpvalue, OBJ_UPVALUE);
    Upvalue->location = slot;
    Upvalue->next = NULL;
    Upvalue->closed = NULL_VALUE;
    return Upvalue;
}

KTN_ObjArray* ArrayNew(KTN_VM* vm) {
    KTN_ObjArray* array = ALLOCATE_OBJ(KTN_ObjArray, OBJ_ARRAY);
    ValueArrayInit(&array->items);
    return array;
}

KTN_ObjMap* MapNew(KTN_VM* vm) {
    KTN_ObjMap* map = ALLOCATE_OBJ(KTN_ObjMap, OBJ_MAP);

    KTN_HashMapInit(&map->map);

    return map;
}

KTN_ObjEnum* EnumNew(KTN_VM* vm) {
    KTN_ObjEnum* _enum = ALLOCATE_OBJ(KTN_ObjEnum, OBJ_ENUM);

    TableInit(&_enum->getters);
    TableInit(&_enum->methods);
    TableInit(&_enum->variants);

    _enum->name = NULL;
    _enum->docs = NULL;
    _enum->variantList = NULL;

    return _enum;
}

KTN_ObjEnumVariant* EnumVariantNew(KTN_VM* vm) {
    KTN_ObjEnumVariant* _enum = ALLOCATE_OBJ(KTN_ObjEnumVariant, OBJ_ENUM_VARIANT);

    _enum->name = NULL;
    _enum->owner = NULL;
    _enum->ordinal = 0;
    _enum->value = EMPTY_VALUE;

    return _enum;
}

KTN_ObjKata* KataNew(KTN_VM* vm, KTN_ObjString* name) {
    KTN_ObjKata* kata = ALLOCATE_OBJ(KTN_ObjKata, OBJ_CLASS);
    kata->className = name;

    ValueArrayInit(&kata->methodNames);

    TableInit(&kata->methods);
    TableInit(&kata->properties);
    TableInit(&kata->staticMethods);
    TableInit(&kata->staticProperties);

    TableInit(&kata->privateMembers);
    TableInit(&kata->staticPrivateMembers);

    TableInit(&kata->fieldTypes);
    TableInit(&kata->staticFieldTypes);

    TableInit(&kata->innerDocs);

    kata->constructor = NULL_VALUE;
    kata->overloads = NULL;
    kata->docs = NULL;
    kata->typeParameters = NULL;
    kata->sokata = NULL;

    kata->typeParameterCount = 0;
    kata->overloadMask = 0;
    kata->hasTypedFields = false;
    kata->optionalGenerics = false;
    kata->privateConstructor = false;

    return kata;
}

KTN_ObjInstance* InstanceNew(KTN_VM* vm, KTN_ObjKata* classObj) {
    KTN_ObjInstance* instance = ALLOCATE_OBJ(KTN_ObjInstance, OBJ_INSTANCE);
    instance->kata = classObj;
    ValueArrayInit(&instance->propertyNames);
    TableInit(&instance->properties);
    instance->typeArguments = NULL;
    TableAddAll(vm, &classObj->properties, &instance->properties);
    return instance;
}

KTN_ObjBoundMethod* BoundMethodNew(KTN_VM* vm, KTN_Value receiver, KTN_ObjClosure* method, KTN_ObjKata* owner) {
    KTN_ObjBoundMethod* bound = ALLOCATE_OBJ(KTN_ObjBoundMethod, OBJ_BOUND_METHOD);

    bound->receiver = receiver;
    bound->method = method;
    bound->owner = owner;

    return bound;
}

KTN_ObjAccessor* AccessorNew(KTN_VM* vm) {
    KTN_ObjAccessor* accessor = ALLOCATE_OBJ(KTN_ObjAccessor, OBJ_ACCESSOR);

    accessor->getter = NULL;
    accessor->setter = NULL;

    return accessor;
}

// Returns a GC-managed ObjString* containing the string
// representation of any value. Safe against cyclic structures.
// This is the foundation for string interpolation.
KTN_ObjString* ObjectToString(KTN_VM* vm, KTN_Value value) {
    StringBuilder sb;

    SBInit(&sb);
    VisitedSet visited;
    visited.count = 0;
    ValueStringify(&sb, value, &visited);

    // StringCopy interns the string and owns its own copy.
    // SBFree then releases the temporary builder buffer.
    KTN_ObjString* result = StringCopy(vm, sb.buffer ? sb.buffer : "", sb.length);
    SBFree(&sb);

    return result;
}

/// Prints a value to stdout. Cycle-safe for arrays and maps.
void ObjectPrint(KTN_Value value) {
    StringBuilder sb;
    VisitedSet visited;
    
    SBInit(&sb);
    
    visited.count = 0;
    ValueStringify(&sb, value, &visited);

    if (sb.buffer)
        printf("%s", sb.buffer);
    
    SBFree(&sb);
}


void ObjectRepr(KTN_Value value) {
    StringBuilder sb;
    SBInit(&sb);
    VisitedSet visited;
    visited.count = 0;
    ValueRepr(&sb, value, &visited);
    if (sb.buffer)
        printf("%s", sb.buffer);
    SBFree(&sb);
}
