#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Utilities.h"
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
    if (newString == NULL)
        return oldString;

    size_t oldLength = strlen(oldString);
    size_t outputLength = oldLength + newLength;
    char* output = (char*)realloc((void*)oldString, outputLength + 1);

    if (output == NULL)
        return oldString;

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

    int newCap = sb->capacity == 0 ? 64 : sb->capacity * 2;
    
    while (newCap < sb->length + extra + 1) {
        newCap *= 2;
    }
    
    sb->buffer = realloc(sb->buffer, newCap);
    sb->capacity = newCap;
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

void SBFree(StringBuilder* sb) {
    free(sb->buffer);
    sb->buffer = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

static int DecodeEscapeSequence(const char* source, int sourceLength, int index, StringBuilder* builder) {
    if (index + 1 >= sourceLength) {
        SBAppend(builder, source + index, 1);
        return 1;
    }

    char escapeChar = source[index + 1];

    switch (escapeChar) {
        case 'n':  SBAppendCStr(builder, "\n"); return 2;
        case 't':  SBAppendCStr(builder, "\t"); return 2;
        case 'r':  SBAppendCStr(builder, "\r"); return 2;
        case 'a':  SBAppendCStr(builder, "\a"); return 2;
        case 'b':  SBAppendCStr(builder, "\b"); return 2;
        case 'f':  SBAppendCStr(builder, "\f"); return 2;
        case 'v':  SBAppendCStr(builder, "\v"); return 2;
        case '\\': SBAppendCStr(builder, "\\"); return 2;
        case '\'': SBAppendCStr(builder, "\'"); return 2;
        case '"':  SBAppendCStr(builder, "\""); return 2;
        default:   break;
    }

    if (escapeChar == 'x') {
        int position = index + 2;
        uint32_t value = 0;
        int digitCount = 0;

        while (position < sourceLength && digitCount < 2 && IsHexadecimal(source[position])) {
            uint8_t nibble = (uint8_t)source[position];

            nibble = (nibble >= 'a') ? nibble - 'a' + 10 :
                     (nibble >= 'A') ? nibble - 'A' + 10 :
                                       nibble - '0';

            value = (value << 4) | nibble;
            position++;
            digitCount++;
        }

        if (digitCount == 0) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        char encoded[4];
        int encodedLength = Utf8Encode(value, encoded);
        SBAppend(builder, encoded, encodedLength);
        return 2 + digitCount;
    }

    if (escapeChar == 'u') {
        int position = index + 2;

        if (position + 4 > sourceLength) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        uint32_t codepoint = 0;

        for (int digit = 0; digit < 4; digit++) {
            if (!IsHexadecimal(source[position + digit])) {
                SBAppend(builder, source + index, 2);
                return 2;
            }

            uint8_t nibble = (uint8_t)source[position + digit];

            nibble = (nibble >= 'a') ? nibble - 'a' + 10 :
                     (nibble >= 'A') ? nibble - 'A' + 10 :
                                       nibble - '0';

            codepoint = (codepoint << 4) | nibble;
        }

        char encoded[4];
        int encodedLength = Utf8Encode(codepoint, encoded);
        SBAppend(builder, encoded, encodedLength);
        return 6; // backslash + 'u' + 4 digits
    }

    if (escapeChar == 'U') {
        int position = index + 2;

        if (position + 8 > sourceLength) {
            SBAppend(builder, source + index, 2);
            return 2;
        }

        uint32_t codepoint = 0;

        for (int digit = 0; digit < 8; digit++) {
            if (!IsHexadecimal(source[position + digit])) {
                SBAppend(builder, source + index, 2);
                return 2;
            }

            uint8_t nibble = (uint8_t)source[position + digit];

            nibble = (nibble >= 'a') ? nibble - 'a' + 10 :
                     (nibble >= 'A') ? nibble - 'A' + 10 :
                                       nibble - '0';

            codepoint = (codepoint << 4) | nibble;
        }

        char encoded[4];
        int encodedLength = Utf8Encode(codepoint, encoded);
        SBAppend(builder, encoded, encodedLength);
        return 10; // backslash + 'U' + 8 digits
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

        char encoded[4];
        int encodedLength = Utf8Encode(value, encoded);
        SBAppend(builder, encoded, encodedLength);
        return 1 + digitCount;
    }

    SBAppend(builder, source + index, 2);
    return 2;
}

char* ProcessEscapes(const char* source, int sourceLength) {
    StringBuilder builder;
    SBInit(&builder);
    SBEnsure(&builder, sourceLength);

    int index = 0;

    while (index < sourceLength) {
        if (source[index] == '\\') {
            index += DecodeEscapeSequence(source, sourceLength, index, &builder);
            continue;
        }

        SBAppend(&builder, source + index, 1);
        index++;
    }

    return builder.buffer;
}
