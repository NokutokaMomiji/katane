#ifndef KATANE_BUFFER_H
#define KATANE_BUFFER_H

#include "Memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#define KTN_BUFFER_FLOAT_NAN 0x7FC00000u
#define KTN_BUFFER_DOUBLE_NAN 0x7FF8000000000000ULL

typedef enum {
    KTN_BUFFER_LE,
    KTN_BUFFER_BE
} KTN_BufferEndianness;

typedef enum {
    KTN_BUFFER_TYPE_GROW,
    KTN_BUFFER_TYPE_FIXED,
    KTN_BUFFER_TYPE_WRAP
} KTN_BufferType;

typedef enum {
    KTN_BUFFER_ERROR_NONE,
    KTN_BUFFER_ERROR_OOM,
    KTN_BUFFER_ERROR_OVERFLOW,
    KTN_BUFFER_ERROR_UNDERFLOW
} KTN_BufferError;

typedef struct {
    uint8_t* buffer;
    size_t count;
    size_t capacity;
    size_t cursor;
    bool error;
    KTN_BufferEndianness endianness;
    KTN_BufferType type;
} KTN_Buffer;

void KTN_BufferInit(KTN_Buffer* buffer, size_t size, KTN_BufferType type, KTN_BufferEndianness endianness) {
    buffer->buffer = NULL;
    buffer->count = 0;
    buffer->capacity = 0;
    buffer->cursor = 0;
    buffer->endianness = endianness;
    buffer->type = type;
    buffer->error = KTN_BUFFER_ERROR_NONE;


    if (size != 0) {
        buffer->buffer = (uint8_t*)malloc(size);

        if (buffer->buffer == NULL) {
            buffer->error = KTN_BUFFER_ERROR_OOM;
            return;
        }

        buffer->capacity = size;
    }
}

void KTN_BufferFree(KTN_Buffer* buffer) {
    free(buffer->buffer);
    buffer->capacity = 0;
    buffer->count = 0;
    buffer->cursor = 0;
    buffer->error = KTN_BUFFER_ERROR_NONE;
}

static inline uint32_t FloatToBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline uint64_t DoubleToBits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline float BitsToFloat(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline double BitsToDouble(uint64_t bits) {
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline bool KTN_BufferReserve(KTN_Buffer* buffer, size_t needed) {
    if (buffer->capacity - buffer->count >= needed) return true;

    if (buffer->type == KTN_BUFFER_TYPE_FIXED || buffer->type == KTN_BUFFER_TYPE_WRAP) {
        buffer->error = KTN_BUFFER_ERROR_OVERFLOW;
        return false;
    }

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

static inline bool KTN_BufferAlign(KTN_Buffer* buffer, size_t alignment) {
    size_t pad = (alignment - (buffer->count % alignment)) % alignment;

    for (size_t i = 0; i < pad; i++) {
        if (!KTN_BufferWriteU8(buffer, 0)) return false;
    }

    return true;
}

static inline bool KTN_BufferWriterAtEnd(KTN_Buffer* buffer) {
    return (buffer->count >= buffer->capacity);
}

static inline bool KTN_BufferReaderAtEnd(KTN_Buffer* buffer) {
    return (buffer->cursor >= buffer->count);
}

static inline size_t KTN_BufferReaderRemaining(KTN_Buffer* buffer) {
    return (buffer->count - buffer->cursor);
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

    switch (buffer->endianness) {
        case KTN_BUFFER_LE: {
            buffer->buffer[buffer->count++] = (value & 0xFF);
            buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
            break;
        }

        case KTN_BUFFER_BE: {
            buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
            buffer->buffer[buffer->count++] = (value & 0xFF);
            break;
        }
    }

    return true;
}

static inline bool KTN_BufferWriteS16(KTN_Buffer* buffer, int16_t value) {
    return KTN_BufferWriteU16(buffer, (uint16_t)value);
}

static inline bool KTN_BufferWriteU32(KTN_Buffer* buffer, uint32_t value) {
    if (!KTN_BufferReserve(buffer, 4)) return false;

    switch (buffer->endianness) {
        case KTN_BUFFER_LE: {
            buffer->buffer[buffer->count++] = (value) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 16) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 24) & 0xFF;
            break;
        }

        case KTN_BUFFER_BE: {
            buffer->buffer[buffer->count++] = (value >> 24) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 16) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
            buffer->buffer[buffer->count++] = (value) & 0xFF;
        }
    }

    return true;
}

static inline bool KTN_BufferWriteS32(KTN_Buffer* buffer, int32_t value) {
    return KTN_BufferWriteU32(buffer, (uint32_t)value);
}

static inline bool KTN_BufferWriteU64(KTN_Buffer* buffer, uint64_t value) {
    if (!KTN_BufferReserve(buffer, 8)) return false;

    switch (buffer->endianness) {
        case KTN_BUFFER_LE: {
            buffer->buffer[buffer->count++] = (value) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 16) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 24) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 32) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 40) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 48) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 56) & 0xFF;
            break;
        }

        case KTN_BUFFER_BE: {
            buffer->buffer[buffer->count++] = (value >> 56) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 48) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 40) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 32) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 24) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 16) & 0xFF;
            buffer->buffer[buffer->count++] = (value >> 8) & 0xFF;
            buffer->buffer[buffer->count++] = (value) & 0xFF;
            break;
        }
    }

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

static inline bool KTN_BufferWriteString(KTN_Buffer* buffer, const char* chars, size_t length) {
    if (!KTN_BufferWriteU32(buffer, (uint32_t)length)) return false;
    if (!KTN_BufferReserve(buffer, length)) return false;

    memcpy(buffer->buffer + buffer->count, chars, length);

    buffer->count += length;

    return true;
}

