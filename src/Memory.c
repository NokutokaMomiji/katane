#include <stdlib.h>
#include <stdio.h>

#include "Compiler.h"
#include "Memory.h"
#include "VM.h"
#include "TypeDescriptor.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "Debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(KTN_VM* vm, void* pointer, size_t oldSize, size_t newSize) {
    vm->allocatedBytes += newSize - oldSize;
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        KTN_MemoryCollectGarbage(vm);
#endif
        if (vm->allocatedBytes >= vm->nextCollection)
            KTN_MemoryCollectGarbage(vm);
    }

    if (newSize == 0) {
        //Free up memory.
        free(pointer);
        return NULL;
    }

    void* Result = realloc(pointer, newSize);

    //Exit if there is no more memory left.
    if (Result == NULL)
        exit(1);

    //Return pointer to memory.
    return Result;
}

void KTN_MemoryMarkObject(KTN_VM* vm, KTN_Object* object) {
    if (object == NULL)
        return;

    if (object->isMarked)
        return;

#ifdef DEBUG_LOG_GC
    printf("> %p mark ", (void*)object);
    ValuePrint(OBJECT_VALUE(object));
    printf("\n");
#endif

    object->isMarked = true;

    if (vm->grayCapacity < vm->grayCount + 1) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);
        vm->grayStack = (KTN_Object**)realloc(vm->grayStack, sizeof(KTN_Object*) * vm->grayCapacity);

        if (vm->grayStack == NULL && vm->safeguardStack != NULL) {
            free(vm->safeguardStack);
            vm->grayStack = (KTN_Object**)realloc(vm->grayStack, sizeof(KTN_Object*) * vm->grayCapacity);

            if (vm->grayStack == NULL)
                exit(1);
        }
        else if (vm->grayStack == NULL)
            exit(1);
    }

    vm->grayStack[vm->grayCount++] = object;
}

void KTN_MemoryMarkValue(KTN_VM* vm, KTN_Value value) {
    if (IS_OBJECT(value))
        KTN_MemoryMarkObject(vm, AS_OBJECT(value));
}

static void MarkArray(KTN_VM* vm, KTN_ValueArray* array) {
    for (uint32_t i = 0; i < array->count; i++) {
        KTN_MemoryMarkValue(vm, array->values[i]);
    }
}

