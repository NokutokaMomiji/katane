#ifndef KATANE_VALUE_H
#define KATANE_VALUE_H

#include <string.h>

#include "Common.h"
#include "Config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KTN_Object KTN_Object;
typedef struct KTN_ObjString KTN_ObjString; 
typedef struct KTN_VM KTN_VM;

typedef enum {
    VALUE_BOOL,
    VALUE_NULL,
    VALUE_INT,
    VALUE_NUMBER,
    VALUE_OBJECT,
    VALUE_EMPTY
} KTN_ValueType;

#ifdef NAN_BOXING
typedef uint64_t KTN_Value;

#define SIGN_BIT            ((uint64_t)0x8000000000000000)
#define QNAN                ((uint64_t)0x7ffc000000000000)
#define TAG_NULL            1  // 00
#define TAG_FALSE           2  // 01
#define TAG_TRUE            3  // 11

// TAG_INT lives at bit 32, safely above the 32-bit integer payload
//  and below the QNAN payload area (bits 49-32 are all zero in QNAN).
// This prevents the tag from colliding with any bit of a stored int32_t.
#define TAG_INT             ((uint64_t)1 << 32)
#define TAG_EMPTY           ((uint64_t)1 << 33)

#define IS_NULL(value)      ((value) == NULL_VALUE)
#define IS_BOOL(value)      (((value) | 1) == TRUE_VALUE)
#define IS_INT(value)       (((value) & (QNAN | SIGN_BIT | TAG_INT)) == (QNAN | TAG_INT))
#define IS_DOUBLE(value)    (((value) & QNAN) != QNAN)
#define IS_NUMERIC(value)   (IS_INT(value) || IS_DOUBLE(value))
#define IS_OBJECT(value)    (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define IS_EMPTY(value)     ((value) == EMPTY_VALUE)

#define AS_BOOL(value)      ((value) == TRUE_VALUE)
#define AS_INT(value)       ((int32_t)((value) & 0xFFFFFFFFULL))
#define AS_DOUBLE(value)    (ValueToDouble(value))
#define AS_NUMERIC(value)   (IS_INT(value) ? (double)AS_INT(value) : AS_DOUBLE(value))
#define AS_OBJECT(value)    ((KTN_Object*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define NULL_VALUE          ((KTN_Value)(uint64_t)(QNAN | TAG_NULL))
#define FALSE_VALUE         ((KTN_Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VALUE          ((KTN_Value)(uint64_t)(QNAN | TAG_TRUE))
#define EMPTY_VALUE         ((KTN_Value)(uint64_t)(QNAN | TAG_EMPTY))
#define BOOL_VALUE(value)   ((value) ? TRUE_VALUE : FALSE_VALUE)
#define DOUBLE_VALUE(num)   (DoubleToValue(num))
#define INT_VALUE(num)      ((KTN_Value)(QNAN | TAG_INT | (uint64_t)(uint32_t)(int32_t)(num)))
#define OBJECT_VALUE(value) ((KTN_Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(value)))

static inline double ValueToDouble(KTN_Value value) {
    double num;
    memcpy(&num, &value, sizeof(KTN_Value));
    return num;
}

static inline KTN_Value DoubleToValue(double num) {
    KTN_Value value;
    memcpy(&value, &num, sizeof(double));
    return value;
}

#else
typedef struct {
    KTN_ValueType type;
    union {
        bool boolean;
        int32_t integer;
        double number;
        KTN_Object* object;
    } as;
} KTN_Value;

// These macros check if the type assigned to a Value object is the one we are looking for
// This is in order to make sure that the type of the value is the one that we want when trying 
//  to get the actual value from the union

#define IS_BOOL(value)      ((value).type == VALUE_BOOL)    
#define IS_NULL(value)      ((value).type == VALUE_NULL)
#define IS_INT(value)       ((value).type == VALUE_INT)
#define IS_DOUBLE(value)    ((value).type == VALUE_NUMBER)
#define IS_NUMERIC(value)   (IS_INT(value) || IS_DOUBLE(value))
#define IS_OBJECT(value)    ((value).type == VALUE_OBJECT)
#define IS_EMPTY(value)     ((value).type == VALUE_EMPTY)

// Unpack the C value from the Value object.
// Since these access the union, we need to make sure that the value type is the correct one before using the macro.

#define AS_BOOL(value)      ((value).as.boolean)
#define AS_INT(value)       ((value).as.integer)
#define AS_DOUBLE(value)    ((value).as.number)
#define AS_NUMERIC(value)   (IS_INT(value) ? (double)(value).as.integer : (value).as.number)
#define AS_OBJECT(value)    ((value).as.object)

// Allow us to pass a C value to our custom Value object with the appropriate tag and value.

#define BOOL_VALUE(value)   ((KTN_Value){VALUE_BOOL,   {.boolean = value}})
#define NULL_VALUE          ((KTN_Value){VALUE_NULL,   {.number = 0}})
#define DOUBLE_VALUE(value) ((KTN_Value){VALUE_NUMBER, {.number = value}})
#define INT_VALUE(value)    ((KTN_Value){VALUE_INT,    {.integer = (int32_t)(value)}})
#define OBJECT_VALUE(value) ((KTN_Value){VALUE_OBJECT, {.object = (KTN_Object*)value}})
#define EMPTY_VALUE         ((KTN_Value){VALUE_EMPTY,  {.number = 0}})

#endif

#define STRING_VALUE(value) OBJECT_VALUE(StringCopy(vm, value, (int)strlen(value), true))
#define STRING_COPY_AUTO(value) StringCopy(vm, value, (int)strlen(value), true)

#define STRING_COPY(vm, value, length) StringCopy(vm, value, length, true)
#define STRING_TAKE(vm, value, length) StringTake(vm, value, length, true)
#define STRING_COPY_RAW(vm, value, length) StringCopy(vm, value, length, false)
#define STRING_TAKE_RAW(vm, value, length) StringCopy(vm, value, length, false)

typedef struct {
    uint32_t capacity;   // Contains the full capacity of the array.
    uint32_t count;      // Number of elements in array.
    KTN_Value* values;       // Elements.
} KTN_ValueArray;

void ValueArrayInit(KTN_ValueArray* array);
void ValueArrayWrite(KTN_VM* vm, KTN_ValueArray* array, KTN_Value value);
KTN_Value* ValueArrayGet(KTN_ValueArray* array, int position);
void ValueArrayFree(KTN_VM* vm, KTN_ValueArray* array);

void ValuePrint(KTN_Value value);
bool ValuesEqual(KTN_Value a, KTN_Value b);

#ifdef __cplusplus
}
#endif

#endif