#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Utf8.h"
#include "Utilities.h"

/// Represents the casing for a given UTF-8 codepoint. 
/// Codepoint is the UTF-8 for a given character. Delta indicates how much to add or 
///  subtract in order to get the character of opposite casing.
typedef struct { 
    uint32_t codepoint;
    int32_t delta;
} CaseEntry;
 
/// UTF-8 is so brilliant that the only way to be able to do switch casing is like this.
/// You have to create a map that just maps a codepoint to its alternative. This could be bigger,
///  trust me...
static const CaseEntry UPPER_TO_LOWER[] = {
    {0x0041,32},{0x0042,32},{0x0043,32},{0x0044,32},{0x0045,32},{0x0046,32},
    {0x0047,32},{0x0048,32},{0x0049,32},{0x004A,32},{0x004B,32},{0x004C,32},
    {0x004D,32},{0x004E,32},{0x004F,32},{0x0050,32},{0x0051,32},{0x0052,32},
    {0x0053,32},{0x0054,32},{0x0055,32},{0x0056,32},{0x0057,32},{0x0058,32},
    {0x0059,32},{0x005A,32},
    {0x00C0,32},{0x00C1,32},{0x00C2,32},{0x00C3,32},{0x00C4,32},{0x00C5,32},
    {0x00C6,32},{0x00C7,32},{0x00C8,32},{0x00C9,32},{0x00CA,32},{0x00CB,32},
    {0x00CC,32},{0x00CD,32},{0x00CE,32},{0x00CF,32},{0x00D0,32},{0x00D1,32},
    {0x00D2,32},{0x00D3,32},{0x00D4,32},{0x00D5,32},{0x00D6,32},
    {0x00D8,32},{0x00D9,32},{0x00DA,32},{0x00DB,32},{0x00DC,32},{0x00DD,32},
    {0x00DE,32},
    {0x0100,1},{0x0102,1},{0x0104,1},{0x0106,1},{0x0108,1},{0x010A,1},
    {0x010C,1},{0x010E,1},{0x0110,1},{0x0112,1},{0x0114,1},{0x0116,1},
    {0x0118,1},{0x011A,1},{0x011C,1},{0x011E,1},{0x0120,1},{0x0122,1},
    {0x0124,1},{0x0126,1},{0x0128,1},{0x012A,1},{0x012C,1},{0x012E,1},
    {0x0130,-199},
    {0x0132,1},{0x0134,1},{0x0136,1},{0x0139,1},{0x013B,1},{0x013D,1},
    {0x013F,1},{0x0141,1},{0x0143,1},{0x0145,1},{0x0147,1},{0x014A,1},
    {0x014C,1},{0x014E,1},{0x0150,1},{0x0152,1},{0x0154,1},{0x0156,1},
    {0x0158,1},{0x015A,1},{0x015C,1},{0x015E,1},{0x0160,1},{0x0162,1},
    {0x0164,1},{0x0166,1},{0x0168,1},{0x016A,1},{0x016C,1},{0x016E,1},
    {0x0170,1},{0x0172,1},{0x0174,1},{0x0176,1},{0x0178,-121},
    {0x0179,1},{0x017B,1},{0x017D,1},
    {0x0391,32},{0x0392,32},{0x0393,32},{0x0394,32},{0x0395,32},{0x0396,32},
    {0x0397,32},{0x0398,32},{0x0399,32},{0x039A,32},{0x039B,32},{0x039C,32},
    {0x039D,32},{0x039E,32},{0x039F,32},{0x03A0,32},{0x03A1,32},{0x03A3,32},
    {0x03A4,32},{0x03A5,32},{0x03A6,32},{0x03A7,32},{0x03A8,32},{0x03A9,32},
    {0x0410,32},{0x0411,32},{0x0412,32},{0x0413,32},{0x0414,32},{0x0415,32},
    {0x0416,32},{0x0417,32},{0x0418,32},{0x0419,32},{0x041A,32},{0x041B,32},
    {0x041C,32},{0x041D,32},{0x041E,32},{0x041F,32},{0x0420,32},{0x0421,32},
    {0x0422,32},{0x0423,32},{0x0424,32},{0x0425,32},{0x0426,32},{0x0427,32},
    {0x0428,32},{0x0429,32},{0x042A,32},{0x042B,32},{0x042C,32},{0x042D,32},
    {0x042E,32},{0x042F,32},
};
#define UPPER_TO_LOWER_COUNT (int)(sizeof(UPPER_TO_LOWER)/sizeof(UPPER_TO_LOWER[0]))
 
