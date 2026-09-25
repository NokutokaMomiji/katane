#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Utilities.h"
#include "Memory.h"
#include "Utf8.h"

bool IsDigit(char digit) {
    return (digit >= '0' && digit <= '9');
}

bool IsNumber(const char* number) {
    (void)number;
    return false;
}

bool IsAlphanumeric(char character) {
    return ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character == '_'));
}

bool IsOctal(char character) {
    return (character >= '0' && character <= '7');
}

bool IsHexadecimal(char character) {
    return (Between(character, '0', '9') ||
            Between(character, 'a', 'f') ||
            Between(character, 'A', 'F'));
}

bool IsBinary(char character) {
    return (character == '0' || character == '1');
}

void writeUInt32(uint8_t* buffer, uint32_t value) {
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

uint32_t readUInt32(const uint8_t* buffer) {
    return ((uint32_t)buffer[0] |
            ((uint32_t)buffer[1] << 8) |
            ((uint32_t)buffer[2] << 16) |
            ((uint32_t)buffer[3] << 24));
}

char* StringAppend(char* oldString, const char* newString) {
    if (newString == NULL)
        return oldString;

    return StringAppendN(oldString, newString, strlen(newString));
}

char* StringAppendN(char* oldString, const char* newString, size_t newLength) {
    if (newString == NULL) {
        return oldString;
    }

    size_t oldLength = (oldString == NULL) ? 0 : strlen(oldString);
    size_t outputLength = oldLength + newLength;
    char* output = (char*)realloc((void*)oldString, outputLength + 1);

    if (output == NULL) {
        return oldString;
    }

    memcpy(output + oldLength, newString, newLength);
    output[outputLength] = '\0';
    return output;
}

bool ParseSize(const char* input, size_t* outSize) {
    static const char suffixes[] = "bBkKmMgGtT";

    if (input == NULL || outSize == NULL)
        return false;

    errno = 0;

    char* end = NULL;
    long double value = strtold(input, &end);

    if (errno != 0 || end == input || value < 0)
        return false;

    while (*end == ' ' || *end == '\t')
        end++;

    size_t shift = 0;

    if (*end != '\0') {
        char* match = strchr(suffixes, *end);

        if (match == NULL)
            return false;

        if ((*match != 'b') && (*match != 'B')) {
            shift = (size_t)(((match - suffixes) / 2) + 1) * 10;
        }

        end++;

        while (*end == ' ' || *end == '\t')
            end++;
    }

    if (*end != '\0')
        return false;

    *outSize = (size_t)(value * (long double)(1ULL << shift));
    return true;
}

bool FormatSize(size_t size, char* buffer, size_t bufferSize) {
    static const char* labels[] = { "TiB", "GiB", "MiB", "KiB", "B" };
    size_t multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;

    if (buffer == NULL || bufferSize == 0)
        return false;

    for (int index = 0; index < ArrayCount(labels); index++) {
        if (size < multiplier) {
            multiplier /= 1024;
            continue;
        }

        int written;

        if ((size % multiplier) == 0) {
            written = snprintf(buffer, bufferSize, "%zu %s", size / multiplier, labels[index]);
        } else {
            written = snprintf(buffer, bufferSize, "%.1Lf %s", (long double)size / (long double)multiplier, labels[index]);
        }

        return (written >= 0 && (size_t)written < bufferSize);
    }

    return (snprintf(buffer, bufferSize, "0 B") >= 0);
}

void SBInit(StringBuilder* sb) {
    sb->buffer = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

void SBEnsure(StringBuilder* sb, int extra) {
    if (sb->length + extra + 1 <= sb->capacity) return;

    int newCapacity = (sb->capacity == 0) ? 64 : sb->capacity * 2;
    
    while (newCapacity < sb->length + extra + 1) {
        newCapacity *= 2;
    }
    
    sb->buffer = realloc(sb->buffer, newCapacity);
    sb->capacity = newCapacity;
}

void SBAppend(StringBuilder* sb, const char* str, int length) {
    SBEnsure(sb, length);
    memcpy(sb->buffer + sb->length, str, length);
    sb->length += length;
    sb->buffer[sb->length] = '\0';
}

void SBAppendCStr(StringBuilder* sb, const char* str) {
    SBAppend(sb, str, (int)strlen(str));
}

char* SBDetach(StringBuilder *sb) {
    char* buffer = sb->buffer;

    if (!buffer) {
        buffer = malloc(1);
        if (!buffer) return NULL;
        buffer[0] = '\0';
    }

    sb->buffer = NULL;
    sb->length = 0;
    sb->capacity = 0;

    return buffer;
}

void SBFree(StringBuilder* sb) {
    if (sb->buffer) {
        free(sb->buffer);
        sb->buffer = NULL;
    }

    sb->length = 0;
    sb->capacity = 0;
}

static uint32_t HexNibbleValue(char character) {
    if (character >= 'a') {
        return (uint32_t)(character - 'a' + 10);
    }

    if (character >= 'A') {
        return (uint32_t)(character - 'A' + 10);
    }

    return (uint32_t)(character - '0');
}

static bool ReadHexValue(const char* source, int sourceLength, int position, int digitCount, uint32_t* outValue) {
    if (position + digitCount > sourceLength) {
        return false;
    }

    uint32_t value = 0;

    for (int digit = 0; digit < digitCount; digit++) {
        if (!IsHexadecimal(source[position + digit])) {
            return false;
        }

        value = (value << 4) | HexNibbleValue(source[position + digit]);
    }

    *outValue = value;
    return true;
}

static bool IsValidCodepoint(uint32_t codepoint) {
    if (codepoint > 0x10FFFF) {
        return false;
    }

    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
        return false;
    }

    return true;
}

static int DecodeEscapeSequence(const char* source, int sourceLength, int index, StringBuilder* builder) {
    if (index + 1 >= sourceLength) {
        SBAppend(builder, source + index, 1);
        return 1;
    }

    char escapeChar = source[index + 1];

    switch (escapeChar) {
        case 'n': {
            SBAppendCStr(builder, "\n");
            return 2;
        }
        case 't': {
            SBAppendCStr(builder, "\t");
            return 2;
        }
        case 'r': {
            SBAppendCStr(builder, "\r");
            return 2;
        }
        case 'a': {
            SBAppendCStr(builder, "\a");
            return 2;
        }
        case 'b': {
            SBAppendCStr(builder, "\b");
            return 2;
        }
        case 'f': {
            SBAppendCStr(builder, "\f");
            return 2;
        }
        case 'v': {
            SBAppendCStr(builder, "\v");
            return 2;
        }
        case '\\': {
            SBAppendCStr(builder, "\\");
            return 2;
        }
        case '\'': {
            SBAppendCStr(builder, "\'");
            return 2;
        }
        case '"': {
            SBAppendCStr(builder, "\"");
            return 2;
        }
        default: {
            break;
        }
    }

    if (escapeChar == 'x') {
        int position = index + 2;
        uint32_t value = 0;
        int digitCount = 0;

        while (position < sourceLength && digitCount < 2 && IsHexadecimal(source[position])) {
            value = (value << 4) | HexNibbleValue(source[position]);
            position++;
            digitCount++;
        }

        if (digitCount == 0) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        char byteValue = (char)value;
        SBAppend(builder, &byteValue, 1);
        return 2 + digitCount;
    }

    if (escapeChar == 'u') {
        uint32_t codepoint = 0;

        if (!ReadHexValue(source, sourceLength, index + 2, 4, &codepoint)) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            uint32_t lowSurrogate = 0;
            bool hasLowSurrogate = (index + 7 < sourceLength) &&
                                   (source[index + 6] == '\\') &&
                                   (source[index + 7] == 'u') &&
                                   ReadHexValue(source, sourceLength, index + 8, 4, &lowSurrogate);

            if (hasLowSurrogate && lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                uint32_t combined = 0x10000 + ((codepoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
                char encoded[4];
                int encodedLength = Utf8Encode(combined, encoded);
                SBAppend(builder, encoded, encodedLength);
                return 12;
            }

            SBAppend(builder, source + index, 2);
            return 2;
        }

        if (!IsValidCodepoint(codepoint)) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        char encoded[4];
        int encodedLength = Utf8Encode(codepoint, encoded);
        SBAppend(builder, encoded, encodedLength);
        return 6;
    }

    if (escapeChar == 'U') {
        uint32_t codepoint = 0;

        if (!ReadHexValue(source, sourceLength, index + 2, 8, &codepoint)) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        if (!IsValidCodepoint(codepoint)) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        char encoded[4];
        int encodedLength = Utf8Encode(codepoint, encoded);
        SBAppend(builder, encoded, encodedLength);
        return 10;
    }

    if (IsOctal(escapeChar)) {
        int position = index + 1;
        uint32_t value = 0;
        int digitCount = 0;

        while (position < sourceLength && digitCount < 3 && IsOctal(source[position])) {
            value = (value << 3) | (uint32_t)(source[position] - '0');
            position++;
            digitCount++;
        }

        char byteValue = (char)(value & 0xFF);
        SBAppend(builder, &byteValue, 1);
        return 1 + digitCount;
    }

    SBAppend(builder, source + index, 2);
    return 2;
}

char* ProcessEscapes(const char* source, int sourceLength, int* outLength) {
    StringBuilder builder;
    SBInit(&builder);
    SBEnsure(&builder, sourceLength);
    builder.buffer[0] = '\0';

    int index = 0;

    while (index < sourceLength) {
        if (source[index] == '\\') {
            index += DecodeEscapeSequence(source, sourceLength, index, &builder);
            continue;
        }

        SBAppend(&builder, source + index, 1);
        index++;
    }

    if (outLength != NULL) {
        *outLength = builder.length;
    }

    return builder.buffer;
}

// From eiszapfen2000 on Github (https://github.com/eiszapfen2000/asprintf/blob/master/asprintf.c)
// THANK YOU! (and fuck you too Windows.)
#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#if _MSC_VER < 1800
#undef va_copy
#define va_copy(dst, src) (dst = src)
#endif

#ifndef EOTHER
#define EOTHER 131
#endif

#ifdef __cplusplus
extern "C"
#endif
int vasprintf(char** strp, const char* fmt, va_list ap)
{
    va_list ap_copy;
    int formattedLength, actualLength;
    size_t requiredSize;

    // be paranoid
    *strp = NULL;

    // copy va_list, as it is used twice 
    va_copy(ap_copy, ap);

    // compute length of formatted string, without NULL terminator
    formattedLength = _vscprintf(fmt, ap_copy);
    va_end(ap_copy);

    // bail out on error
    if (formattedLength < 0)
    {
        return -1;
    }

    // allocate buffer, with NULL terminator
    requiredSize = ((size_t)formattedLength) + 1;
    *strp = (char*)malloc(requiredSize);

    // bail out on failed memory allocation
    if (*strp == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    // write formatted string to buffer, use security hardened _s function
    actualLength = vsnprintf_s(*strp, requiredSize, requiredSize - 1, fmt, ap);

    // again, be paranoid
    if (actualLength != formattedLength)
    {
        free(*strp);
        *strp = NULL;
        errno = EOTHER;
        return -1;
    }

    return formattedLength;
}

#ifdef __cplusplus
extern "C"
#endif
int asprintf(char** strp, const char* fmt, ...)
{
    int result;

    va_list ap;
    va_start(ap, fmt);
    result = vasprintf(strp, fmt, ap);
    va_end(ap);

    return result;
}
#endif