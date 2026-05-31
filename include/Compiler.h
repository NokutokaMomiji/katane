#ifndef KATANE_COMPILER_H
#define KATANE_COMPILER_H

#include "Chunk.h"
#include "VM.h"
#include "Object.h"

KTN_ObjShiki* KTN_Compile(KTN_VM* vm, const char* source);
void KTN_CompilerMarkRoots();

#endif