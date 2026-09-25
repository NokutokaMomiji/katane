#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "TypeDescriptor.h"
#include "Memory.h"
#include "Object.h"
#include "Table.h"
#include "VM.h"
#include "Value.h"
#include "Utilities.h"

#define INTERN_MAX_LOAD 0.75f

static inline uint32_t MixU32(uint32_t first, uint32_t second) {
    first ^= second; //xor between the values.
    first *= 16777619u; // Magic numbers make the heart grow fonder.
    return first;
}

static inline uint32_t PointerHash(const void* pointer) {
    uintptr_t casted = (uintptr_t)pointer;
    return (uint32_t)(casted ^ (casted >> 16));
}

static uint32_t ComputeHash(KTN_TypeDescriptorType type, KTN_ObjString* name, KTN_ObjTypeDescriptor* base, KTN_ObjTypeDescriptor** arguments, int argumentCount) {
    uint32_t hash = 2166136261u;

    hash = MixU32(hash, (uint32_t)type);

    if (name) hash = MixU32(hash, name->hash);
    if (base) hash = MixU32(hash, PointerHash(base));

    for (int i = 0; i < argumentCount; i++) {
        hash = MixU32(hash, PointerHash(arguments[i]));
    }

    return (hash) ? hash : 1u;
}

static void InternGrow(KTN_VM* vm, KTN_DescriptorSet* set) {
    int newCapacity = GROW_CAPACITY(set->capacity);
    KTN_ObjTypeDescriptor** newBuckets = ALLOCATE(KTN_ObjTypeDescriptor*, newCapacity);

    memset(newBuckets, 0, (size_t)newCapacity * sizeof(KTN_ObjTypeDescriptor*));

    uint32_t mask = (uint32_t)(newCapacity - 1);

    // We relocate the old buckets into the new one.
    for (int i = 0; i < set->capacity; i++) {
        KTN_ObjTypeDescriptor* current = set->buckets[i];

        // Skip empty buckets.
        if (!current) continue;

        uint32_t index = current->hash & mask;

        // Linear probing to find the next best index.
        while (newBuckets[index]) {
            index = (index + 1) & mask;
        }

        newBuckets[index] = current;
    }

    // Free the old bucket array.
    FREE_ARRAY(KTN_ObjTypeDescriptor*, set->buckets, set->capacity);

    // We set the old elements to point to the new ones.
    set->buckets = newBuckets;
    set->capacity = newCapacity;
}

static KTN_ObjTypeDescriptor* InternFind(KTN_DescriptorSet* set, const KTN_ObjTypeDescriptor* probe) {
    if (set->capacity == 0)
        return NULL;

    uint32_t mask = (uint32_t)(set->capacity - 1);
    uint32_t index = probe->hash & mask;

    for (;;) {
        KTN_ObjTypeDescriptor* slot = set->buckets[index];

        if (!slot)
            return NULL;

        if (KTN_DescriptorsEqual(probe, slot))
            return slot;

        index = (index + 1) & mask;
    }
}

static void InternInsert(KTN_VM* vm, KTN_DescriptorSet* set, KTN_ObjTypeDescriptor* descriptor) {
    if (set->capacity == 0 || (float)(set->count) > ((float)set->capacity * INTERN_MAX_LOAD))
        InternGrow(vm, set);

    uint32_t mask = (uint32_t)(set->capacity - 1);
    uint32_t index = descriptor->hash & mask;

    while (set->buckets[index]) {
        index = (index + 1) & mask;
    }

    set->buckets[index] = descriptor;
    set->count++;
}

static int ComparePointers(const void* first, const void* second) {
    uintptr_t firstPointer = (uintptr_t)(*(const KTN_ObjTypeDescriptor**)first);
    uintptr_t secondPointer = (uintptr_t)(*(const KTN_ObjTypeDescriptor**)second);

    return (firstPointer > secondPointer) - (firstPointer < secondPointer);
}

static KTN_ObjKata* PrimitiveKataOf(KTN_VM* vm, KTN_Value value) {
    if (IS_INT(value))
        return vm->typeInt;

    if (IS_DOUBLE(value))
        return vm->typeFloat;

    if (IS_BOOL(value))
        return vm->typeBool;

    if (IS_STRING(value))
        return vm->typeString;

    if (IS_NULL(value))
        return vm->typeNull;

    return NULL;
}