static const CaseEntry LOWER_TO_UPPER[] = {
    {0x0061,-32},{0x0062,-32},{0x0063,-32},{0x0064,-32},{0x0065,-32},{0x0066,-32},
    {0x0067,-32},{0x0068,-32},{0x0069,-32},{0x006A,-32},{0x006B,-32},{0x006C,-32},
    {0x006D,-32},{0x006E,-32},{0x006F,-32},{0x0070,-32},{0x0071,-32},{0x0072,-32},
    {0x0073,-32},{0x0074,-32},{0x0075,-32},{0x0076,-32},{0x0077,-32},{0x0078,-32},
    {0x0079,-32},{0x007A,-32},
    {0x00E0,-32},{0x00E1,-32},{0x00E2,-32},{0x00E3,-32},{0x00E4,-32},{0x00E5,-32},
    {0x00E6,-32},{0x00E7,-32},{0x00E8,-32},{0x00E9,-32},{0x00EA,-32},{0x00EB,-32},
    {0x00EC,-32},{0x00ED,-32},{0x00EE,-32},{0x00EF,-32},{0x00F0,-32},{0x00F1,-32},
    {0x00F2,-32},{0x00F3,-32},{0x00F4,-32},{0x00F5,-32},{0x00F6,-32},
    {0x00F8,-32},{0x00F9,-32},{0x00FA,-32},{0x00FB,-32},{0x00FC,-32},{0x00FD,-32},
    {0x00FE,-32},{0x00FF,121},
    {0x0101,-1},{0x0103,-1},{0x0105,-1},{0x0107,-1},{0x0109,-1},{0x010B,-1},
    {0x010D,-1},{0x010F,-1},{0x0111,-1},{0x0113,-1},{0x0115,-1},{0x0117,-1},
    {0x0119,-1},{0x011B,-1},{0x011D,-1},{0x011F,-1},{0x0121,-1},{0x0123,-1},
    {0x0125,-1},{0x0127,-1},{0x0129,-1},{0x012B,-1},{0x012D,-1},{0x012F,-1},
    {0x0131,232},
    {0x0133,-1},{0x0135,-1},{0x0137,-1},{0x013A,-1},{0x013C,-1},{0x013E,-1},
    {0x0140,-1},{0x0142,-1},{0x0144,-1},{0x0146,-1},{0x0148,-1},{0x014B,-1},
    {0x014D,-1},{0x014F,-1},{0x0151,-1},{0x0153,-1},{0x0155,-1},{0x0157,-1},
    {0x0159,-1},{0x015B,-1},{0x015D,-1},{0x015F,-1},{0x0161,-1},{0x0163,-1},
    {0x0165,-1},{0x0167,-1},{0x0169,-1},{0x016B,-1},{0x016D,-1},{0x016F,-1},
    {0x0171,-1},{0x0173,-1},{0x0175,-1},{0x0177,-1},{0x017A,-1},{0x017C,-1},
    {0x017E,-1},
    {0x03B1,-32},{0x03B2,-32},{0x03B3,-32},{0x03B4,-32},{0x03B5,-32},{0x03B6,-32},
    {0x03B7,-32},{0x03B8,-32},{0x03B9,-32},{0x03BA,-32},{0x03BB,-32},{0x03BC,-32},
    {0x03BD,-32},{0x03BE,-32},{0x03BF,-32},{0x03C0,-32},{0x03C1,-32},{0x03C3,-32},
    {0x03C4,-32},{0x03C5,-32},{0x03C6,-32},{0x03C7,-32},{0x03C8,-32},{0x03C9,-32},
    {0x0430,-32},{0x0431,-32},{0x0432,-32},{0x0433,-32},{0x0434,-32},{0x0435,-32},
    {0x0436,-32},{0x0437,-32},{0x0438,-32},{0x0439,-32},{0x043A,-32},{0x043B,-32},
    {0x043C,-32},{0x043D,-32},{0x043E,-32},{0x043F,-32},{0x0440,-32},{0x0441,-32},
    {0x0442,-32},{0x0443,-32},{0x0444,-32},{0x0445,-32},{0x0446,-32},{0x0447,-32},
    {0x0448,-32},{0x0449,-32},{0x044A,-32},{0x044B,-32},{0x044C,-32},{0x044D,-32},
    {0x044E,-32},{0x044F,-32},
};
#define LOWER_TO_UPPER_COUNT (int)(sizeof(LOWER_TO_UPPER)/sizeof(LOWER_TO_UPPER[0]))

