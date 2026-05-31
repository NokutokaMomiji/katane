#include <stdio.h>
#include "Map.h"
#include "Table.h"

bool MapGet(KTN_ObjMap* map, KTN_Value key, KTN_Value* value) { return true; }

bool MapSet(KTN_VM* vm, KTN_ObjMap* map, KTN_Value key, KTN_Value value) { 
    return KTN_HashMapSet(vm, &map->map, key, value);
}