#include <stdlib.h>
#include <string.h>

#include "Native.h"
#include "VM.h"
#include "Object.h"
#include "Table.h"
#include "Memory.h"

KTN_NativeKataBuilder* KTN_BeginNativeClass(KTN_VM* vm, const char* name, const char* superclassName) {
    KTN_NativeKataBuilder* builder = malloc(sizeof(KTN_NativeKataBuilder));
    builder->vm = vm;

    KTN_ObjString* className = STRING_COPY(name);
    Push(vm, OBJECT_VALUE(className));
    KTN_ObjKata* newClass = KataNew(vm, className);
    Push(vm, OBJECT_VALUE(newClass));

    if (superclassName != NULL) {
        KTN_Value superclassValue;
        KTN_ObjString* superclassKey = STRING_COPY(superclassName);
        Push(vm, OBJECT_VALUE(superclassKey));

        if (TableGet(&vm->globals, superclassKey, &superclassValue) && IS_CLASS(superclassValue)) {
            KTN_ObjKata* sokata = AS_CLASS(superclassValue);
            
            TableAddAll(vm, &sokata->methods, &newClass->methods);
            TableAddAll(vm, &sokata->properties, &newClass->properties);

            newClass->sokata = sokata;
            // Inherit native constructor by default
            if (!IS_NULL(sokata->constructor))
                newClass->constructor = sokata->constructor;
        }

        Pop(vm);
    }

    builder->builtClass = newClass;
    // Keep class and className on the stack for GC protection while building.
    return builder;
}

KTN_ObjKata* KTN_EndNativeClass(KTN_NativeKataBuilder* builder) {
    KTN_VM* vm = builder->vm;
    KTN_ObjKata* result = builder->builtClass;

    // Register in globals.
    TableSet(vm, &vm->globals, result->className, OBJECT_VALUE(result));

    // Pop the class and className that were pushed for GC protection.
    Pop(vm); // builtClass
    Pop(vm); // className

    free(builder);
    return result;
}

void KTN_AddNativeMethod(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_ObjString* methodName = StringCopy(vm, name, (int)strlen(name));
    Push(vm, OBJECT_VALUE(methodName));
    KTN_ObjNative* nativeObj = NativeNew(vm, function, name, signature, docs);
    Push(vm, OBJECT_VALUE(nativeObj));
    nativeObj->type = TYPE_METHOD;
    TableSet(vm, &builder->builtClass->methods, methodName, OBJECT_VALUE(nativeObj));
    Pop(vm); // nativeObj
    Pop(vm); // methodName
}

void KTN_SetNativeConstructor(KTN_NativeKataBuilder* builder, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_ObjNative* constructorNative = NativeNew(vm, function, builder->builtClass->className->chars, signature, docs);
    Push(vm, OBJECT_VALUE(constructorNative));
    builder->builtClass->constructor = OBJECT_VALUE(constructorNative);
    // Also store in methods table under the class name (for super calls etc.)
    TableSet(vm, &builder->builtClass->methods, builder->builtClass->className, OBJECT_VALUE(constructorNative));
    Pop(vm);
}

void KTN_AddNativeGetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_Table* methods = &builder->builtClass->methods;
    KTN_ObjString* key = STRING_COPY(name);
    KTN_Value existing;
    KTN_ObjAccessor* accessor;

    if (TableGet(methods, key, &existing) && IS_ACCESSOR(existing)) {
        accessor = AS_ACCESSOR(existing);
    } else {
        accessor = AccessorNew(vm);
        TableSet(vm, methods, key, OBJECT_VALUE(accessor));
    }

    accessor->getter = (KTN_ObjClosure*)NativeNew(vm, function, name, signature, docs);
}

