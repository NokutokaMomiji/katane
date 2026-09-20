#ifndef KATANE_EXCEPTIONS_H
#define KATANE_EXCEPTIONS_H

#include "VM.h"

KTN_Value BuildStackTraceObject(KTN_VM* vm, KTN_ObjInstance* stackTraceInstance);
KTN_ObjInstance* KTN_ExceptionCreate(KTN_VM* vm, const char* type, KTN_ObjString* message);
const char* KTN_ValueTypeName(KTN_Value value);
void KTN_InitializeExceptions(KTN_VM* vm, KTN_ObjModule* module);

#endif
