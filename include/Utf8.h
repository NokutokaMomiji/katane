#ifndef KATANE_UTF8_H
#define KATANE_UTF8_H

#include "Common.h"

#define UTF8_REPLACEMENT_CHAR 0xFFFDu
#define UTF8_MAX_CODEPOINT 0x10FFFFu

#define UTF8_ASCII_DECODE(string, index) ((Utf8Char){ (uint32_t)(uint8_t)(string)[index], 1 })
#define UTF8_DECODE_FAST(string, byteLength, index) \
    ((((uint8_t)(string)[index]) < 0x80) ? UTF8_ASCII_DECODE(string, index) : DecodeUtf8(string, byteLength, index))

typedef struct {
    uint32_t codepoint;
    int length;
} Utf8Char;

typedef struct {
    uint32_t* offsets;
    int codepointCount;
    int byteLength;
} Utf8Index;

bool IsValidCodePoint(uint32_t codepoint);
Utf8Char DecodeUtf8(const char* string, int byteLength, int index);
int Utf8Strlen(const char* string);
int Utf8StrnCpLen(const char* string, int byteLength);
Utf8Char Utf8CodepointAt(const char* string, int byteLength, int codepointIndex);
int Utf8ByteOffsetAt(const char* string, int byteLength, int codepointIndex);
int Utf8Encode(uint32_t codepoint, char* output);
char* Utf8Reverse(const char* string, int byteLength, int* outByteLength);

bool Utf8IndexBuild(Utf8Index* index, const char* string, int byteLength);
void Utf8IndexFree(Utf8Index* index);
Utf8Char Utf8IndexCharAt(const Utf8Index* index, const char* string, int codepointIndex);
int Utf8IndexByteOffset(const Utf8Index* index, int codepointIndex);
char* Utf8IndexSubstring(const Utf8Index* index, const char* string, int codepointStart, int codepointEnd);

char* Utf8ToLower(const char* string, int byteLength);
char* Utf8ToUpper(const char* string, int byteLength);
char* Utf8ToTitle(const char* string, int byteLength);
bool Utf8IsUpper(uint32_t codepoint);
bool Utf8IsLower(uint32_t codepoint);
uint32_t Utf8SimpleUpper(uint32_t codepoint);
uint32_t Utf8SimpleLower(uint32_t codepoint);

char* Utf8Strip(const char* string, int byteLength, int* outByteLength);
char* Utf8LStrip(const char* string, int byteLength, int* outByteLength);
char* Utf8RStrip(const char* string, int byteLength, int* outByteLength);
bool Utf8StartsWith(const char* string, int stringLength, const char* prefix, int prefixLength);
bool Utf8EndsWith(const char* string, int stringLength, const char* suffix, int suffixLength);
int Utf8FindBytes(const char* haystack, int haystackLength, const char* needle, int needleLength);
int Utf8Count(const char* string, int stringLength, const char* substring, int substringLength);
char* Utf8Replace(const char* string, int stringLength, const char* oldSubstring, int oldLength,
                  const char* newSubstring, int newLength, int maxReplacements, int* outByteLength);
char** Utf8Split(const char* string, int stringLength, const char* separator, int separatorLength, int* outCount);
char* Utf8Repeat(const char* string, int stringLength, int count, int* outByteLength);
char* Utf8Join(const char* separator, int separatorLength, const char** parts,
               const int* partLengths, int count, int* outByteLength);
bool Utf8IsWhitespace(uint32_t codepoint);

#endif
