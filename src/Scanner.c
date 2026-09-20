#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Common.h"
#include "Scanner.h"
#include "Utilities.h"
#include "Memory.h"

#define SCANNER_STATE_STACK_MAX 64

typedef struct {
    const char* start;
    const char* current;
    int line;
    int previousLine;
    char* source;
} Scanner;

Scanner scanner;
bool inStringInterpolation = false;
static KTN_ScannerSnapshot scannerStateStack[SCANNER_STATE_STACK_MAX];
static int scannerStateDepth = 0;

void KTN_ScannerInit(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
    scanner.previousLine = 0;
    scanner.source = NULL;
}

static bool ScannerAtEnd() {
    return (*scanner.current == '\0');
}

void KTN_ScannerPushState() {
    if (scannerStateDepth >= SCANNER_STATE_STACK_MAX) {
        fprintf(stderr, "Reached maximum depth of scanner state stack.\n");
        exit(1);
    }

    KTN_ScannerSnapshot* snapshot = &scannerStateStack[scannerStateDepth++];

    snapshot->start = scanner.start;
    snapshot->current = scanner.current;
    snapshot->line = scanner.line;
    snapshot->previousLine = scanner.previousLine;
    snapshot->source = (scanner.source != NULL) ? strdup(scanner.source) : NULL;
}

void KTN_ScannerRestoreTopState() {
    if (scannerStateDepth == 0) {
        fprintf(stderr, "Scanner state stack is empty.\n");
        exit(1);
    }

    KTN_ScannerSnapshot* snapshot = &scannerStateStack[scannerStateDepth - 1];

    scanner.start = snapshot->start;
    scanner.current = snapshot->current;
    scanner.line = snapshot->line;
    scanner.previousLine = snapshot->previousLine;
    
    if (scanner.source) free(scanner.source);

    scanner.source = (snapshot->source) ? strdup(snapshot->source) : NULL;
}

void KTN_ScannerPopState() {
    if (scannerStateDepth == 0) {
        fprintf(stderr, "Scanner state stack is empty.\n");
        exit(1);
    }

    KTN_ScannerSnapshot* snapshot = &scannerStateStack[--scannerStateDepth];

    if (snapshot->source) {
        free(snapshot->source);
        snapshot->source = NULL;
    }
}

static KTN_Token TokenMake(KTN_TokenType type) {
    KTN_Token newToken;
    
    newToken.type = type;
    newToken.start = scanner.start;
    newToken.length = (int)(scanner.current - scanner.start);
    newToken.line = scanner.line;

    return newToken;
}

static KTN_Token TokenError(const char* msg) {
    KTN_Token errorToken;

    errorToken.type = TOKEN_ERROR;
    errorToken.start = msg;
    errorToken.length = (int)strlen(msg);
    errorToken.line = scanner.line;

    return errorToken;
}

static char ScannerAdvance() {
    scanner.current++;
    
    return scanner.current[-1];
}

static char ScannerMatch(char expected) {
    if (ScannerAtEnd())
        return false;

    if (*scanner.current != expected)
        return false;

    scanner.current++;
    return true;
}

static char ScannerPeek() {
    return *scanner.current;
}

static char ScannerPeekNext() {
    if (ScannerAtEnd())
        return '\0';
    return scanner.current[1];
}

static char ScannerPeekPrevious() {
    return scanner.current[-1];
}

/// @brief Advance past a full UTF-8 codepoint starting at scanner.current.
/// For ASCII bytes this is identical to ScannerAdvance(). For multi-byte
/// sequences it advances all continuation bytes in one call.
static void ScannerAdvanceCodepoint() {
    uint8_t b = (uint8_t)*scanner.current;

    int seqLen = 1;
    
    if ((b & 0xE0) == 0xC0) seqLen = 2;
    else if ((b & 0xF0) == 0xE0) seqLen = 3;
    else if ((b & 0xF8) == 0xF0) seqLen = 4;

    for (int i = 0; i < seqLen && !ScannerAtEnd(); i++)
        scanner.current++;
}