bool IsValidCodePoint(uint32_t codepoint) {
    if (codepoint > UTF8_MAX_CODEPOINT)
        return false;
    
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
        return false;
    
    return true;
}

/// Decodes one endpoint starting at the given index.
/// Will return a Utf8Char which represents a single Unicode character.
/// If the sequence is malformed, the replacement character U+FFFD is returned.
Utf8Char DecodeUtf8(const char* str, int byteLength, int index) {
    if (index < 0 || index >= byteLength)
        return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 0 };

    uint8_t firstByte = (uint8_t)str[index];

    // ASCII char, return it.
    if (firstByte < 0x80)
        return (Utf8Char){ (uint32_t)firstByte, 1 };

    if (firstByte < 0xC0)
        return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 };

#define CONT_BYTE(offset, var)                                                       \
    if (index + (offset) >= byteLength) return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 }; \
    uint8_t var = (uint8_t)str[index + (offset)];                                   \
    if ((var & 0xC0) != 0x80) return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 }

    // 2-byte scheme (110xxxxx 10xxxxxx)
    if (firstByte < 0xE0) {
        CONT_BYTE(1, secondByte);

        uint32_t codepoint = ((uint32_t)(firstByte & 0x1F) << 6) | (uint32_t)(secondByte & 0x3F);
        
        if (codepoint < 0x80)
            return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 };

        return (Utf8Char){ codepoint, 2 };
    }

    // 3-byte scheme (1110xxxx 10xxxxxx 10xxxxxx)
    if (firstByte < 0xF0) {
        CONT_BYTE(1, secondByte);
        CONT_BYTE(2, thirdByte);

        uint32_t codepoint = ((uint32_t)(firstByte & 0x0F) << 12) | 
                      ((uint32_t)(secondByte & 0x3F) <<  6) | 
                      (uint32_t)(thirdByte & 0x3F);

        if (codepoint < 0x800)
            return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 };
        
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
            return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 };

        return (Utf8Char){ codepoint, 3 };
    }

    // 4-byte scheme (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
    if (firstByte < 0xF8) {
        CONT_BYTE(1, secondByte);
        CONT_BYTE(2, thirdByte);
        CONT_BYTE(3, fourthByte);

        uint32_t codepoint = ((uint32_t)(firstByte & 0x07) << 18) |
                             ((uint32_t)(secondByte & 0x3F) << 12) |
                             ((uint32_t)(thirdByte & 0x3F) << 6) |
                             (uint32_t)(fourthByte & 0x3F);

        if (codepoint < 0x10000)
            return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 }; /* overlong */
        
        if (codepoint > UTF8_MAX_CODEPOINT)
            return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 };
        
        return (Utf8Char){ codepoint, 4 };
    }

#undef CONT_BYTE

    return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 1 };
}

/// Returns the number of UTF-8 codepoints in a string.
int Utf8Strlen(const char* str) {
    if (!str) return 0;

    int byteLength = (int)strlen(str);
    int count = 0;
    int i = 0;

    while (i < byteLength) {
        Utf8Char chr = DecodeUtf8(str, byteLength, i);

        i += (chr.length > 0) ? chr.length : 1;
        count++;
    }

    return count;
}