static void BlackenObject(KTN_VM* vm, KTN_Object* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    ValuePrint(OBJECT_VALUE(object));
    printf("\n");
#endif
    switch (object->type) {
        case OBJ_CLOSURE: {
            KTN_ObjClosure* Closure = (KTN_ObjClosure*)object;
            KTN_MemoryMarkObject(vm, (KTN_Object*)Closure->function);
            for (int i = 0; i < Closure->upvalueCount; i++) {
                KTN_MemoryMarkObject(vm, (KTN_Object*)Closure->upvalues[i]);
            }
            break;
        }

        case OBJ_FUNCTION: {
            KTN_ObjShiki* Function = (KTN_ObjShiki*)object;
            KTN_MemoryMarkObject(vm, (KTN_Object*)Function->name);
            KTN_MemoryMarkObject(vm, (KTN_Object*)Function->signature);
            MarkArray(vm, &Function->chunk.constants);
            break;
        }

        case OBJ_UPVALUE:
            KTN_MemoryMarkValue(vm, ((KTN_ObjUpvalue*)object)->closed);
            break;
        
        case OBJ_CLASS: {
            KTN_ObjKata* kata = (KTN_ObjKata*)object;
            KTN_MemoryMarkObject(vm, (KTN_Object*)kata->className);
            MarkArray(vm, &kata->methodNames);
            TableMark(vm, &kata->methods);
            TableMark(vm, &kata->properties);
            TableMark(vm, &kata->fieldTypes);

            TableMark(vm, &kata->privateMembers);
            
            if (kata->docs != NULL)
                KTN_MemoryMarkObject(vm, (KTN_Object*)kata->docs);
            
            TableMark(vm, &kata->innerDocs);
            
            // Type parameters for generics.
            for (int i = 0; i < kata->typeParameterCount; i++) {
                KTN_MemoryMarkObject(vm, (KTN_Object*)kata->typeParameters[i].name);
                KTN_MemoryMarkObject(vm, (KTN_Object*)kata->typeParameters[i].constraint);
            }

            if (kata->overloads) {
                // Did not know this was a thing, but apparently it's very efficient so...
                int overloadCount = __builtin_popcount(kata->overloadMask);

                for (int i = 0; i < overloadCount; i++) {
                    if (kata->overloads[i] == NULL)
                        continue;
                    KTN_MemoryMarkObject(vm, (KTN_Object*)kata->overloads[i]);
                }
            }
            break;
        }

        case OBJ_INSTANCE: {
            KTN_ObjInstance* instance = (KTN_ObjInstance*)object;
            KTN_MemoryMarkObject(vm, (KTN_Object*)instance->kata);
            MarkArray(vm, &instance->propertyNames);
            TableMark(vm, &instance->properties);

            if (instance->typeArguments != NULL) {
                int parameterCount = instance->kata->typeParameterCount;

                for (int i = 0; i < parameterCount; i++) {
                    if (instance->typeArguments[i] == NULL)
                        continue;
                    KTN_MemoryMarkObject(vm, (KTN_Object*)instance->typeArguments[i]);
                }
            }
            break;
        }

        case OBJ_BOUND_METHOD: {
            KTN_ObjBoundMethod* bound = (KTN_ObjBoundMethod*)object;
            KTN_MemoryMarkValue(vm, bound->receiver);
            KTN_MemoryMarkObject(vm, (KTN_Object*)bound->method);
            break;
        }

        case OBJ_ARRAY: {
            KTN_ObjArray* Array = (KTN_ObjArray*)object;
            MarkArray(vm, &Array->items);
            break;
        }

        case OBJ_ENUM: {
            KTN_ObjEnum* _enum = (KTN_ObjEnum*)object;
            KTN_MemoryMarkObject(vm, (KTN_Object*)_enum->name);
            TableMark(vm, &_enum->variants);
            KTN_MemoryMarkObject(vm, (KTN_Object*)_enum->variantList);
            TableMark(vm, &_enum->methods);
            TableMark(vm, &_enum->getters);
            KTN_MemoryMarkObject(vm, (KTN_Object*)_enum->docs);
            break;
        }

        case OBJ_ENUM_VARIANT: {
            KTN_ObjEnumVariant* variant = (KTN_ObjEnumVariant*)object;
            KTN_MemoryMarkObject(vm, (KTN_Object*)variant->owner);
            KTN_MemoryMarkObject(vm, (KTN_Object*)variant->name);
            KTN_MemoryMarkValue(vm, variant->value);
            break;
        }

        case OBJ_MAP: {
            KTN_ObjMap* map = (KTN_ObjMap*)object;
            KTN_HashMapMark(vm, &map->map);
            break;
        }

        case OBJ_MODULE: {
            KTN_ObjModule* module = (KTN_ObjModule*)object;
            TableMark(vm, &module->values);
            break;
        }

        case OBJ_TYPE_DESCRIPTOR: {
            KTN_ObjTypeDescriptor* descriptor = (KTN_ObjTypeDescriptor*)object;

            if (descriptor->name)
                KTN_MemoryMarkObject(vm, (KTN_Object*)descriptor->name);

            if (descriptor->resolved)
                KTN_MemoryMarkObject(vm, (KTN_Object*)descriptor->resolved);

            for (int i = 0; i < descriptor->argumentCount; i++) {
                KTN_MemoryMarkObject(vm, (KTN_Object*)descriptor->arguments[i]);
            }

            if (descriptor->base)
                KTN_MemoryMarkObject(vm, (KTN_Object*)descriptor->base);
            
            break;
        }

        case OBJ_SIGNATURE: {
            KTN_ObjSignature* signature = (KTN_ObjSignature*)object;

            KTN_MemoryMarkObject(vm, (KTN_Object*)signature->display);
            KTN_MemoryMarkObject(vm, (KTN_Object*)signature->name);
            KTN_MemoryMarkObject(vm, (KTN_Object*)signature->returnType);

            for (int i = 0; i < signature->parameterCount; i++) {
                KTN_MemoryMarkObject(vm, (KTN_Object*)signature->parameters[i].name);
                KTN_MemoryMarkObject(vm, (KTN_Object*)signature->parameters[i].type);
            }

            break;
        }

        case OBJ_ACCESSOR: {
            KTN_ObjAccessor* accessor = (KTN_ObjAccessor*)object;

            KTN_MemoryMarkObject(vm, (KTN_Object*)accessor->getter);
            KTN_MemoryMarkObject(vm, (KTN_Object*)accessor->setter);
        
            break;
        }

        case OBJ_NATIVE:
        case OBJ_STRING:
            break;

        
    }
}

