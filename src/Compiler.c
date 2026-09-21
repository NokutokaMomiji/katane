#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "Compiler.h"
#include "Array.h"
#include "Chunk.h"
#include "Common.h"
#include "Debug.h"
#include "Memory.h"
#include "Scanner.h"
#include "Utilities.h"
#include "Config.h"

typedef struct {
    KTN_Token current;
    KTN_Token previous;
    bool hadError;
    bool panicMode;
    bool lastExpressionWasAssignment;
    KTN_VM* vm;
} Parser; // For parsing the tokenized source code into OP codes.

typedef struct {
    KTN_Token current;
    KTN_Token previous;
    bool hadError;
    bool panicMode;
} ParserSnapshot;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     //*  /
    PREC_POWER,
    PREC_UNARY, // ! -
    PREC_CALL,  // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign); // Essentially a void.

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    KTN_Token name;
    int depth;

    int32_t typeDescriptorIndex; // -1 if there is no type. Otherwise, the index
                                                              // in the constant table.

    bool isCaptured;
    bool isFinal;
    bool isAssigned;
} Local;

typedef struct {
    KTN_Token name;
    int depth;
    KTN_Value value;
} ConstBinding;

typedef struct {
    KTN_ObjString* name;
    KTN_Value value;
} TopLevelConst;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef struct {
    uint8_t slot;            // Local slot index of this parameter
    int32_t descriptorIndex; // Constant pool index of the type descriptor
} ParamAnnotation;

typedef struct Compiler {
    struct Compiler* enclosing;
    KTN_ObjShiki* function;
    KTN_ShikiType type;

    Local locals[UINT16_COUNT];
    ConstBinding constBindings[UINT8_COUNT];
    Upvalue upvalues[UINT16_COUNT];
    ParamAnnotation typeDescriptors[MAX_TYPED_PARAMS];
    TopLevelConst* topLevelConsts;
    KTN_Value tempValues[UINT8_COUNT];

    int topLevelConstCount;
    int topLevelConstCapacity;
    int localCount;
    int constBindingCount;
    int scopeDepth;
    int typedDescriptorCount;
    int tempValueCount;

    int32_t returnDescriptorIndex;

    bool inFinally;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;

    KTN_Token propertyNames[MAX_CLASS_PROPERTIES];
    int propertyCount;

    KTN_Token methodNames[MAX_CLASS_PROPERTIES];
    int methodCount;
} ClassCompiler;

typedef enum { 
    CONTEXT_LOOP,
    CONTEXT_SWITCH
} BreakableContextType;

typedef struct BreakableContext {
    BreakableContextType type;

    struct BreakableContext* enclosing;
    
    int breakJumps[MAX_BREAK_JUMPS];
    int breakJumpCount;
    int continueTarget;
    int fallJumps[MAX_BREAK_JUMPS];
    int fallJumpCount;
    int scopeDepth;
} BreakableContext;

typedef struct TryContext {
    struct TryContext* enclosing;
    int scopeDepth;
} TryContext;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;
BreakableContext* currentBreakable = NULL;
TryContext* currentTry = NULL;
bool collectOnly = false;

static KTN_Chunk* CurrentChunk() { return &current->function->chunk; }

static const char* ShikiTypeName(KTN_ShikiType type) {
    switch (type) {
        case TYPE_FUNCTION:    return "function";
        case TYPE_SCRIPT:      return "script";
        case TYPE_METHOD:      return "method";
        case TYPE_CONSTRUCTOR: return "constructor";
        case TYPE_LAMBDA:      return "lambda";
        case TYPE_GETTER:      return "getter";
        case TYPE_SETTER:      return "setter";
        case TYPE_ENTRY:       return "entry";
    }
    return "unknown";
}

static void DumpCompilerState(void) {
    fprintf(stderr, "\n========== COMPILER PANIC STATE ==========\n");

    // Parser position
    fprintf(stderr, "Line: %d\n", parser.current.line);
    fprintf(stderr, "Current token:  type=%d text=\"%.*s\"\n",
            parser.current.type, parser.current.length, parser.current.start);
    fprintf(stderr, "Previous token: type=%d text=\"%.*s\"\n",
            parser.previous.type, parser.previous.length, parser.previous.start);

    char* sourceLine = KTN_ScannerGetSource();
    if (sourceLine != NULL)
        fprintf(stderr, "Source: %d | %s\n", parser.current.line, sourceLine);

    fprintf(stderr, "Parser: hadError=%s panicMode=%s collectOnly=%s\n",
            (parser.hadError) ? "true" : "false",
            (parser.panicMode) ? "true" : "false",
            (collectOnly) ? "true" : "false");

    // Compiler chain (innermost to outermost)
    fprintf(stderr, "\nCompiler chain (innermost first):\n");
    for (Compiler* currentCompiler = current; currentCompiler != NULL; currentCompiler = currentCompiler->enclosing) {
        const char* name = (
            currentCompiler->function && currentCompiler->function->name
        ) ? currentCompiler->function->name->chars : "<anonymous>";

        fprintf(
            stderr,
            "  [%s] type=%s scopeDepth=%d locals=%d consts=%d "
            "typedDescriptors=%d inFinally=%s\n",
            name,
            ShikiTypeName(currentCompiler->type),
            currentCompiler->scopeDepth,
            currentCompiler->localCount,
            currentCompiler->constBindingCount,
            currentCompiler->typedDescriptorCount,
            currentCompiler->inFinally ? "true" : "false"
        );
    }

    // Chunk being emitted into
    if (current != NULL && current->function != NULL) {
        KTN_Chunk* chunk = &current->function->chunk;

        fprintf(
            stderr,
            "\nCurrent chunk: %d instructions, %d constants\n",
            chunk->count,
            (int)chunk->constants.count
        );
    }

    // Class context
    if (currentClass != NULL) {
        fprintf(
            stderr,
            "\nClass context: properties=%d methods=%d hasSuperclass=%s\n",
            currentClass->propertyCount,
            currentClass->methodCount,
            (currentClass->hasSuperclass) ? "true" : "false"
        );
    } else {
        fprintf(stderr, "\nClass context: none\n");
    }

    // Try context
    if (currentTry != NULL)
        fprintf(stderr, "Try context: scopeDepth=%d\n", currentTry->scopeDepth);
    else
        fprintf(stderr, "Try context: none\n");

    // Breakable context
    if (currentBreakable != NULL) {
        const char* kind = (currentBreakable->type == CONTEXT_LOOP) ? "loop" : "switch";
        
        fprintf(stderr,
            "Breakable context: %s scopeDepth=%d breakJumps=%d "
            "fallJumps=%d continueTarget=%d\n",
            kind,
            currentBreakable->scopeDepth,
            currentBreakable->breakJumpCount,
            currentBreakable->fallJumpCount,
            currentBreakable->continueTarget
        );
    } else {
        fprintf(stderr, "Breakable context: none\n");
    }

    fprintf(stderr, "==========================================\n");
}

void CompilerPanic(const char* format, ...) {
    fprintf(stderr, "\n" COLOR_RED "!!! COMPILER PANIC !!!" COLOR_RESET "\n");

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");

    DumpCompilerState();

    exit(EXIT_FAILURE);
}

static void ErrorAt(KTN_Token* token, const char* msg) {
    if (parser.panicMode)
        return;

    parser.panicMode = true;

    fprintf(stderr, COLOR_RED "SyntaxError" COLOR_RESET ": %s", msg);

    if (token->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else if (token->type != TOKEN_ERROR) {
        fprintf(stderr, " at line %d | \"%.*s\"", token->line, token->length,
                        token->start);
    }

    char* currentLine = KTN_ScannerGetSource();

    if (currentLine != NULL)
        fprintf(stderr, "\n   %d | %s", token->line, currentLine);

    fprintf(stderr, "\n");
    parser.hadError = true;
}

static void Error(const char* msg) { ErrorAt(&parser.previous, msg); }

static void ErrorAtCurrent(const char* msg) { ErrorAt(&parser.current, msg); }

static void CompilerAdvance() {
    parser.previous = parser.current;

    for (;;) {
        parser.current = KTN_ScannerScanToken();

        if (parser.current.type != TOKEN_ERROR)
            break;

        ErrorAtCurrent(parser.current.start);
    }
}

static void CompilerConsume(KTN_TokenType type, const char* msg) {
    if (parser.current.type == type) {
        CompilerAdvance();
        return;
    }

    ErrorAtCurrent(msg);
}

static bool Check(KTN_TokenType type) { return parser.current.type == type; }

static bool Match(KTN_TokenType type) {
    if (!Check(type))
        return false;

    CompilerAdvance();
    return true;
}

static ParserSnapshot ParserSaveState() {
    return (ParserSnapshot){
        .current = parser.current,
        .previous = parser.previous,
        .hadError = parser.hadError,
        .panicMode = parser.panicMode
    };
}

static void ParserRestoreState(ParserSnapshot snapshot) {
    parser.current = snapshot.current;
    parser.previous = snapshot.previous;
    parser.hadError = snapshot.hadError;
    parser.panicMode = snapshot.panicMode;
}

static void CompilerEmitByte(uint8_t byte) {
    KTN_ChunkWrite(parser.vm, CurrentChunk(), byte, parser.previous.line,KTN_ScannerGetSource());
}

static void CompilerEmitBytes(uint8_t firstByte, uint8_t secondByte) {
    CompilerEmitByte(firstByte);
    CompilerEmitByte(secondByte);
}

static void CompilerEmitLong(uint32_t longNumber) {
    KTN_ChunkWriteLong(parser.vm, CurrentChunk(), longNumber, parser.previous.line, KTN_ScannerGetSource());
}

static void CompilerEmitByteLong(uint8_t byte, uint32_t longNumber) {
    CompilerEmitByte(byte);
    CompilerEmitLong(longNumber);
}

static void CompilerEmitShort(uint16_t shortNumber) {
    CompilerEmitByte((shortNumber >> 8) & 0xff);
    CompilerEmitByte(shortNumber & 0xff);
}

static int CompilerEmitJump(uint8_t instruction) {
    CompilerEmitByte(instruction);
    CompilerEmitByte(0xff);
    CompilerEmitByte(0xff);
    return CurrentChunk()->count - 2;
}

static int CompilerEmitDeferredJump() {
    CompilerEmitBytes(OP_DEFER_ACTION, 0x01);
    CompilerEmitShort((uint16_t)currentTry->scopeDepth);
    CompilerEmitByte(0xff);
    CompilerEmitByte(0xff);
    return CurrentChunk()->count - 2;
}

static void CompilerPatchJump(int offset) {
    int Jump = CurrentChunk()->count - offset - 2;

    if (Jump > UINT16_MAX) {
        Error("Too much code to jump over");
    }

    CurrentChunk()->code[offset] = (Jump >> 8) & 0xff;
    CurrentChunk()->code[offset + 1] = Jump & 0xff;
}

static void CompilerEmitLoop(int loopStart) {
    CompilerEmitByte(OP_LOOP);

    int Offset = CurrentChunk()->count - loopStart + 2;
    if (Offset > UINT16_MAX)
        Error("Loop body too large");

    CompilerEmitByte((Offset >> 8) & 0xff);
    CompilerEmitByte(Offset & 0xff);
}

static void CompilerEmitDeferredLoop(int loopStart) {
    CompilerEmitBytes(OP_DEFER_ACTION, 0x02);
    CompilerEmitShort((uint16_t)currentTry->scopeDepth);

    int offset = CurrentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX)
        Error("Loop body too large");

    CompilerEmitByte((offset >> 8) & 0xff);
    CompilerEmitByte(offset & 0xff);
}

static void CompilerEmitReturn() {
    if (current->type == TYPE_CONSTRUCTOR) {
        CompilerEmitBytes(OP_GET_LOCAL, 0);
    } else {
        CompilerEmitByte(OP_NULL);
    }

    if (currentTry != NULL) {
        CompilerEmitBytes(OP_DEFER_ACTION, 0x00);
        CompilerEmitShort((uint16_t)currentTry->scopeDepth);
        return;
    }

    CompilerEmitByte(OP_RETURN);
}

static uint32_t CompilerMakeConstant(KTN_Value value) {
    KTN_ValueArray* constants = &CurrentChunk()->constants;

    for (int i = 0; i < constants->count; i++) {
        if (ValuesEqual(value, constants->values[i])) {
            return i;
        }
    }

    if (constants->count == UINT32_MAX) {
        Error("Too many constants in one chunk");
        return 0;
    }

    return KTN_ChunkAddConstant(parser.vm, CurrentChunk(), value);
}

static void CompilerEmitConstant(KTN_Value value) {
    CompilerEmitByteLong(OP_CONSTANT_LONG, CompilerMakeConstant(value));
}

static void CompilerInit(Compiler* compiler, KTN_ShikiType type, bool isStatic) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->inFinally = false;
    compiler->function = ShikiNew(parser.vm, NULL, type);
    current = compiler;

    // Type annotation support shenanigans.
    compiler->typedDescriptorCount = 0;
    compiler->returnDescriptorIndex = -1;
    compiler->tempValueCount = 0;

    if (type != TYPE_SCRIPT && type != TYPE_LAMBDA) {
        current->function->name = StringCopy(parser.vm, parser.previous.start, parser.previous.length);
    }

    Local* local = &current->locals[current->localCount++];

    local->depth = 0;
    local->isCaptured = false;
    local->typeDescriptorIndex = -1;

    if (!isStatic && (type == TYPE_METHOD || type == TYPE_CONSTRUCTOR || type == TYPE_GETTER || type == TYPE_SETTER)) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static KTN_ObjShiki* CompilerEnd() {
    CompilerEmitReturn();

    KTN_ObjShiki* function = current->function;

    if (!parser.hadError && parser.vm->shouldPrintBytecode)
        DisassembleChunk(CurrentChunk(), (function->name != NULL) ? function->name->chars : "<script>");

#ifdef DEBUG_PRINT_CODE
        DisassembleChunk(CurrentChunk(), function->name != NULL ? function->name->chars : "<script>");
#endif

    current = current->enclosing;
    return function;
}

