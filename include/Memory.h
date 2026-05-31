#ifndef KATANE_MEMORY_H
#define KATANE_MEMORY_H

#include "Common.h"
#include "Object.h"

#define ALLOCATE(type, count) (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

//Increases capacity by a factor of two.
#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

#define FREE(type, pointer) reallocate(vm, pointer, sizeof(type), 0)

#define GROW_ARRAY(type, pointer, oldCount, newCount) (type*)reallocate(vm, pointer, sizeof(type) * (oldCount), sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) reallocate(vm, pointer, sizeof(type) * (oldCount), 0)

void* reallocate(KTN_VM* vm, void* pointer, size_t oldSize, size_t newSize);
void KTN_MemoryMarkObject(KTN_VM* vm, KTN_Object* object);
void KTN_MemoryMarkValue(KTN_VM* vm, KTN_Value value);
void KTN_MemoryCollectGarbage(KTN_VM* vm);
void KTN_MemoryFreeObjects(KTN_VM* vm);

#endif