static void FreeObject(KTN_VM* vm, KTN_Object* object) {
#ifdef DEBUG_LOG_GC
    printf("> %p free type %d\n", (void*)object, object->type);
#endif
    switch(object->type) {
        case OBJ_STRING: {
            KTN_ObjString* string = (KTN_ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(KTN_ObjString, object);
            break;
        }

        case OBJ_ARRAY: {
            KTN_ObjArray* Array = (KTN_ObjArray*)object;
            ValueArrayFree(vm, &Array->items);
            FREE(KTN_ObjArray, object);
            break;
        }

        case OBJ_ENUM: {
            KTN_ObjEnum* _enum = (KTN_ObjEnum*)object;
            TableFree(vm, &_enum->variants);
            TableFree(vm, &_enum->methods);
            TableFree(vm, &_enum->getters);
            FREE(KTN_ObjEnum, object);
            break;
        }

        case OBJ_ENUM_VARIANT:
            FREE(KTN_ObjEnumVariant, object);
            break;

        case OBJ_NATIVE:
            FREE(KTN_ObjNative, object);
            break;

        case OBJ_CLOSURE: {
            KTN_ObjClosure* Closure = (KTN_ObjClosure*)object;
            FREE_ARRAY(KTN_ObjUpvalue*, Closure->upvalues, Closure->upvalueCount);
            FREE(KTN_ObjClosure, object);
            break;
        }
        
        case OBJ_UPVALUE:
            FREE(KTN_ObjUpvalue, object);
            break;
        
        case OBJ_FUNCTION: {
            KTN_ObjShiki* function = (KTN_ObjShiki*)object;
            KTN_ChunkFree(vm, &function->chunk);
            FREE(KTN_ObjShiki, object);
            break;
        }

        case OBJ_CLASS: {
            KTN_ObjKata* kata = (KTN_ObjKata*)object;
            ValueArrayFree(vm, &kata->methodNames);
            TableFree(vm, &kata->methods);
            TableFree(vm, &kata->properties);
            TableFree(vm, &kata->staticProperties);
            TableFree(vm, &kata->fieldTypes);

            TableFree(vm, &kata->privateMembers);

            TableFree(vm, &kata->innerDocs);

            if (kata->typeParameters != NULL)
                FREE_ARRAY(KTN_TypeParameter, kata->typeParameters, kata->typeParameterCount);

            if (kata->overloads)
                FREE_ARRAY(KTN_ObjClosure*, kata->overloads, __builtin_popcount(kata->overloadMask));

            FREE(KTN_ObjKata, object);
            break;
        }

        case OBJ_ACCESSOR: {
            KTN_ObjAccessor* accessor = (KTN_ObjAccessor*)object;
            
            FREE(KTN_ObjClosure, accessor->getter);
            FREE(KTN_ObjClosure, accessor->setter);
            
            FREE(KTN_ObjAccessor, accessor);
            break;
        }

        case OBJ_MAP: {
            KTN_ObjMap* map = (KTN_ObjMap*)object;
            KTN_HashMapFree(vm, &map->map);
            FREE(KTN_ObjMap, object);
            break;
        }

        case OBJ_INSTANCE: {
            KTN_ObjInstance* instance = (KTN_ObjInstance*)object;
            ValueArrayFree(vm, &instance->propertyNames);
            TableFree(vm, &instance->properties);

            if (instance->typeArguments != NULL) {
                int parameterCount = instance->kata->typeParameterCount;
                
                FREE_ARRAY(KTN_ObjTypeDescriptor*, instance->typeArguments, parameterCount);
            }

            FREE(KTN_ObjInstance, object);
            break;
        }

        case OBJ_MODULE: {
            KTN_ObjModule* module = (KTN_ObjModule*)object;
            TableFree(vm, &module->values);
            free(module->name);
            free(module->file);
            FREE(KTN_ObjModule, object);
            break;
        }

        case OBJ_TYPE_DESCRIPTOR: {
            KTN_ObjTypeDescriptor* descriptor = (KTN_ObjTypeDescriptor*)object;

            if (descriptor->arguments) {
                FREE_ARRAY(KTN_ObjTypeDescriptor*, descriptor->arguments, descriptor->argumentCount);
            }

            FREE(KTN_ObjTypeDescriptor, descriptor);
            break;
        }

        case OBJ_SIGNATURE: {
            KTN_ObjSignature* signature = (KTN_ObjSignature*)object;

            if (signature->parameters != NULL)
                FREE_ARRAY(KTN_SignatureParameter, signature->parameters, signature->parameterCount);

            FREE(KTN_ObjSignature, signature);
            break;
        }

        case OBJ_BOUND_METHOD:
            FREE(KTN_ObjBoundMethod, object);
            break;
    }
}

void KTN_MemoryFreeObjects(KTN_VM* vm) {
    KTN_Object* object = vm->objects;
    while (object != NULL) {
        KTN_Object* next = object->next;
        FreeObject(vm, object);
        object = next;
    }

    free(vm->grayStack);
    free(vm->safeguardStack);
}

void KTN_WellKnownNamesMark(KTN_VM* vm, KTN_WellKnownNames* names) {
    for (int i = 0; i < KTN_NAME_COUNT; i++) {
        if (names->values[i] != NULL)
            KTN_MemoryMarkObject(vm, (KTN_Object*)names->values[i]);
    }
}

static void MarkRoots(KTN_VM* vm) {
    KTN_MemoryMarkValue(vm, vm->finalResult);

    for (KTN_Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        KTN_MemoryMarkValue(vm, *slot);
    }

    for (int i = 0; i < vm->frameCount; i++) {
        KTN_MemoryMarkObject(vm, (KTN_Object*)vm->frames[i].closure);
        KTN_MemoryMarkObject(vm, (KTN_Object*)vm->frames[i].owner);
    }

    for (KTN_ObjUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        KTN_MemoryMarkObject(vm, (KTN_Object*)upvalue);
    }

    if (vm->hasPendingException)
        KTN_MemoryMarkValue(vm, vm->pendingException);
    if (vm->hasActiveHandlerException)
        KTN_MemoryMarkValue(vm, vm->activeHandlerException);

    KTN_WellKnownNamesMark(vm, &vm->wellKnownNames);

    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->initString);            // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->exceptionClass);        // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->stackTraceClass);       // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->typeInt);               // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->typeFloat);             // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->typeBool);              // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->typeString);            // ADD
    KTN_MemoryMarkObject(vm, (KTN_Object*)vm->typeNull);

    TableMark(vm, &vm->globals);
    TableMark(vm, &vm->modules);
    TableMark(vm, &vm->stringMethods);                                // ADD
    TableMark(vm, &vm->arrayMethods);                                 // ADD
    TableMark(vm, &vm->mapMethods);                                   // ADD
    TableMark(vm, &vm->fileMethods);                                  // ADD
    TableMark(vm, &vm->bytesMethods);
    KTN_DescriptorSetMark(vm, &vm->typeDescriptors);
    TableMark(vm, &vm->globalTypes);
    KTN_CompilerMarkRoots();
}