static void CompilerBeginScope() { current->scopeDepth++; }

static void CompilerEndScope() {
    current->scopeDepth--;

    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured)
            CompilerEmitByte(OP_CLOSE_UPVALUE);
        else
            CompilerEmitByte(OP_POP);
        current->localCount--;
    }
}

static void CompilerExpression();
static void CompilerStatement();
static void CompilerDeclaration();
static void VariableDeclaration();
static void VariableSet(KTN_Token name, bool canAssign);
static void VariableSetPrevious(bool canAssign);
static ParseRule* CompilerGetRule(KTN_TokenType type);
static void CompilerParsePrecedence(Precedence precedence);
static void NamedVariable(KTN_Token name, bool canAssign);
static KTN_Token SyntheticToken(const char* text);
static void CompilerVariable(bool canAssign);
static void RegisterTopLevelConst(KTN_Token name, KTN_Value value);

static char* Substring(const char* string, int length) {
    KTN_VM* vm = parser.vm;
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, string, length);
    heapChars[length] = '\0';
    return heapChars;
}

static uint32_t IdentifierConstant(KTN_Token* name) {
    return CompilerMakeConstant(OBJECT_VALUE(StringCopy(parser.vm, name->start, name->length)));
}

static bool IdentifiersEqual(KTN_Token* a, KTN_Token* b) {
    if (a->length != b->length)
        return false;

    return memcmp(a->start, b->start, a->length) == 0;
}

static bool StringsEqual(KTN_ObjString* a, KTN_ObjString* b) {
    if (a->charLength != b->charLength)
        return false;

    return memcmp(a->chars, b->chars, a->charLength);
}

static bool ResolveLocalConst(KTN_Token* name, KTN_Value* value) {
    for (int i = current->constBindingCount - 1; i >= 0; i--) {
        ConstBinding* binding = &current->constBindings[i];

        if (binding->depth <= current->scopeDepth && IdentifiersEqual(&binding->name, name)) {
            *value = binding->value;
            return true;
        }
    }

    return false;
}

static bool ResolveTopLevelConst(KTN_VM* vm, KTN_Token* name,
                                                                  KTN_Value* value) {
    KTN_ObjString* key = StringCopy(vm, name->start, name->length);
    for (int i = 0; i < current->topLevelConstCount; i++) {
        if (current->topLevelConsts[i].name == key || StringsEqual(current->topLevelConsts[i].name, key)) {
            *value = current->topLevelConsts[i].value;
            return true;
        }
    }

    return false;
}

void AddConstBinding(KTN_Token name, int depth, KTN_Value value) {
    if (current->constBindingCount + 1 >= UINT8_COUNT) {
        Error("Maximum number of local constants reached");
        return;
    }

    ConstBinding* binding = &current->constBindings[current->constBindingCount++];

    binding->name = name;
    binding->depth = depth;
    binding->value = value;
}

static bool EvaluateCompiledExpression(KTN_Value* value) {
    switch (parser.current.type) {
        case TOKEN_INT: {
            CompilerAdvance();
            *value = INT_VALUE((int32_t)strtol(parser.previous.start, NULL, 10));
            return true;
        }

        case TOKEN_NUMBER: {
            CompilerAdvance();
            *value = DOUBLE_VALUE(strtod(parser.previous.start, NULL));
            return true;
        }

        case TOKEN_STRING: {
            CompilerAdvance();
            const char* start = parser.previous.start + 1;
            int length = parser.previous.length - 2;
            
            int outputLength = 0;
            char* decoded = ProcessEscapes(start, length, &outputLength);

            *value = OBJECT_VALUE(StringCopy(parser.vm, decoded, outputLength));

            return true;
        }

        case TOKEN_TRUE: {
            CompilerAdvance();
            *value = TRUE_VALUE;
            return true;
        }

        case TOKEN_FALSE: {
            CompilerAdvance();
            *value = FALSE_VALUE;
            return true;
        }

        case TOKEN_NULL: {
            CompilerAdvance();
            *value = NULL_VALUE;
            return true;
        }

        case TOKEN_MINUS:
        case TOKEN_NOT: {
            KTN_TokenType operator = parser.current.type;
            CompilerAdvance();
            KTN_Value inner;

            if (!EvaluateCompiledExpression(&inner))
                return false;

            if (operator == TOKEN_MINUS) {
                if (IS_INT(inner)) {
                    *value = INT_VALUE(-AS_INT(inner));
                    return true;
                }

                if (IS_DOUBLE(inner)) {
                    *value = DOUBLE_VALUE(-AS_DOUBLE(inner));
                    return true;
                }

                return false;
            }

            if (operator == TOKEN_NOT && IS_BOOL(inner)) {
                *value = BOOL_VALUE(!AS_BOOL(inner));
                return true;
            }

            return false;
        }

        case TOKEN_IDENTIFIER: {
            KTN_Token name = parser.current;

            if (ResolveLocalConst(&name, value) || ResolveTopLevelConst(parser.vm, &name, value)) {
                CompilerAdvance();
                return true;
            }

            return false;
        }

        case TOKEN_SQUARE_OPEN: {
            CompilerAdvance();

            KTN_ObjArray* array = ArrayNew(parser.vm);
            Push(parser.vm, OBJECT_VALUE(array));

            if (Match(TOKEN_SQUARE_CLOSE)) {
                *value = OBJECT_VALUE(array);
                Pop(parser.vm);
                return true;
            }

            uint16_t numOfItems = 0;

            do {
                if (!Check(TOKEN_SQUARE_CLOSE)) {
                    if (numOfItems >= UINT16_MAX)
                        Error("Too many items to store in array");

                    KTN_Value arrayValue;

                    if (!EvaluateCompiledExpression(&arrayValue)) {
                        Pop(parser.vm);
                        return false;
                    }
                
                    KTN_ArrayAdd(parser.vm, array, arrayValue);

                    numOfItems++;
                }
            } while (Match(TOKEN_COMMA));

            CompilerConsume(TOKEN_SQUARE_CLOSE, "Expected ']' at the end of the array");
            Pop(parser.vm);
            return true;
        }

        case TOKEN_BRACKET_OPEN: {
            CompilerAdvance();

            KTN_ObjMap* map = MapNew(parser.vm);
            Push(parser.vm, OBJECT_VALUE(map));

            if (Match(TOKEN_BRACKET_CLOSE)) {
                *value = OBJECT_VALUE(map);
                Pop(parser.vm);
                return true;
            }

            KTN_HashMap* hashMap = &map->map;
            uint16_t numOfPairs = 0;

            do {
                if (!Check(TOKEN_BRACKET_CLOSE)) {
                    if (numOfPairs >= UINT16_MAX)
                        Error("Too many items to store in map");

                    KTN_Value mapKey;
                    KTN_Value mapValue;

                    if (!EvaluateCompiledExpression(&mapKey)) {
                        Pop(parser.vm);
                        return false;
                    }
                    CompilerConsume(TOKEN_COLON, "Expected ':' for value for pair");
                    
                    if (!EvaluateCompiledExpression(&mapValue)) {
                        Pop(parser.vm);
                        return false;
                    }
                    
                    KTN_HashMapSet(parser.vm, hashMap, mapKey, mapValue);
                    numOfPairs++;
                }
            } while (Match(TOKEN_COMMA));

            CompilerConsume(TOKEN_BRACKET_CLOSE, "Expected '}' at the end of the map");
            Pop(parser.vm);
            return true;
        }

        default:
            return false;
    }

    return false;
}

static int ResolveLocal(Compiler* compiler, KTN_Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (IdentifiersEqual(name, &local->name)) {
            if (local->depth == -1)
                Error("Cannot read local mochi in its own initializer");
            return i;
        }
    }

    return -1;
}

static int AddUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal)
            return i;
    }

    if (upvalueCount == UINT16_COUNT) {
        Error("Too many closure variables in shiki");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int ResolveUpvalue(Compiler* compiler, KTN_Token* name, Local* *local) {
    if (compiler->enclosing == NULL)
        return -1;

    int Local = ResolveLocal(compiler->enclosing, name);
    if (Local != -1) {
        *local = &compiler->enclosing->locals[Local];

        compiler->enclosing->locals[Local].isCaptured = true;
        return AddUpvalue(compiler, (uint8_t)Local, true);
    }

    int Upvalue = ResolveUpvalue(compiler->enclosing, name, local);
    if (Upvalue != -1)
        return AddUpvalue(compiler, (uint8_t)Upvalue, false);

    return -1;
}

static void AddLocal(KTN_Token name) {
    if (current->localCount == UINT16_COUNT) {
        Error("Too many local variables in shiki");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
    local->typeDescriptorIndex = -1;
}

static void DeclareVariable() {
    if (current->scopeDepth == 0)
        return;

    KTN_Token* name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];

        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (IdentifiersEqual(name, &local->name)) {
            Error("Already a mochi with this name in this scope");
        }
    }

    AddLocal(*name);
}

static uint32_t ParseVariable(const char* errorMessage) {
    CompilerConsume(TOKEN_IDENTIFIER, errorMessage);

    DeclareVariable();
    if (current->scopeDepth > 0)
        return 0;

    return IdentifierConstant(&parser.previous);
}