/// @brief Return true if the byte at scanner.current can continue a Katane
///  identifier. This allows ASCII alphanumerics/underscore plus any high byte
///  (>= 0x80) so that Unicode letters work naturally in names.
static bool ScannerIsIdentifierContinue() {
    unsigned char c = (unsigned char)*scanner.current;

    return (IsAlphanumeric((char)c) || IsDigit((char)c) || c >= 0x80);
}

static void SkipWhitespace() {
    for (;;) {
        char currentChar = ScannerPeek();
        char secondChar;

        switch(currentChar) {
            case ' ':
            case '\r':
            case '\t':
                ScannerAdvance();
                break;
            case '/':
                secondChar = ScannerPeekNext();
                if (secondChar == '/') {
                    while (ScannerPeek() != '\n' && !ScannerAtEnd())
                        ScannerAdvance();
                }
                else if (secondChar == '*') {
                    while (!(ScannerPeek() == '*' && ScannerPeekNext() == '/') && !ScannerAtEnd())
                        ScannerAdvance();

                    if (!ScannerAtEnd()) {
                        ScannerAdvance();
                        ScannerAdvance();
                    }
                }
                else {
                    return;
                }
                break;
            case '\n':
                scanner.line++;
                ScannerAdvance();
                break;
            default:
                return;
        }
    }
}

static KTN_Token ScannerScanString(char stringChar) {
    while (!ScannerAtEnd()) {
        char currentPeek = ScannerPeek();

        if (currentPeek == stringChar) {
            break;
        }

        if (currentPeek == '\n') {
            scanner.line++;
        }

        if (currentPeek == '\\') {
            ScannerAdvance();
            
            if (ScannerAtEnd()) {
                return TokenError("Unterminated string escape sequence.");
            }
            
            ScannerAdvance();
        } else {
            ScannerAdvance();
        }
    }

    if (ScannerAtEnd()) {
        return TokenError("Unterminated string literal.");
    }

    ScannerAdvance();
    return TokenMake(TOKEN_STRING);
}


static KTN_Token ScannerScanNumber() {
    // Check for a base prefix (0b, 0o, 0x)
    if (ScannerPeekPrevious() == '0') {
        char next = ScannerPeek();

        if (next == 'b' || next == 'B') {
            ScannerAdvance();

            if (!IsBinary(ScannerPeek()))
                return TokenError("Expected binary digits after '0b'");

            while (IsBinary(ScannerPeek()) || (ScannerPeek() == '_' && IsBinary(ScannerPeekNext())))
                ScannerAdvance();

            return TokenMake(TOKEN_BINARY);
        }

        if (next == 'o' || next == 'O') {
            ScannerAdvance();
            
            if (!IsOctal(ScannerPeek()))
                return TokenError("Expected octal digits after '0o'");

            while (IsOctal(ScannerPeek()) || (ScannerPeek() == '_' && IsOctal(ScannerPeekNext())))
                ScannerAdvance();

            return TokenMake(TOKEN_OCTAL);
        }

        if (next == 'x' || next == 'X') {
            ScannerAdvance();
            
            if (!IsHexadecimal(ScannerPeek()))
                return TokenError("Expected hex digits after '0x'");

            while (IsHexadecimal(ScannerPeek()) || (ScannerPeek() == '_' && IsHexadecimal(ScannerPeekNext())))
                ScannerAdvance();

            return TokenMake(TOKEN_HEX);
        }
    }

    while (IsDigit(ScannerPeek()) || (ScannerPeek() == '_' && IsDigit(ScannerPeekNext())))
        ScannerAdvance();

    bool isFloat = false;

    // Fractional part.
    if (ScannerPeek() == '.' && IsDigit(ScannerPeekNext())) {
        isFloat = true;
        ScannerAdvance(); // consume '.'
        while (IsDigit(ScannerPeek()) ||
               (ScannerPeek() == '_' && IsDigit(ScannerPeekNext())))
            ScannerAdvance();
    }

    if (ScannerPeek() == 'e' || ScannerPeek() == 'E') {
        isFloat = true;

        ScannerAdvance();
        
        if (ScannerPeek() == '+' || ScannerPeek() == '-')
            ScannerAdvance();
            
        if (!IsDigit(ScannerPeek()))
            return TokenError("Expected digits in exponent");

        while (IsDigit(ScannerPeek()))
            ScannerAdvance();
    }

    return TokenMake(isFloat ? TOKEN_NUMBER : TOKEN_INT);
}