static void TraceReferences(KTN_VM* vm) {
    while (vm->grayCount > 0) {
        KTN_Object* object = vm->grayStack[--vm->grayCount];
        BlackenObject(vm, object);
    }
}

static void Sweep(KTN_VM* vm) {
    KTN_Object* Previous = NULL;
    KTN_Object* Current = vm->objects;

    while (Current != NULL) {
        if (Current->isMarked) {
            Current->isMarked = false;
            // If the current object is marked, then we know we don't have to get rid of it.
            // We merely set it as the previous object and move to the next.
            Previous = Current;
            Current = Current->next;
        }
        else {
            // We keep the currently unreached object.
            KTN_Object* Unreached = Current;

            // We check the following object to see if we ought to link the elements.
            Current = Current->next;

            // There is a previous element in the linked list, so we link the elements.
            if (Previous != NULL)
                Previous->next = Current;
            else    // There isn't, so the current element is now the beginning element in the linked list.i
                vm->objects = Current;

            FreeObject(vm, Unreached);
        }
    }
}

void KTN_MemoryCollectGarbage(KTN_VM* vm) {
#ifdef DEBUG_LOG_GC
    printf("-- [GC BEGIN] --\n");
    size_t Before = vm->allocatedBytes;
#endif

    MarkRoots(vm);
    TraceReferences(vm);
    TableRemoveWhite(vm, &vm->strings);
    Sweep(vm);

    vm->nextCollection = vm->allocatedBytes * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_LOG_GC
    printf("-- [GC END] --\n");
    printf("   > Collected %zu (from %zu to %zu). Next at %zu.\n", Before - vm->allocatedBytes, Before, vm->allocatedBytes, vm->nextCollection);
#endif
}