static inline bool KTN_BufferWriteBool(KTN_Buffer* buffer, bool value) {
    return KTN_BufferWriteU8(buffer, value);
}

static inline uint8_t KTN_BufferReadU8(KTN_Buffer* buffer) {
    if (buffer->error) return UINT8_MAX;
    
    if (KTN_BufferReaderAtEnd(buffer)) {
        buffer->error = KTN_BUFFER_ERROR_UNDERFLOW;
        return UINT8_MAX;
    }

    return buffer->buffer[buffer->cursor++];
}

static inline int8_t KTN_BufferReadS8(KTN_Buffer* buffer) {
    uint8_t result = KTN_BufferReadU8(buffer);
    return (buffer->error) ? INT8_MAX : (int8_t)result;
}

static inline uint16_t KTN_BufferReadU16(KTN_Buffer* buffer) {
    if (buffer->error) return UINT16_MAX;
    
    if (KTN_BufferReaderRemaining(buffer) < 2) {
        buffer->error = KTN_BUFFER_ERROR_UNDERFLOW;
        return UINT16_MAX;
    }

    uint16_t firstByte = buffer->buffer[buffer->cursor++];
    uint16_t secondByte = buffer->buffer[buffer->cursor++];

    if (buffer->endianness == KTN_BUFFER_LE) {
        return firstByte | (secondByte << 8);
    }

    return (firstByte << 8) | secondByte;
}

static inline int16_t KTN_BufferReadS16(KTN_Buffer* buffer) {
    uint16_t result = KTN_BufferReadU16(buffer);
    return (buffer->error) ? INT16_MAX : result;
}

static inline uint32_t KTN_BufferReadU32(KTN_Buffer* buffer) {
    if (buffer->error) return UINT32_MAX;

    if (KTN_BufferReaderRemaining(buffer) < 4) {
        buffer->error = KTN_BUFFER_ERROR_UNDERFLOW;
        return UINT32_MAX;
    }

    uint32_t firstByte = buffer->buffer[buffer->cursor++];
    uint32_t secondByte = buffer->buffer[buffer->cursor++];
    uint32_t thirdByte = buffer->buffer[buffer->cursor++];
    uint32_t fourthByte = buffer->buffer[buffer->cursor++];

    if (buffer->endianness == KTN_BUFFER_LE) {
        return firstByte | (secondByte << 8) | (thirdByte << 16) | (fourthByte << 24);
    }

    return (firstByte << 24) | (secondByte << 16) | (thirdByte << 8) | fourthByte;
}

static inline int32_t KTN_BufferReadS32(KTN_Buffer* buffer) {
    uint32_t value = KTN_BufferReadU32(buffer);
    return (buffer->error) ? INT32_MAX : value;
}

static inline uint64_t KTN_BufferReadU64(KTN_Buffer* buffer) {
    if (buffer->error) return UINT64_MAX;

    if (KTN_BufferReaderRemaining(buffer) < 8) {
        buffer->error = KTN_BUFFER_ERROR_UNDERFLOW;
        return UINT64_MAX;
    }

    uint64_t firstByte = buffer->buffer[buffer->cursor++];
    uint64_t secondByte = buffer->buffer[buffer->cursor++];
    uint64_t thirdByte = buffer->buffer[buffer->cursor++];
    uint64_t fourthByte = buffer->buffer[buffer->cursor++];
    uint64_t fifthByte = buffer->buffer[buffer->cursor++];
    uint64_t sixthByte = buffer->buffer[buffer->cursor++];
    uint64_t seventhByte = buffer->buffer[buffer->cursor++];
    uint64_t eighthByte = buffer->buffer[buffer->cursor++];

    if (buffer->endianness == KTN_BUFFER_LE) {
        return firstByte | (secondByte << 8) | 
               (thirdByte << 16) | (fourthByte << 24) | (fifthByte << 32) | 
               (sixthByte << 40) | (seventhByte << 48) | (eighthByte << 56);
    }

    return (firstByte << 56) | (secondByte << 48) |
           (thirdByte << 40) | (fourthByte << 32) |
           (fifthByte << 24) | (sixthByte << 16) |
           (seventhByte << 8) | (eighthByte);
}

static inline int64_t KTN_BufferReadS64(KTN_Buffer* buffer) {
    uint64_t value = KTN_BufferReadU64(buffer);
    return (buffer->error) ? INT64_MAX : value;
}

static inline float KTN_BufferReadFloat(KTN_Buffer* buffer) {
    uint32_t value = KTN_BufferReadU32(buffer);
    return BitsToFloat((buffer->error) ? KTN_BUFFER_FLOAT_NAN : value);
}

static inline double KTN_BufferReadDouble(KTN_Buffer* buffer) {
    uint64_t value = KTN_BufferReadU64(buffer);
    return BitsToDouble((buffer->error) ? KTN_BUFFER_DOUBLE_NAN : value);
}

static inline char* KTN_BufferReadString(KTN_Buffer* buffer, size_t* outLength) {
    uint32_t length = KTN_BufferReadU32(buffer);

    if (buffer->error) return NULL;

    if (KTN_BufferReaderRemaining(buffer) < length) {
        buffer->error = KTN_BUFFER_ERROR_UNDERFLOW;
        return NULL;
    }

    char* str = (char*)malloc(length + 1);
    if (str == NULL) {
        buffer->error = KTN_BUFFER_ERROR_OOM;
        return NULL;
    }

    memcpy(str, buffer->buffer + buffer->cursor, length);
    str[length + 1] = '\0';
    buffer->cursor += length;

    *outLength = length;

    return str;
}

static inline bool KTN_BufferReadBool(KTN_Buffer* buffer) {
    return KTN_BufferReadU8(buffer);
}

#endif
