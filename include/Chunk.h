#ifndef KATANE_CHUNK_H
#define KATANE_CHUNK_H

#include "Common.h" //Common stuff.
#include "Value.h"  //Value and ValueArray.

typedef enum {
    OP_CONSTANT,        //Represents a constant value.
    OP_CONSTANT_LONG,   //Represents a long constant value.
    OP_NULL,
    OP_TRUE,
    OP_FALSE,
    OP_MAYBE,
    OP_POP,
    OP_POP_RESULT,
    OP_DUPLICATE,
    OP_SWAP,
    
    OP_DEFINE_GLOBAL,
    OP_DEFINE_GLOBAL_TYPED,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_SET_GLOBAL_TYPED,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_SET_LOCAL_TYPED,
    OP_SET_INDEX,
    OP_GET_INDEX,
    OP_GET_INDEX_RANGED,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_SET_UPVALUE_TYPED,
    OP_CLOSE_UPVALUE,
    OP_SET_PROPERTY,
    OP_GET_PROPERTY,
    OP_INIT_PROPERTY,
    OP_INIT_PROPERTY_TYPED,
    OP_GET_SUPER,

    OP_DEFINE_STATIC,
    OP_GET_STATIC,
    OP_MARK_PRIVATE,
    OP_SET_TYPE_PARAMS,
    OP_ENUM_GETTER,

    OP_ARRAY,
    OP_MAP,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
    OP_ENUM,
    OP_ENUM_VARIANT,
    OP_ENUM_METHOD,

    // Import / module system (Phase 7 — stubs)
    OP_CALL_IMPORT,
    OP_NATIVE_MODULE,
    OP_SELECT_IMPORT,
    OP_SELECT_NATIVE_IMPORT,
    OP_IMPORT_ALL,
    OP_IMPORT_ALL_NATIVE,
    OP_EJECT_IMPORT,
    OP_EJECT_NATIVE_IMPORT,

    OP_BEGIN_CATCH,
    OP_END_CATCH,
    OP_DEFER_ACTION,
    OP_END_FINALLY,
    OP_RAISE,
    OP_RETHROW,
    OP_ASSERT,
    OP_INSTANCEOF,
    OP_BUILD_STACK_TRACE,

    OP_CHECK_TYPE,
    OP_CHECK_PARAMS,

    OP_MARK_GLOBAL_FLAGS,

    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_SMALLER,
    OP_GREATER_EQ,
    OP_SMALLER_EQ,
    OP_IS,
    OP_ADD,
    OP_PREINCREASE,
    OP_POSTINCREASE,
    OP_SUBTRACT,
    OP_PREDECREASE,
    OP_POSTDECREASE,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_FLOOR_DIV,   // a /~ b
    OP_POW,         // a ** b
    OP_MOD,
    OP_BITWISE_OR,
    OP_BITWISE_AND,
    OP_BITWISE_XOR,
    OP_BITWISE_NOT,
    OP_SHIFT_LEFT,    // <<
    OP_SHIFT_RIGHT,   // >>
    OP_NOT,
    OP_NEGATE,          //Negates a value.
    OP_PRINT,
    OP_JUMP_IF_FALSE,   
    OP_JUMP,
    OP_LOOP,
    OP_CALL,
    OP_CALL_GENERIC,
    OP_INVOKE,
    OP_INVOKE_GENERIC,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_RETURN,          //Return from current function.
} KTN_OpCode;

typedef struct {
    int offset;
    int line;
    char* content;
} KTN_LineStart;

typedef struct {
    int count;              // Number of elements in array.
    int capacity;           // Number of available slots.
    uint8_t* code;          // Instruction elements.
    KTN_ValueArray constants;   // Array of constant values.
    KTN_LineStart* lines;    // Contains number of instruction elements per line.
    int lineCount;          // Current line in program.
    int lineCapacity;       // Total capacity of the Lines array.
} KTN_Chunk;

void KTN_ChunkInit(KTN_Chunk* chunk);                                             // Initializes a chunk.
void KTN_ChunkWrite(KTN_VM* vm, KTN_Chunk* chunk, uint8_t byte, int line, char* source);      // Writes an instruction byte to a chunk array.
void KTN_ChunkWriteLong(KTN_VM* vm, KTN_Chunk* chunk, uint32_t number, int line, char* source);
uint32_t KTN_ChunkAddConstant(KTN_VM* vm, KTN_Chunk* chunk, KTN_Value value);                          // Writes a constant to the constant array inside a chunk.
int KTN_ChunkWriteConstant(KTN_Chunk* chunk, KTN_Value value);
int KTN_ChunkGetLine(KTN_Chunk* chunk, int instruction);
char* KTN_ChunkGetSource(KTN_Chunk* chunk, int instruction);
void KTN_ChunkFree(KTN_VM* vm, KTN_Chunk* chunk);

#endif