static void MarkInitialized() {
    if (current->scopeDepth == 0)
        return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void DefineVariable(uint32_t global) {
    if (current->scopeDepth > 0) {
        MarkInitialized();
        return;
    }

    CompilerEmitByteLong(OP_DEFINE_GLOBAL, global);
}

static uint8_t ArgumentList() {
    uint8_t argumentCount = 0;
    if (!Check(TOKEN_PARENTHESIS_CLOSE)) {
        do {
            CompilerExpression();
            if (argumentCount == 255)
                Error("Cannot have more than 255 arguments in a shiki call");
            argumentCount++;
        } while (Match(TOKEN_COMMA));
    }

    CompilerConsume(TOKEN_PARENTHESIS_CLOSE,
                                    "Expected ')' after shiki call parameters");
    return argumentCount;
}

static void CompilerAnd(bool canAssign) {
    int endJump = CompilerEmitJump(OP_JUMP_IF_FALSE);

    CompilerEmitByte(OP_POP);
    CompilerParsePrecedence(PREC_AND);

    CompilerPatchJump(endJump);
}

static void CompilerOr(bool canAssign) {
    int elseJump = CompilerEmitJump(OP_JUMP_IF_FALSE);
    int endJump = CompilerEmitJump(OP_JUMP);

    CompilerPatchJump(elseJump);
    CompilerEmitByte(OP_POP);

    CompilerParsePrecedence(PREC_OR);
    CompilerPatchJump(endJump);
}

static void CompilerBinary(bool canAssign) {
    KTN_TokenType operatorType = parser.previous.type;
    ParseRule* Rule = CompilerGetRule(operatorType);
    CompilerParsePrecedence((Precedence)(Rule->precedence + 1));

    switch (operatorType) {
    case TOKEN_PLUS:
        CompilerEmitByte(OP_ADD);
        break;
    case TOKEN_MINUS:
        CompilerEmitByte(OP_SUBTRACT);
        break;
    case TOKEN_STAR:
        CompilerEmitByte(OP_MULTIPLY);
        break;
    case TOKEN_SLASH:
        CompilerEmitByte(OP_DIVIDE);
        break;
    case TOKEN_ADD_EQUAL:
        CompilerEmitByte(OP_ADD);
        break;
    case TOKEN_SUB_EQUAL:
        CompilerEmitByte(OP_SUBTRACT);
        break;
    case TOKEN_EQUAL:
        CompilerEmitByte(OP_EQUAL);
        break;
    case TOKEN_NOT_EQUAL:
        CompilerEmitByte(OP_NOT_EQUAL);
        break;
    case TOKEN_GREATER:
        CompilerEmitByte(OP_GREATER);
        break;
    case TOKEN_GREATER_EQ:
        CompilerEmitByte(OP_GREATER_EQ);
        break;
    case TOKEN_SMALLER:
        CompilerEmitByte(OP_SMALLER);
        break;
    case TOKEN_SMALLER_EQ:
        CompilerEmitByte(OP_SMALLER_EQ);
        break;
    case TOKEN_MOD:
        CompilerEmitByte(OP_MOD);
        break;
    case TOKEN_FLOOR:
        CompilerEmitByte(OP_FLOOR_DIV);
        break;
    case TOKEN_POW:
        CompilerEmitByte(OP_POW);
        break;
    case TOKEN_FLOOR_EQUAL:
        CompilerEmitByte(OP_FLOOR_DIV);
        break;
    case TOKEN_POW_EQUAL:
        CompilerEmitByte(OP_POW);
        break;
    case TOKEN_BITWISE_AND:
        CompilerEmitByte(OP_BITWISE_AND);
        break;
    case TOKEN_BITWISE_OR:
        CompilerEmitByte(OP_BITWISE_OR);
        break;
    case TOKEN_BITWISE_XOR:
        CompilerEmitByte(OP_BITWISE_XOR);
        break;
    case TOKEN_BITWISE_NOT:
        CompilerEmitByte(OP_BITWISE_NOT);
        break;
    case TOKEN_IS:
        CompilerEmitByte(OP_INSTANCEOF);
        break;
    default:
        return;
    }
}

static void CompilerCall(bool canAssign) {
    uint8_t argumentCount = ArgumentList();
    CompilerEmitBytes(OP_CALL, argumentCount);
}

static void CompilerEmitOperand(int argument, bool isGlobal) {
    if (isGlobal)
        CompilerEmitLong((uint32_t)argument);
    else
        CompilerEmitByte((uint8_t)argument);
}

static void EmitCheckedSet(uint8_t setOp, int argument, KTN_Token* name) {
    if (setOp == OP_SET_LOCAL) {
        Local* local = &current->locals[argument];

        if (local->typeDescriptorIndex >= 0) {
            CompilerEmitByte(OP_SET_LOCAL_TYPED);
            CompilerEmitByte((uint8_t)argument);
            CompilerEmitLong((uint32_t)local->typeDescriptorIndex);
        } else {
            CompilerEmitBytes(OP_SET_LOCAL, (uint8_t)argument);
        }
        return;
    }

    if (setOp == OP_SET_GLOBAL) {
        KTN_ObjString* nameStr = StringCopy(parser.vm, name->start, name->length);
        KTN_Value typeDescriptor;

        if (TableGet(&parser.vm->globalTypes, nameStr, &typeDescriptor) && IS_TYPE_DESCRIPTOR(typeDescriptor)) {
            int32_t descriptorIndex = (int32_t)CompilerMakeConstant(typeDescriptor);
            CompilerEmitByteLong(OP_SET_GLOBAL_TYPED, (uint32_t)argument);
            CompilerEmitLong((uint32_t)descriptorIndex);
        } else {
            CompilerEmitByteLong(OP_SET_GLOBAL, (uint32_t)argument);
        }
        return;
    }

    if (setOp == OP_SET_UPVALUE) {
        Compiler* enclosing = current->enclosing;

        while (enclosing != NULL) {
            int slot = ResolveLocal(enclosing, name);

            if (slot != -1) {
                if (enclosing->locals[slot].typeDescriptorIndex >= 0)
                    CompilerEmitByteLong(
                            OP_CHECK_TYPE,
                            (uint32_t)enclosing->locals[slot].typeDescriptorIndex);
                break;
            }

            enclosing = enclosing->enclosing;
        }

        CompilerEmitBytes(setOp, (uint8_t)argument);
        return;
    }

    if (setOp == OP_SET_PROPERTY) {
        // We cannot distinguish an untyped property from a typed property
        //  at compile time.
        CompilerEmitByteLong(OP_SET_PROPERTY, argument);
        parser.lastExpressionWasAssignment = true;
    }
}

static void ResolveExtraAssignments(int getOp, int setOp, int argument,
                                                                        bool isGlobal, KTN_Token name) {
    KTN_Token currentToken = parser.current;

    bool isPropertyAssignment =
            (setOp == OP_SET_PROPERTY) &&
            (Check(TOKEN_INCREASE) || Check(TOKEN_DECREASE) ||
              Check(TOKEN_ADD_EQUAL) || Check(TOKEN_SUB_EQUAL) ||
              Check(TOKEN_MULT_EQUAL) || Check(TOKEN_DIV_EQUAL) ||
              Check(TOKEN_FLOOR_EQUAL) || Check(TOKEN_POW_EQUAL));

    if (isPropertyAssignment)
        CompilerEmitByte(OP_DUPLICATE);

    CompilerEmitByte(getOp);

    if (argument != -1)
        CompilerEmitOperand(argument, isGlobal || (getOp == OP_GET_PROPERTY));

    if (Match(TOKEN_INCREASE)) {
        if (setOp == OP_SET_PROPERTY) {
            CompilerEmitConstant(INT_VALUE(1));
            CompilerEmitByte(OP_ADD);
            CompilerEmitByteLong(OP_SET_PROPERTY, (uint32_t)argument);
        } else {
            CompilerEmitByte(OP_POSTINCREASE);
            if (argument != -1)
                EmitCheckedSet(setOp, argument, &name);
        }
        CompilerEmitByte(OP_POP);
    } else if (Match(TOKEN_DECREASE)) {
        if (setOp == OP_SET_PROPERTY) {
            CompilerEmitConstant(INT_VALUE(1));
            CompilerEmitByte(OP_SUBTRACT);
            CompilerEmitByteLong(OP_SET_PROPERTY, (uint32_t)argument);
        } else {
            CompilerEmitByte(OP_POSTDECREASE);
            if (argument != -1)
                EmitCheckedSet(setOp, argument, &name);
        }
        CompilerEmitByte(OP_POP);
    } else if (Match(TOKEN_ADD_EQUAL) || Match(TOKEN_SUB_EQUAL) ||
                          Match(TOKEN_MULT_EQUAL) || Match(TOKEN_DIV_EQUAL) ||
                          Match(TOKEN_FLOOR_EQUAL) || Match(TOKEN_POW_EQUAL)) {
        CompilerExpression();

        switch (currentToken.type) {
        case TOKEN_ADD_EQUAL:
            CompilerEmitByte(OP_ADD);
            break;
        case TOKEN_SUB_EQUAL:
            CompilerEmitByte(OP_SUBTRACT);
            break;
        case TOKEN_MULT_EQUAL:
            CompilerEmitByte(OP_MULTIPLY);
            break;
        case TOKEN_DIV_EQUAL:
            CompilerEmitByte(OP_DIVIDE);
            break;
        case TOKEN_FLOOR_EQUAL:
            CompilerEmitByte(OP_FLOOR_DIV);
            break;
        case TOKEN_POW_EQUAL:
            CompilerEmitByte(OP_POW);
            break;
        default:
            break;
        }

        if (argument != -1)
            EmitCheckedSet(setOp, argument, &name);

        parser.lastExpressionWasAssignment = true;
    }
}

static void CompilerDot(bool canAssign) {
    CompilerConsume(TOKEN_IDENTIFIER, "Expected property name after '.'");
    KTN_Token nameToken = parser.previous;
    uint32_t name = IdentifierConstant(&parser.previous);

    if (canAssign && Match(TOKEN_ASSIGN)) {
        CompilerExpression();
        CompilerEmitByteLong(OP_SET_PROPERTY, name);
        parser.lastExpressionWasAssignment = true;
    } else if (Match(TOKEN_PARENTHESIS_OPEN)) {
        uint8_t argumentCount = ArgumentList();
        CompilerEmitByteLong(OP_INVOKE, name);
        CompilerEmitByte(argumentCount);
    } else {
        ResolveExtraAssignments(OP_GET_PROPERTY, OP_SET_PROPERTY, name, true,
                                                        nameToken);
    }
}

static void CompilerLiteral(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_TRUE:
            CompilerEmitByte(OP_TRUE);
            break;
        case TOKEN_FALSE:
            CompilerEmitByte(OP_FALSE);
            break;
        case TOKEN_NULL:
            CompilerEmitByte(OP_NULL);
            break;
        case TOKEN_MAYBE:
            CompilerEmitByte(OP_MAYBE);
            break;
        default:
            return; // Unreachable.
    }
}

static void CompilerExpression() { CompilerParsePrecedence(PREC_ASSIGNMENT); }

static void CompilerBlock() {
    while (!Check(TOKEN_BRACKET_CLOSE) && !Check(TOKEN_EOF)) {
        CompilerDeclaration();
    }

    CompilerConsume(TOKEN_BRACKET_CLOSE, "Expected '}' after block");
}

static KTN_ObjTypeDescriptor* TypeAnnotationExpression() {
    KTN_VM* vm = parser.vm;

    if (!Check(TOKEN_IDENTIFIER)) {
        ErrorAtCurrent("Expected type name in annotation");
        return NULL;
    }

    CompilerAdvance();

    KTN_ObjString* name = StringCopy(vm, parser.previous.start, parser.previous.length);
    Push(vm, OBJECT_VALUE(name));
    KTN_ObjTypeDescriptor* base = KTN_TypeDescriptorNamed(vm, name);
    Pop(vm);

    if (Match(TOKEN_SMALLER)) {
        KTN_ObjTypeDescriptor* parameters[MAX_GENERICS];
        int count = 0;

        do {
            if (count >= MAX_GENERICS) {
                Error("Maximum number of type parameters reached");
                break;
            }

            KTN_ObjTypeDescriptor* parameter = TypeAnnotationExpression();

            if (parameter == NULL)
                return NULL;

            parameters[count++] = parameter;
        } while (Match(TOKEN_COMMA));

        CompilerConsume(TOKEN_GREATER, "Expected '>' after type parameters");
        base = KTN_TypeDescriptorParam(vm, base, parameters, count);
    }

    return base;
}
static int32_t ParseTypeAnnotation() {
    KTN_VM* vm = parser.vm;
    KTN_ObjTypeDescriptor* members[MAX_TYPE_UNION];
    int count = 0;

    do {
        if (count >= 8) {
            Error("Maximum number of type union members reached");
            break;
        }

        KTN_ObjTypeDescriptor* member = TypeAnnotationExpression();
        if (member == NULL)
            return -1;

        if (Match(TOKEN_QUESTION)) {
            member = KTN_TypeDescriptorNullable(vm, member);
        }

        members[count++] = member;
    } while (Match(TOKEN_BITWISE_OR));

    KTN_ObjTypeDescriptor* descriptor = (count == 1) ? members[0] : KTN_TypeDescriptorUnion(vm, members, count);

    if (descriptor == NULL)
        return -1;

    for (int i = 0; i < current->typedDescriptorCount; i++) {
        int32_t descriptorIndex = current->typeDescriptors[i].descriptorIndex;

        if (descriptorIndex < 0) {
            // TODO: THIS SHOULD NEVER HAPPEN. Add error message heree.
            CompilerPanic("Cached descriptor index is negative. Invalid descriptors should not be cached.");
            continue;
        }

        KTN_Value value = CurrentChunk()->constants.values[descriptorIndex];
        
        if (!IS_TYPE_DESCRIPTOR(value)) {
            // TODO: THIS SHOULD NEVER HAPPEN. Add error message here.
            CompilerPanic("Cached descriptor index points to a non-descriptor value.");
            continue;
        }

        KTN_ObjTypeDescriptor* storedDescriptor = AS_TYPE_DESCRIPTOR(value);

        if (descriptor == storedDescriptor) {
            return descriptorIndex;
        }
    }

    int32_t newDescriptorIndex = (int32_t)CompilerMakeConstant(OBJECT_VALUE(descriptor));

    if (current->typedDescriptorCount < MAX_TYPED_PARAMS) {
        current->typeDescriptors[current->typedDescriptorCount++].descriptorIndex = newDescriptorIndex;
    }

    return newDescriptorIndex;
}

// After all parameters have been parsed, emit OP_CHECK_PARAMS if any were
// annotated. Builds the param descriptor: an KTN_ObjArray of alternating
// [slot, typeDescriptorIdx, slot, typeDescriptorIdx, ...] encoded as
// INT_VALUEs.
static void CompilerEmitCheckParams() {
    int count = 0;

    for (int i = 1; i < current->localCount; i++) {
        if (current->locals[i].typeDescriptorIndex >= 0)
            count++;
    }

    if (count == 0)
        return;

    CompilerEmitBytes(OP_CHECK_PARAMS, (uint8_t)count);

    for (int i = 0; i < current->localCount; i++) {
        if (current->locals[i].typeDescriptorIndex < 0)
            continue;

        CompilerEmitByte((uint8_t)i);
        CompilerEmitLong((uint32_t)current->locals[i].typeDescriptorIndex);
    }
}