static KTN_ObjKata* ResolveNamed(KTN_VM* vm, KTN_ObjTypeDescriptor* descriptor) {
    if (descriptor->resolved)
        return descriptor->resolved;

    KTN_Value kataValue;

    if (TableGet(&vm->globals, descriptor->name, &kataValue) && IS_CLASS(kataValue)) {
        descriptor->resolved = AS_CLASS(kataValue);
    }

    return descriptor->resolved;
}

static uint32_t TableAppend(KTN_DescriptorTable* table, KTN_ObjTypeDescriptor* descriptor) {
    if (table->count >= table->capacity) {
        int newCapacity = GROW_CAPACITY(table->capacity);
        KTN_ObjTypeDescriptor** newItems = (KTN_ObjTypeDescriptor**)realloc(table->items, sizeof(KTN_ObjTypeDescriptor*) * newCapacity);

        if (!newItems)
            return UINT32_MAX;

        table->items = newItems;
        table->capacity = newCapacity;
    }

    uint32_t index = (uint32_t)table->count;
    table->items[index++] = descriptor;
    return index;
}

bool KTN_DescriptorsEqual(const KTN_ObjTypeDescriptor* first, const KTN_ObjTypeDescriptor* second) {
    if (first->type != second->type) return false;
    if (first->hash != second->hash) return false;
    if (first->name != second->name) return false;
    if (first->base != second->base) return false;
    if (first->argumentCount != second->argumentCount) return false;

    for (int i = 0; i < first->argumentCount; i++) {
        if (first->arguments[i] != second->arguments[i])
            return false;
    }

    return true;
}

void KTN_DescriptorSetInit(KTN_DescriptorSet *set) {
    set->buckets = NULL;
    set->capacity = 0;
    set->count = 0;
}

void KTN_DescriptorSetFree(KTN_VM *vm, KTN_DescriptorSet *set) {
    FREE_ARRAY(KTN_ObjTypeDescriptor*, set->buckets, set->capacity);

    set->buckets = NULL;
    set->capacity = 0;
    set->count = 0;
}

void KTN_DescriptorSetMark(KTN_VM *vm, KTN_DescriptorSet *set) {
    for (int i = 0; i < set->capacity; i++) {
        if (set->buckets[i]) {
            KTN_MemoryMarkObject(vm, (KTN_Object*)set->buckets[i]);
        }
    }
}

KTN_ObjTypeDescriptor* KTN_TypeDescriptorNamed(KTN_VM* vm, KTN_ObjString* name) {
    uint32_t hash = ComputeHash(TD_NAMED, name, NULL, NULL, 0);

    KTN_ObjTypeDescriptor probe;
    memset(&probe, 0, sizeof(probe));

    probe.type = TD_NAMED;
    probe.hash = hash;
    probe.name = name;

    KTN_ObjTypeDescriptor* hit = InternFind(&vm->typeDescriptors, &probe);

    if (hit)
        return hit;

    KTN_ObjTypeDescriptor* descriptor = TypeDescriptorNew(vm);

    Push(vm, OBJECT_VALUE(descriptor));

    descriptor->type = TD_NAMED;
    descriptor->hash = hash;
    descriptor->name = name;

    InternInsert(vm, &vm->typeDescriptors, descriptor);

    Pop(vm);

    return descriptor;
}

