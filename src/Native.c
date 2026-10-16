#include <stdlib.h>
#include <string.h>

#include "Native.h"
#include "TypeDescriptor.h"
#include "VM.h"
#include "Object.h"
#include "Table.h"
#include "Memory.h"
#include "Value.h"

KTN_ObjSignature* KTN_NativeBuiltinToSignature(KTN_VM* vm, const KTN_NativeSignatureItem* items) {
    const KTN_NativeSignatureItem* item = items;
    
    // Being more strict with my own functions.
    // Because this is an internal function, it does less error checks, like stack overflow. 
    const int maxParameterCount = 64;

    KTN_ObjString* name = NULL;
    KTN_ObjString* docs = NULL;
    KTN_SignatureParameterSpec parameters[maxParameterCount];
    KTN_ObjTypeDescriptor* returns = NULL;
    KTN_ShikiType type = TYPE_FUNCTION;
    KTN_Value* stackBase = vm->stackTop;

    int parameterIndex = 0;

    while (item->item != KTN_ITEM_END) {    
        switch (item->item) {
            case KTN_ITEM_NAME: {
                if (item->name == NULL) {
                    name = STRING_COPY_AUTO("<lambda>");
                } else {
                    name = STRING_COPY_AUTO(item->name);
                }

                Push(vm, OBJECT_VALUE(name));
                break;
            }
            case KTN_ITEM_PARAMETER: {
                if (parameterIndex > maxParameterCount) {
                    KTN_VMPanic(vm, "Native signature has more than %d parameters: %s", maxParameterCount, (name != NULL) ? name->chars : "<unknown>");
                }

                KTN_SignatureParameterSpec* spec = &parameters[parameterIndex++];
                
                spec->start = item->name;
                spec->length = strlen(item->name);
                spec->kind = item->kind;
                spec->defaultValue = item->defaultValue;
                spec->defaultIsImmutable = item->defaultIsImmutable;

                if (IS_OBJECT(item->defaultValue))
                    Push(vm, item->defaultValue);

                if (item->type != NULL) {
                    Push(vm, STRING_VALUE(item->type));

                    spec->type = KTN_TypeDescriptorNamed(vm, AS_STRING(Peek(vm, 0)));

                    Pop(vm);
                    Push(vm, OBJECT_VALUE(spec->type));
                }

                break;
            }
            case KTN_ITEM_DOCS: {
                if (!item->name) {
                    KTN_VMPanic(vm, "Native signature has docs param but set to NULL: %s.", (name != NULL) ? name->chars : "<unknown>");
                }

                Push(vm, STRING_VALUE(item->name));
                docs = AS_STRING(Peek(vm, 0));
                
                break;
            }
            case KTN_ITEM_RETURNS: {
                Push(vm, STRING_VALUE(item->type));

                returns = KTN_TypeDescriptorNamed(vm, AS_STRING(Peek(vm, 0)));

                Pop(vm);
                Push(vm, OBJECT_VALUE(returns));
                break;
            }
            case KTN_ITEM_SHIKI_TYPE: {
                type = item->shikiType;
                break;
            }
            case KTN_ITEM_END: break;
        }

        item++;
    }

    KTN_ObjSignature* signature = SignatureNew(
        vm,
        name,
        returns,
        parameters,
        parameterIndex,
        type,
        docs
    );

    vm->stackTop = stackBase;

    return signature;
}

KTN_NativeKataBuilder* KTN_NativeKataBegin(KTN_VM* vm, const char* name, const char* superclassName) {
    KTN_NativeKataBuilder* builder = malloc(sizeof(KTN_NativeKataBuilder));
    builder->vm = vm;

    KTN_ObjString* className = STRING_COPY_AUTO(name);
    Push(vm, OBJECT_VALUE(className));

    KTN_ObjKata* newClass = KataNew(vm, className);
    Push(vm, OBJECT_VALUE(newClass));

    if (superclassName != NULL) {
        KTN_Value superclassValue;
        KTN_ObjString* superclassKey = STRING_COPY_AUTO(superclassName);

        Push(vm, OBJECT_VALUE(superclassKey));

        if (TableGet(&vm->globals, superclassKey, &superclassValue) && IS_CLASS(superclassValue)) {
            KTN_ObjKata* sokata = AS_CLASS(superclassValue);
            
            TableAddAll(vm, &sokata->methods, &newClass->methods);
            TableAddAll(vm, &sokata->properties, &newClass->properties);

            newClass->sokata = sokata;

            // Inherit native constructor by default.
            if (!IS_NULL(sokata->constructor))
                newClass->constructor = sokata->constructor;
        }

        Pop(vm);
    }

    builder->builtClass = newClass;
    return builder;
}