static void CompilerFunction(KTN_ShikiType type, bool isStatic) {
    // Initialize the function compiler as well as the parameter specs.
    Compiler* compiler = (Compiler*)malloc(sizeof(Compiler));
    KTN_SignatureParameterSpec parameterSpecs[UINT8_MAX];
    int parameterSpecCount = 0;

    if (compiler == NULL) {
        Error("[ERROR]: Compiler could not be allocated. Out of memory.");
        return;
    }

    // We'll be using a StringBuilder to create the canonical signature of the
    // function.
    StringBuilder sb;
    SBInit(&sb);

    // Just to handle the name of the function if there is one. Script indicates the top-level code.
    // Lambda functions are, by definition, anonymous.
    if (type != TYPE_SCRIPT && type != TYPE_LAMBDA)
        SBAppend(&sb, parser.previous.start, parser.previous.length);
    else
        SBAppendCStr(&sb, "<lambda>");

    CompilerInit(compiler, type, isStatic);
    CompilerBeginScope();

    // Getters (get property => ...;) do not take parameters, thus we do not allow for parenthesis.
    // Otherwise, parentheses for parameters is expected.
    if (type != TYPE_GETTER) {
        SBAppendCStr(&sb, "(");
        CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after shiki name");
    }

    bool firstParameter = true;
    bool inNamedBlock = false;

    // Consume all parameters. As mentioned beforehand, getters do not take parameters.
    if (!Check(TOKEN_PARENTHESIS_CLOSE) && type != TYPE_GETTER) {
        do {
            current->function->arity++;
            if (current->function->arity > UINT8_MAX) {
                ErrorAtCurrent("Cannot have more that 255 parameters for a shiki");
            }

            if (!firstParameter)
                SBAppendCStr(&sb, ", ");
            else
                firstParameter = false;

            // Check if we have entered named parameters section.
            if (Check(TOKEN_BRACKET_OPEN)) {
                if (type == TYPE_SETTER) {
                    ErrorAtCurrent("Setters cannot have positional parameter");
                }

                if (inNamedBlock) {
                    ErrorAtCurrent("Unexpected '{'");
                }

                SBAppendCStr(&sb, "{");
                CompilerAdvance();
                inNamedBlock = true;
            }

            uint32_t Constant = ParseVariable("Expected parameter name");
            int parameterIndex = parameterSpecCount++;

            parameterSpecs[parameterIndex].start = parser.previous.start;
            parameterSpecs[parameterIndex].length = parser.previous.length;
            parameterSpecs[parameterIndex].type = NULL;
            parameterSpecs[parameterIndex].hasDefaultValue = false;
            parameterSpecs[parameterIndex].isNamed = inNamedBlock;
            parameterSpecs[parameterIndex].defaultValue = EMPTY_VALUE;

            SBAppend(&sb, parser.previous.start, parser.previous.length);

            if (Match(TOKEN_COLON)) {
                int32_t descriptorIndex = ParseTypeAnnotation();

                if (descriptorIndex >= 0) {
                    char typeBuffer[512];
                    KTN_TypeDescriptorFormat(
                        AS_TYPE_DESCRIPTOR(
                            CurrentChunk()->constants.values[descriptorIndex]
                        ),
                        typeBuffer,
                        sizeof(typeBuffer)
                    );
                    SBAppendCStr(&sb, ": ");
                    SBAppendCStr(&sb, typeBuffer);
                    current->locals[current->localCount - 1].typeDescriptorIndex = descriptorIndex;
                    parameterSpecs[parameterIndex].type = AS_TYPE_DESCRIPTOR(CurrentChunk()->constants.values[descriptorIndex]);
                } else {
                    SBAppendCStr(&sb, "?");
                }
            }

            if (Match(TOKEN_ASSIGN)) {
                if (type == TYPE_SETTER) {
                    ErrorAtCurrent("Setter parameter cannot have default value");
                }

                parameterSpecs[parameterIndex].hasDefaultValue = true;
                SBAppendCStr(&sb, " = ?");
                
                KTN_Value defaultValue;

                if (!EvaluateCompiledExpression(&defaultValue)) {
                    ErrorAtCurrent("Shiki parameters can only have const default values");
                } else {
                    if (compiler->tempValueCount > UINT8_MAX) {
                        CompilerPanic("Shiki parameters are already capped to 255 values. If we got here, there's a bug.");
                    }

                    compiler->tempValues[compiler->tempValueCount] = defaultValue;
                    parameterSpecs[parameterIndex].defaultValue = defaultValue;
                }
            }

            DefineVariable(Constant);

            if (type == TYPE_GETTER) {
                break;
            }
        } while (Match(TOKEN_COMMA));
    }

    if (type != TYPE_GETTER && type != TYPE_SETTER) {
        if (inNamedBlock) {
            CompilerConsume(TOKEN_BRACKET_CLOSE, "Expected '}' after positional parameters");
            SBAppendCStr(&sb, "}");
            inNamedBlock = false;
        }
        CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after shiki parameters");
        SBAppendCStr(&sb, ")");
    } else if (type == TYPE_SETTER) {
        if (current->function->arity < 1) {
            ErrorAtCurrent("Setter requires one value parameter");
        }
        CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after setter value parameter");
        SBAppendCStr(&sb, ")");
    }

    if (Match(TOKEN_COLON)) {
        int32_t descriptorIndex = ParseTypeAnnotation();

        if (descriptorIndex >= 0) {
            char typeBuffer[512];
            KTN_TypeDescriptorFormat(
                AS_TYPE_DESCRIPTOR(CurrentChunk()->constants.values[descriptorIndex]),
                typeBuffer, sizeof(typeBuffer)
            );
            SBAppendCStr(&sb, ": ");
            SBAppendCStr(&sb, typeBuffer);
            current->returnDescriptorIndex = descriptorIndex;
            current->function->returnTypeDescriptor = (int)descriptorIndex;
        }
    }

    KTN_ObjString* displaySignature = StringCopy(parser.vm, (sb.buffer != NULL) ? sb.buffer : "", sb.length);
    KTN_ObjTypeDescriptor* returnType = NULL;

    Push(parser.vm, OBJECT_VALUE(displaySignature));

    if (current->function->returnTypeDescriptor >= 0) {
        returnType = AS_TYPE_DESCRIPTOR(CurrentChunk()->constants.values[current->function->returnTypeDescriptor]);
    }

    current->function->signature = SignatureNew(
        parser.vm,
        displaySignature,
        current->function->name,
        returnType,
        parameterSpecs,
        parameterSpecCount,
        type
    );

    Pop(parser.vm);
    SBFree(&sb);

    // Emit OP_CHECK_PARAMS as the very first instruction if any params were
    // annotated.
    CompilerEmitCheckParams();

    if (Match(TOKEN_FAT_ARROW)) {
        CompilerExpression();
        CompilerEmitByte(OP_RETURN);
        if (type != TYPE_LAMBDA)
            CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after expression");
    } else {
        CompilerConsume(
            TOKEN_BRACKET_OPEN, 
            (type == TYPE_GETTER) ? "Expected '{' before getter body" : "Expected '{' before shiki body"
        );
        CompilerBlock();
    }

    KTN_ObjShiki* function = CompilerEnd();
    CompilerEmitByteLong(OP_CLOSURE, CompilerMakeConstant(OBJECT_VALUE(function)));

    for (int i = 0; i < function->upvalueCount; i++) {
        CompilerEmitByte((compiler->upvalues[i].isLocal) ? 1 : 0);
        CompilerEmitByte(compiler->upvalues[i].index);
    }

    free(compiler);
}

