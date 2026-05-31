#ifndef KATANE_LIST_H
#define KATANE_LIST_H

#include "Object.h"
#include "Value.h"

extern void KTN_ArrayAdd(KTN_VM* vm, KTN_ObjArray* array, KTN_Value value);

bool KTN_ArraySet(KTN_VM* vm, KTN_ObjArray* array, KTN_Value index, KTN_Value value);
bool KTN_ArraySetRange(KTN_VM* vm, KTN_ObjArray* array, KTN_Value min, KTN_Value max, KTN_Value value);

bool KTN_ArrayGet(KTN_ObjArray* array, KTN_Value index, KTN_Value* value);
bool KTN_ArrayGetRange(KTN_VM* vm, KTN_ObjArray* array, KTN_Value min, KTN_Value max, KTN_Value step, KTN_Value* value);


#endif