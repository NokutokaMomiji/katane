#ifndef KATANE_DEBUG_H
#define KATANE_DEBUG_H

#include "Chunk.h"
#include "Value.h"

void DisassembleChunk(KTN_Chunk* chunk, const char* name);
int DisassembleInstruction(KTN_Chunk* chunk, int offset);
int GetLine(KTN_Chunk* chunk, int offset);

#endif