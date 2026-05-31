#include <stdio.h>
#include <inttypes.h>
#include "Debug.h"
#include "Object.h"
#include "Value.h"

/// @brief ``[DEBUG]`` Displays the stored information in a chunk array.
/// @param chunk Chunk that contains the information to display.
/// @param name Name for identification.
void DisassembleChunk(KTN_Chunk* chunk, const char* name) {
    printf("|==[ %s ]==|\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = DisassembleInstruction(chunk, offset);
    }
}

/// @brief [INTERNAL] Displays an instruction and increases the offset.
/// @param name Name of the instruction.
/// @param offset Current instruction offset.
/// @return Next offset.
static int SimpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

static int ByteInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint8_t Slot = chunk->code[offset + 1];
    printf("%-16s %4d\n", name, Slot);
    return offset + 2;
}

static int ShortInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint16_t value = (uint16_t)(chunk->code[offset + 1] << 8) | chunk->code[offset + 2];
    printf("%-16s %4d\n", name, value);
    return offset + 3;
}

static int JumpInstruction(const char* name, int sign, KTN_Chunk* chunk, int offset) {
    uint16_t Jump = (uint16_t)(chunk->code[offset + 1] << 8);
    Jump |= chunk->code[offset + 2];
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * Jump);
    return offset + 3;
}
/// @brief Shows value of constant (byte)
/// @param name Name of the constant instruction (OP_CONSTANT)
/// @param chunk Chunk that contains the constants.
/// @param offset Offset inside progam.
/// @return New offset (+2).
static int ConstantInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint8_t Constant = chunk->code[offset + 1]; //Grab constant index from chunk.
    printf("%-16s %4d '", name, Constant);
    ObjectRepr(chunk->constants.values[Constant]);
    printf("'\n");
    return offset + 2; //We skip over the Constant Operation Code + the Constant Index.
}

static int ConstantLongInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint32_t constantIndex = (uint32_t)(chunk->code[offset + 1] << 24)
                           | (uint32_t)(chunk->code[offset + 2] << 16)
                           | (uint32_t)(chunk->code[offset + 3] << 8)
                           | (uint32_t)(chunk->code[offset + 4]);

    printf("%-16s %4" PRIu32 " '", name, constantIndex);
    ObjectRepr(chunk->constants.values[constantIndex]);
    printf("'\n");
    return offset + 5;  // We skip over the Constant Operation Code + the 4 bytes that make up the long index.
}

static int ConstantLongPairInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint32_t first  = (uint32_t)(chunk->code[offset + 1] << 24)
                    | (uint32_t)(chunk->code[offset + 2] << 16)
                    | (uint32_t)(chunk->code[offset + 3] << 8)
                    | (uint32_t)(chunk->code[offset + 4]);

    uint32_t second = (uint32_t)(chunk->code[offset + 5] << 24)
                    | (uint32_t)(chunk->code[offset + 6] << 16)
                    | (uint32_t)(chunk->code[offset + 7] << 8)
                    | (uint32_t)(chunk->code[offset + 8]);

    printf("%-16s %4u '", name, first);
    ObjectRepr(chunk->constants.values[first]);
    printf("' desc=%u\n", second);

    return offset + 9;
}

static int InitPropertyInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint32_t constantIndex = (uint32_t)(chunk->code[offset + 1] << 24)
                           | (uint32_t)(chunk->code[offset + 2] << 16)
                           | (uint32_t)(chunk->code[offset + 3] << 8)
                           | (uint32_t)(chunk->code[offset + 4]);
    uint8_t isStatic = chunk->code[offset + 5];
    uint8_t flags = chunk->code[offset + 6];

    printf("%-16s %4" PRIu32 " '", name, constantIndex);
    ObjectRepr(chunk->constants.values[constantIndex]);
    printf("' static=%u flags=%u\n", isStatic, flags);
    return offset + 7;
}

