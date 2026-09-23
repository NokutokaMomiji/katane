#ifndef KATANE_BUFFER_H
#define KATANE_BUFFER_H

#include "Memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum {
    KTN_BUFFER_LE,
    KTN_BUFFER_BE
} KTN_BufferEndianness;

typedef struct {
    uint8_t* buffer;
    size_t count;
    size_t capacity;
    KTN_BufferEndianness endianness;
} KTN_Buffer;

void KTN_BufferInit(KTN_Buffer* buffer, KTN_BufferEndianness endianness) {
    buffer->buffer = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->endianness = endianness;
}

void KTN_BufferFree(KTN_Buffer* buffer) {
    free(buffer->buffer);
    buffer->capacity = 0;
    buffer->count = 0;
}

static uint32_t FloatToBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t DoubleToBits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float BitsToFloat(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static double BitsToDouble(uint64_t bits) {
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline bool KTN_BufferReserve(KTN_Buffer* buffer, size_t needed) {
    if (buffer->capacity - buffer->count >= needed) return true;

    size_t oldCapacity = buffer->capacity;
    size_t newCapacity = GROW_CAPACITY(buffer->capacity);

    while (newCapacity - oldCapacity < needed) {
        newCapacity = GROW_CAPACITY(newCapacity);
    }

    uint8_t* newBuffer = (uint8_t*)realloc(buffer->buffer, newCapacity);

    if (newBuffer == NULL) return false;

    buffer->buffer = newBuffer;
    buffer->capacity = newCapacity;

    return true;
}

static inline bool KTN_BufferShrink(KTN_Buffer* buffer) {
    if (buffer->capacity == buffer->count) return true;

    if (buffer->count == 0) {
        free(buffer->buffer);
        
        buffer->count = 0;
        buffer->capacity = 0;

        return true;
    }

    uint8_t* newBuffer = (uint8_t*)realloc(buffer->buffer, buffer->count);

    if (newBuffer == NULL) return false;

    buffer->buffer = newBuffer;
    buffer->capacity = buffer->count;

    return true;
}

static inline bool KTN_BufferAtEnd(KTN_Buffer* buffer) {
    return (buffer->count >= buffer->capacity);
}

static inline bool KTN_BufferWriteU8(KTN_Buffer* buffer, uint8_t value) {
    if (!KTN_BufferReserve(buffer, 1)) return false;

    buffer->buffer[buffer->count++] = value;

    return true;
}

static inline bool KTN_BufferWriteS8(KTN_Buffer* buffer, int8_t value) {
    return KTN_BufferWriteU8(buffer, (uint8_t)value);
}

static inline bool KTN_BufferWriteU16(KTN_Buffer* buffer, uint16_t value) {
    if (!KTN_BufferReserve(buffer, 2)) return false;
    if (buffer->endianness == KTN_BUFFER_LE) {
        buffer->buffer[buffer->count++] = (value & 0xFF);
        buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
        return true;
    }
    buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
    buffer->buffer[buffer->count++] = (value & 0xFF);

    return true;
}

static inline bool KTN_BufferWriteS16(KTN_Buffer* buffer, int16_t value) {
    return KTN_BufferWriteU16(buffer, (uint16_t)value);
}

static inline bool KTN_BufferWriteU32(KTN_Buffer* buffer, uint32_t value) {
    if (!KTN_BufferReserve(buffer, 4)) return false;

    buffer->buffer[buffer->count++] = (value >> 24) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 16) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
    buffer->buffer[buffer->count++] = (value) & 0xFF;

    return true;
}

static inline bool KTN_BufferWriteS32(KTN_Buffer* buffer, int32_t value) {
    return KTN_BufferWriteU32(buffer, (uint32_t)value);
}

static inline bool KTN_BufferWriteU64(KTN_Buffer* buffer, uint64_t value) {
    if (!KTN_BufferReserve(buffer, 8)) return false;

    buffer->buffer[buffer->count++] = (value >> 56) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 48) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 40) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 32) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 24) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 16) & 0xFF;
    buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
    buffer->buffer[buffer->count++] = (value) & 0xFF;

    return true;
}

static inline bool KTN_BufferWriteS64(KTN_Buffer* buffer, int64_t value) {
    return KTN_BufferWriteU64(buffer, (uint64_t)value);
}

static inline bool KTN_BufferWriteFloat(KTN_Buffer* buffer, float value) {
    return KTN_BufferWriteU32(buffer, FloatToBits(value));
}

static inline bool KTN_BufferWriteDouble(KTN_Buffer* buffer, double value) {
    return KTN_BufferWriteU64(buffer, DoubleToBits(value));
}

#endif