KTN_ObjKata* KTN_NativeKataEnd(KTN_NativeKataBuilder* builder) {
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

void KTN_NativeKataAddMethod(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate, bool isStatic) {
    KTN_VM* vm = builder->vm;
    KTN_ObjString* methodName = STRING_COPY(vm, name, (int)strlen(name));
    Push(vm, OBJECT_VALUE(methodName));

    KTN_ObjNative* nativeObj = NativeNew(vm, function, name, signature);
    Push(vm, OBJECT_VALUE(nativeObj));
    
    nativeObj->type = TYPE_METHOD;
    
    KTN_Table* methodTable = (isStatic) ? &builder->builtClass->staticMethods : &builder->builtClass->methods;

    if (isPrivate) {
        KTN_Table* privateTable = (isStatic) ? &builder->builtClass->staticPrivateMembers : &builder->builtClass->privateMembers;
        // TODO: Add hidden, I suppose.
        TableSet(vm, privateTable, methodName, false);
    }

    TableSet(vm, methodTable, methodName, OBJECT_VALUE(nativeObj));
    Pop(vm); // nativeObj
    Pop(vm); // methodName
}

void KTN_NativeKataSetConstructor(KTN_NativeKataBuilder* builder, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate) {
    KTN_VM* vm = builder->vm;
    KTN_ObjNative* constructorNative = NativeNew(vm, function, builder->builtClass->className->chars, signature);
    Push(vm, OBJECT_VALUE(constructorNative));
    builder->builtClass->constructor = OBJECT_VALUE(constructorNative);
    //TableSet(vm, &builder->builtClass->methods, builder->builtClass->className, OBJECT_VALUE(constructorNative));
    builder->builtClass->privateConstructor = isPrivate;
    Pop(vm);
}

void KTN_NativeKataAddGetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate, bool isStatic) {
    KTN_VM* vm = builder->vm;
    KTN_Table* methods = (isStatic) ? &builder->builtClass->staticMethods : &builder->builtClass->methods;
    KTN_ObjString* key = STRING_COPY_AUTO(name);
    KTN_Value existing;
    KTN_ObjAccessor* accessor;

    if (TableGet(methods, key, &existing) && IS_ACCESSOR(existing)) {
        accessor = AS_ACCESSOR(existing);
    } else {
        accessor = AccessorNew(vm);
        TableSet(vm, methods, key, OBJECT_VALUE(accessor));
    }

    accessor->getter = OBJECT_VALUE((KTN_ObjNative*)NativeNew(vm, function, name, signature));

    if (isPrivate) {
        KTN_Table* privateTable = (isStatic) ? &builder->builtClass->staticPrivateMembers : &builder->builtClass->privateMembers;
        // TODO: Add hidden, I suppose.
        // TODO: Separate privacy from the general accessor.
        TableSet(vm, privateTable, key, false);
    }
}

void KTN_NativeKataAddSetter(KTN_NativeKataBuilder* builder, const char* name, NativeFnEx function, KTN_ObjSignature* signature, bool isPrivate, bool isStatic) {
    KTN_VM* vm = builder->vm;
    KTN_Table* methods = (isStatic) ? &builder->builtClass->staticMethods : &builder->builtClass->methods;
    KTN_ObjString* key = STRING_COPY_AUTO(name);
    KTN_Value existing;
    KTN_ObjAccessor* accessor;

    if (TableGet(methods, key, &existing) && IS_ACCESSOR(existing)) {
        accessor = AS_ACCESSOR(existing);
    } else {
        accessor = AccessorNew(vm);
        TableSet(vm, methods, key, OBJECT_VALUE(accessor));
    }

    accessor->setter = OBJECT_VALUE(NativeNew(vm, function, name, signature));

    if (isPrivate) {
        KTN_Table* privateTable = (isStatic) ? &builder->builtClass->staticPrivateMembers : &builder->builtClass->privateMembers;
        // TODO: Add hidden, I suppose.
        // TODO: Separate privacy from the general accessor.
        TableSet(vm, privateTable, key, false);
    }
}

void KTN_NativeKataAddProperty(KTN_NativeKataBuilder* builder, const char* name, KTN_Value defaultValue, bool isPrivate, bool isStatic) {
    KTN_VM* vm = builder->vm;
    KTN_Table* table = (isStatic) ? &builder->builtClass->staticProperties : &builder->builtClass->properties;
    KTN_ObjString* propertyName = STRING_COPY(vm, name, (int)strlen(name));

    Push(vm, OBJECT_VALUE(propertyName));
    TableSet(vm, table, propertyName, defaultValue);

    if (isPrivate) {
        KTN_Table* privateTable = (isStatic) ? &builder->builtClass->staticPrivateMembers : &builder->builtClass->privateMembers;
        // TODO: Add hidden, I suppose.
        TableSet(vm, privateTable, propertyName, false);
    }
 
    Pop(vm);
}

void KTN_NativeKataSetDoc(KTN_NativeKataBuilder* builder, const char* docs) {
    KTN_VM* vm = builder->vm;
    builder->builtClass->docs = STRING_COPY_AUTO(docs);
}

void KTN_NativeKataSetGenerics(KTN_NativeKataBuilder* builder) {
    builder->builtClass->optionalGenerics = true;
}

void KTN_NativeKataAddType(KTN_NativeKataBuilder* builder, const char* parameterName) {
    KTN_VM* vm = builder->vm;
    KTN_ObjKata* kata = builder->builtClass;
    int parameterCount = kata->typeParameterCount;

    kata->typeParameters = GROW_ARRAY(KTN_TypeParameter, kata->typeParameters, parameterCount, parameterCount + 1);

    kata->typeParameters[parameterCount].constraint = NULL;
    kata->typeParameterCount++;
}