/// Returns the number of UTF-8 codepoints in a string of known byte-length.
/// This works identically to Utf8StrLen, just that you also pass a byte length.
int Utf8StrnCpLen(const char* str, int byteLength) {
    if (!str || byteLength <= 0) return 0;

    int count = 0;
    int i = 0;
    
    while (i < byteLength) {
        Utf8Char chr = DecodeUtf8(str, byteLength, i);
   
        i += (chr.length > 0) ? chr.length : 1;
        count++;
    }
   
    return count;
}

/*
 * Utf8CodepointAt — return the codepoint at logical index codepointIndex.
 * Returns {UTF8_REPLACEMENT_CHAR, 0} if codepointIndex is out of range.
 */
Utf8Char Utf8CodepointAt(const char* str, int byteLength, int codepointIndex) {
    if (!str || codepointIndex < 0) return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 0 };

    int i = 0;
    int codepoint = 0;

    while (i < byteLength) {
        Utf8Char chr = DecodeUtf8(str, byteLength, i);
        
        if (codepoint == codepointIndex) return chr;
        
        i += (chr.length > 0) ? chr.length : 1;
        codepoint++;
    }

    return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 0 };
}

/*
 * Utf8ByteOffsetAt — return the byte offset of logical codepoint index codepointIndex.
 * Allows pointing one past the end for slicing semantics.  Returns -1 if truly
 * out of range.
 */
int Utf8ByteOffsetAt(const char* str, int byteLength, int codepointIndex) {
    if (!str || codepointIndex < 0) return -1;
    
    int i = 0;
    int codepoint = 0;

    while (i < byteLength) {
        if (codepoint == codepointIndex) return i;

        Utf8Char chr = DecodeUtf8(str, byteLength, i);
        i += (chr.length > 0) ? chr.length : 1;

        codepoint++;
    }

    if (codepoint == codepointIndex) return i;

    return -1;
}

/*
 * Utf8Encode — encode one Unicode codepoint into UTF-8.
 * `out` must be at least 4 bytes.  Returns bytes written (1–4).
 * Invalid codepoints emit the replacement character.
 */
int Utf8Encode(uint32_t codepoint, char* output) {
    if (!IsValidCodePoint(codepoint)) codepoint = UTF8_REPLACEMENT_CHAR;

    if (codepoint < 0x80) {
        output[0] = (char)codepoint;
        return 1;
    }

    if (codepoint < 0x800) {
        output[0] = (char)(0xC0 | ( codepoint >> 6));
        output[1] = (char)(0x80 | ( codepoint & 0x3F));
        return 2;
    }

    if (codepoint < 0x10000) {
        output[0] = (char)(0xE0 | ( codepoint >> 12));
        output[1] = (char)(0x80 | ((codepoint >>  6) & 0x3F));
        output[2] = (char)(0x80 | ( codepoint        & 0x3F));
        return 3;
    }

    output[0] = (char)(0xF0 | (codepoint >> 18));
    output[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    output[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    output[3] = (char)(0x80 | (codepoint & 0x3F));

    return 4;
}

/*
 * Utf8Reverse — return a heap-allocated copy of str (byteLength bytes) with its
 * codepoints in reverse order.  The reversed string has the same byte length as
 * the original.  Writes the byte length into *outByteLength.
 * The caller owns the returned buffer and must free() it.
 */
char* Utf8Reverse(const char* str, int byteLength, int* outByteLength) {
    if (!str || byteLength <= 0) {
        if (outByteLength)
            *outByteLength = 0;

        char* empty = (char*)malloc(1);
        
        if (empty)
            empty[0] = '\0';
        
        return empty;
    }

    uint32_t* codepoints = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)byteLength);
    int* lens = (int*)malloc(sizeof(int) * (size_t)byteLength);
    
    if (!codepoints || !lens) {
        free(codepoints);
        free(lens);
        
        if (outByteLength) 
            *outByteLength = 0;
        
        return NULL;
    }

    int codepointCount = 0;
    int i = 0;
    
    while (i < byteLength) {
        Utf8Char chr = DecodeUtf8(str, byteLength, i);
        int advance = (chr.length > 0) ? chr.length : 1;
    
        codepoints[codepointCount] = chr.codepoint;
        lens[codepointCount] = advance;
    
        i += advance;
        codepointCount++;
    }

    char* output = (char*)malloc((size_t)byteLength + 1);
    if (!output) {
        free(codepoints);
        free(lens);
        
        if (outByteLength)
            *outByteLength = 0;
        
        return NULL;
    }

    int pos = 0;

    for (int j = codepointCount - 1; j >= 0; j--) {
        char enc[4];
        int n = Utf8Encode(codepoints[j], enc);
        
        memcpy(output + pos, enc, (size_t)n);
        
        pos += n;
    }

    output[pos] = '\0';

    free(codepoints);
    free(lens);

    if (outByteLength)
        *outByteLength = pos;

    return output;
}

