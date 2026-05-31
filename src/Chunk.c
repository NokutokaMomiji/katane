#include <string.h>
#include "Chunk.h"
#include "Memory.h"
#include "VM.h"

/// @brief Initializes a Chunk.
/// @param chunk Chunk to initialize.
void KTN_ChunkInit(KTN_Chunk* chunk) {
    //Intialize empty chunk array.
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;

    // Initialize line stuff.
    chunk->lineCount = 0;
    chunk->lineCapacity = 0;
    chunk->lines = NULL;

    //Initialize internal constant array.
    ValueArrayInit(&chunk->constants);
}

/// @brief Writes a byte to a Chunk array.
/// @param chunk Chunk to write the byte to.
/// @param byte Byte to store on Chunk.
/// @param line The current line number (for exception purposes).
/// @param source The current source code line (for exception purposes).
void KTN_ChunkWrite(KTN_VM* vm, KTN_Chunk* chunk, uint8_t byte, int line, char* source) {
    //If there is not enough capacity for the new byte, then increase the size of the array.
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
    }
    
    //Store byte on array and increase the number of elements.
    chunk->code[chunk->count] = byte;
    chunk->count++;

    // If we are still in the current line, we don't add more to the array.
    if (chunk->lineCount > 0 && chunk->lines[chunk->lineCount - 1].line == line)
        return;
    
    // If there is not enough capacity for the new line data, we grow the array.
    if (chunk->lineCapacity < chunk->lineCount + 1) {
        int oldCapacity = chunk->lineCapacity;
        chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
        chunk->lines = GROW_ARRAY(KTN_LineStart, chunk->lines, oldCapacity, chunk->lineCapacity);
    }

    // We set the data for the current line.
    KTN_LineStart* lineStart = &chunk->lines[chunk->lineCount++];
    lineStart->offset = chunk->count - 1;
    lineStart->line = line;
    
    // We copy the string from the source code to the line data array.
    if (source == NULL) {
        lineStart->content = '\0';
        return;
    }

    int contentLength = strlen(source);
    lineStart->content = ALLOCATE(char, contentLength + 1);
    memcpy(lineStart->content, source, contentLength);
    lineStart->content[contentLength] = '\0';
}

/// @brief Writes a long to the chunk.
/// @param chunk Chunk to write the number to.
/// @param number The number to write.
/// @param line The current line number (for exception purposes).
/// @param source The current source code line (for exception purposes).
void KTN_ChunkWriteLong(KTN_VM* vm, KTN_Chunk* chunk, uint32_t number, int line, char* source) {
    // Some bit shifting to get each individual byte.
    uint8_t firstByte = (number & 0xff000000UL) >> 24;
    uint8_t secondByte = (number & 0x00ff0000UL) >> 16;
    uint8_t thirdByte = (number & 0x0000ff00UL) >> 8;
    uint8_t fourthByte = (number & 0x000000ffUL);

    // We write the bytes in sequence.
    KTN_ChunkWrite(vm, chunk, firstByte, line, source);
    KTN_ChunkWrite(vm, chunk, secondByte, line, source);
    KTN_ChunkWrite(vm, chunk, thirdByte, line, source);
    KTN_ChunkWrite(vm, chunk, fourthByte, line, source);
}

/// @brief Writes a value to the chunk's value array.
/// @param chunk The chunk to write to.
/// @param value The value to write to the chunk.
/// @return Next index.
uint32_t KTN_ChunkAddConstant(KTN_VM* vm, KTN_Chunk* chunk, KTN_Value value) {
    Push(vm, value);
    ValueArrayWrite(vm, &chunk->constants, value);
    Pop(vm);
    return chunk->constants.count - 1;
}

/// @brief For getting the current line number based on the instruction number (from the VM).
/// @param chunk Chunk to get the line number from.
/// @param instruction The current instruction offset (from the VM).
/// @return Line number.
int KTN_ChunkGetLine(KTN_Chunk* chunk, int instruction) {
    int start = 0;
    int end = chunk->lineCount - 1;

    for (;;) {
        int mid = (start + end) / 2;    
        KTN_LineStart* line = &chunk->lines[mid];
        if (instruction < line->offset)
            end = mid - 1;
        else if (mid == chunk->lineCount - 1 || instruction < chunk->lines[mid + 1].offset)
            return line->line;
        else
            start = mid + 1;
    }
}

/// @brief For getting the current source code line based on the instruction number (from the VM).
/// @param chunk The chunk to get the source code info from.
/// @param instruction The instruction offset (from the VM).
/// @return Source code line.
char* KTN_ChunkGetSource(KTN_Chunk* chunk, int instruction) {
    int start = 0;
    int end = chunk->lineCount - 1;

    for (;;) {
        int mid = (start + end) / 2;    
        KTN_LineStart* line = &chunk->lines[mid];
        if (instruction < line->offset)
            end = mid - 1;
        else if (mid == chunk->lineCount - 1 || instruction < chunk->lines[mid + 1].offset)
            return line->content;
        else
            start = mid + 1;
    }
}

/// @brief Frees up the memory occupied by an array.
/// @param chunk Chunk array to free the memory of.
void KTN_ChunkFree(KTN_VM* vm, KTN_Chunk* chunk) {
    //Free memory from array.
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    
    //Free chunk constants.
    ValueArrayFree(vm, &chunk->constants);

    //Free memory from line array.
    FREE_ARRAY(KTN_LineStart, chunk->lines, chunk->lineCapacity);

    //Reinitialize chunk.
    KTN_ChunkInit(chunk);
}