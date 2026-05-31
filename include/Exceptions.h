#ifndef KATANE_EXCEPTIONS_H
#define KATANE_EXCEPTIONS_H

#include "VM.h"

KTN_Value BuildStackTraceObject(KTN_VM* vm, KTN_ObjInstance* stackTraceInstance);
void KTN_InitializeExceptions(KTN_VM* vm, KTN_ObjModule* module);

#endif
