#include <stdio.h>
#include <string.h>

#include "Memory.h"
#include "Value.h"
#include "Object.h"

void ValueArrayInit(KTN_ValueArray* array) {
    //Intialize an empty array.
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void ValueArrayWrite(KTN_VM* vm, KTN_ValueArray* array, KTN_Value value) {
    //If there is not enough capacity for the new byte, then increase the size of the array.
    if (array->capacity < array->count + 1) {
        uint32_t oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(KTN_Value, array->values, oldCapacity, array->capacity);
    }

    //Store byte on array and increase the number of elements.
    array->values[array->count] = value;
    array->count++;
}

KTN_Value* ValueArrayGet(KTN_ValueArray* array, int position) {
    if (position < 0 || position > array->count)
        return NULL;
    
    return &array->values[position];
}

void ValueArrayFree(KTN_VM* vm, KTN_ValueArray* array) {
    //Free array memory.
    FREE_ARRAY(KTN_Value, array->values, array->capacity);

    //Reintialize array.
    ValueArrayInit(array);
}

void ValuePrint(KTN_Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
        return;
    }

    if (IS_NULL(value)) {
        printf("null");
        return;
    }

    if (IS_INT(value)) {
        printf("%d", AS_INT(value));
        return;
    }

    if (IS_DOUBLE(value)) {
        // Use the minimum number of significant digits that round-trips through
        // sscanf to uniquely identify this double. This matches Python 3 / Dart
        // behaviour: 100000.5 prints as "100000.5", not "100000".
        double number = AS_DOUBLE(value);
        char buffer[32];

        for (int precision = 6; precision <= 17; precision++) {
            snprintf(buffer, sizeof(buffer), "%.*g", precision, number);
            double parsed;
            if (sscanf(buffer, "%lf", &parsed) == 1 && parsed == number) break;
        }

        printf("%s", buffer);
        return;
    }

    if (IS_OBJECT(value)) {
        ObjectPrint(value);
    }
#else
    switch (value.type) {
        case VALUE_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;

        case VALUE_NULL:
            printf("null");
            break;

        case VALUE_INT:
            printf("%d", AS_INT(value));
            break;

        case VALUE_NUMBER: {
            double number = AS_DOUBLE(value);
            char buffer[32];

            for (int precision = 6; precision <= 17; precision++) {
                snprintf(buffer, sizeof(buffer), "%.*g", precision, number);
                double parsed;
                if (sscanf(buffer, "%lf", &parsed) == 1 && parsed == number) break;
            }

            printf("%s", buffer);
            break;
        }

        case VALUE_OBJECT:
            ObjectPrint(value);
            break;

        default: break;
    }
#endif
}

bool ValuesEqual(KTN_Value a, KTN_Value b) {
#ifdef NAN_BOXING
    if (a == b) {
        if (IS_DOUBLE(a)) return AS_DOUBLE(a) == AS_DOUBLE(b);
        return true;
    }

    if (IS_NUMERIC(a) && IS_NUMERIC(b))
        return AS_NUMERIC(a) == AS_NUMERIC(b);
        
    return false;
#else
    if (IS_NUMERIC(a) && IS_NUMERIC(b))
        return AS_NUMERIC(a) == AS_NUMERIC(b);

    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VALUE_BOOL:   return AS_BOOL(a)   == AS_BOOL(b);
        case VALUE_NULL:   return true;
        case VALUE_INT:    return AS_INT(a)     == AS_INT(b);
        case VALUE_NUMBER: return AS_DOUBLE(a)  == AS_DOUBLE(b);
        case VALUE_OBJECT: return AS_OBJECT(a)  == AS_OBJECT(b);
        default:           return false;
    }
#endif
}