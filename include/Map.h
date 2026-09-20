#ifndef KATANE_MAP_H
#define KATANE_MAP_H

#include "Object.h"
#include "Value.h"

bool MapGet(KTN_ObjMap* map, KTN_Value key, KTN_Value* value);
bool MapSet(KTN_VM* vm, KTN_ObjMap* map, KTN_Value key, KTN_Value value);
bool MapGetKeys(KTN_VM* vm, KTN_ObjMap* map, KTN_Value* value);
bool MapGetValues(KTN_VM* vm, KTN_ObjMap* map, KTN_Value* value);

#endif