KTN_ObjTypeDescriptor* KTN_TypeDescriptorUnion(KTN_VM* vm, KTN_ObjTypeDescriptor** members, int count) {
    if (count > 32)
        count = 32;

    if (count < 2)
        return (count == 1) ? members[0] : NULL;

    KTN_ObjTypeDescriptor* sorted[32];
    memcpy(sorted, members, (size_t)count * sizeof(KTN_ObjTypeDescriptor*));
    
    // Sorting is necessary to make sure that the canonical hash is always the same.
    // Otherwise, two unions with the same types, but in different order, would yield different
    //  hashes.
    qsort(sorted, (size_t)count, sizeof(KTN_ObjTypeDescriptor*), ComparePointers);

    uint32_t hash = ComputeHash(TD_UNION, NULL, NULL, sorted, count);
    
    KTN_ObjTypeDescriptor probe;
    memset(&probe, 0, sizeof(probe));

    probe.type = TD_UNION;
    probe.hash = hash;
    probe.arguments = sorted;
    probe.argumentCount = (uint8_t)count;

    KTN_ObjTypeDescriptor* hit = InternFind(&vm->typeDescriptors, &probe);

    if (hit)
        return hit;

    KTN_ObjTypeDescriptor* descriptor = TypeDescriptorNew(vm);

    Push(vm, OBJECT_VALUE(descriptor));

    descriptor->type = TD_UNION;
    descriptor->hash = hash;
    descriptor->arguments = ALLOCATE(KTN_ObjTypeDescriptor*, count);
    descriptor->argumentCount = (uint8_t)count;

    memcpy(descriptor->arguments, sorted, (size_t)count * sizeof(KTN_ObjTypeDescriptor*));

    InternInsert(vm, &vm->typeDescriptors, descriptor);

    Pop(vm);

    return descriptor;
}

KTN_ObjTypeDescriptor* KTN_TypeDescriptorParam(KTN_VM* vm, KTN_ObjTypeDescriptor* base, KTN_ObjTypeDescriptor** parameters, int count) {
    if (count > 8)
        count = 8;

    uint32_t hash = ComputeHash(TD_PARAMETER, NULL, base, parameters, count);

    KTN_ObjTypeDescriptor probe;
    memset(&probe, 0, sizeof(probe));

    probe.type = TD_PARAMETER;
    probe.hash = hash;
    probe.base = base;
    probe.arguments = parameters;
    probe.argumentCount = (uint8_t)count;

    KTN_ObjTypeDescriptor* hit = InternFind(&vm->typeDescriptors, &probe);

    if (hit)
        return hit;

    KTN_ObjTypeDescriptor* descriptor = TypeDescriptorNew(vm);

    Push(vm, OBJECT_VALUE(descriptor));

    descriptor->type = TD_PARAMETER;
    descriptor->hash = hash;
    descriptor->base = base;
    descriptor->arguments = ALLOCATE(KTN_ObjTypeDescriptor*, count);
    descriptor->argumentCount = (uint8_t)count;

    memcpy(descriptor->arguments, parameters, (size_t)count * sizeof(KTN_ObjTypeDescriptor*));

    InternInsert(vm, &vm->typeDescriptors, descriptor);

    Pop(vm);

    return descriptor;
}

KTN_ObjTypeDescriptor* KTN_TypeDescriptorNullable(KTN_VM* vm, KTN_ObjTypeDescriptor* inner) {
    KTN_ObjString* nullName = STRING_COPY(vm, "Null", 4);
    KTN_ObjTypeDescriptor* nullDescriptor = KTN_TypeDescriptorNamed(vm, nullName);
    KTN_ObjTypeDescriptor* members[2] = { inner, nullDescriptor };
    return KTN_TypeDescriptorUnion(vm, members, 2);
}

KTN_ObjTypeDescriptor* KTN_TypeDescriptorTypeVar(KTN_VM* vm, KTN_ObjString* name) {
    uint32_t hash = ComputeHash(TD_TYPE_VARIABLE, name, NULL, NULL, 0);

    KTN_ObjTypeDescriptor probe;
    memset(&probe, 0, sizeof(probe));

    probe.type = TD_TYPE_VARIABLE;
    probe.hash = hash;
    probe.name = name;

    KTN_ObjTypeDescriptor* hit = InternFind(&vm->typeDescriptors, &probe);
    
    if (hit)
        return hit;

    KTN_ObjTypeDescriptor* descriptor = TypeDescriptorNew(vm);

    Push(vm, OBJECT_VALUE(descriptor));

    descriptor->type = TD_TYPE_VARIABLE;
    descriptor->hash = hash;
    descriptor->name = name;

    InternInsert(vm, &vm->typeDescriptors, descriptor);

    Pop(vm);

    return descriptor;
}

typedef struct {
    const char* name;
    int length;
    KTN_ObjKata* kata;
} PrimitiveType;