static int32_t CaseDelta(const CaseEntry* table, int count, uint32_t codepoint) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (table[mid].codepoint == codepoint) return table[mid].delta;
        if (table[mid].codepoint < codepoint) lo = mid + 1;
        else                      hi = mid - 1;
    }
    return 0;
}
 
bool Utf8IndexBuild(Utf8Index* idx, const char* str, int byteLength) {
    idx->offsets = NULL;
    idx->codepointCount = 0;
    idx->byteLength = byteLength;
    if (!str || byteLength <= 0) {
        idx->offsets = (uint32_t*)malloc(sizeof(uint32_t));
        if (!idx->offsets) return false;
        idx->offsets[0] = 0;
        return true;
    }
    uint32_t* offsets = (uint32_t*)malloc(sizeof(uint32_t) * ((size_t)byteLength + 1));
    if (!offsets) return false;
    int i = 0, count = 0;
    while (i < byteLength) {
        offsets[count++] = (uint32_t)i;
        Utf8Char ch = UTF8_DECODE_FAST(str, byteLength, i);
        i += (ch.length > 0) ? ch.length : 1;
    }
    offsets[count] = (uint32_t)byteLength;
    uint32_t* trimmed = (uint32_t*)realloc(offsets, sizeof(uint32_t) * ((size_t)count + 1));
    idx->offsets = trimmed ? trimmed : offsets;
    idx->codepointCount = count;
    return true;
}
 
void Utf8IndexFree(Utf8Index* idx) {
    free(idx->offsets);
    idx->offsets = NULL;
    idx->codepointCount = 0;
    idx->byteLength = 0;
}
 
Utf8Char Utf8IndexCharAt(const Utf8Index* idx, const char* str, int cpIndex) {
    if (!idx || cpIndex < 0 || cpIndex >= idx->codepointCount)
        return (Utf8Char){ UTF8_REPLACEMENT_CHAR, 0 };
    return UTF8_DECODE_FAST(str, idx->byteLength, (int)idx->offsets[cpIndex]);
}
 
int Utf8IndexByteOffset(const Utf8Index* idx, int cpIndex) {
    if (!idx || cpIndex < 0 || cpIndex > idx->codepointCount) return -1;
    return (int)idx->offsets[cpIndex];
}
 
char* Utf8IndexSubstring(const Utf8Index* idx, const char* str, int cpStart, int cpEnd) {
    if (!idx || !str || cpStart < 0 || cpEnd < cpStart || cpEnd > idx->codepointCount) return NULL;
    int startByte = (int)idx->offsets[cpStart];
    int endByte   = (int)idx->offsets[cpEnd];
    int len       = endByte - startByte;
    char* out = (char*)malloc((size_t)len + 1);
    if (!out) return NULL;
    memcpy(out, str + startByte, (size_t)len);
    out[len] = '\0';
    return out;
}
 
