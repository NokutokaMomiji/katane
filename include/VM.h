#ifndef KATANE_VM_H
#define KATANE_VM_H

#include "Object.h"
#include "Chunk.h"
#include "TypeDescriptor.h"
#include "Value.h"
#include "Table.h"
#include "Config.h"

#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
    KTN_ObjClosure* closure;
    KTN_ObjKata* owner;
    uint8_t* ip;
    KTN_Value* slots;
} KTN_CallFrame;

typedef enum {
    KTN_EF_WAITING,
    KTN_EF_IN_CATCH,
    KTN_EF_IN_FINALLY
} KTN_ErrorFrameState;

typedef enum {
    KTN_DEFERRED_NONE,
    KTN_DEFERRED_RETURN,
    KTN_DEFERRED_JUMP,
    KTN_DEFERRED_EXCEPTION
} KTN_DeferredAction;

typedef struct {
    KTN_CallFrame* frame;
    uint16_t offset;
    uint8_t* catchTarget;
    uint8_t* finallyTarget;

    KTN_ErrorFrameState state;
    KTN_DeferredAction deferredAction;

    KTN_Value deferredValue;
    uint8_t* deferredJumpTarget;
    int targetScopeDepth;

    KTN_Value* stackHead;
    KTN_Value value;
} KTN_ErrorFrame;

struct KTN_VM {
    KTN_CallFrame frames[FRAMES_MAX];
    KTN_CallFrame* currentFrame;
    int frameCount;

    KTN_Value stack[STACK_MAX];
    KTN_Value* stackTop;

    KTN_ErrorFrame* errors[ERRORS_MAX];
    int errorCount;

    KTN_Table strings;
    KTN_Table globals;
    KTN_Table modules;

    KTN_Table stringMethods;
    KTN_Table arrayMethods;
    KTN_Table mapMethods;
    KTN_Table fileMethods;
    KTN_Table bytesMethods;

    KTN_DescriptorSet typeDescriptors;
    KTN_Table globalTypes;

    KTN_Object* objects;
    int grayCount;
    int grayCapacity;
    KTN_Object** grayStack;
    KTN_Object** safeguardStack;
    KTN_ObjUpvalue* openUpvalues;
    KTN_ObjString* initString;

    KTN_Value finalResult;
    KTN_ObjKata* exceptionClass;
    KTN_ObjKata* stackTraceClass;
    KTN_Value pendingException;
    KTN_Value activeHandlerException;
    bool caughtException;
    bool hasPendingException;
    bool hasActiveHandlerException;
    char* rootFile;

    KTN_ObjKata* typeInt;
    KTN_ObjKata* typeFloat;
    KTN_ObjKata* typeBool;
    KTN_ObjKata* typeString;
    KTN_ObjKata* typeNull;

    /*
        KTN_ObjString* strToString;
        KTN_ObjString* strEquals;
        KTN_ObjString* strHashCode;
        KTN_ObjString* strCompare;

        KTN_ObjString* strName;
        KTN_ObjString* strOrdinal;
        KTN_ObjString* strValue;
        KTN_ObjString* strValues;
    */

    KTN_WellKnownNames wellKnownNames;

    size_t allocatedBytes;
    size_t nextCollection;

    bool kataneMode;
    bool showWarnings;
    bool shouldPrintBytecode;
    bool shouldExitAfterBytecode;

    char** stdArgs;
    int stdArgsCount;

    uint64_t id;
    KTN_VM* parentVM;
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} KTN_InterpretResultStatus;

typedef struct {
    KTN_InterpretResultStatus status;
    KTN_Value value;
} KTN_InterpretResult;

#define RUNTIME_ERROR(value) ((KTN_InterpretResult){INTERPRET_RUNTIME_ERROR, (KTN_Value)value})
#define COMPILE_ERROR(value) ((KTN_InterpretResult){INTERPRET_COMPILE_ERROR, (KTN_Value)value})
#define RUNTIME_OK(value) ((KTN_InterpretResult){INTERPRET_OK, (KTN_Value)value})

void VMInit(KTN_VM* vm);
void VMFree(KTN_VM* vm);

void KTN_VMPanic(KTN_VM* vm, const char* format, ...) __attribute__((noreturn));

KTN_InterpretResult KTN_Interpret(KTN_VM* vm, KTN_ObjModule* module, const char* source);
KTN_InterpretResult InterpretChunk(KTN_VM* vm, KTN_Chunk* chunk);

void Push(KTN_VM* vm, KTN_Value value);
KTN_Value Pop(KTN_VM* vm);
KTN_Value PopN(KTN_VM* vm, int n);
KTN_Value Peek(KTN_VM* vm, int distance);

void ErrorPush(KTN_VM* vm, KTN_ErrorFrame* frame);
KTN_ErrorFrame* ErrorPop(KTN_VM* vm);
KTN_ErrorFrame* ErrorPeek(KTN_VM* vm);

bool ThrowException(KTN_VM* vm, const char* type, bool isAssert, const char* format, ...);

static inline void ModuleAdd(KTN_VM* vm, KTN_ObjModule* module, char* name) {
    bool isKnown = true;

    if (name == NULL) {
        isKnown = false;
        name = module->name;
    }

    if (!isKnown) 
        TableSet(vm, &vm->modules, STRING_COPY(module->file), OBJECT_VALUE(module));

    KTN_Table* moduleTable = (vm->frameCount == 0) ? &vm->globals : &vm->currentFrame->closure->function->module->values;

    moduleTable = &vm->globals;

    TableSet(vm, moduleTable, STRING_COPY(name), OBJECT_VALUE(module));
}

#endif