void KTN_TypeDescriptorPreResolvePrimitives(KTN_VM *vm) {
    PrimitiveType primitives[] = {
        { "Int", 3, vm->typeInt },
        { "Float", 5, vm->typeFloat },
        { "Bool", 3, vm->typeBool },
        { "String", 6, vm->typeString },
        { "Null", 4, vm->typeNull }
    };

    for (int i = 0; i < 5; i++) {
        if (!((primitives[i].kata)))
            continue;

        KTN_ObjString* name = STRING_COPY(vm, primitives[i].name, primitives[i].length);
        KTN_ObjTypeDescriptor* descriptor = KTN_TypeDescriptorNamed(vm, name);
        descriptor->resolved = primitives[i].kata;
    }
}

bool KTN_TypeDescriptorCheck(KTN_VM* vm, KTN_Value value, KTN_ObjTypeDescriptor* descriptor) {
    switch (descriptor->type) {
        case TD_NAMED: {
            // Should not happen, but... ya know...
            if (!descriptor->name)
                return false;

            // Null bypass.
            if (descriptor->name->length == 4 && memcmp(descriptor->name->chars, "Null", 4) == 0) {
                return IS_NULL(value);
            }

            // For primitives.
            KTN_ObjKata* primitiveKata = PrimitiveKataOf(vm, value);

            if (primitiveKata) {
                KTN_ObjKata* target = ResolveNamed(vm, descriptor);
                return (target == primitiveKata);
            }

            // For instances of user-made katas (got to resolve the inheritance chain).
            if (IS_INSTANCE(value)) {
                KTN_ObjKata* target = ResolveNamed(vm, descriptor);
                return (target && IsInstanceOfKata(AS_INSTANCE(value)->kata, target));
            }

            const char* name = descriptor->name->chars;
            int length = descriptor->name->length;
            
            // Is there a better solution than hard-coded values? I'm not sure.
            // I think that, since we have a very smol number of primitives... 
            // It's not that much of an issue, is it?
            if (IS_ARRAY(value))
                return (length == 5 && memcmp(name, "Array", 5) == 0);
            if (IS_MAP(value))
                return (length == 3 && memcmp(name, "Map", 3) == 0);
            if (IS_CLOSURE(value) || IS_FUNCTION(value))
                return (length == 5 && memcmp(name, "Shiki", 5) == 0);
            if (IS_CLASS(value))
                return (length == 4 && memcmp(name, "Kata", 4) == 0);

            return false;
        }

        case TD_UNION: {
            for (int i = 0; i < descriptor->argumentCount; i++) {
                if (KTN_TypeDescriptorCheck(vm, value, descriptor->arguments[i])) {
                    return true;
                }
            }

            return false;
        }

        case TD_PARAMETER: {
            return (descriptor->base && KTN_TypeDescriptorCheck(vm, value, descriptor->base));
        }

        case TD_TYPE_VARIABLE: {
            // KTN_TypeDescriptorCheckWithArgs handles generics.
            return true;
        }
    }

    return false;
}

bool KTN_TypeDescriptorCheckWithArgs(KTN_VM *vm, KTN_Value value, KTN_ObjTypeDescriptor *descriptor, KTN_ObjKata *genericClass, KTN_ObjTypeDescriptor **typeArguments) {
    if (!genericClass || !typeArguments)
        return KTN_TypeDescriptorCheck(vm, value, descriptor);

    switch (descriptor->type) {
        case TD_TYPE_VARIABLE: {
            for (int i = 0; i < genericClass->typeParameterCount; i++) {
                if (genericClass->typeParameters[i].name == descriptor->name) {
                    return (typeArguments[i]) ? KTN_TypeDescriptorCheck(vm, value, typeArguments[i]) : true;
                }
            }

            return true;
        }

        case TD_UNION: {
            for (int i = 0; i < descriptor->argumentCount; i++) {
                if (KTN_TypeDescriptorCheckWithArgs(
                    vm,
                    value,
                    descriptor->arguments[i],
                    genericClass,
                    typeArguments)
                ) {
                    return true;
                }
            }

            return false;
        }

        case TD_PARAMETER: {
            return (descriptor->base && KTN_TypeDescriptorCheckWithArgs(vm, value, descriptor->base, genericClass, typeArguments));
        }

        case TD_NAMED: 
        default: {
            return KTN_TypeDescriptorCheck(vm, value, descriptor);
        }
    }
}