static void ConstDeclaration() {
    CompilerConsume(TOKEN_VAR, "Expected \"mochi\" after \"const\"");
    CompilerConsume(TOKEN_IDENTIFIER, "Expected mochi name after \"mochi\"");
    KTN_Token name = parser.previous;

    KTN_Value dummy;
    if (ResolveLocalConst(&name, &dummy) || ResolveTopLevelConst(parser.vm, &name, &dummy)) {
        Error("Const mochi has already been defined and cannot be reassigned");
    }

    CompilerConsume(TOKEN_ASSIGN, "Expected '=' after const mochi name");

    KTN_Value constValue;

    // Convert expression into a constant KTN_Value if it is a valid constant.
    if (!EvaluateCompiledExpression(&constValue)) {
        Error("Const value must be a compile-time constant");
        return;
    }

    if (current->scopeDepth > 0) {
        // We declare the variable so that we can avoid shadowing.
        // But we decrease the count afterwards to free up the slot.
        DeclareVariable();
        current->localCount--;
        AddConstBinding(name, current->scopeDepth, constValue);
    } else {
        // Global constant. We create the identifier and the constant.
        uint32_t nameIndex = IdentifierConstant(&name);
        uint32_t valueIndex = CompilerMakeConstant(constValue);

        // Emit code for grabbing the constant, defining the global variable, then marking it as const.
        CompilerEmitByteLong(OP_CONSTANT_LONG, valueIndex);
        CompilerEmitByteLong(OP_DEFINE_GLOBAL, nameIndex);
        CompilerEmitByteLong(OP_MARK_GLOBAL_FLAGS, nameIndex);
        CompilerEmitByte(KTN_TABLE_ENTRY_CONST);

        // We register the constant value so that we can optimize later.
        RegisterTopLevelConst(name, constValue);
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after const declaration");
}

static void FinalDeclaration() {
    CompilerConsume(TOKEN_VAR, "Expected \"mochi\" after \"final\"");
    CompilerConsume(TOKEN_IDENTIFIER, "Expected mochi name for final");
    KTN_Token nameToken = parser.previous;

    DeclareVariable();

    bool hasInitializer = Match(TOKEN_ASSIGN);
    if (hasInitializer) {
        CompilerExpression();
    } else {
        CompilerEmitByte(OP_NULL);
    }

    uint32_t name = IdentifierConstant(&nameToken);

    DefineVariable(name);

    if (current->scopeDepth > 0) {
        Local* local = &current->locals[current->localCount - 1];
        local->isFinal = true;
        local->isAssigned = true;
    } else {
        CompilerEmitByteLong(OP_MARK_GLOBAL_FLAGS, name);
        CompilerEmitByte(KTN_TABLE_ENTRY_FINAL);
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after final declaration");
}

static void SkipBalanced(KTN_TokenType open, KTN_TokenType close) {
    int depth = 1;
    while (depth > 0 && !Check(TOKEN_EOF)) {
        if (Check(open)) {
            depth++;
            CompilerAdvance();
            continue;
        }

        if (Check(close)) {
            depth--;
            CompilerAdvance();

            if (depth == 0)
                break;

            continue;
        }

        CompilerAdvance();
    }
}

static void SkipTypeExpression() {
    int depth = 0;

    for (;;) {
        if (Check(TOKEN_SMALLER)) {
            depth++;
            CompilerAdvance();
            continue;
        }

        if (Check(TOKEN_GREATER)) {
            if (depth > 0) {
                depth--;
                CompilerAdvance();
                continue;
            }

            CompilerAdvance();
            break;
        }

        if (Check(TOKEN_BITWISE_OR) || Check(TOKEN_QUESTION) ||
                Check(TOKEN_COMMA)) {
            if (depth > 0) {
                CompilerAdvance();
                continue;
            }

            CompilerAdvance();
            break;
        }

        if (Check(TOKEN_IDENTIFIER)) {
            CompilerAdvance();
            continue;
        }

        break;
    }
}

// Skip a field after "mochi" has been consumed and the name has been
// registered.
static void SkipFieldTail() {
    if (Match(TOKEN_COLON))
        SkipTypeExpression();

    if (Match(TOKEN_ASSIGN)) {
        int depth = 0;

        while (!Check(TOKEN_EOF)) {
            if (Check(TOKEN_PARENTHESIS_OPEN) || Check(TOKEN_BRACKET_OPEN) ||
                    Check(TOKEN_SQUARE_OPEN))
                depth++;

            if (Check(TOKEN_BRACKET_CLOSE) || Check(TOKEN_BRACKET_CLOSE) ||
                    Check(TOKEN_SQUARE_CLOSE))
                depth--;

            if (depth == 0 && Check(TOKEN_SEMICOLON))
                break;

            CompilerAdvance();
        }
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after field declaration");
}

static void SkipMethod() {
    if (!Check(TOKEN_BRACKET_OPEN)) {
        CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '('");
        SkipBalanced(TOKEN_PARENTHESIS_OPEN, TOKEN_PARENTHESIS_CLOSE);
    }

    if (Match(TOKEN_COLON))
        SkipTypeExpression();

    // Handle lambdas.
    if (Match(TOKEN_FAT_ARROW)) {
        int depth = 0;

        while (!Check(TOKEN_EOF)) {
            if (Check(TOKEN_PARENTHESIS_OPEN) || Check(TOKEN_BRACKET_OPEN) ||
                    Check(TOKEN_SQUARE_OPEN))
                depth++;

            if (Check(TOKEN_BRACKET_CLOSE) || Check(TOKEN_BRACKET_CLOSE) ||
                    Check(TOKEN_SQUARE_CLOSE))
                depth--;

            if (depth == 0 && Check(TOKEN_SEMICOLON))
                break;

            CompilerAdvance();
        }

        CompilerConsume(TOKEN_SEMICOLON, "Expected ';'");
        return;
    }

    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");
    SkipBalanced(TOKEN_BRACKET_OPEN, TOKEN_BRACKET_CLOSE);
}

static void SkipGetter() {
    if (Match(TOKEN_COLON))
        SkipTypeExpression();

    if (Match(TOKEN_FAT_ARROW)) {
        int depth = 0;

        while (!Check(TOKEN_EOF)) {
            if (Check(TOKEN_PARENTHESIS_OPEN) || Check(TOKEN_BRACKET_OPEN) ||
                    Check(TOKEN_SQUARE_OPEN))
                depth++;

            if (Check(TOKEN_BRACKET_CLOSE) || Check(TOKEN_BRACKET_CLOSE) ||
                    Check(TOKEN_SQUARE_CLOSE))
                depth--;

            if (depth == 0 && Check(TOKEN_SEMICOLON))
                break;

            CompilerAdvance();
        }

        CompilerConsume(TOKEN_SEMICOLON, "Expected ';'");
        return;
    }

    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");
    SkipBalanced(TOKEN_BRACKET_OPEN, TOKEN_BRACKET_CLOSE);
}

static void SkipSetter() {
    CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '('");
    SkipBalanced(TOKEN_PARENTHESIS_OPEN, TOKEN_PARENTHESIS_CLOSE);

    if (Match(TOKEN_COLON))
        SkipTypeExpression();

    if (Match(TOKEN_FAT_ARROW)) {
        int depth = 0;

        while (!Check(TOKEN_EOF)) {
            if (Check(TOKEN_PARENTHESIS_OPEN) || Check(TOKEN_BRACKET_OPEN) ||
                    Check(TOKEN_SQUARE_OPEN))
                depth++;

            if (Check(TOKEN_BRACKET_CLOSE) || Check(TOKEN_BRACKET_CLOSE) ||
                    Check(TOKEN_SQUARE_CLOSE))
                depth--;

            if (depth == 0 && Check(TOKEN_SEMICOLON))
                break;

            CompilerAdvance();
        }

        CompilerConsume(TOKEN_SEMICOLON, "Expected ';'");
        return;
    }

    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");
    SkipBalanced(TOKEN_BRACKET_OPEN, TOKEN_BRACKET_CLOSE);
}

static void SkipNestedClassDeclaration(void) {
    CompilerConsume(TOKEN_IDENTIFIER, "Expected kata name");

    if (Match(TOKEN_COLON))
        CompilerConsume(TOKEN_IDENTIFIER, "Expected sokata name");

    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");
    SkipBalanced(TOKEN_BRACKET_OPEN, TOKEN_BRACKET_CLOSE);
}

static void RegisterTopLevelConst(KTN_Token name, KTN_Value value) {
    // Grow global constant array.
    if (current->topLevelConstCount + 1 >= current->topLevelConstCapacity) {
        KTN_VM* vm = parser.vm;

        int newCapacity = GROW_CAPACITY(current->topLevelConstCapacity);

        current->topLevelConsts = GROW_ARRAY(TopLevelConst, current->topLevelConsts, current->topLevelConstCapacity, newCapacity);
        current->topLevelConstCapacity = newCapacity;
    }

    // Add global const to list.
    TopLevelConst* entry = &current->topLevelConsts[current->topLevelConstCount++];

    entry->name = StringCopy(parser.vm, name.start, name.length);
    entry->value = value;
}

static void RegisterClassProperty(KTN_Token name) {
    if (currentClass == NULL)
        return;

    if (currentClass->propertyCount >= MAX_CLASS_PROPERTIES) {
        Error("Too many instance fields in this kata for implicit \"this\" tracking");
        return;
    }

    currentClass->propertyNames[currentClass->propertyCount++] = name;
}

static void RegisterClassMethod(KTN_Token name) {
    if (currentClass == NULL)
        return;

    if (currentClass->methodCount >= MAX_CLASS_PROPERTIES) {
        Error("Too many instance methods in this kata for implicit \"this\" tracking");
        return;
    }

    currentClass->methodNames[currentClass->methodCount++] = name;
}

static void CollectClassMembers() {
    while (!Check(TOKEN_BRACKET_CLOSE) && !Check(TOKEN_EOF)) {
        bool isStatic = false;

        for (;;) {
            if (Match(TOKEN_STATIC)) {
                isStatic = true;
                continue;
            }

            if (Match(TOKEN_HIDDEN) || Match(TOKEN_PRIVATE) || Match(TOKEN_FINAL))
                continue;

            break;
        }

        if (Match(TOKEN_VAR)) {
            CompilerConsume(TOKEN_IDENTIFIER, "Expected field name");
            if (!isStatic)
                RegisterClassProperty(parser.previous);
            SkipFieldTail();
            continue;
        }

        if (Match(TOKEN_GET)) {
            CompilerConsume(TOKEN_IDENTIFIER, "Expected getter name");

            if (!isStatic)
                RegisterClassMethod(parser.previous);

            SkipGetter();
            continue;
        }

        if (Match(TOKEN_SET)) {
            CompilerConsume(TOKEN_IDENTIFIER, "Expected setter name");

            if (!isStatic)
                RegisterClassProperty(parser.previous);

            SkipSetter();
            continue;
        }

        if (Match(TOKEN_CLASS)) {
            SkipNestedClassDeclaration();
            continue;
        }

        Match(TOKEN_FUNCTION);
        CompilerConsume(TOKEN_IDENTIFIER, "Expected method name");

        if (!isStatic)
            RegisterClassProperty(parser.previous);

        SkipMethod();
    }
}

static void CompilerMethod(const char* className, bool matchedShiki, bool isPrivate, bool isHidden, bool isStatic) {
    KTN_Token shikiToken = parser.previous;

    CompilerConsume(TOKEN_IDENTIFIER, "Expected method name");
    uint32_t constant = IdentifierConstant(&parser.previous);

    if (!isStatic)
        RegisterClassProperty(parser.previous);

    KTN_ShikiType type = TYPE_METHOD;

    size_t classNameLength = strlen(className);

    if (parser.previous.length == classNameLength && memcmp(parser.previous.start, className, classNameLength) == 0) {
        if (matchedShiki) {
            ErrorAt(&shikiToken, "Unexpected \"shiki\" found before constructor");
            return;
        }

        if (isStatic) {
            ErrorAt(&shikiToken, "Constructor cannot be static.");
            return;
        }

        type = TYPE_CONSTRUCTOR;
    }

    CompilerFunction(type, isStatic);
    if (isPrivate) {
        CompilerEmitByte((isHidden) ? OP_TRUE : OP_FALSE);
        CompilerEmitByteLong(OP_MARK_PRIVATE, constant);
    }
    CompilerEmitByteLong(OP_METHOD, constant);
    CompilerEmitByte((isStatic) ? 1 : 0);
}

static void CompilerGetter(bool isPrivate, bool isHidden, bool isStatic) {
    CompilerConsume(TOKEN_IDENTIFIER, "Expected getter name");
    uint32_t constant = IdentifierConstant(&parser.previous);

    if (!isStatic)
        RegisterClassProperty(parser.previous);

    CompilerFunction(TYPE_GETTER, isStatic);

    if (isPrivate) {
        CompilerEmitByte((isHidden) ? OP_TRUE : OP_FALSE);
        CompilerEmitByteLong(OP_MARK_PRIVATE, constant);
    }

    CompilerEmitByteLong(OP_METHOD, constant);
    CompilerEmitByte((isStatic) ? 1 : 0);
}

static void CompilerSetter(bool isPrivate, bool isHidden, bool isStatic) {
    CompilerConsume(TOKEN_IDENTIFIER, "Expected setter name");
    uint32_t constant = IdentifierConstant(&parser.previous);

    if (!isStatic)
        RegisterClassProperty(parser.previous);

    CompilerFunction(TYPE_SETTER, isStatic);

    if (isPrivate) {
        CompilerEmitByte((isHidden) ? OP_TRUE : OP_FALSE);
        CompilerEmitByteLong(OP_MARK_PRIVATE, constant);
    }

    CompilerEmitByteLong(OP_METHOD, constant);
    CompilerEmitByte((isStatic) ? 1 : 0);
}

static void CompilerLambda(bool canAssign) {
    CompilerFunction(TYPE_LAMBDA, false);
}

static void CompilerInitProperty(bool isPrivate, bool isHidden, bool isStatic,
                                                                  bool isFinal) {
    CompilerConsume(TOKEN_IDENTIFIER, "Expected property name after \"mochi\"");
    uint32_t name = IdentifierConstant(&parser.previous);
    uint8_t flags = isFinal ? KTN_TABLE_ENTRY_FINAL : 0;

    if (!isStatic)
        RegisterClassProperty(parser.previous);

    int32_t typeDescriptorIndex = -1;
    if (Match(TOKEN_COLON))
        typeDescriptorIndex = ParseTypeAnnotation();

    if (Match(TOKEN_ASSIGN))
        CompilerExpression();
    else if (isFinal)
        CompilerEmitConstant(EMPTY_VALUE);
    else
        CompilerEmitByte(OP_NULL);

    if (isPrivate) {
        CompilerEmitByte((isHidden) ? OP_TRUE : OP_FALSE);
        CompilerEmitByteLong(OP_MARK_PRIVATE, name);
    }

    if (typeDescriptorIndex >= 0) {
        CompilerEmitByteLong(OP_INIT_PROPERTY_TYPED, name);
        CompilerEmitByteLong((isStatic) ? 1 : 0, (uint32_t)typeDescriptorIndex);
        CompilerEmitByte(flags);
    } else {
        CompilerEmitByteLong(OP_INIT_PROPERTY, name);
        CompilerEmitByte((isStatic) ? 1 : 0);
        CompilerEmitByte(flags);
    }

    CompilerAdvance();
}

static void ClassDeclaration() {
    CompilerConsume(TOKEN_IDENTIFIER, "Expected kata name");
    KTN_Token className = parser.previous;
    uint32_t nameConstant = IdentifierConstant(&parser.previous);
    if (current->scopeDepth != 0)
        DeclareVariable();

    CompilerEmitByteLong(OP_CLASS, nameConstant);
    DefineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    classCompiler.propertyCount = 0;
    classCompiler.methodCount = 0;
    currentClass = &classCompiler;

    CompilerBeginScope();

    if (Match(TOKEN_COLON)) {
        CompilerConsume(TOKEN_IDENTIFIER, "Expected sokata name");
        CompilerVariable(false);

        if (IdentifiersEqual(&className, &parser.previous)) {
            Error("A kata cannot inherit from itself");
        }

        AddLocal(SyntheticToken("sokata"));
        DefineVariable(0);

        NamedVariable(className, false);
        CompilerEmitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }

    NamedVariable(className, false);
    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{' before kata body");

    KTN_ScannerPushState();
    ParserSnapshot savedParser = ParserSaveState();

    bool outerCollectOnly = collectOnly;
    collectOnly = true;

    CollectClassMembers();

    collectOnly = outerCollectOnly;

    KTN_ScannerRestoreTopState();
    ParserRestoreState(savedParser);

    char* classNameString = Substring(className.start, className.length);

    while (!Check(TOKEN_BRACKET_CLOSE) && !Check(TOKEN_EOF)) {
        bool isPrivate = false;
        bool isHidden = false;
        bool isStatic = false;
        bool isFinal = false;

        for (;;) {
            if (Match(TOKEN_STATIC)) {
                isStatic = true;
                continue;
            }

            if (Match(TOKEN_HIDDEN)) {
                isPrivate = true;
                isHidden = true;
                continue;
            }

            if (Match(TOKEN_PRIVATE)) {
                isPrivate = true;
                continue;
            }

            if (Match(TOKEN_FINAL)) {
                isFinal = true;
                continue;
            }

            break;
        }

        if (Match(TOKEN_VAR)) {
            CompilerInitProperty(isPrivate, isHidden, isStatic, isFinal);
            continue;
        }

        if (isFinal)
            ErrorAt(&parser.previous, "\"final\" can only be used on kata fields.");

        if (Match(TOKEN_GET)) {
            CompilerGetter(isPrivate, isHidden, isStatic);
            continue;
        }

        if (Match(TOKEN_SET)) {
            CompilerSetter(isPrivate, isHidden, isStatic);
            continue;
        }

        if (Match(TOKEN_CLASS)) {
            ClassDeclaration();
            continue;
        }

        // Optional "shiki" keyword before method name is allowed.
        bool matchedShiki = Match(TOKEN_FUNCTION);
        CompilerMethod(classNameString, matchedShiki, isPrivate, isHidden, isStatic);
    }

    KTN_VM* vm = parser.vm;
    FREE(char, classNameString);

    CompilerConsume(TOKEN_BRACKET_CLOSE, "Expected '}' after kata body");
    CompilerEmitByte(OP_POP);

    CompilerEndScope();

    currentClass = currentClass->enclosing;

    KTN_ScannerPopState();
}

static void FunctionDeclaration() {
    // Checking because CompilerFunction consumes the opening parenthesis.
    if (Check(TOKEN_PARENTHESIS_OPEN)) {
        CompilerFunction(TYPE_LAMBDA, false);
        return;
    }

    // Regular function.
    uint32_t global = ParseVariable("Expected shiki name");
    MarkInitialized();
    CompilerFunction(TYPE_FUNCTION, false);
    DefineVariable(global);
}

static void EntryDeclaration() {
    if (current->scopeDepth != 0) {
        Error("Entry point must be declared at the top level.");
        return;
    }

    CompilerConsume(TOKEN_FUNCTION, "Expected \"shiki\" after \"entry\"");

    uint32_t global = ParseVariable("Expected shiki name");
    MarkInitialized();
    CompilerFunction(TYPE_ENTRY, false);
    DefineVariable(global);
}

static void StatementExpression() {
    parser.lastExpressionWasAssignment = false;
    CompilerExpression();
    CompilerConsume(TOKEN_SEMICOLON, "Expect ';' after expression");

    CompilerEmitByte((!parser.lastExpressionWasAssignment &&
                                        current->scopeDepth == 0 && current->type == TYPE_SCRIPT)
                                              ? OP_POP_RESULT
                                              : OP_POP);
}

static void StatementSwitch() {
    BreakableContext context;
    context.type = CONTEXT_SWITCH;
    context.breakJumpCount = 0;
    context.fallJumpCount = 0;
    context.continueTarget = -1;
    context.enclosing = currentBreakable;
    context.scopeDepth = current->scopeDepth;
    currentBreakable = &context;

    CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after \"switch\"");
    CompilerExpression();
    CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after value");
    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{' before switch cases");

    int state = 0;
    int previousCaseSkip = -1;

    while (!Match(TOKEN_BRACKET_CLOSE) && !Check(TOKEN_EOF)) {
        if (Match(TOKEN_CASE) || Match(TOKEN_DEFAULT)) {
            KTN_TokenType caseType = parser.previous.type;

            if (state == 2)
                Error("Cannot have a case or default after the default case");

            if (state == 1) {
                context.breakJumps[context.breakJumpCount++] = CompilerEmitJump(OP_JUMP);
                CompilerPatchJump(previousCaseSkip);
                CompilerEmitByte(OP_POP);
            }

            if (caseType == TOKEN_CASE) {
                state = 1;
                CompilerEmitByte(OP_DUPLICATE);
                CompilerExpression();
                CompilerConsume(TOKEN_COLON, "Expected ':' after case value");
                CompilerEmitByte(OP_EQUAL);
                previousCaseSkip = CompilerEmitJump(OP_JUMP_IF_FALSE);
                CompilerEmitByte(OP_POP);

                for (int i = 0; i < context.fallJumpCount; i++)
                    CompilerPatchJump(context.fallJumps[i]);
                context.fallJumpCount = 0;
            } else {
                state = 2;
                CompilerConsume(TOKEN_COLON, "Expected ':' after default case");
                previousCaseSkip = -1;

                for (int i = 0; i < context.fallJumpCount; i++)
                    CompilerPatchJump(context.fallJumps[i]);
                context.fallJumpCount = 0;
            }
        } else {
            if (state == 0)
                Error("Cannot have statements before any case");
            CompilerDeclaration();
        }
    }

    if (state == 1) {
        // Emit a jump for any path that actually executed the last case's body
        // (whether it matched normally or arrived via fall). This jump skips the
        // OP_POP below which is only for the no-match (JUMP_IF_FALSE) skip path.
        context.breakJumps[context.breakJumpCount++] = CompilerEmitJump(OP_JUMP);

        // Patch the last case's JUMP_IF_FALSE so the skip (no-match) path
        // lands here, then pop the comparison result it still has on the stack.
        CompilerPatchJump(previousCaseSkip);
        CompilerEmitByte(OP_POP);
    }

    for (int i = 0; i < context.breakJumpCount; i++)
        CompilerPatchJump(context.breakJumps[i]);
    for (int i = 0; i < context.fallJumpCount; i++)
        CompilerPatchJump(context.fallJumps[i]);

    CompilerEmitByte(OP_POP);
    currentBreakable = context.enclosing;
}

static void StatementForIn(BreakableContext* context, KTN_Token itemName) {
    // TODO: implement for-in compilation. We don't yet have iterables, so...
    (void)context;
    (void)itemName;
}

static void StatementFor() {
    BreakableContext context;

    context.type = CONTEXT_LOOP;
    context.breakJumpCount = 0;
    context.fallJumpCount = 0;
    context.enclosing = currentBreakable;
    currentBreakable = &context;

    // This is the whole-loop scope, which houses the header variables.
    // Not to be confused with the loop body scope!
    CompilerBeginScope();
    context.scopeDepth = current->scopeDepth;

    CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after \"for\"");

    // Check if we have a for (mochi item in iterable).
    // We can't really know which of the two for loops we have until we see
    //  an "in" token, but by then we have consumed the identifier, which 
    //  VariableDeclaration expects. So, we need to do this small double pass.
    if (Check(TOKEN_VAR)) {
        // This saves the state of both the scanner, which tokenizes the file, as well
        //  as the parser. This allows us to revert back on both processing and file position.
        KTN_ScannerPushState();
        ParserSnapshot snapshot = ParserSaveState();
        CompilerAdvance();
        
        if (Check(TOKEN_IDENTIFIER)) {
            KTN_Token itemName = parser.current;
            CompilerAdvance();
            
            if (Match(TOKEN_IN)) {
                KTN_ScannerPopState();
                StatementForIn(&context, itemName);

                return;
            }
        }
        
        KTN_ScannerRestoreTopState();
        KTN_ScannerPopState();
        ParserRestoreState(snapshot);
    }

    // Rest of this is the standard for loop: (for (var; condition; increment))

    // We store the position in the locals array where we will contain the value of the
    //  iteration variable. This will allow us to later know where to find it so that we can
    //  create a shadowing variable. (For more info, read below.)
    int headerLocalBase = current->localCount;

    if (Match(TOKEN_SEMICOLON)) {
        // no init
    } else if (Match(TOKEN_VAR)) {
        VariableDeclaration();
    } else {
        StatementExpression();
    }

    int numHeaderLocals = current->localCount - headerLocalBase;

    // At this point, we have parsed the loop mochi. This means we're at the loop
    //  condition. Thus, we record the position in the chunk which will be where we land
    //  after each loop. We also update context.continueTarget to know where we have to jump
    //  if we find a continue.
    int loopStart = CurrentChunk()->count;
    context.continueTarget = loopStart;

    // Exit jump will be the jump that allows us to exit the loop if the loop has a condition.
    // This is optional so that we can allow for infinite, C-style "for (;;)" loops.
    int exitJump = -1;

    if (!Match(TOKEN_SEMICOLON)) {
        CompilerExpression();
        CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after loop condition");
        exitJump = CompilerEmitJump(OP_JUMP_IF_FALSE);
        CompilerEmitByte(OP_POP);
    }

    // Optional increment statement.
    // Since we found an increment statement, we have to repatch the previous loop starts
    //  and targets to increment the variable before jumping to the loop condition.
    if (!Match(TOKEN_PARENTHESIS_CLOSE)) {
        int bodyJump = CompilerEmitJump(OP_JUMP);
        int incrementStart = CurrentChunk()->count;
        context.continueTarget = incrementStart;

        CompilerExpression();
        CompilerEmitByte(OP_POP);
        CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after for clauses");

        CompilerEmitLoop(loopStart);
        loopStart = incrementStart;
        CompilerPatchJump(bodyJump);
    }

    // The loop body gets its own per iteration scope. This allows us to create a shadowing
    //  iteration variable, which gets captured by any closures within. This way, we solve
    //  the infamous JS bug, since CompilerEndScope will issue an OP_CLOSE_UPVALUE whenever
    //  we reach the end of the body (or use a break, or continue), which will copy the inner
    //  variable every iteration. Poggers.
    CompilerBeginScope();

    // We go through each of the header's locals, so we can create new shadowing copies inside
    //  the current scope of the body. We use the previous locals offset to find each header local.
    // We then: get the name of the local, emit an OP_GET_LOCAL to get its value on the stack, 
    //  add the new local to the scope, and mark it as initialized (to avoid the -1 sentinel)
    for (int i = 0; i < numHeaderLocals; i++) {
        KTN_Token copyName = current->locals[headerLocalBase + i].name;
        CompilerEmitBytes(OP_GET_LOCAL, (uint8_t)(headerLocalBase + i));
        AddLocal(copyName);
        MarkInitialized();
    }

    CompilerStatement();

    // Let's go! We close the body's scope, which will automatically push the OP_CLOSE_UPVALUE opcodes
    //  to copy the upvalue references to each of the closure's locals, if they captured it.
    CompilerEndScope();

    // Emit the loop back to the start (usually the increment statement, but can be the 
    //  condition statement if no increment statement was found).
    CompilerEmitLoop(loopStart);

    // If we do have an exit condition (meaning we generated an exit jump), we patch it here.
    if (exitJump != -1) {
        CompilerPatchJump(exitJump);
        
        // This pop is for popping off the bool exit condition value from the stack (under
        //  normal execution).
        CompilerEmitByte(OP_POP);
        
        // Patch any break jumps to go after the condition value (since it will have already been popped)
        for (int i = 0; i < context.breakJumpCount; i++)
            CompilerPatchJump(context.breakJumps[i]);
    } else {
        // Breaks always have to be patched, regardless of whether we have an exit jump.
        // They are always the way to exit loops unconditionally.
        for (int i = 0; i < context.breakJumpCount; i++)
            CompilerPatchJump(context.breakJumps[i]);
    }

    // This closes the whole loop scope, which gets rid of the header variables.
    // Sorry folks, 'i' only exists within the loop, not outside of it.
    CompilerEndScope();

    // We "pop" back to the previous breakable context. This supports continues and breaks
    //  inside nested loops.
    currentBreakable = context.enclosing;
}

static void StatementBreak() {
    BreakableContext* target = currentBreakable;
    while (target != NULL && target->type != CONTEXT_LOOP)
        target = target->enclosing;

    if (target == NULL)
        target = currentBreakable;

    if (target == NULL) {
        Error("\"break\" used outside of a loop or switch statement");
        return;
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after \"break\"");

    if (target->breakJumpCount >= MAX_BREAK_JUMPS) {
        Error("Too many break statements");
        return;
    }

    for (int i = current->localCount - 1; i >= 0; i--) {
        if (current->locals[i].depth <= target->scopeDepth)
            break;

        if (current->locals[i].isCaptured)
            CompilerEmitByte(OP_CLOSE_UPVALUE);
        else
            CompilerEmitByte(OP_POP);
    }

    if (currentTry != NULL && target->type == CONTEXT_LOOP) {
        target->breakJumps[target->breakJumpCount++] = CompilerEmitDeferredJump();  
    }

    target->breakJumps[target->breakJumpCount++] = CompilerEmitJump(OP_JUMP);
}

static void StatementContinue() {
    BreakableContext* context = currentBreakable;
    while (context != NULL && context->type != CONTEXT_LOOP)
        context = context->enclosing;

    if (context == NULL) {
        Error("\"continue\" used outside of a loop");
        return;
    }

    if (current->inFinally) {
        Error("\"continue\" used in a finally block");
        return;
    }

    // Pop (or close) any locals declared inside the loop body before jumping
    // back. Must mirror CompilerEndScope: captured locals need OP_CLOSE_UPVALUE
    // so that any closures referencing them get a heap-allocated snapshot of the
    // current value. Plain OP_POP for non-captured locals.
    for (int i = current->localCount - 1; i >= 0; i--) {
        if (current->locals[i].depth <= context->scopeDepth)
            break;

        if (current->locals[i].isCaptured)
            CompilerEmitByte(OP_CLOSE_UPVALUE);
        else
            CompilerEmitByte(OP_POP);
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after \"continue\"");

    if (currentTry != NULL) {
        CompilerEmitDeferredLoop(context->continueTarget);
        return;
    }

    CompilerEmitLoop(context->continueTarget);
}

static void StatementFall() {
    BreakableContext* context = currentBreakable;
    while (context != NULL && context->type != CONTEXT_SWITCH)
        context = context->enclosing;

    if (context == NULL) {
        Error("\"fall\" used outside of a switch statement");
        return;
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after \"fall\"");
    if (context->fallJumpCount >= MAX_BREAK_JUMPS) {
        Error("Too many fall statements");
        return;
    }
    context->fallJumps[context->fallJumpCount++] = CompilerEmitJump(OP_JUMP);
}

static void ExpressionChoice(bool canAssign) {
    CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after \"choice\"");
    CompilerExpression();
    CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')'");
    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");

    int caseEnds[MAX_CASES];
    int caseEndCount = 0;
    int previousCaseSkip = -1;
    bool hasDefault = false;

    while (!Match(TOKEN_BRACKET_CLOSE) && !Check(TOKEN_EOF)) {
        if (Match(TOKEN_CASE)) {
            if (hasDefault) {
                Error("Cannot have a case after default in choice");
                break;
            }
            if (previousCaseSkip != -1) {
                CompilerPatchJump(previousCaseSkip);
                CompilerEmitByte(OP_POP);
            }

            CompilerEmitByte(OP_DUPLICATE);
            CompilerExpression();
            CompilerConsume(TOKEN_COLON, "Expected ':' after case value");
            CompilerEmitByte(OP_EQUAL);
            previousCaseSkip = CompilerEmitJump(OP_JUMP_IF_FALSE);
            CompilerEmitByte(OP_POP);

            CompilerExpression();
            Match(TOKEN_COMMA);

            CompilerEmitByte(OP_SWAP);
            CompilerEmitByte(OP_POP);
            if (caseEndCount < MAX_CASES)
                caseEnds[caseEndCount++] = CompilerEmitJump(OP_JUMP);
        } else if (Match(TOKEN_DEFAULT)) {
            hasDefault = true;
            if (previousCaseSkip != -1) {
                CompilerPatchJump(previousCaseSkip);
                CompilerEmitByte(OP_POP);
                previousCaseSkip = -1;
            }
            CompilerConsume(TOKEN_COLON, "Expected ':' after default");
            CompilerEmitByte(OP_POP);
            CompilerExpression();
            Match(TOKEN_COMMA);
            if (caseEndCount < MAX_CASES)
                caseEnds[caseEndCount++] = CompilerEmitJump(OP_JUMP);
        } else {
            Error("Expected \"case\" or \"default\" in choice expression");
            break;
        }
    }

    if (previousCaseSkip != -1) {
        CompilerPatchJump(previousCaseSkip);
        CompilerEmitByte(OP_POP);
    }

    if (!hasDefault) {
        CompilerEmitByte(OP_POP);
        CompilerEmitByte(OP_NULL);
    }

    for (int i = 0; i < caseEndCount; i++)
        CompilerPatchJump(caseEnds[i]);
}

static void StatementIf() {
    CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after \"if\"");
    CompilerExpression();
    CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after condition");

    int thenJump = CompilerEmitJump(OP_JUMP_IF_FALSE);
    CompilerEmitByte(OP_POP);
    CompilerStatement();

    int elseJump = CompilerEmitJump(OP_JUMP);

    CompilerPatchJump(thenJump);
    CompilerEmitByte(OP_POP);

    if (Match(TOKEN_ELSE))
        CompilerStatement();

    CompilerPatchJump(elseJump);
}

static KTN_MAYBE_UNUSED void PrintToken(const KTN_Token* token) {
    for (int i = 0; i < token->length; i++) {
        printf("%c", token->start[i]);
    }
    printf("\n");
}

static void VariableDeclaration() {
    uint32_t localIndex = ParseVariable("Expected mochi name");
    KTN_Token name = parser.previous;
    int32_t typeDescriptorIndex = -1;

    if (Match(TOKEN_COLON)) {
        typeDescriptorIndex = ParseTypeAnnotation();

        if (current->scopeDepth > 0) {
            Local* local = &current->locals[current->localCount - 1];
            local->typeDescriptorIndex = typeDescriptorIndex;
        } else if (typeDescriptorIndex >= 0) {
            KTN_ObjString* nameString = StringCopy(parser.vm, name.start, name.length);
            KTN_Value typeDescriptor = CurrentChunk()->constants.values[typeDescriptorIndex];

            TableSet(parser.vm, &parser.vm->globalTypes, nameString, typeDescriptor);
        }
    }

    if (Match(TOKEN_ASSIGN))
        CompilerExpression();
    else
        CompilerEmitByte(OP_NULL);

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after mochi declaration");

    if (typeDescriptorIndex >= 0) {
        if (current->scopeDepth == 0) {
            CompilerEmitByteLong(OP_DEFINE_GLOBAL_TYPED, localIndex);
            CompilerEmitLong((uint32_t)typeDescriptorIndex);
            return;
        }

        CompilerEmitByteLong(OP_CHECK_TYPE, (uint32_t)typeDescriptorIndex);
    }

    DefineVariable(localIndex);
}

static void StatementPrint() {
    CompilerExpression();
    CompilerConsume(TOKEN_SEMICOLON, "Expect ; after value");
    CompilerEmitByte(OP_PRINT);
}

static KTN_MAYBE_UNUSED void StatementRethrow() {

}

static void StatementReturn() {
    if (current->inFinally) {
        Error("Cannot return from a finally block");
        return;
    }

    if (current->type == TYPE_SCRIPT) {
        Error("Cannot return from outside a shiki");
        return;
    }

    if (Match(TOKEN_SEMICOLON))
        CompilerEmitReturn();
    else {
        if (current->type == TYPE_CONSTRUCTOR) {
            Error("Cannot return a value from a constructor");
            return;
        }

        CompilerExpression();
        CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after return value");

        if (currentTry != NULL) {
            CompilerEmitBytes(OP_DEFER_ACTION, 0x00);
            CompilerEmitShort((uint16_t)currentTry->scopeDepth);
            return;
        }

        CompilerEmitByte(OP_RETURN);
    }
}

static void StatementWhile() {
    BreakableContext context;
    context.type = CONTEXT_LOOP;
    context.breakJumpCount = 0;
    context.fallJumpCount = 0;
    context.enclosing = currentBreakable;
    int loopStart = CurrentChunk()->count;
    context.continueTarget = loopStart;
    context.scopeDepth = current->scopeDepth;
    currentBreakable = &context;

    CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after \"while\"");
    CompilerExpression();
    CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after condition");

    int exitJump = CompilerEmitJump(OP_JUMP_IF_FALSE);
    CompilerEmitByte(OP_POP);
    CompilerStatement();

    CompilerEmitLoop(loopStart);

    CompilerPatchJump(exitJump);
    for (int i = 0; i < context.breakJumpCount; i++)
        CompilerPatchJump(context.breakJumps[i]);
    CompilerEmitByte(OP_POP);

    currentBreakable = context.enclosing;
}

