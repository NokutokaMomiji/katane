#ifndef KATANE_PRIMITIVES_H
#define KATANE_PRIMITIVES_H

#include <stdlib.h>
#include "VM.h"

KTN_ObjKata* KTN_PrimitiveClassInt(KTN_VM* vm);
KTN_ObjKata* KTN_PrimitiveClassFloat(KTN_VM* vm);
KTN_ObjKata* KTN_PrimitiveClassBool(KTN_VM* vm);
KTN_ObjKata* KTN_PrimitiveClassString(KTN_VM* vm);
KTN_ObjKata* KTN_PrimitiveClassNull(KTN_VM* vm);
void KTN_InitializePrimitives(KTN_VM* vm, KTN_ObjModule* module);

#endif
