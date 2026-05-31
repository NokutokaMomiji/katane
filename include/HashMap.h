#ifndef KATANE_HASHMAP_H
#define KATANE_HASHMAP_H

#include "Value.h"

typedef struct {
    KTN_Value key;
    KTN_Value value;
    uint32_t hash;
    uint32_t psl;
    int32_t orderPrevious;
    int32_t orderNext;
} KTN_HashEntry;

typedef struct {
    int count;
    int capacity;
    int orderHead;
    int orderTail;
    KTN_HashEntry* entries;
} KTN_HashMap;

#define HASHMAP_MAX_LOAD 0.75

void KTN_HashMapInit(KTN_HashMap* map);
void KTN_HashMapFree(KTN_VM* vm, KTN_HashMap* map);
bool KTN_HashMapGet(KTN_VM* vm, KTN_HashMap* map, KTN_Value key, KTN_Value* output);
bool KTN_HashMapSet(KTN_VM* vm, KTN_HashMap* map, KTN_Value key, KTN_Value value);
bool KTN_HashMapDelete(KTN_VM* vm, KTN_HashMap* map, KTN_Value key);
void KTN_HashMapMark(KTN_VM* vm, KTN_HashMap* map);
void KTN_HashMapRemoveWhite(KTN_VM* vm, KTN_HashMap* map);
bool KTN_HashMapNext(KTN_HashMap* map, int* index, KTN_Value* outputKey, KTN_Value* outputValue);
bool KTN_HashMapNextOrdered(KTN_HashMap* map, int* index, KTN_Value* outputKey, KTN_Value* outputValue);


#endif