static void StatementTry() {
    // OP_BEGIN_CATCH -> 16-bit offset to first catch handler byte + 16-bit offset to finally block.
    int beginCatchJump = CompilerEmitJump(OP_BEGIN_CATCH);

    // Placeholder. If it remains zero, we don't have a finally block.
    CompilerEmitShort(0); 

    int beginFinallyOffset = CurrentChunk()->count - 2;

    TryContext tryContext;
    tryContext.scopeDepth = current->scopeDepth;
    tryContext.enclosing = currentTry;

    currentTry = &tryContext;

    CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{' after \"try\"");
    CompilerBeginScope();
    CompilerBlock();
    CompilerEndScope();

    CompilerEmitByte(OP_END_CATCH);

    // Jump over all catch handlers on the normal (non-exception) path
    int overCatchJump = CompilerEmitJump(OP_JUMP);

    // Patch OP_BEGIN_CATCH to land here on exception
    CompilerPatchJump(beginCatchJump);

    // When execution arrives here, the exception is at the top of the stack
    // (pushed by ThrowValue).  Catch clauses may consume it; unmatched clauses
    // must leave it on the stack so the next clause or an implicit rethrow can
    // use it.

    int endJumps[32];
    int endJumpCount = 0;
    bool hasCatchAll = false;

    CompilerConsume(TOKEN_CATCH, "Expected \"catch\" after \"try\" block");

    do {
        if (hasCatchAll) {
            Error("Cannot have a catch clause after a catch-all");
            break;
        }

        if (Match(TOKEN_ON)) {
            // Typed clause: catch on TypeName (varName) { }
            //           or: catch on TypeName (varName, traceVar) { }
            if (!Check(TOKEN_IDENTIFIER)) {
                Error("Expected exception kata name after \"on\"");
                return;
            }
            KTN_Token typeName = parser.current;
            CompilerAdvance();

            // Duplicate the exception so the instanceof check can consume the copy
            // while the original stays on the stack for binding or the next handler.
            CompilerEmitByte(OP_DUPLICATE);

            // OP_INSTANCEOF: pops TOS, pushes bool.
            NamedVariable(typeName, false);
            CompilerEmitByte(OP_INSTANCEOF);

            // If false, skip to next handler.  JUMP_IF_FALSE does NOT pop,
            // so we clean up the bool ourselves on both paths.
            int skipThisClause = CompilerEmitJump(OP_JUMP_IF_FALSE);
            CompilerEmitByte(OP_POP); // pop bool=true

            // Bind variables inside a new scope.
            // At this point the stack has: [..., exception]. 
            // The exception becomes local[0] of the scope without any emit (it is already there).
            CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after exception type name");
            CompilerBeginScope();

            CompilerConsume(TOKEN_IDENTIFIER, "Expected exception mochi name");
            AddLocal(parser.previous);
            MarkInitialized();

            if (Match(TOKEN_COMMA)) {
                CompilerConsume(TOKEN_IDENTIFIER, "Expected stacktrace mochi name");

                CompilerEmitByte(OP_BUILD_STACK_TRACE);
                AddLocal(parser.previous);
                MarkInitialized();
            }

            CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after mochi binding");

            CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");
            CompilerBlock();
            CompilerEndScope();

            if (endJumpCount < 32)
                endJumps[endJumpCount++] = CompilerEmitJump(OP_JUMP);

            // Patch the skip: land here when instanceof was false.
            CompilerPatchJump(skipThisClause);
            CompilerEmitByte(OP_POP);

        } else {
            // Untyped catch-all: catch (varName) { }
            //                or: catch (varName, traceVar) { }
            hasCatchAll = true;

            CompilerConsume(TOKEN_PARENTHESIS_OPEN, "Expected '(' after \"catch\"");
            CompilerBeginScope();

            CompilerConsume(TOKEN_IDENTIFIER, "Expected exception mochi name");
            AddLocal(parser.previous);
            MarkInitialized();

            if (Match(TOKEN_COMMA)) {
                CompilerConsume(TOKEN_IDENTIFIER, "Expected stacktrace mochi name");

                CompilerEmitByte(OP_BUILD_STACK_TRACE);
                AddLocal(parser.previous);
                MarkInitialized();
            }

            CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after mochi binding");

            CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{'");
            CompilerBlock();
            CompilerEndScope();

            if (endJumpCount < 32)
                endJumps[endJumpCount++] = CompilerEmitJump(OP_JUMP);
        }
    } while (Match(TOKEN_CATCH));

    // If no catch-all matched (or there was none), rethrow the exception still on
    // stack.
    if (!hasCatchAll) {
        CompilerEmitByte(OP_RAISE);
    }

    // Patch all "jump to end" emitted from each catch body, and the normal-path
    // skip.
    for (int i = 0; i < endJumpCount; i++) {
        CompilerPatchJump(endJumps[i]);
    }
    CompilerPatchJump(overCatchJump);

    currentTry = tryContext.enclosing;

    if (Match(TOKEN_FINALLY)) {
        CompilerPatchJump(beginFinallyOffset);

        // We guard against using finally in a few statements.
        current->inFinally = true;

        CompilerBeginScope();
        CompilerConsume(TOKEN_BRACKET_OPEN, "Expected '{' after \"finally\"");
        CompilerBlock();
        CompilerEndScope();

        current->inFinally = false;

        CompilerEmitByte(OP_END_FINALLY);
    }
}

static void StatementThrow() {
    // throw <expression>;
    // The expression must evaluate to an Exception subclass instance at runtime.
    CompilerExpression();
    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after throw expression");
    CompilerEmitByte(OP_RAISE);
}

static void StatementAssert() {
    // assert <condition>;
    // assert <condition> : <message>;
    CompilerExpression();

    if (Match(TOKEN_COLON)) {
        CompilerExpression();
    } else {
        // Default message as a string constant.
        uint32_t msgConst = CompilerMakeConstant(OBJECT_VALUE(StringCopy(parser.vm, "Assertion failed.", 17)));
        CompilerEmitByteLong(OP_CONSTANT_LONG, msgConst);
    }

    CompilerConsume(TOKEN_SEMICOLON, "Expected ';' after assert");
    CompilerEmitByte(OP_ASSERT);
}

static void CompilerSynchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON)
            return;

        switch (parser.current.type) {
        case TOKEN_CLASS:
        case TOKEN_FUNCTION:
        case TOKEN_ENTRY:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_SWITCH:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;
        default:;
        }

        CompilerAdvance();
    }
}

