#include <stdio.h>
#include "Map.h"
#include "Table.h"
#include "VM.h"
#include "Utilities.h"

bool MapGet(KTN_ObjMap* map, KTN_Value key, KTN_Value* value) { return true; }

bool MapSet(KTN_VM* vm, KTN_ObjMap* map, KTN_Value key, KTN_Value value) { 
    return KTN_HashMapSet(vm, &map->map, key, value);
}

bool MapGetKeys(KTN_VM* vm, KTN_ObjMap* map, KTN_Value* value) {
    KTN_ObjArray* array = ArrayNew(vm);
    Push(vm, OBJECT_VALUE(array));

    int index = map->map.orderHead;
    KTN_Value entryKey;
    KTN_Value entryValue;

    while (KTN_HashMapNextOrdered(&map->map, &index, &entryKey, &entryValue)) {
        ValueArrayWrite(vm, &array->items, entryKey);
    }

    *value = OBJECT_VALUE(array);

    Pop(vm);
    return true;
}

bool MapGetValues(KTN_VM* vm, KTN_ObjMap* map, KTN_Value* value) {
    KTN_ObjArray* array = ArrayNew(vm);
    Push(vm, OBJECT_VALUE(array));

    int index = map->map.orderHead;
    KTN_Value entryKey;
    KTN_Value entryValue;

    while (KTN_HashMapNextOrdered(&map->map, &index, &entryKey, &entryValue)) {
        ValueArrayWrite(vm, &array->items, entryValue);
    }

    *value = OBJECT_VALUE(array);

    Pop(vm);
    return true;
}