static int InitTypedPropertyInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint32_t nameIndex = (uint32_t)(chunk->code[offset + 1] << 24)
                       | (uint32_t)(chunk->code[offset + 2] << 16)
                       | (uint32_t)(chunk->code[offset + 3] << 8)
                       | (uint32_t)(chunk->code[offset + 4]);
    uint8_t isStatic = chunk->code[offset + 5];
    uint32_t descriptorIndex = (uint32_t)(chunk->code[offset + 6] << 24)
                             | (uint32_t)(chunk->code[offset + 7] << 16)
                             | (uint32_t)(chunk->code[offset + 8] << 8)
                             | (uint32_t)(chunk->code[offset + 9]);
    uint8_t flags = chunk->code[offset + 10];

    printf("%-16s %4" PRIu32 " '", name, nameIndex);
    ObjectRepr(chunk->constants.values[nameIndex]);
    printf("' static=%u desc=%" PRIu32 " flags=%u\n", isStatic, descriptorIndex, flags);
    return offset + 11;
}

static int DescriptorInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint8_t count = chunk->code[offset + 1];
    printf("%-24s count=%u\n", name, count);
    
    int next = offset + 2;
    
    for (int i = 0; i < (int)count; i++) {
        uint8_t  slot = chunk->code[next];
        uint32_t idx  = (uint32_t)(chunk->code[next + 1] << 24)
                        | (uint32_t)(chunk->code[next + 2] << 16)
                        | (uint32_t)(chunk->code[next + 3] << 8)
                        | (uint32_t)(chunk->code[next + 4]);
        printf("   slot=%-3u desc=%u\n", slot, idx);
        next += 5;
    }
    
    return next;
}

static KTN_MAYBE_UNUSED int InvokeInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint8_t constant = chunk->code[offset + 1];
    uint8_t argumentCount = chunk->code[offset + 2];

    printf("%-16s (%d args) %4d '", name, argumentCount, constant);
    ObjectRepr(chunk->constants.values[constant]);
    printf("'\n");

    return offset + 3;
}

static int InvokeInstructionLong(const char* name, KTN_Chunk* chunk, int offset) {
    uint32_t constant = (uint32_t)(chunk->code[offset + 1] << 24);
    constant |= (uint32_t)(chunk->code[offset + 2] << 16);
    constant |= (uint32_t)(chunk->code[offset + 3] << 8);
    constant |= (uint32_t)(chunk->code[offset + 4]);

    uint8_t argumentCount = chunk->code[offset + 5];

    printf("%-16s (%d args) %4d '", name, argumentCount, constant);
    ObjectRepr(chunk->constants.values[constant]);
    printf("'\n");

    return offset + 6;
}

static int TypedLocalInstruction(const char* name, KTN_Chunk* chunk, int offset) {
    uint8_t slot = chunk->code[offset + 1];
    uint32_t descriptor = (uint32_t)(chunk->code[offset + 2] << 24)
                        | (uint32_t)(chunk->code[offset + 3] << 16)
                        | (uint32_t)(chunk->code[offset + 4] << 8)
                        | (uint32_t)(chunk->code[offset + 5]);

    printf("%-16s slot=%d desc=%u\n", name, slot, descriptor);
    return offset + 6;
}