static void CompilerDeclaration() {
    if (Match(TOKEN_CLASS))
        ClassDeclaration();
    else if (Match(TOKEN_FUNCTION))
        FunctionDeclaration();
    else if (Match(TOKEN_ENTRY))
        EntryDeclaration();
    else if (Match(TOKEN_CONST))
        ConstDeclaration();
    else if (Match(TOKEN_FINAL))
        FinalDeclaration();
    else if (Match(TOKEN_VAR))
        VariableDeclaration();
    else
        CompilerStatement();

    if (parser.panicMode)
        CompilerSynchronize();
}

static void CompilerStatement() {
    if (Match(TOKEN_PRINT)) {
        StatementPrint();
    } else if (Match(TOKEN_IF)) {
        StatementIf();
    } else if (Match(TOKEN_RETURN)) {
        StatementReturn();
    } else if (Match(TOKEN_WHILE)) {
        StatementWhile();
    } else if (Match(TOKEN_FOR)) {
        StatementFor();
    } else if (Match(TOKEN_BRACKET_OPEN)) {
        CompilerBeginScope();
        CompilerBlock();
        CompilerEndScope();
    } else if (Match(TOKEN_SWITCH)) {
        StatementSwitch();
    } else if (Match(TOKEN_TRY)) {
        StatementTry();
    } else if (Match(TOKEN_THROW)) {
        StatementThrow();
    } else if (Match(TOKEN_ASSERT)) {
        StatementAssert();
    } else if (Match(TOKEN_BREAK)) {
        StatementBreak();
    } else if (Match(TOKEN_CONTINUE)) {
        StatementContinue();
    } else if (Match(TOKEN_FALL)) {
        StatementFall();
    } else {
        StatementExpression();
    }
}

static void CompilerGrouping(bool canAssign) {
    CompilerExpression();
    CompilerConsume(TOKEN_PARENTHESIS_CLOSE, "Expected ')' after expression");
}

static void CompilerNumber(bool canAssign) {
    KTN_TokenType type = parser.previous.type;

    // Build a clean copy of the token text without any '_' separators.
    // Max realistic literal length is well under 64 chars.
    char buffer[128] = "";
    int length = parser.previous.length;
    int offset = 0;

    for (int i = 0; i < length && offset < (int)sizeof(buffer) - 1; i++) {
        if (parser.previous.start[i] != '_')
            buffer[offset++] = parser.previous.start[i];
    }

    buffer[offset] = '\0';

    switch (type) {
    case TOKEN_INT: {
        long long value = strtoll(buffer, NULL, 10);
        if (value >= INT32_MIN && value <= INT32_MAX) {
            CompilerEmitConstant(INT_VALUE((int32_t)value));
        } else {
            CompilerEmitConstant(DOUBLE_VALUE((double)value));
        }
        break;
    }
    case TOKEN_BINARY: {
        long long value = strtoll(buffer + 2, NULL, 2);
        if (value >= INT32_MIN && value <= INT32_MAX)
            CompilerEmitConstant(INT_VALUE((int32_t)value));
        else
            CompilerEmitConstant(DOUBLE_VALUE((double)value));
        break;
    }
    case TOKEN_OCTAL: {
        long long value = strtoll(buffer + 2, NULL, 8);
        if (value >= INT32_MIN && value <= INT32_MAX)
            CompilerEmitConstant(INT_VALUE((int32_t)value));
        else
            CompilerEmitConstant(DOUBLE_VALUE((double)value));
        break;
    }
    case TOKEN_HEX: {
        long long value = strtoll(buffer, NULL, 16);
        if (value >= INT32_MIN && value <= INT32_MAX)
            CompilerEmitConstant(INT_VALUE((int32_t)value));
        else
            CompilerEmitConstant(DOUBLE_VALUE((double)value));
        break;
    }
    case TOKEN_NUMBER: {
        double value = strtod(buffer, NULL);
        CompilerEmitConstant(DOUBLE_VALUE(value));
        break;
    }
    default:
        Error("Internal: unexpected numeric token type");
        break;
    }
}

static void CompilerString(bool canAssign) {
    const char* rawString = parser.previous.start + 1;
    int rawLength = parser.previous.length - 2;

    int outputLength = 0;
    char* decoded = ProcessEscapes(rawString, rawLength, &outputLength);

    CompilerEmitConstant(OBJECT_VALUE(StringCopy(parser.vm, decoded, outputLength)));
}

static void CompilerArray(bool canAssign) {
    CompilerEmitByte(OP_NULL);

    uint16_t numOfItems = 0;

    if (!Check(TOKEN_SQUARE_CLOSE)) {
        do {
            if (!Check(TOKEN_SQUARE_CLOSE)) {
                if (numOfItems >= UINT16_MAX)
                    Error("Too many items to store in array");

                CompilerExpression();
                numOfItems++;
            }
        } while (Match(TOKEN_COMMA));
    }

    CompilerConsume(TOKEN_SQUARE_CLOSE, "Expected ']' at the end of the array");
    CompilerEmitByte(OP_ARRAY);
    CompilerEmitShort(numOfItems);
}

static void CompilerMap(bool canAssign) {
    CompilerEmitByte(OP_NULL);

    uint16_t numOfPairs = 0;

    if (!Check(TOKEN_BRACKET_CLOSE)) {
        do {
            if (!Check(TOKEN_BRACKET_CLOSE)) {
                if (numOfPairs >= UINT16_MAX)
                    Error("Too many items to store in map");

                CompilerExpression();
                CompilerConsume(TOKEN_COLON, "Expected ':' for value for pair");
                CompilerExpression();
                numOfPairs++;
            }
        } while (Match(TOKEN_COMMA));
    }

    CompilerConsume(TOKEN_BRACKET_CLOSE, "Expected '}' at the end of the map");
    CompilerEmitByte(OP_MAP);
    CompilerEmitShort(numOfPairs);
}

static void VariableSet(KTN_Token name, bool canAssign) {
    uint8_t setOp;

    int argument = ResolveLocal(current, &name);

    if (argument != -1) {
        setOp = OP_SET_LOCAL;
    } else {
        argument = IdentifierConstant(&name);
        setOp = OP_SET_GLOBAL;
    }

    if (setOp == OP_SET_GLOBAL)
        CompilerEmitByteLong(setOp, (uint32_t)argument);
    else
        CompilerEmitBytes(setOp, (uint8_t)argument);
    CompilerEmitByte(OP_POP);
}

static void VariableSetPrevious(bool canAssign) {
    VariableSet(parser.previous, canAssign);
}

static bool CanUseImplicitThis() {
#ifdef EXPERIMENTAL_IMPLICIT_THIS
    if (currentClass == NULL)
        return false;

    switch (current->type) {
        case TYPE_METHOD:
        case TYPE_CONSTRUCTOR:
        case TYPE_GETTER:
        case TYPE_SETTER:
            return true;
        default:
            return false;
    }
#else
    return false;
#endif
}

static bool ResolveClassProperty(KTN_Token* name) {
    if (currentClass == NULL)
        return false;

    for (int i = 0; i < currentClass->propertyCount; i++) {
        if (IdentifiersEqual(name, &currentClass->propertyNames[i]))
            return true;
    }

    return false;
}

static bool ResolveClassMethod(KTN_Token* name) {
    if (currentClass == NULL)
        return false;

    for (int i = 0; i < currentClass->methodCount; i++) {
        if (IdentifiersEqual(name, &currentClass->methodNames[i]))
            return true;
    }

    return false;
}