void KTN_AddNativeSetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_Table* methods = &builder->builtClass->methods;
    KTN_ObjString* key = STRING_COPY(name);
    KTN_Value existing;
    KTN_ObjAccessor* accessor;

    if (TableGet(methods, key, &existing) && IS_ACCESSOR(existing)) {
        accessor = AS_ACCESSOR(existing);
    } else {
        accessor = AccessorNew(vm);
        TableSet(vm, methods, key, OBJECT_VALUE(accessor));
    }

    accessor->setter = (KTN_ObjClosure*)NativeNew(vm, function, name, signature, docs);
}

void KTN_AddNativeStaticMethod(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_ObjNative* native = NativeNew(vm, function, name, signature, docs);
    TableSet(vm, &builder->builtClass->staticMethods, STRING_COPY(name), OBJECT_VALUE(native));
}

void KTN_AddNativeStaticGetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_Table* methods = &builder->builtClass->staticMethods;
    KTN_ObjString* key = STRING_COPY(name);
    KTN_Value existing;
    KTN_ObjAccessor* accessor;

    if (TableGet(methods, key, &existing) && IS_ACCESSOR(existing)) {
        accessor = AS_ACCESSOR(existing);
    } else {
        accessor = AccessorNew(vm);
        TableSet(vm, methods, key, OBJECT_VALUE(accessor));
    }

    accessor->getter = (KTN_ObjClosure*)NativeNew(vm, function, name, signature, docs);
}

void KTN_AddNativeStaticSetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    KTN_VM* vm = builder->vm;
    KTN_Table* methods = &builder->builtClass->staticMethods;
    KTN_ObjString* key = STRING_COPY(name);
    KTN_Value existing;
    KTN_ObjAccessor* accessor;

    if (TableGet(methods, key, &existing) && IS_ACCESSOR(existing)) {
        accessor = AS_ACCESSOR(existing);
    } else {
        accessor = AccessorNew(vm);
        TableSet(vm, methods, key, OBJECT_VALUE(accessor));
    }

    accessor->setter = (KTN_ObjClosure*)NativeNew(vm, function, name, signature, docs);
}

void KTN_AddNativeStaticProperty(KTN_NativeKataBuilder* builder, const char* name, KTN_Value defaultValue) {
    KTN_VM* vm = builder->vm;
    TableSet(vm, &builder->builtClass->staticProperties, STRING_COPY(name), defaultValue);
}

void KTN_AddNativeProperty(KTN_NativeKataBuilder* builder, const char* name, KTN_Value defaultValue) {
    KTN_VM* vm = builder->vm;
    KTN_ObjString* propName = StringCopy(vm, name, (int)strlen(name));
    Push(vm, OBJECT_VALUE(propName));
    TableSet(vm, &builder->builtClass->properties, propName, defaultValue);
    Pop(vm);
}

void KTN_SetClassDoc(KTN_NativeKataBuilder* builder, const char* docs) {
    KTN_VM* vm = builder->vm;
    builder->builtClass->docs = STRING_COPY(docs);
}

void KTN_MarkNativePrivate(KTN_NativeKataBuilder* builder, const char* name) {
    KTN_VM* vm = builder->vm;
    TableSet(vm, &builder->builtClass->privateMembers, STRING_COPY(name), TRUE_VALUE);
}

void KTN_SetPrivateConstructor(KTN_NativeKataBuilder* builder) {
    builder->builtClass->privateConstructor = true;
}

void KTN_SetOptionalGenerics(KTN_NativeKataBuilder* builder) {
    builder->builtClass->optionalGenerics = true;
}

void KTN_AddTypeParameter(KTN_NativeKataBuilder* builder, const char* parameterName) {
    KTN_VM* vm = builder->vm;
    KTN_ObjKata* kata = builder->builtClass;
    int parameterCount = kata->typeParameterCount;

    kata->typeParameters = GROW_ARRAY(KTN_TypeParameter, kata->typeParameters, parameterCount, parameterCount + 1);

    kata->typeParameters[parameterCount].constraint = NULL;
    kata->typeParameterCount++;
}