/// @brief [DEBUG] Prints out an instruction from a Chunk array at the given offset.
/// @param chunk Chunk array with instructions.
/// @param offset Instruction offset.
/// @return Next offset.
int DisassembleInstruction(KTN_Chunk* chunk, int offset) {
    printf("%04d ", offset);

    printf("%4d ", KTN_ChunkGetLine(chunk, offset));

    uint8_t instruction = chunk->code[offset];
    switch(instruction) {
        case OP_CONSTANT:
            return ConstantInstruction("OP_CONSTANT", chunk, offset);
        case OP_CONSTANT_LONG:
            return ConstantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
        case OP_NULL:
            return SimpleInstruction("OP_NULL", offset);
        case OP_TRUE:   
            return SimpleInstruction("OP_TRUE", offset);
        case OP_FALSE:
            return SimpleInstruction("OP_FALSE", offset);
        case OP_MAYBE:
            return SimpleInstruction("OP_MAYBE", offset);
        case OP_POP:
            return SimpleInstruction("OP_POP", offset);
        case OP_POP_RESULT:
            return SimpleInstruction("OP_POP_RESULT", offset);
        case OP_DUPLICATE:
            return SimpleInstruction("OP_DUPLICATE", offset);
        case OP_SWAP:
            return SimpleInstruction("OP_SWAP", offset);
	    case OP_DEFINE_GLOBAL:
            return ConstantLongInstruction("OP_DEFINE_GLOBAL", chunk, offset);
        case OP_DEFINE_GLOBAL_TYPED:
            return ConstantLongPairInstruction("OP_DEFINE_GLOBAL_TYPED", chunk, offset);
        case OP_GET_GLOBAL:
            return ConstantLongInstruction("OP_GET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL:
            return ConstantLongInstruction("OP_SET_GLOBAL", chunk, offset);
        case OP_SET_GLOBAL_TYPED:
            return ConstantLongPairInstruction("OP_SET_GLOBAL_TYPED", chunk, offset);
        case OP_SET_LOCAL:
            return ByteInstruction("OP_SET_LOCAL", chunk, offset);
        case OP_SET_LOCAL_TYPED:
            return TypedLocalInstruction("OP_SET_LOCAL_TYPED", chunk, offset);
        case OP_GET_LOCAL:
            return ByteInstruction("OP_GET_LOCAL", chunk, offset);
        case OP_SET_INDEX:
            return SimpleInstruction("OP_SET_INDEX", offset);
        case OP_GET_INDEX:
            return SimpleInstruction("OP_GET_INDEX", offset);
        case OP_GET_INDEX_RANGED:
            return SimpleInstruction("OP_GET_INDEX_RANGED", offset);
        case OP_SET_UPVALUE:
            return ByteInstruction("OP_SET_UPVALUE", chunk, offset);
        case OP_GET_UPVALUE:
            return ByteInstruction("OP_GET_UPVALUE", chunk, offset);
        case OP_SET_PROPERTY:
            return ConstantLongInstruction("OP_SET_PROPERTY", chunk, offset);
        case OP_GET_PROPERTY:
            return ConstantLongInstruction("OP_GET_PROPERTY", chunk, offset);
        case OP_INIT_PROPERTY:
            return InitPropertyInstruction("OP_INIT_PROPERTY", chunk, offset);
        case OP_INIT_PROPERTY_TYPED:
            return InitTypedPropertyInstruction("OP_INIT_TYPED_PROPERTY", chunk, offset);
        case OP_GET_SUPER:
            return ConstantLongInstruction("OP_GET_SUPER", chunk, offset);
        case OP_MARK_PRIVATE:
            return ConstantLongInstruction("OP_MARK_PRIVATE", chunk, offset);
        case OP_EQUAL:
            return SimpleInstruction("OP_EQUAL", offset);
        case OP_NOT_EQUAL:
            return SimpleInstruction("OP_NOT_EQUAL", offset);
        case OP_GREATER:
            return SimpleInstruction("OP_GREATER", offset);
        case OP_SMALLER:
            return SimpleInstruction("OP_SMALLER", offset);
        case OP_GREATER_EQ:
            return SimpleInstruction("OP_GREATER_EQ", offset);
        case OP_SMALLER_EQ:
            return SimpleInstruction("OP_SMALLER_EQ", offset);
        case OP_IS:
            return SimpleInstruction("OP_IS", offset);
        case OP_ADD:
            return SimpleInstruction("OP_ADD", offset);
        case OP_POSTINCREASE:
            return SimpleInstruction("OP_POSTINCREASE", offset);
        case OP_PREINCREASE:
            return SimpleInstruction("OP_PREINCREASE", offset);
        case OP_SUBTRACT:
            return SimpleInstruction("OP_SUBTRACT", offset);
        case OP_POSTDECREASE:
            return SimpleInstruction("OP_POSTDECREASE", offset);
        case OP_PREDECREASE:
            return SimpleInstruction("OP_PREDECREASE", offset);
        case OP_MULTIPLY:
            return SimpleInstruction("OP_MULTIPLY", offset);
        case OP_DIVIDE:
            return SimpleInstruction("OP_DIVIDE", offset);
        case OP_FLOOR_DIV:
            return SimpleInstruction("OP_FLOOR_DIV", offset);
        case OP_POW:
            return SimpleInstruction("OP_POW", offset);
        case OP_MOD:
            return SimpleInstruction("OP_MOD", offset);
        case OP_BITWISE_AND:
            return SimpleInstruction("OP_BITWISE_AND", offset);
        case OP_BITWISE_OR:
            return SimpleInstruction("OP_BITWISE_OR", offset);
        case OP_BITWISE_XOR:
            return SimpleInstruction("OP_BITWISE_XOR", offset);
        case OP_BITWISE_NOT:
            return SimpleInstruction("OP_BITWISE_NOT", offset);
        case OP_SHIFT_LEFT:
            return SimpleInstruction("OP_SHIFT_LEFT", offset);
        case OP_SHIFT_RIGHT:
            return SimpleInstruction("OP_SHIFT_RIGHT", offset);
        case OP_NEGATE:
            return SimpleInstruction("OP_NEGATE", offset);
        case OP_RETURN:
            return SimpleInstruction("OP_RETURN", offset);
        case OP_NOT:
            return SimpleInstruction("OP_NOT", offset);
        case OP_PRINT:
            return SimpleInstruction("OP_PRINT", offset);
        case OP_JUMP:
            return JumpInstruction("OP_JUMP", 1, chunk, offset);
        case OP_JUMP_IF_FALSE:
            return JumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
        case OP_LOOP:
            return JumpInstruction("OP_LOOP", -1, chunk, offset);
        case OP_CALL:
            return ByteInstruction("OP_CALL", chunk, offset);
        case OP_ARRAY:
            return ShortInstruction("OP_ARRAY", chunk, offset);
        case OP_MAP:
            return ShortInstruction("OP_MAP", chunk, offset);
        case OP_CLASS:
            return ConstantLongInstruction("OP_CLASS", chunk, offset);
        case OP_INVOKE:
            return InvokeInstructionLong("OP_INVOKE", chunk, offset);
        case OP_INHERIT:
            return SimpleInstruction("OP_INHERIT", offset);
        case OP_METHOD: {
            int lastOffset = ConstantLongInstruction("OP_METHOD", chunk, offset);
            lastOffset++;
            return lastOffset;
        }
        case OP_SUPER_INVOKE:
            return InvokeInstructionLong("OP_SUPER_INVOKE", chunk, offset);
        case OP_CLOSURE: {
            offset++;
            uint32_t Constant = chunk->code[offset++] << 24;
            Constant += chunk->code[offset++] << 16;
            Constant += chunk->code[offset++] << 8;
            Constant += chunk->code[offset++];

            printf("%-16s %4d ", "OP_CLOSURE", Constant);
            ObjectRepr(chunk->constants.values[Constant]);
            printf("\n");

            KTN_ObjShiki* function = AS_FUNCTION(chunk->constants.values[Constant]);
            for (int n = 0; n < function->upvalueCount; n++) {
                int isLocal = chunk->code[offset++];
                int index = chunk->code[offset++];
                printf("%04d      |                     %s %d\n", offset - 2, (isLocal) ? "local" : "upvalue", index);
            }

            return offset;
        }
        case OP_CLOSE_UPVALUE:
            return SimpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OP_BEGIN_CATCH:
            return JumpInstruction("OP_BEGIN_CATCH", 1, chunk, offset);
        case OP_END_CATCH:
            return SimpleInstruction("OP_END_CATCH", offset);
        case OP_RAISE:
            return SimpleInstruction("OP_RAISE", offset);
        case OP_ASSERT:
            return SimpleInstruction("OP_ASSERT", offset);
        case OP_INSTANCEOF:
            return ConstantLongInstruction("OP_INSTANCEOF", chunk, offset);
        case OP_BUILD_STACK_TRACE:
            return SimpleInstruction("OP_BUILD_STACK_TRACE", offset);
        case OP_CHECK_PARAMS:
            return DescriptorInstruction("OP_CHECK_PARAMS", chunk, offset);
        case OP_CHECK_TYPE:
            return ConstantLongInstruction("OP_CHECK_TYPE", chunk, offset);
        default:
            printf("Unknown Operation Code %d\n", instruction);
            return offset + 1;
    }
}