static void NamedVariable(KTN_Token name, bool canAssign) {
    uint8_t getOp, setOp;

    bool isAssignment = (canAssign && Match(TOKEN_ASSIGN));

    KTN_Value constValue;
    if (ResolveLocalConst(&name, &constValue) || ResolveTopLevelConst(parser.vm, &name, &constValue)) {
        if (isAssignment) {
            Error("Cannot assign to a const mochi");
            return;
        }

        uint32_t index = CompilerMakeConstant(constValue);
        CompilerEmitByteLong(OP_CONSTANT_LONG, index);
        return;
    }

    int argument = ResolveLocal(current, &name);
    Local* local = NULL;

    if (argument != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;

        local = &current->locals[argument];

        if (isAssignment && local->isFinal && local->isAssigned) {
            Error("Cannot assign to a final mochi after initialization");
            return;
        }

    } else if ((argument = ResolveUpvalue(current, &name, &local)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;

        if (isAssignment && local != NULL && local->isFinal && local->isAssigned) {
            Error("Cannot assign to a final mochi after initialization");
            return;
        }
    } else if (CanUseImplicitThis() && ResolveClassProperty(&name)) {
        argument = IdentifierConstant(&name);

        CompilerEmitBytes(OP_GET_LOCAL, 0);

        getOp = OP_GET_PROPERTY;
        setOp = OP_SET_PROPERTY;
    } else if (CanUseImplicitThis() && ResolveClassMethod(&name)) {
        uint32_t methodConst = IdentifierConstant(&name);

        if (isAssignment) {
            Error("Cannot assign to a method name");
            return;
        }

        if (Match(TOKEN_PARENTHESIS_OPEN)) {
            CompilerEmitBytes(OP_GET_LOCAL, 0);
            uint8_t argCount = ArgumentList();
            CompilerEmitByteLong(OP_INVOKE, methodConst);
            CompilerEmitByte(argCount);
        } else {
            CompilerEmitBytes(OP_GET_LOCAL, 0);
            CompilerEmitByteLong(OP_GET_PROPERTY, methodConst);
        }

        return;
    } else {
        argument = IdentifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (isAssignment) {
        CompilerExpression();
        EmitCheckedSet(setOp, argument, &name);
        parser.lastExpressionWasAssignment = true;
    } else {
        ResolveExtraAssignments(
            getOp,
            setOp,
            argument,
            (getOp == OP_GET_GLOBAL),
            name
        );
    }
}

static void CompilerVariable(bool canAssign) {
    NamedVariable(parser.previous, canAssign);
}

static KTN_Token SyntheticToken(const char* text) {
    KTN_Token token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

static void CompilerSuper(bool canAssign) {
    if (currentClass == NULL) {
        Error("Cannot use \"sokata\" outside of a kata");
    } else if (!currentClass->hasSuperclass) {
        Error("Cannot use \"sokata\" in a kata with no sokata");
    }

    if (Match(TOKEN_PARENTHESIS_OPEN)) {
        if (current->type != TYPE_CONSTRUCTOR) {
            Error("Sokata cannot be called outside of the kata's constructor");
            return;
        }

        KTN_Token superToken = SyntheticToken("sokata");
        uint32_t superName = IdentifierConstant(&superToken);
        NamedVariable(SyntheticToken("this"), false);
        uint8_t argumentCount = ArgumentList();
        NamedVariable(superToken, false);
        CompilerEmitByteLong(OP_SUPER_INVOKE, superName);
        CompilerEmitByte(argumentCount);
        return;
    }

    CompilerConsume(TOKEN_DOT, "Expected '.' after \"sokata\".");
    CompilerConsume(TOKEN_IDENTIFIER, "Expected sokata method name.");
    uint32_t name = IdentifierConstant(&parser.previous);

    NamedVariable(SyntheticToken("this"), false);
    if (Match(TOKEN_PARENTHESIS_OPEN)) {
        uint8_t argumentCount = ArgumentList();
        NamedVariable(SyntheticToken("sokata"), false);
        CompilerEmitByteLong(OP_SUPER_INVOKE, name);
        CompilerEmitByte(argumentCount);
        return;
    }

    NamedVariable(SyntheticToken("sokata"), false);
    CompilerEmitByteLong(OP_GET_SUPER, name);
}

static void CompilerThis(bool canAssign) {
    if (currentClass == NULL) {
        Error("Cannot use \"this\" outside of a kata");
        return;
    }

    CompilerVariable(false);
}

static void CompilerIndex(bool canAssign) {
    // Arrays have numeric indexes, but can also have ranges.
    // Numeric indexes return a single value. Range indexes return a list with the
    // contents of the range.
    bool isAssignable = true; // We can assign to a single index but we can't assign to a range.
    bool matchedColon = false;
    KTN_Token name = parser.previous;
    uint8_t getOp = OP_GET_INDEX;

    // If we find a colon, we know that this is going to be a range starting from
    // the beginning of the array.
    //      Ex: [:2] = [(start of array):2] / [0:2]
    if (Match(TOKEN_COLON)) {
        isAssignable = false; // We disable assigning to a range.
        matchedColon = true;
        CompilerEmitByte(OP_NULL); // Null represents the beginning of an array.
        getOp = OP_GET_INDEX_RANGED;
    } else {
        // There was no colon so we try to get an expression.
        CompilerExpression();
    }

    // If we haven't yet found a bracket after the index, we might be looking at
    // the end of a range.
    if (!Match(TOKEN_SQUARE_CLOSE)) {
        getOp = OP_GET_INDEX_RANGED;

        // If we haven't matched a token, then we got an expression. So we look for
        // the colon again.
        if (!matchedColon)
            CompilerConsume(TOKEN_COLON, "Expected ':\" or \"]'");

        matchedColon = true;

        isAssignable = false;
        // If we got here, a colon token got consumed. Now we check for either the
        // end of the range or the end of the index.
        if (Match(TOKEN_SQUARE_CLOSE)) {
            CompilerEmitByte(OP_NULL);
            CompilerEmitByte(OP_NULL);
        } else {
            if (Match(TOKEN_COLON))
                CompilerEmitByte(OP_NULL);
            else
                CompilerExpression();

            if (!Match(TOKEN_SQUARE_CLOSE)) {
                CompilerConsume(TOKEN_COLON, "Expected ':' or ']'");
                CompilerExpression();
                CompilerConsume(TOKEN_SQUARE_CLOSE, "Expected ']' after index range");
            } else
                CompilerEmitByte(OP_NULL);
        }
    } else {
        // We found a closing square bracket.
        // If we previously matched a colon, that means we have a range to the end
        // of the array.
        //      Ex: [2:] -> [2:(end of array)]
        if (matchedColon) {
            CompilerEmitByte(OP_NULL);
            CompilerEmitByte(OP_NULL);
        }
    }

    if (Match(TOKEN_ASSIGN)) {
        if (!isAssignable)
            Error("Cannot assign a value to a ranged index");

        CompilerExpression();
        CompilerEmitByte(OP_SET_INDEX);
        parser.lastExpressionWasAssignment = true;
    } else {
        if (matchedColon)
            ResolveExtraAssignments(getOp, OP_SET_INDEX, -1, false, name);
        else
            CompilerEmitByte(OP_GET_INDEX);
    }
}

static void CompilerUnary(bool canAssign) {
    KTN_TokenType operatorType = parser.previous.type;

    // Compile operand.
    CompilerParsePrecedence(PREC_UNARY);

    switch (operatorType) {
    case TOKEN_MINUS:
        CompilerEmitByte(OP_NEGATE);
        break;
    case TOKEN_NOT:
        CompilerEmitByte(OP_NOT);
        break;
    case TOKEN_BITWISE_NOT:
        CompilerEmitByte(OP_BITWISE_NOT);
        break;
    case TOKEN_INCREASE:
        CompilerEmitByte(OP_PREINCREASE);
        VariableSetPrevious(canAssign);
        break;
    case TOKEN_DECREASE:
        CompilerEmitByte(OP_PREDECREASE);
        VariableSetPrevious(canAssign);
        break;
    default:
        return;
    }
}

ParseRule rules[] = {
        //    [TOKEN]                     [Functions]
        [TOKEN_PARENTHESIS_OPEN] = {CompilerGrouping, CompilerCall, PREC_CALL},
        [TOKEN_PARENTHESIS_CLOSE] = {NULL, NULL, PREC_NONE},
        [TOKEN_BRACKET_OPEN] = {CompilerMap, NULL, PREC_NONE},
        [TOKEN_BRACKET_CLOSE] = {NULL, NULL, PREC_NONE},
        [TOKEN_SQUARE_OPEN] = {CompilerArray, CompilerIndex, PREC_CALL},
        [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
        [TOKEN_DOT] = {NULL, CompilerDot, PREC_CALL},
        [TOKEN_MINUS] = {CompilerUnary, CompilerBinary, PREC_TERM},
        [TOKEN_PLUS] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_ADD_EQUAL] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_SUB_EQUAL] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_MULT_EQUAL] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_DIV_EQUAL] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_INCREASE] = {CompilerUnary, NULL, PREC_TERM},
        [TOKEN_DECREASE] = {CompilerUnary, NULL, PREC_TERM},
        [TOKEN_MOD] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_FLOOR] = {NULL, CompilerBinary, PREC_FACTOR},
        [TOKEN_POW] = {NULL, CompilerBinary, PREC_POWER},
        [TOKEN_FLOOR_EQUAL] = {NULL, CompilerBinary, PREC_ASSIGNMENT},
        [TOKEN_POW_EQUAL] = {NULL, CompilerBinary, PREC_ASSIGNMENT},
        [TOKEN_BITWISE_AND] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_BITWISE_OR] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_BITWISE_XOR] = {NULL, CompilerBinary, PREC_TERM},
        [TOKEN_BITWISE_NOT] = {CompilerUnary, CompilerBinary, PREC_TERM},
        [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
        [TOKEN_SLASH] = {NULL, CompilerBinary, PREC_FACTOR},
        [TOKEN_STAR] = {NULL, CompilerBinary, PREC_FACTOR},
        [TOKEN_NOT] = {CompilerUnary, NULL, PREC_NONE},
        [TOKEN_NOT_EQUAL] = {NULL, CompilerBinary, PREC_EQUALITY},
        [TOKEN_ASSIGN] = {NULL, NULL, PREC_NONE},
        [TOKEN_EQUAL] = {NULL, CompilerBinary, PREC_EQUALITY},
        [TOKEN_GREATER] = {NULL, CompilerBinary, PREC_COMPARISON},
        [TOKEN_GREATER_EQ] = {NULL, CompilerBinary, PREC_COMPARISON},
        [TOKEN_SMALLER] = {NULL, CompilerBinary, PREC_COMPARISON},
        [TOKEN_SMALLER_EQ] = {NULL, CompilerBinary, PREC_COMPARISON},
        [TOKEN_IS] = {NULL, CompilerBinary, PREC_COMPARISON},
        [TOKEN_IDENTIFIER] = {CompilerVariable, NULL, PREC_NONE},
        [TOKEN_STRING] = {CompilerString, NULL, PREC_NONE},
        [TOKEN_NUMBER] = {CompilerNumber, NULL, PREC_NONE},
        [TOKEN_INT] = {CompilerNumber, NULL, PREC_NONE},
        [TOKEN_BINARY] = {CompilerNumber, NULL, PREC_NONE},
        [TOKEN_OCTAL] = {CompilerNumber, NULL, PREC_NONE},
        [TOKEN_HEX] = {CompilerNumber, NULL, PREC_NONE},
        [TOKEN_AND] = {NULL, CompilerAnd, PREC_AND},
        [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
        [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
        [TOKEN_FALSE] = {CompilerLiteral, NULL, PREC_NONE},
        [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
        [TOKEN_FUNCTION] = {CompilerLambda, NULL, PREC_NONE},
        [TOKEN_CHOICE] = {ExpressionChoice, NULL, PREC_NONE},
        [TOKEN_IF] = {NULL, NULL, PREC_NONE},
        [TOKEN_NULL] = {CompilerLiteral, NULL, PREC_NONE},
        [TOKEN_OR] = {NULL, CompilerOr, PREC_OR},
        [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
        [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
        [TOKEN_SUPER] = {CompilerSuper, NULL, PREC_NONE},
        [TOKEN_THIS] = {CompilerThis, NULL, PREC_NONE},
        [TOKEN_TRUE] = {CompilerLiteral, NULL, PREC_NONE},
        [TOKEN_MAYBE] = {CompilerLiteral, NULL, PREC_NONE},
        [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
        [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
        [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
        [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static void CompilerParsePrecedence(Precedence precedence) {
    CompilerAdvance();

    ParseFn prefixRule = CompilerGetRule(parser.previous.type)->prefix;

    if (prefixRule == NULL) {
        Error("Expected expression");
        return;
    }

    bool canAssign = (precedence <= PREC_ASSIGNMENT);
    prefixRule(canAssign);

    while (precedence <= CompilerGetRule(parser.current.type)->precedence) {
        CompilerAdvance();
        ParseFn infixRule = CompilerGetRule(parser.previous.type)->infix;

        if (infixRule == NULL) {
            CompilerPanic("Infix rule for token number %d was null.", parser.previous.type);
        }

        infixRule(canAssign);
    }

    if (canAssign && Match(TOKEN_ASSIGN)) {
        Error("Invalid assignment target");
    }
}

static ParseRule* CompilerGetRule(KTN_TokenType type) { return &rules[type]; }

KTN_ObjShiki* KTN_Compile(KTN_VM* vm, const char* source) {
    KTN_ScannerInit(source);

    parser.vm = vm;

    Compiler* compiler = (Compiler* )malloc(sizeof(Compiler));

    if (compiler == NULL) {
        fprintf(stderr, "[ERROR]: Compiler could not be allocated. Out of memory.");
        return NULL;
    }

    CompilerInit(compiler, TYPE_SCRIPT, false);

    parser.hadError = false;
    parser.panicMode = false;

    CompilerAdvance();

    while (!Match(TOKEN_EOF)) {
        CompilerDeclaration();
    }

    KTN_ObjShiki* function = CompilerEnd();

    free(compiler);

    return ((parser.hadError) ? NULL : function);
}

void KTN_CompilerMarkRoots() {
    Compiler* compiler = current;

    while (compiler != NULL) {
        KTN_MemoryMarkObject(parser.vm, (KTN_Object*)compiler->function);

        for (int i = 0; i < compiler->constBindingCount; i++) {
            KTN_MemoryMarkValue(parser.vm, compiler->constBindings[i].value);
        }

        for (int i = 0; i < compiler->tempValueCount; i++) {
            KTN_MemoryMarkValue(parser.vm, compiler->tempValues[i]);
        }

        compiler = compiler->enclosing;
    }
}
