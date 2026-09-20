/** 
 * HashMap.c
 * Nokutoka Momiji
 * 
 * Let me preface this by saying that I am convinced hashing functions are black magic
 *  and I will never understand them. Literally had never seen a more huge collection of
 *  magic numbers in my life.
 * 
 * Have fun, or something, I don't know...
*/

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "HashMap.h"
#include "Memory.h"
#include "Object.h"
#include "Value.h"

#define IDEAL(hash, capacity)       ((hash) & (uint32_t)((capacity) - 1))
#define NEXT_SLOT(index, capacity)  (((index) + 1) & (uint32_t)((capacity) - 1))

#define GOLDEN_RATIO 11400714819323198485ULL
#define NONPRIME_GOLDEN_RATIO 2654435761u
#define NAN_VALUE 0x7fc00000u

// I dunno man. Grabbed from: https://www.reddit.com/r/RNG/comments/jqnq20/comment/mea7of8
uint32_t HashInt32(int32_t number) {
    uint32_t hash = (uint32_t)number;
    
    hash ^= hash >> 16;
    hash *= 0x21f0aaadU;
    hash ^= hash >> 15;
    hash *= 0x735a2d97U;
    hash ^= hash >> 15;
    
    return hash;
}

static uint32_t HashDouble(double number) {
    // Not a Number value.
    if (number != number) return NAN_VALUE;
    if (number == 0.0) return 0;

    uint64_t bits;
    memcpy(&bits, &number, sizeof(bits));

    // Supposedly this helps. Imagine.
    uint32_t hash = (uint32_t)(bits ^ (bits >> 32));

    hash ^= hash >> 16;
    hash *= 0x21f0aaadU;
    hash ^= hash >> 15;
    hash *= 0x735a2d97U;
    hash ^= hash >> 15;
    
    return hash;
}

// Fibonacci hashing: multiply by floor(2^64 / phi) = 11400714819323198485.
// Yes, imagine having the fuuuuucking golden ratio for your hash table.
// https://en.wikipedia.org/wiki/Hash_function#Fibonacci_hashing
// https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
static uint32_t HashPointer(const void* pointer) {
    uintptr_t address = (uintptr_t)pointer;
    return (uint32_t)((address * GOLDEN_RATIO) >> 32);
}

static void Grow(KTN_VM* vm, KTN_HashMap* map) {
    int newCapacity = GROW_CAPACITY(map->capacity);
    KTN_HashEntry* newEntries = ALLOCATE(KTN_HashEntry, newCapacity);

    for (int i = 0; i < newCapacity; i++) {
        newEntries[i].key = EMPTY_VALUE;
        newEntries[i].psl = 0;
        newEntries[i].orderPrevious = -1;
        newEntries[i].orderNext = -1;
    }

    // We reinsert all live entries into the new array.
    int oldCapacity = map->capacity;
    int32_t walk = map->orderHead;
    KTN_HashEntry* oldEntries = map->entries;

    map->count = 0;
    map->capacity = newCapacity;
    map->entries = newEntries;
    map->orderHead = -1;
    map->orderTail = -1;

    while (walk >= 0) {
        KTN_HashEntry* source = &oldEntries[walk];
        int32_t nextWalk = source->orderNext;

        KTN_HashMapSet(vm, map, source->key, source->value);

        walk = nextWalk;
    }

    /*for (int i = 0; i < oldCapacity; i++) {
        KTN_HashEntry* source = &oldEntries[i];

        // If the current spot is empty, we have nothing else to do.
        if (IS_EMPTY(source->key)) continue;

        // Robin Hood insert (such a funny name...)
        uint32_t index = IDEAL(source->hash, newCapacity);
        uint32_t psl = 0;
        Value insertKey = source->key;
        Value insertValue = source->value;
        uint32_t insertHash = source->hash;

        for (;;) {
            KTN_HashEntry* destination = &newEntries[index];

            // If the spot is empty, just place the entry there.
            if (IS_EMPTY(destination->key)) {
                destination->key = insertKey;
                destination->value = insertValue;
                destination->hash = insertHash;
                destination->psl = psl;
                map->count++;
                break;
            }

            if (psl > destination->psl) {
                // Compiler-san, ganbatte!

                Value tempKey  = destination->key;
                Value tempValue  = destination->value;
                uint32_t tempHash = destination->hash;
                uint32_t tempPsl  = destination->psl;
            
                destination->key = insertKey;
                destination->value = insertValue;
                destination->hash = insertHash;
                destination->psl = psl;

                insertKey = tempKey;
                insertValue = tempValue;
                insertHash  = tempHash;
                psl = tempPsl;
            }

            index = NEXT_SLOT(index, newCapacity);
            psl++;
        }
    }*/

    if (oldEntries != NULL)
        FREE_ARRAY(KTN_HashEntry, oldEntries, oldCapacity);
}