void KTN_DescriptorTableInit(KTN_DescriptorTable *table) {
    table->items = NULL;
    table->capacity = 0;
    table->count = 0;
}

void KTN_DescriptorTableFree(KTN_DescriptorTable *table) {
    // Since the elements are GC-collected, we can freely free the table without freeing the items.
    free(table->items);
    
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

uint32_t KTN_DescriptorTableIndex(KTN_DescriptorTable *table, KTN_ObjTypeDescriptor *descriptor) {
    for (int i = 0; i < table->count; i++) {
        if (table->items[i] == descriptor) return i;
    }

    return UINT32_MAX;
}

uint32_t KTN_DescriptorTableRegister(KTN_DescriptorTable *table, KTN_ObjTypeDescriptor *descriptor) {
    uint32_t existing = KTN_DescriptorTableIndex(table, descriptor);

    if (existing != UINT32_MAX) return existing;

    if (descriptor->base)
        return KTN_DescriptorTableRegister(table, descriptor->base);

    for (int i = 0; i < descriptor->argumentCount; i++) {
        KTN_DescriptorTableRegister(table, descriptor->arguments[i]);
    }

    return TableAppend(table, descriptor);
}

// TODO: Replace with Buffer object.
int KTN_TypeDescriptorWrite(KTN_ObjTypeDescriptor *descriptor, KTN_DescriptorTable *table, uint8_t *buffer, int bufferCapacity, uint32_t (*getStringIndex)(KTN_ObjString *, void *), void *context) {
    if (!descriptor || bufferCapacity < 1) return -1;

    int position = 0;

#define NEED(offset) do { if ((position + offset) > bufferCapacity) return -1; } while (0)
#define WRITE_U32(value) do { NEED(4); writeUInt32(buffer + position, (value)); position += 4; } while (0)
#define WRITE_U8(value) do { NEED(1); buffer[position++] = (uint8_t)(value); } while (0);

    WRITE_U8((uint8_t)descriptor->type);

    switch(descriptor->type) {
        case TD_NAMED:
        case TD_TYPE_VARIABLE: {
            if (!descriptor->name)
                return -1;

            uint32_t stringIndex = getStringIndex(descriptor->name, context);

            if (stringIndex == UINT32_MAX)
                return -1;

            WRITE_U32(stringIndex);                
            break;
        }

        case TD_UNION: {
            WRITE_U8((uint8_t)descriptor->argumentCount);
            for (int i = 0; i < descriptor->argumentCount; i++) {
                uint32_t argumentIndex = KTN_DescriptorTableIndex(table, descriptor);
                if (argumentIndex == UINT32_MAX) 
                    return -1;

                WRITE_U32(argumentIndex);
            }
            break;
        }

        case TD_PARAMETER: {
            if (!descriptor->base)
                return -1;

            uint32_t baseIndex = KTN_DescriptorTableIndex(table, descriptor->base);

            if (baseIndex == UINT32_MAX) 
                return -1;

            WRITE_U32(baseIndex);
            WRITE_U8(descriptor->argumentCount);

            for (int i = 0; i < descriptor->argumentCount; i++) {
                uint32_t argumentIndex = KTN_DescriptorTableIndex(table, descriptor->arguments[i]);
                if (argumentIndex == UINT32_MAX)
                    return -1;

                WRITE_U32(argumentIndex);
            }

            break;
        }

        default:
            return -1;
    }

#undef WRITE_U8
#undef WRITE_U32
#undef NEED

    return position;
}

KTN_ObjTypeDescriptor* KTN_TypeDescriptorRead(KTN_VM* vm, const uint8_t* buffer, int bufferLength, int* offset, KTN_DescriptorTable* table, KTN_ObjString* (*getStringByIndex)(uint32_t, void *), void *context) {
    if (!buffer || bufferLength < 1 || *offset >= bufferLength)
        return NULL;

    int position = *offset;

#define RCHK_U8(dest) \
    do { if (position + 1 > bufferLength) return NULL; \
         (dest) = buffer[position++]; } while (0)
#define RCHK_U32(dest) \
    do { if (position + 4 > bufferLength) return NULL; \
         (dest) = readUInt32(buffer + position); position += 4; } while (0)
#define GET_CHILD(idx) \
    ((idx) < (uint32_t)table->count ? table->items[(idx)] : NULL)

    uint8_t tagByte;
    RCHK_U8(tagByte);
    KTN_TypeDescriptorType tag = (KTN_TypeDescriptorType)tagByte;
    KTN_ObjTypeDescriptor* desc = NULL;

switch (tag) {
        case TD_NAMED:
        case TD_TYPE_VARIABLE: {
            uint32_t strIdx;
            RCHK_U32(strIdx);
            KTN_ObjString* name = getStringByIndex(strIdx, context);
            if (!name) return NULL;
            desc = (tag == TD_NAMED)
                ? KTN_TypeDescriptorNamed(vm, name)
                : KTN_TypeDescriptorTypeVar(vm, name);
            break;
        }

        case TD_UNION: {
            uint8_t argCount;
            RCHK_U8(argCount);
            if (argCount > 32) return NULL;
            /* Reconstruct members — each must already be in the table. */
            KTN_ObjTypeDescriptor* members[32];
            for (int i = 0; i < (int)argCount; i++) {
                uint32_t childIdx;
                RCHK_U32(childIdx);
                members[i] = GET_CHILD(childIdx);
                if (!members[i]) return NULL;
            }
            /* KTN_TypeDescriptorUnion interns, so it finds a hit for anything
             * already created (e.g. T? desugars to an existing union). */
            desc = KTN_TypeDescriptorUnion(vm, members, (int)argCount);
            break;
        }

        case TD_PARAMETER: {
            uint32_t baseIdx;
            RCHK_U32(baseIdx);
            KTN_ObjTypeDescriptor* base = GET_CHILD(baseIdx);
            if (!base) return NULL;

            uint8_t argCount;
            RCHK_U8(argCount);
            if (argCount > 8) return NULL;
            KTN_ObjTypeDescriptor* params[8];
            for (int i = 0; i < (int)argCount; i++) {
                uint32_t childIdx;
                RCHK_U32(childIdx);
                params[i] = GET_CHILD(childIdx);
                if (!params[i]) return NULL;
            }
            desc = KTN_TypeDescriptorParam(vm, base, params, (int)argCount);
            break;
        }

        default:
            return NULL;
    }

#undef RCHK_U8
#undef RCHK_U32
#undef GET_CHILD

    if (!desc) return NULL;

    /* Append to the reconstruction table so later entries can reference this
     * descriptor by index.  Check for allocation failure. */
    if (TableAppend(table, desc) == UINT32_MAX) return NULL;


    *offset = position;
    return desc;
}

static int WriteCanonical(KTN_ObjTypeDescriptor* desc, char* buf, int cap) {
    if (cap <= 0) return 0;
    int pos = 0;

#define EMIT(s) \
    do { int _n = (int)strlen(s); \
         if (pos + _n < cap) { memcpy(buf + pos, (s), _n); pos += _n; } \
    } while (0)

    switch (desc->type) {
        case TD_NAMED:
        case TD_TYPE_VARIABLE:
            if (desc->name) EMIT(desc->name->chars);
            break;
        case TD_UNION:
            for (int i = 0; i < desc->argumentCount; i++) {
                if (i > 0) EMIT(" | ");
                pos += WriteCanonical(desc->arguments[i], buf + pos, cap - pos);
            }
            break;
        case TD_PARAMETER:
            if (desc->base) pos += WriteCanonical(desc->base, buf + pos, cap - pos);
            EMIT("<");
            for (int i = 0; i < desc->argumentCount; i++) {
                if (i > 0) EMIT(", ");
                pos += WriteCanonical(desc->arguments[i], buf + pos, cap - pos);
            }
            EMIT(">");
            break;
    }
#undef EMIT

    if (pos < cap) buf[pos] = '\0';
    return pos;
}

void KTN_TypeDescriptorFormat(KTN_ObjTypeDescriptor* descriptor, char* buffer, int bufferSize) {
    if (bufferSize <= 0) return;

    WriteCanonical(descriptor, buffer, bufferSize);
}
