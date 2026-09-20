#ifndef KATANE_SCANNER_H
#define KATANE_SCANNER_H

#include "Chunk.h"

typedef enum {
    TOKEN_PARENTHESIS_OPEN,
    TOKEN_PARENTHESIS_CLOSE,
    TOKEN_BRACKET_OPEN,
    TOKEN_BRACKET_CLOSE,
    TOKEN_SQUARE_OPEN,
    TOKEN_SQUARE_CLOSE,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_PLUS,
    TOKEN_INCREASE,
    TOKEN_MINUS,
    TOKEN_DECREASE,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_ASSIGN,
    TOKEN_EQUAL,
    TOKEN_FAT_ARROW,
    TOKEN_ADD_EQUAL,
    TOKEN_SUB_EQUAL,
    TOKEN_MULT_EQUAL,
    TOKEN_DIV_EQUAL,
    TOKEN_FLOOR,
    TOKEN_FLOOR_EQUAL,
    TOKEN_POW,
    TOKEN_POW_EQUAL,
    TOKEN_NOT,
    TOKEN_NOT_EQUAL,
    TOKEN_GREATER,
    TOKEN_SMALLER,
    TOKEN_GREATER_EQ,
    TOKEN_SMALLER_EQ,
    TOKEN_MOD,
    TOKEN_BITWISE_OR,
    TOKEN_BITWISE_AND,
    TOKEN_BITWISE_XOR,
    TOKEN_BITWISE_NOT,
    TOKEN_BI_DOT,
    TOKEN_TRI_DOT,
    TOKEN_QUESTION,

    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_INT,
    TOKEN_NUMBER,
    TOKEN_BINARY,
    TOKEN_OCTAL,
    TOKEN_HEX,
    TOKEN_INTERPOLATION,

    TOKEN_AND,
    TOKEN_OR,
    TOKEN_FUNCTION,
    TOKEN_CLASS,
    TOKEN_TYPE,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_MAYBE,
    TOKEN_VAR,
    TOKEN_ENTRY,
    TOKEN_STATIC,
    TOKEN_CONST,
    TOKEN_FINAL,
    TOKEN_PRIVATE,
    TOKEN_HIDDEN,
    TOKEN_GET,
    TOKEN_SET,
    TOKEN_ENUM,
    TOKEN_NULL,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_CONTINUE,
    TOKEN_BREAK,
    TOKEN_CASE,
    TOKEN_DEFAULT,
    TOKEN_SWITCH,
    TOKEN_THIS,
    TOKEN_RETURN,
    TOKEN_PRINT,
    TOKEN_SUPER,
    TOKEN_IS,
    TOKEN_AS,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_FINALLY,
    TOKEN_IN,
    TOKEN_ON,
    TOKEN_THROW,
    TOKEN_RETHROW,
    TOKEN_PARENT,
    TOKEN_USING,
    TOKEN_IMPORT,
    TOKEN_ASSERT,
    TOKEN_FALL,
    TOKEN_CHOICE,
    TOKEN_LATE,
    TOKEN_OPERATOR,

    TOKEN_ERROR,
    TOKEN_EOF
} KTN_TokenType;

typedef struct {
    KTN_TokenType type;
    const char* start;
    int length;
    int line;
} KTN_Token;

/// @brief Represents a snapshot of a scanner at a given point in class.
///        Used primarily to compile classes.
typedef struct {
    const char* start;
    const char* current;
    int line;
    int previousLine;
    char* source;
} KTN_ScannerSnapshot;

void KTN_ScannerInit(const char* source);

char* KTN_ScannerGetSource();
KTN_Token KTN_ScannerScanToken();

void KTN_ScannerPushState();
void KTN_ScannerRestoreTopState();
void KTN_ScannerPopState();

#endif