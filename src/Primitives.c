#include "Primitives.h"
#include "Native.h"
 
KTN_ObjKata* KTN_PrimitiveClassInt(KTN_VM* vm) {
    KTN_NativeKataBuilder* builder = KTN_BeginNativeClass(vm, "Int", NULL);
    return KTN_EndNativeClass(builder);
}

void KTN_InitializePrimitives(KTN_VM* vm, KTN_ObjModule* module) {
    (void)module;

    static const char* primitiveNames[] = { "Int", "Float", "Bool", "String", "Null", "Array", "Map" };
    KTN_ObjKata** caches[] = { &vm->typeInt, &vm->typeFloat, &vm->typeBool, &vm->typeString, &vm->typeNull, &vm->typeArray, &vm->typeMap };
    for (int i = 0; i < 7; i++) {
        KTN_NativeKataBuilder* b = KTN_BeginNativeClass(vm, primitiveNames[i], NULL);
        *caches[i] = KTN_EndNativeClass(b);
    }
}