uint32_t Utf8SimpleLower(uint32_t cp) {
    int32_t d = CaseDelta(UPPER_TO_LOWER, UPPER_TO_LOWER_COUNT, cp);
    return d ? (uint32_t)((int32_t)cp + d) : cp;
}
 
uint32_t Utf8SimpleUpper(uint32_t cp) {
    int32_t d = CaseDelta(LOWER_TO_UPPER, LOWER_TO_UPPER_COUNT, cp);
    return d ? (uint32_t)((int32_t)cp + d) : cp;
}
 
bool Utf8IsUpper(uint32_t cp) { return CaseDelta(UPPER_TO_LOWER, UPPER_TO_LOWER_COUNT, cp) != 0; }
bool Utf8IsLower(uint32_t cp) { return CaseDelta(LOWER_TO_UPPER, LOWER_TO_UPPER_COUNT, cp) != 0; }
 
static char* ApplyCaseMap(const char* str, int byteLength, uint32_t (*mapFn)(uint32_t)) {
    if (!str || byteLength <= 0) {
        char* e = (char*)malloc(1); if (e) e[0] = '\0'; return e;
    }
    StringBuilder sb; SBInit(&sb); SBEnsure(&sb, byteLength);
    int i = 0;
    while (i < byteLength) {
        Utf8Char ch = UTF8_DECODE_FAST(str, byteLength, i);
        int adv = (ch.length > 0) ? ch.length : 1;
        char enc[4];
        int n = Utf8Encode(mapFn(ch.codepoint), enc);
        SBAppend(&sb, enc, n);
        i += adv;
    }
    return sb.buffer;
}
 
char* Utf8ToLower(const char* str, int byteLength) { return ApplyCaseMap(str, byteLength, Utf8SimpleLower); }
char* Utf8ToUpper(const char* str, int byteLength) { return ApplyCaseMap(str, byteLength, Utf8SimpleUpper); }
 
char* Utf8ToTitle(const char* str, int byteLength) {
    if (!str || byteLength <= 0) {
        char* e = (char*)malloc(1); if (e) e[0] = '\0'; return e;
    }
    StringBuilder sb; SBInit(&sb); SBEnsure(&sb, byteLength);
    bool newWord = true; int i = 0;
    while (i < byteLength) {
        Utf8Char ch = UTF8_DECODE_FAST(str, byteLength, i);
        int adv = (ch.length > 0) ? ch.length : 1;
        uint32_t mapped;
        if (Utf8IsWhitespace(ch.codepoint)) { mapped = ch.codepoint; newWord = true; }
        else if (newWord) { mapped = Utf8SimpleUpper(ch.codepoint); newWord = false; }
        else { mapped = Utf8SimpleLower(ch.codepoint); }
        char enc[4]; int n = Utf8Encode(mapped, enc);
        SBAppend(&sb, enc, n);
        i += adv;
    }
    return sb.buffer;
}
 
bool Utf8IsWhitespace(uint32_t cp) {
    switch (cp) {
        case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D:
        case 0x0020: case 0x0085: case 0x00A0: case 0x1680:
        case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
        case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
        case 0x200A: case 0x2028: case 0x2029: case 0x202F: case 0x205F:
        case 0x3000: return true;
        default:     return false;
    }
}
 
char* Utf8LStrip(const char* str, int byteLength, int* outByteLength) {
    int i = 0;
    while (i < byteLength) {
        Utf8Char ch = UTF8_DECODE_FAST(str, byteLength, i);
        if (!Utf8IsWhitespace(ch.codepoint)) break;
        i += (ch.length > 0) ? ch.length : 1;
    }
    int len = byteLength - i;
    char* out = (char*)malloc((size_t)len + 1);
    if (!out) return NULL;
    memcpy(out, str + i, (size_t)len);
    out[len] = '\0';
    if (outByteLength) *outByteLength = len;
    return out;
}
 