static inline bool KeysEqual(KTN_Value a, KTN_Value b) {
#ifdef NAN_BOXING
    // NAN Boxing gives us a small shortcut, since they are bit-equal if they are the same value.
    if (a == b) return true;

    if (IS_DOUBLE(a) && IS_DOUBLE(b)) {
        double first = AS_DOUBLE(a);
        double second = AS_DOUBLE(b);

        if (first != first && second != second)
            return true;

        return first == second;
    }

    return false;
#else
    if (IS_DOUBLE(a) && IS_DOUBLE(b)) {
        double first = AS_DOUBLE(a);
        double second = AS_DOUBLE(b);

        if (first != first && second != second)
            return true;

        return first == second;
    }

    return ValuesEqual(a, b);
#endif
}

void KTN_HashMapInit(KTN_HashMap* map) {
    map->count = 0;
    map->capacity = 0;
    map->orderHead = -1;
    map->orderTail = -1;
    map->entries = NULL;
}

void KTN_HashMapFree(KTN_VM* vm, KTN_HashMap* map) {
    FREE_ARRAY(KTN_HashEntry, map->entries, map->capacity);
    KTN_HashMapInit(map);
}

uint32_t HashValue(KTN_VM* vm, KTN_Value value) {
    (void)vm; // TODO: implement instance->hashCode dispatch...

    if (IS_INT(value))      return HashInt32(AS_INT(value));
    if (IS_DOUBLE(value))   return HashDouble(AS_DOUBLE(value));
    if (IS_BOOL(value))     return AS_BOOL(value) ? 1231u : 1237u;
    if (IS_NULL(value))     return NONPRIME_GOLDEN_RATIO;

    if (IS_OBJECT(value)) {
        KTN_Object* object = AS_OBJECT(value);

        if (object->type == OBJ_STRING)
            return ((KTN_ObjString*)object)->hash;

        if (object->type == OBJ_ENUM_VARIANT) {
            KTN_ObjEnumVariant* variant = ((KTN_ObjEnumVariant*)object);
            return (uint32_t)variant->ordinal ^ variant->owner->name->hash;
        }

        if (object->type == OBJ_INSTANCE) {
            //return HashMapDispatchHashCode(vm, value);
        }

        return HashPointer(object);
    }

    return 0;
}

bool KTN_HashMapGet(KTN_VM* vm, KTN_HashMap* map, KTN_Value key, KTN_Value* outputValue) {
    if (map->count == 0)
        return false;

    uint32_t hash = HashValue(vm, key);
    uint32_t index = IDEAL(hash, map->capacity);
    uint32_t psl = 0;

    for (;;) {
        KTN_HashEntry* entry = &map->entries[index];

         if (IS_EMPTY(entry->key) || psl > entry->psl)
            return false;

        if (entry->hash == hash && KeysEqual(entry->key, key)) {
            *outputValue = entry->value;
            return true;
        }

        index = NEXT_SLOT(index, map->capacity);
        psl++;
    }
}

bool KTN_HashMapSet(KTN_VM* vm, KTN_HashMap* map, KTN_Value key, KTN_Value value) {
    if (map->count + 1 > map->capacity * HASHMAP_MAX_LOAD)
        Grow(vm, map);

    uint32_t hash = HashValue(vm, key);
    uint32_t index = IDEAL(hash, map->capacity);
    uint32_t psl = 0;

    KTN_Value insertKey = key;
    KTN_Value insertValue = value;
    uint32_t insertHash = hash;
    int32_t insertPrevious = -2;
    int32_t insertNext = -1;

    bool newKeyAdded = false;

    for (;;) {
        KTN_HashEntry* entry = &map->entries[index];

        if (IS_EMPTY(entry->key)) {
            entry->key = insertKey;
            entry->value = insertValue;
            entry->hash = insertHash;
            entry->psl = psl;

            if (insertPrevious == -2) {
                entry->orderPrevious = map->orderTail;
                entry->orderNext = -1;

                if (map->orderTail >= 0)
                    map->entries[map->orderTail].orderNext = (int32_t)index;
                else
                    map->orderHead = (int32_t)index;

                map->orderTail = (int32_t)index;
                map->count++;

                return true;
            } else {
                entry->orderPrevious = insertPrevious;
                entry->orderNext = insertNext;

                if (insertPrevious >= 0)
                    map->entries[insertPrevious].orderNext = (int32_t)index;
                else
                    map->orderHead = (int32_t)index;

                if (insertNext >= 0)
                    map->entries[insertNext].orderPrevious = (int32_t)index;
                else
                    map->orderTail = (int32_t)index;
                
                return newKeyAdded;
            }
        }

        if (entry->hash == insertHash && KeysEqual(entry->key, insertKey)) {
            entry->value = insertValue;
            return false;
        }

        if (psl > entry->psl) {
            // Compiler-san, ganbatte!
            int32_t savedPrev = entry->orderPrevious;
            int32_t savedNext = entry->orderNext;

            KTN_Value tempKey = entry->key;
            KTN_Value tempValue = entry->value;
            uint32_t tempHash = entry->hash;
            uint32_t tempPsl = entry->psl;

            entry->key = insertKey;
            entry->value = insertValue;
            entry->hash = insertHash;
            entry->psl = psl;

            if (insertPrevious == -2) {
                // Brand new key: append to the end of the order list.
                entry->orderPrevious = map->orderTail;
                entry->orderNext = -1;

                if (map->orderTail >= 0)
                    map->entries[map->orderTail].orderNext = (int32_t)index;
                else
                    map->orderHead = (int32_t)index;
                
                map->orderTail = (int32_t)index;
                map->count++;
                newKeyAdded = true;   // remember that we added a new key
            } else {
                // Displaced key: preserve its original order links.
                entry->orderPrevious = insertPrevious;
                entry->orderNext = insertNext;
                
                if (insertPrevious >= 0)
                    map->entries[insertPrevious].orderNext = (int32_t)index;
                else
                    map->orderHead = (int32_t)index;
                
                if (insertNext >= 0)
                    map->entries[insertNext].orderPrevious = (int32_t)index;
                else
                    map->orderTail = (int32_t)index;
            }

            // Now the displaced entry becomes the one to re‑insert.
            insertKey = tempKey;
            insertValue = tempValue;
            insertHash = tempHash;
            psl = tempPsl;
            insertPrevious = savedPrev;
            insertNext = savedNext;
        }

        index = NEXT_SLOT(index, map->capacity);
        psl++;
    }
}

