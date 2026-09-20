#ifndef KATANE_TYPE_DESCRIPTOR_H
#define KATANE_TYPE_DESCRIPTOR_H

#include "Object.h"

/**
 * Struct used by the VM at runtime to intern Type Descriptors, this ensures that only one
 *  escriptor exists at runtime per type. 
 *
 * It lives throughout the lifetime of the VM's run, and never deletes any instance, only inserts.
 * We can then get any type descriptor we want via its structural hash (hash made of its type + children).
 *
 * This structure is non-serializable since it keeps live pointers.
 * For serialization, look at KTN_DescriptorTable.
 */
typedef struct {
    KTN_ObjTypeDescriptor** buckets;
    int count;
    int capacity;
} KTN_DescriptorSet;

/** 
 * Struct used for serializing and deserializing Type Descriptors from compiled bytecode.
 *
 * During the serialization or deserialization process, we keep an index that is assigned to 
 * each descriptor. This way, every composite Descriptor can reference its children via indexes
 * instead of pointers (since these change from run to run).
 *
 * In terms of structure, it is the same as [KTN_DescriptorSet], but in order to avoid confusion since their respective
 * implementations are incompatible, we create a new struct.
 */
typedef struct {
    KTN_ObjTypeDescriptor** items;
    int count;
    int capacity;
} KTN_DescriptorTable;

bool KTN_DescriptorsEqual(const KTN_ObjTypeDescriptor* first, const KTN_ObjTypeDescriptor* second);

void KTN_DescriptorSetInit(KTN_DescriptorSet* set);
void KTN_DescriptorSetFree(KTN_VM* vm, KTN_DescriptorSet* set);
void KTN_DescriptorSetMark(KTN_VM* vm, KTN_DescriptorSet* set);

void KTN_DescriptorTableInit(KTN_DescriptorTable* table);
void KTN_DescriptorTableFree(KTN_DescriptorTable* table);
uint32_t KTN_DescriptorTableIndex(KTN_DescriptorTable* table, KTN_ObjTypeDescriptor* descriptor);
uint32_t KTN_DescriptorTableRegister(KTN_DescriptorTable* table, KTN_ObjTypeDescriptor* descriptor);


KTN_ObjTypeDescriptor* KTN_TypeDescriptorNamed(KTN_VM* vm, KTN_ObjString* name);
KTN_ObjTypeDescriptor* KTN_TypeDescriptorUnion(KTN_VM* vm, KTN_ObjTypeDescriptor** members, int count);
KTN_ObjTypeDescriptor* KTN_TypeDescriptorParam(KTN_VM* vm, KTN_ObjTypeDescriptor* base, KTN_ObjTypeDescriptor** parameters, int count);
KTN_ObjTypeDescriptor* KTN_TypeDescriptorNullable(KTN_VM* vm, KTN_ObjTypeDescriptor* inner);
KTN_ObjTypeDescriptor* KTN_TypeDescriptorTypeVar(KTN_VM* vm, KTN_ObjString* name);

int KTN_TypeDescriptorWrite(KTN_ObjTypeDescriptor* descriptor, KTN_DescriptorTable* table, uint8_t* buffer, int bufferCapacity, uint32_t (*getStringIndex)(KTN_ObjString*, void*), void* context);
KTN_ObjTypeDescriptor* KTN_TypeDescriptorRead(KTN_VM* vm, const uint8_t* buffer, int bufferLength, int* offset, KTN_DescriptorTable* table, KTN_ObjString* (*getStringByIndex)(uint32_t, void*), void* context);

bool KTN_TypeDescriptorCheck(KTN_VM* vm, KTN_Value value, KTN_ObjTypeDescriptor* descriptor);
bool KTN_TypeDescriptorCheckWithArgs(KTN_VM* vm, KTN_Value value, KTN_ObjTypeDescriptor* descriptor, KTN_ObjKata* genericClass, KTN_ObjTypeDescriptor** typeArguments);
void KTN_TypeDescriptorFormat(KTN_ObjTypeDescriptor* descriptor, char* buffer, int bufferSize);
void KTN_TypeDescriptorPreResolvePrimitives(KTN_VM* vm);

#endif
