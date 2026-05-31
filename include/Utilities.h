#ifndef KATANE_UTILITIES_H
#define KATANE_UTILITIES_H

#include <math.h>
#include "Common.h"

#define ISINT(num) (floor(num) == num)

#define Min(a, b) (((a) <= (b)) ? (a) : (b))
#define Max(a, b) (((a) >= (b)) ? (a) : (b))
#define Between(x, minimum, maximum) \
    (((((x) >= (minimum)) && ((x) <= (maximum))) || (((x) >= (maximum)) && ((x) <= (minimum)))) ? 1 : 0)
#define Clamp(x, minimum, maximum) \
    (((minimum) <= (maximum)) \
        ? ((((x) < (minimum)) ? (minimum) : (((x) > (maximum)) ? (maximum) : (x)))) \
        : ((((x) < (maximum)) ? (maximum) : (((x) > (minimum)) ? (minimum) : (x)))))
#define Sign(x) ((((x) < 0) ? -1 : (((x) > 0) ? 1 : 0)))
#define ArrayCount(array) ((int)(sizeof(array) / sizeof((array)[0])))

typedef struct {
    char* buffer;
    int length;
    int capacity;
} StringBuilder;

bool IsDigit(char digit);
bool IsNumber(const char* number);
bool IsAlphanumeric(char character);
bool IsOctal(char character);
bool IsHexadecimal(char character);
bool IsBinary(char character);

void writeUInt32(uint8_t* buffer, uint32_t value);
uint32_t readUInt32(const uint8_t* buffer);

char* StringAppend(char* oldString, const char* newString);
char* StringAppendN(char* oldString, const char* newString, size_t newLength);

bool ParseSize(const char* input, size_t* outSize);
bool FormatSize(size_t size, char* buffer, size_t bufferSize);

void SBInit(StringBuilder* sb);
void SBEnsure(StringBuilder* sb, int extra);
void SBAppend(StringBuilder* sb, const char* str, int length);
void SBAppendCStr(StringBuilder* sb, const char* str);
void SBFree(StringBuilder* sb);

char* ProcessEscapes(const char* source, int sourceLength);

#endif