bool KTN_HashMapDelete(KTN_VM* vm, KTN_HashMap* map, KTN_Value key) {
    if (map->count == 0)
        return false;

    uint32_t hash = HashValue(vm, key);
    uint32_t index = IDEAL(hash, map->capacity);
    uint32_t psl = 0;

    for (;;) {
        KTN_HashEntry* entry = &map->entries[index];

        if (IS_EMPTY(entry->key) || psl > entry->psl)
            return false;

        if (entry->hash == hash && KeysEqual(entry->key, key))
            break;

        index = NEXT_SLOT(index, map->capacity);
        psl++;
    }

    KTN_HashEntry* deleted = &map->entries[index];

    if (deleted->orderPrevious >= 0)
        map->entries[deleted->orderPrevious].orderNext = deleted->orderNext;
    else
        map->orderHead = deleted->orderNext;

    if (deleted->orderNext >= 0)
        map->entries[deleted->orderNext].orderPrevious = deleted->orderPrevious;
    else
        map->orderTail = deleted->orderPrevious;

    map->count--;

    for (;;) {
        uint32_t nextIndex = NEXT_SLOT(index, map->capacity);
        KTN_HashEntry* nextEntry = &map->entries[nextIndex];

        if (IS_EMPTY(nextEntry->key) || nextEntry->psl == 0) {
            map->entries[index].key = EMPTY_VALUE;
            map->entries[index].psl = 0;

            return true;
        }

        map->entries[index] = *nextEntry;
        map->entries[index].psl--;

        int32_t slot = (int32_t)index;

        if (map->entries[slot].orderPrevious >= 0)
            map->entries[map->entries[slot].orderPrevious].orderNext = slot;
        else
            map->orderHead = slot;

        if (map->entries[slot].orderNext >= 0)
            map->entries[map->entries[slot].orderNext].orderPrevious = slot;
        else
            map->orderTail = slot;

        index = nextIndex;
    }
}

void KTN_HashMapMark(KTN_VM* vm, KTN_HashMap* map) {
    /*for (int i = 0; i < map->capacity; i++) {
        KTN_HashEntry* entry = &map->entries[i];

        if (IS_EMPTY(entry->key))
            continue;

        KTN_MemoryMarkValue(vm, entry->key);
        KTN_MemoryMarkValue(vm, entry->value);
    }*/

    int32_t index = map->orderHead;
    
    while (index >= 0) {
        KTN_HashEntry* entry = &map->entries[index];

        KTN_MemoryMarkValue(vm, entry->key);
        KTN_MemoryMarkValue(vm, entry->value);
        
        index = entry->orderNext;
    }
}

void KTN_HashMapRemoveWhite(KTN_VM* vm, KTN_HashMap* map) {
    for (int i = 0; i < map->capacity; i++) {
        KTN_HashEntry* entry = &map->entries[i];

        if (IS_EMPTY(entry->key) || !IS_OBJECT(entry->key))
            continue;

        KTN_Object* object = AS_OBJECT(entry->key);

        if (object->isMarked)
            continue;

        KTN_HashMapDelete(vm, map, entry->key);
        i--;
    }
}

bool KTN_HashMapNext(KTN_HashMap* map, int* index, KTN_Value* outputKey, KTN_Value* outputValue) {
    while (*index < map->capacity) {
        KTN_HashEntry* entry = &map->entries[*index];
        (*index)++;

        if (!IS_EMPTY(entry->key)) {
            *outputKey = entry->key;
            *outputValue = entry->value;

            return true;
        }
    }

    return false;
}

bool KTN_HashMapNextOrdered(KTN_HashMap *map, int *index, KTN_Value *outputKey, KTN_Value *outputValue) {
    if (*index < 0) return false;
    
    KTN_HashEntry* entry = &map->entries[*index];
    
    *outputKey = entry->key;
    *outputValue = entry->value;
    *index = entry->orderNext;

    return true;
}