char* Utf8RStrip(const char* str, int byteLength, int* outByteLength) {
    int i = 0, last_nonws_end = 0;
    while (i < byteLength) {
        Utf8Char ch = UTF8_DECODE_FAST(str, byteLength, i);
        int adv = (ch.length > 0) ? ch.length : 1;
        if (!Utf8IsWhitespace(ch.codepoint)) last_nonws_end = i + adv;
        i += adv;
    }
    char* out = (char*)malloc((size_t)last_nonws_end + 1);
    if (!out) return NULL;
    memcpy(out, str, (size_t)last_nonws_end);
    out[last_nonws_end] = '\0';
    if (outByteLength) *outByteLength = last_nonws_end;
    return out;
}
 
char* Utf8Strip(const char* str, int byteLength, int* outByteLength) {
    int dummy;
    char* tmp = Utf8LStrip(str, byteLength, &dummy);
    if (!tmp) return NULL;
    char* result = Utf8RStrip(tmp, dummy, outByteLength);
    free(tmp);
    return result;
}
 
bool Utf8StartsWith(const char* str, int strLen, const char* prefix, int prefixLen) {
    if (prefixLen > strLen) return false;
    return memcmp(str, prefix, (size_t)prefixLen) == 0;
}
 
bool Utf8EndsWith(const char* str, int strLen, const char* suffix, int suffixLen) {
    if (suffixLen > strLen) return false;
    return memcmp(str + strLen - suffixLen, suffix, (size_t)suffixLen) == 0;
}
 
int Utf8FindBytes(const char* haystack, int haystackLen, const char* needle, int needleLen) {
    if (needleLen == 0) return 0;
    if (needleLen > haystackLen) return -1;
    unsigned char skip[256];
    for (int k = 0; k < 256; k++) skip[k] = (unsigned char)needleLen;
    for (int k = 0; k < needleLen - 1; k++)
        skip[(unsigned char)needle[k]] = (unsigned char)(needleLen - 1 - k);
    int i = needleLen - 1;
    while (i < haystackLen) {
        int j = needleLen - 1, k = i;
        while (j >= 0 && haystack[k] == needle[j]) { j--; k--; }
        if (j < 0) return k + 1;
        i += skip[(unsigned char)haystack[i]];
    }
    return -1;
}
 
int Utf8Count(const char* str, int strLen, const char* sub, int subLen) {
    if (subLen == 0 || subLen > strLen) return 0;
    int count = 0, pos = 0;
    while (pos <= strLen - subLen) {
        int found = Utf8FindBytes(str + pos, strLen - pos, sub, subLen);
        if (found < 0) break;
        count++;
        pos += found + subLen;
    }
    return count;
}
 
char* Utf8Replace(const char* str, int strLen, const char* oldSub, int oldLen,
                  const char* newSub, int newLen, int maxReplacements, int* outByteLength) {
    if (oldLen == 0 || oldLen > strLen) {
        char* copy = (char*)malloc((size_t)strLen + 1);
        if (!copy) return NULL;
        memcpy(copy, str, (size_t)strLen);
        copy[strLen] = '\0';
        if (outByteLength) *outByteLength = strLen;
        return copy;
    }
    StringBuilder sb; SBInit(&sb); SBEnsure(&sb, strLen);
    int pos = 0, replaced = 0;
    while (pos < strLen) {
        if (maxReplacements < 0 || replaced < maxReplacements) {
            int found = Utf8FindBytes(str + pos, strLen - pos, oldSub, oldLen);
            if (found >= 0) {
                SBAppend(&sb, str + pos, found);
                SBAppend(&sb, newSub, newLen);
                pos += found + oldLen;
                replaced++;
                continue;
            }
        }
        SBAppend(&sb, str + pos, strLen - pos);
        break;
    }
    if (outByteLength) *outByteLength = sb.length;
    return sb.buffer;
}
 