static KTN_TokenType CheckKeyword(int start, const char* rest, KTN_TokenType type) {
    int restLength = (int)strlen(rest);

    if ((scanner.current - scanner.start == start + restLength) && memcmp(scanner.start + start, rest, restLength) == 0)
        return type;
    
    return TOKEN_IDENTIFIER;
}

static KTN_TokenType IdentifierType() {
    switch (scanner.start[0]) {
        case 'a':
            if (scanner.current - scanner.start > 1) {
                KTN_TokenType possible = CheckKeyword(1, "s", TOKEN_AS);

                if (possible != TOKEN_IDENTIFIER) {
                    return possible;
                }

                switch(scanner.start[1]) {
                    case 'n': return CheckKeyword(2, "d", TOKEN_AND);
                    case 's': return CheckKeyword(2, "sert", TOKEN_ASSERT);
                }
            }
            break;
        case 'b': return CheckKeyword(1, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 'h': return CheckKeyword(2, "oice", TOKEN_CHOICE);
                    case 'o': {
                        KTN_TokenType possible;

                        possible = CheckKeyword(2, "nst", TOKEN_CONST);
          
                        if (possible == TOKEN_IDENTIFIER) {
                            possible = CheckKeyword(2, "ntinue", TOKEN_CONTINUE);
                        }

                        return possible;
                    }
                    case 'a': {
                        switch(scanner.start[2]) {
                            case 's': return CheckKeyword(3, "e", TOKEN_CASE);
                            case 't': return CheckKeyword(3, "ch", TOKEN_CATCH);
                        }
                    }
                }
            }
            break;
        case 'd': return CheckKeyword(1, "efault", TOKEN_DEFAULT);
        case 'e': {
            if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 'l': return CheckKeyword(2, "se", TOKEN_ELSE);
                    case 'n': return CheckKeyword(2, "try", TOKEN_ENTRY);
                }
            }
            break;
        }
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 'a': {
                        KTN_TokenType possible = CheckKeyword(2, "ll", TOKEN_FALL);
                        if (possible != TOKEN_IDENTIFIER) return possible;
                        return CheckKeyword(2, "lse", TOKEN_FALSE);
                    }
                    case 'o': return CheckKeyword(2, "r", TOKEN_FOR);
                    case 'i': {
                        KTN_TokenType possible = CheckKeyword(2, "nal", TOKEN_FINAL);
                        if (possible != TOKEN_IDENTIFIER) return possible;
                        return CheckKeyword(2, "nally", TOKEN_FINALLY);
                    }
                }
            }
            break;
        case 'g': return CheckKeyword(1, "et", TOKEN_GET);
        case 'h': return CheckKeyword(1, "idden", TOKEN_HIDDEN);
        case 'i': {
            KTN_TokenType possible = CheckKeyword(1, "f", TOKEN_IF);

            if (possible == TOKEN_IDENTIFIER) {
                possible = CheckKeyword(1, "s", TOKEN_IS);
            }

            return possible;
        }
        case 'l': return CheckKeyword(1, "ate", TOKEN_LATE);
        case 'k': return CheckKeyword(1, "ata", TOKEN_CLASS);
        case 'm': {
            KTN_TokenType test = CheckKeyword(1, "aybe", TOKEN_MAYBE);
            if (test != TOKEN_IDENTIFIER) return test;
            return CheckKeyword(1, "ochi", TOKEN_VAR);
        }
        case 'n': return CheckKeyword(1, "ull", TOKEN_NULL);
        case 'o': {
            if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 'p':   return CheckKeyword(2, "erator", TOKEN_OPERATOR);
                    case 'r':   return TOKEN_OR;
                    case 'n':   return TOKEN_ON;
                }
            }
            break;
        }
        case 'p': if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 'r': {
                        KTN_TokenType possible = CheckKeyword(2, "int", TOKEN_PRINT);

                        if (possible == TOKEN_IDENTIFIER) {
                            return CheckKeyword(2, "ivate", TOKEN_PRIVATE);
                        }

                        return possible;
                    }
                }
            }
            break;
        case 'r': {
            KTN_TokenType possible = CheckKeyword(1, "eturn", TOKEN_RETURN);
            if (possible != TOKEN_IDENTIFIER) return possible;
            return CheckKeyword(1, "ethrow", TOKEN_RETHROW);
        }
        case 's': if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 't': return CheckKeyword(2, "atic", TOKEN_STATIC);
                    case 'w': return CheckKeyword(2, "itch", TOKEN_SWITCH);
                    case 'o': return CheckKeyword(2, "kata", TOKEN_SUPER);
                    case 'h': return CheckKeyword(2, "iki", TOKEN_FUNCTION);
                    case 'e': return CheckKeyword(2, "t", TOKEN_SET);
                    case 'u': return CheckKeyword(2, "mmon", TOKEN_IMPORT);
                }
            }
            break;
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch(scanner.start[1]) {
                    case 'h': {
                        KTN_TokenType possible = CheckKeyword(2, "is", TOKEN_THIS);
                        if (possible != TOKEN_IDENTIFIER) return possible;
                        return CheckKeyword(2, "row", TOKEN_THROW);
                    }
                    case 'r': {
                        KTN_TokenType possible = CheckKeyword(2, "ue", TOKEN_TRUE);
                        if (possible != TOKEN_IDENTIFIER) return possible;
                        return CheckKeyword(2, "y", TOKEN_TRY);
                    }
                    case 'y': {
                        return CheckKeyword(2, "pe", TOKEN_TYPE);
                    }
                }
            }
            break;
        case 'u': return CheckKeyword(1, "sing", TOKEN_USING);
        case 'w': return CheckKeyword(1, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static KTN_Token ScannerScanIdentifier() {
    while (ScannerIsIdentifierContinue()) {
        ScannerAdvanceCodepoint();
        continue;

        if ((unsigned char)*scanner.current >= 0x80)
            ScannerAdvanceCodepoint();
        else
            ScannerAdvance();
    }
    return TokenMake(IdentifierType());
}

static int ScannerGetLineLength() {
    const char* p = &scanner.start[0];
    int l = 0;
    while (*p != '\0' && *p != '\n') {
        l++;
        p++;
    }
    return l;
}

static void ScannerSetSource() {
    int length = ScannerGetLineLength();

    if (length <= 0)
        return;

    if (scanner.source != NULL)
        free(scanner.source);

    scanner.source = (char*)malloc(sizeof(char) * (length + 1));

    if (scanner.source == NULL)
        exit(1);

    memcpy(scanner.source, scanner.start, length);
    scanner.source[length] = '\0';
}

char* KTN_ScannerGetSource() {
    if (scanner.source == NULL)
        ScannerSetSource();

    return scanner.source;
}

KTN_Token KTN_ScannerScanToken() {
    if (scanner.previousLine != scanner.line) {
        ScannerSetSource();
        scanner.previousLine = scanner.line;
    }

    SkipWhitespace();
    scanner.start = scanner.current;

    if (ScannerAtEnd())
        return TokenMake(TOKEN_EOF);

    char currentChar = ScannerAdvance();

    // A byte ≥ 0x80 is a UTF-8 continuation or leading byte.
    // We only ever arrive here on a leading byte (continuation bytes are consumed
    // by ScannerScanIdentifier / ScannerAdvanceCodepoint already), so treat any
    // high byte as the start of a Unicode identifier.
    if ((unsigned char)currentChar >= 0x80)
        return ScannerScanIdentifier();

    if (IsAlphanumeric(currentChar))
        return ScannerScanIdentifier();
    if (IsDigit(currentChar))
        return ScannerScanNumber();
  
    switch (currentChar) {
        case '(': return TokenMake(TOKEN_PARENTHESIS_OPEN); break;
        case ')': return TokenMake(TOKEN_PARENTHESIS_CLOSE); break;
        case '{':
            return TokenMake(TOKEN_BRACKET_OPEN);
        case '}': return TokenMake(TOKEN_BRACKET_CLOSE); break;
        case '[': return TokenMake(TOKEN_SQUARE_OPEN); break;
        case ']': return TokenMake(TOKEN_SQUARE_CLOSE); break;
        case ',': return TokenMake(TOKEN_COMMA); break;
        case '.': 
            if (ScannerMatch('.')) {
                if (ScannerMatch('.'))
                    return TokenMake(TOKEN_TRI_DOT);

                return TokenMake(TOKEN_BI_DOT);
            }
            return TokenMake(TOKEN_DOT);
        case ':': return TokenMake(TOKEN_COLON); break;
        case ';': return TokenMake(TOKEN_SEMICOLON);
        case '%': return TokenMake(TOKEN_MOD); break;
        case '&':
            if (ScannerMatch('&'))
                return TokenMake(TOKEN_AND);
            return TokenMake(TOKEN_BITWISE_AND);
            break;
        case '|':
            if (ScannerMatch('|'))
                return TokenMake(TOKEN_OR);
            return TokenMake(TOKEN_BITWISE_OR); 
            break;
        case '~': return TokenMake(TOKEN_BITWISE_NOT); break;
        case '^': return TokenMake(TOKEN_BITWISE_XOR); break;
        case '+':
            return TokenMake(ScannerMatch('=') ? TOKEN_ADD_EQUAL : (ScannerMatch('+') ? TOKEN_INCREASE : TOKEN_PLUS));
        case '-':
            return TokenMake(ScannerMatch('=') ? TOKEN_SUB_EQUAL: (ScannerMatch('-') ? TOKEN_DECREASE : TOKEN_MINUS));
        case '*':
            return TokenMake(ScannerMatch('=') ? TOKEN_MULT_EQUAL : (ScannerMatch('*') ? (ScannerMatch('=') ? TOKEN_POW_EQUAL : TOKEN_POW) : TOKEN_STAR));
        case '/':
            if (ScannerMatch('~'))
                return TokenMake(ScannerMatch('=') ? TOKEN_FLOOR_EQUAL : TOKEN_FLOOR);
            if (ScannerMatch('='))
                return TokenMake(TOKEN_DIV_EQUAL);
            return TokenMake(TOKEN_SLASH);
        case '!':
            return TokenMake(ScannerMatch('=') ? TOKEN_NOT_EQUAL : TOKEN_NOT);
        case '=':
            return TokenMake(ScannerMatch('=') ? TOKEN_EQUAL : (ScannerMatch('>') ? TOKEN_FAT_ARROW : TOKEN_ASSIGN));
        case '>':
            return TokenMake(ScannerMatch('=') ? TOKEN_GREATER_EQ : TOKEN_GREATER);
        case '<':
            return TokenMake(ScannerMatch('=') ? TOKEN_SMALLER_EQ : TOKEN_SMALLER);
        case '?':
            return TokenMake(TOKEN_QUESTION);
        case '\'': 
        case '"': 
            return ScannerScanString(currentChar);
    }

    return TokenError("Unexpected character");
}