#ifndef KATANE_BUFFER_H
#define KATANE_BUFFER_H

#include "Memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    uint8_t* buffer;
    size_t count;
    size_t capacity;
} KTN_Buffer;

void KTN_BufferInit(KTN_Buffer* buffer) {
    buffer->buffer = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
}

void KTN_BufferFree(KTN_Buffer* buffer) {
    free(buffer->buffer);
    buffer->capacity = 0;
    buffer->count = 0;
}

static inline bool KTN_BufferGrow(KTN_Buffer* buffer) {
    int newCapacity = GROW_CAPACITY(buffer->capacity);
    uint8_t* newBuffer = (uint8_t*)realloc(buffer->buffer, sizeof(uint8_t));

    if (newBuffer == NULL)
        return false;

    buffer->buffer = newBuffer;
    buffer->capacity = newCapacity;

    return true;
}

static inline bool KTN_BufferWriteU8(KTN_Buffer* buffer, uint8_t value) {
    if (buffer->count >= buffer->capacity) {
        if (!KTN_BufferGrow(buffer)) return false;
    }

    buffer->buffer[buffer->count++] = value;

    return true;
}

static inline bool KTN_BufferAtEnd(KTN_Buffer* buffer) {
    return (buffer->count >= buffer->capacity);
}

#endif