char** Utf8Split(const char* str, int strLen, const char* sep, int sepLen, int* outCount) {
    *outCount = 0;
    if (sep == NULL) {
        int tokens = 0; bool inToken = false; int i = 0;
        while (i < strLen) {
            Utf8Char ch = UTF8_DECODE_FAST(str, strLen, i);
            int adv = (ch.length > 0) ? ch.length : 1;
            if (!Utf8IsWhitespace(ch.codepoint)) { if (!inToken) { tokens++; inToken = true; } }
            else { inToken = false; }
            i += adv;
        }
        if (tokens == 0) { char** a = (char**)malloc(sizeof(char*)); *outCount = 0; return a; }
        char** arr = (char**)malloc(sizeof(char*) * (size_t)tokens);
        if (!arr) return NULL;
        int t = 0; i = 0;
        while (i < strLen && t < tokens) {
            while (i < strLen) { Utf8Char ch = UTF8_DECODE_FAST(str, strLen, i); int adv = (ch.length>0)?ch.length:1; if (!Utf8IsWhitespace(ch.codepoint)) break; i += adv; }
            int start = i;
            while (i < strLen) { Utf8Char ch = UTF8_DECODE_FAST(str, strLen, i); int adv = (ch.length>0)?ch.length:1; if (Utf8IsWhitespace(ch.codepoint)) break; i += adv; }
            int len = i - start;
            arr[t] = (char*)malloc((size_t)len + 1);
            if (!arr[t]) { for (int k=0;k<t;k++) free(arr[k]); free(arr); return NULL; }
            memcpy(arr[t], str + start, (size_t)len); arr[t][len] = '\0'; t++;
        }
        *outCount = tokens; return arr;
    }
    if (sepLen <= 0) return NULL;
    int pieces = 1, pos = 0;
    while (pos <= strLen - sepLen) {
        int found = Utf8FindBytes(str + pos, strLen - pos, sep, sepLen);
        if (found < 0) break;
        pieces++; pos += found + sepLen;
    }
    char** arr = (char**)malloc(sizeof(char*) * (size_t)pieces);
    if (!arr) return NULL;
    int t = 0; pos = 0;
    while (t < pieces - 1) {
        int found = Utf8FindBytes(str + pos, strLen - pos, sep, sepLen);
        arr[t] = (char*)malloc((size_t)found + 1);
        if (!arr[t]) { for (int k=0;k<t;k++) free(arr[k]); free(arr); return NULL; }
        memcpy(arr[t], str + pos, (size_t)found); arr[t][found] = '\0'; t++;
        pos += found + sepLen;
    }
    int lastLen = strLen - pos;
    arr[t] = (char*)malloc((size_t)lastLen + 1);
    if (!arr[t]) { for (int k=0;k<t;k++) free(arr[k]); free(arr); return NULL; }
    memcpy(arr[t], str + pos, (size_t)lastLen); arr[t][lastLen] = '\0';
    *outCount = pieces; return arr;
}
 
char* Utf8Repeat(const char* str, int strLen, int n, int* outByteLength) {
    if (n <= 0 || strLen <= 0) {
        char* e = (char*)malloc(1);
        if (e) e[0] = '\0';
        if (outByteLength) *outByteLength = 0;
        return e;
    }
    int total = strLen * n;
    char* out = (char*)malloc((size_t)total + 1);
    if (!out) return NULL;
    for (int i = 0; i < n; i++) memcpy(out + i * strLen, str, (size_t)strLen);
    out[total] = '\0';
    if (outByteLength) *outByteLength = total;
    return out;
}
 
char* Utf8Join(const char* sep, int sepLen, const char** parts, const int* partLens,
               int count, int* outByteLength) {
    if (count <= 0) {
        char* e = (char*)malloc(1);
        if (e) e[0] = '\0';
        if (outByteLength) *outByteLength = 0;
        return e;
    }
    int total = 0;
    for (int i = 0; i < count; i++) { total += partLens[i]; if (i < count-1) total += sepLen; }
    char* out = (char*)malloc((size_t)total + 1);
    if (!out) return NULL;
    int pos = 0;
    for (int i = 0; i < count; i++) {
        memcpy(out + pos, parts[i], (size_t)partLens[i]); pos += partLens[i];
        if (i < count-1) { memcpy(out + pos, sep, (size_t)sepLen); pos += sepLen; }
    }
    out[pos] = '\0';
    if (outByteLength) *outByteLength = pos;
    return out;
}
