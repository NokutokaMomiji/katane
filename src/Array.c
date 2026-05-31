#include <stdio.h>

#include "Array.h"
#include "VM.h"
#include "Utilities.h"

inline void KTN_ArrayAdd(KTN_VM* vm, KTN_ObjArray* array, KTN_Value value) {
    Push(vm, value);
    ValueArrayWrite(vm, &array->items, value);
    Pop(vm);
}

bool KTN_ArraySet(KTN_VM* vm, KTN_ObjArray* array, KTN_Value index, KTN_Value value) {
    if (!IS_INT(index))
        return false;

    int i = (int)AS_INT(index);
    
    i = (i < 0) ? array->items.count + i : i;

    if (i < 0 || i > array->items.count)
        return false;

    if (i == array->items.count) {
        ValueArrayWrite(vm, &array->items, value);
        return true;
    }

    array->items.values[i] = value;
    return true;
}

bool KTN_ArraySetRange(KTN_VM* vm, KTN_ObjArray* array, KTN_Value min, KTN_Value max, KTN_Value value) {
    if ((!IS_INT(min) && !IS_NULL(min)) || (!IS_INT(max) && !IS_NULL(max)))
        return false;

    int rangeMin = IS_INT(min) ? (int)AS_INT(min) : 0;
    int rangeMax = IS_INT(max) ? (int)AS_INT(max) : array->items.count;

    rangeMin = (rangeMin < 0) ? array->items.count + rangeMin : rangeMin;
    rangeMax = (rangeMax < 0) ? array->items.count + rangeMax : rangeMax;

    if (rangeMin < 0 || rangeMax < 0 || rangeMin > array->items.count)
        return false;

    for (int i = rangeMin; i < rangeMax && i < array->items.count; i++)
        array->items.values[i] = value;

    return true;
}

bool KTN_ArrayGet(KTN_ObjArray* array, KTN_Value index, KTN_Value* value) {
    if (!IS_INT(index))
        return false;

    int i = (int)AS_INT(index);
    i = (i < 0) ? array->items.count + i : i;

    if (i < 0 || i >= array->items.count)
        return false;

    *value = array->items.values[i];
    return true;
}

// Slicing: end index is exclusive (Python-style). Step defaults to 1.
// [start:end:step] — null start = 0, null end = array length.
bool KTN_ArrayGetRange(KTN_VM* vm, KTN_ObjArray* array, KTN_Value min, KTN_Value max, KTN_Value step, KTN_Value* value) {
    if ((!IS_INT(min) && !IS_NULL(min)) || (!IS_INT(max) && !IS_NULL(max)))
        return false;

    int rangeMin = IS_INT(min) ? (int)AS_INT(min) : 0;
    int rangeMax = IS_INT(max) ? (int)AS_INT(max) : array->items.count;

    rangeMin = (rangeMin < 0) ? array->items.count + rangeMin : rangeMin;
    rangeMax = (rangeMax < 0) ? array->items.count + rangeMax : rangeMax;

    if (rangeMin < 0 || rangeMin > array->items.count || rangeMax < 0 || rangeMax > array->items.count)
        return false;

    int Step = IS_INT(step) ? (int)AS_INT(step) : 1;
    if (Step == 0) Step = 1;

    // Negative step: iterate backwards, swap bounds if not already reversed.
    if (Step < 0 && rangeMax >= rangeMin) {
        int tmp = rangeMin;
        rangeMin = rangeMax - 1;
        rangeMax = tmp - 1;
    }

    KTN_ObjArray* newArray = ArrayNew(vm);

    if (Step > 0) {
        for (int i = rangeMin; i < rangeMax; i += Step)
            ValueArrayWrite(vm, &newArray->items, array->items.values[i]);
    } else {
        for (int i = rangeMin; i > rangeMax; i += Step)
            ValueArrayWrite(vm, &newArray->items, array->items.values[i]);
    }

    *value = OBJECT_VALUE(newArray);
    return true;
}
