#include "HashMap.h"
#include "TypeDescriptor.h"
#include "Value.h"
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "Common.h"
#include "Compiler.h"
#include "Debug.h"
#include "Object.h"
#include "Memory.h"
#include "Array.h"
#include "Map.h"
#include "Exceptions.h"
#include "Native.h"
#include "Utilities.h"
#include "Utf8.h"
#include "Platform.h"
#include "Tutorial.h"
#include "VM.h"



// Forward declarations for helpers defined later in this file.
void registerModuleFile(KTN_VM* vm, KTN_ObjModule* module);
void registerRoot(KTN_VM* vm);

// RuntimeError returns true when the exception was caught by a try/catch frame
// (execution continues at the handler), false when uncaught (caller must return RUNTIME_ERROR).
#define KATANE_RUNTIME_ERROR(normal_fmt, katane_fmt, ...)   \
    ((vm)->kataneMode                                       \
        ? KTN_RuntimeError((vm), (katane_fmt), ##__VA_ARGS__)  \
        : KTN_RuntimeError((vm), (normal_fmt), ##__VA_ARGS__))

static const char* KTN_WellKnownNameText[KTN_NAME_COUNT] = {
    "init",
    "toString",
    "equals",
    "hashCode",
    "compare",
    "dispose",
    "suppressedErrors",
    "message",
    "stackTrace",
    "frames",
    "expected",
    "actual",
    "value",
    "values",
    "name",
    "type",
    "ordinal",
    "minimum",
    "maximum",
    "argumentName",
    "operation",
    "target",
    "propertyName",
    "key",
    "index",
    "moduleName",
    "iterator",
    "moveNext",
    "current",
    "keys",
    "entries",
    "chars",
    "bytes",
    "runes",
    "next",
    "done"
};

// In case we change things up later, to make sure they are the same.
_Static_assert(
    sizeof(KTN_WellKnownNameText) / sizeof(KTN_WellKnownNameText[0]) == KTN_NAME_COUNT,
    "Well-known name text table doesn't match enum size."
);

void KTN_WellKnownNamesInit(KTN_VM* vm, KTN_WellKnownNames* names) {
    for (int i = 0; i < KTN_NAME_COUNT; i++) {
        names->values[i] = STRING_COPY(vm, KTN_WellKnownNameText[i], (int)strlen(KTN_WellKnownNameText[i]));
    }
}

KTN_ObjString* KTN_GetWellKnownName(KTN_VM* vm, KTN_WellKnownName id) {
    return vm->wellKnownNames.values[id];
}

void KTN_WellKnownNamesMark(KTN_VM* vm, KTN_WellKnownNames* names);

void KTN_ResetStack(KTN_VM* vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
}

static KTN_ObjUpvalue* CaptureUpvalue(KTN_VM* vm, KTN_Value* local) {
    KTN_ObjUpvalue* previousUpvalue = NULL;
    KTN_ObjUpvalue* Upvalue = vm->openUpvalues;

    while (Upvalue != NULL && Upvalue->location > local) {
        previousUpvalue = Upvalue;
        Upvalue = Upvalue->next;
    }

    if (Upvalue != NULL && Upvalue->location == local)
        return Upvalue;

    KTN_ObjUpvalue* createdUpvalue = UpvalueNew(vm, local);

    createdUpvalue->next = Upvalue;
    if (previousUpvalue == NULL) {
        vm->openUpvalues = createdUpvalue;
    } else {
        previousUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

void KTN_CloseUpvalues(KTN_VM* vm, KTN_Value* last) {
    while (vm->openUpvalues != NULL && vm->openUpvalues->location >= last) {
        KTN_ObjUpvalue* Upvalue = vm->openUpvalues;
        Upvalue->closed = *Upvalue->location;
        Upvalue->location = &Upvalue->closed;
        vm->openUpvalues = Upvalue->next;
    }
}

static void PrintStackTrace(KTN_VM* vm) {
    if (vm->kataneMode)
        fprintf(stderr, "Ohoho~! Someone messed up~! Time for a stacktrace (most recent call last)~!\n");
    else
        fprintf(stderr, "Exception Stacktrace (most recent call last):\n");

    int cycleStart = -1;
    int cycleLength = -1;
    int cycleRepetitions = -1;

    for (int frameIndex = 0; frameIndex < vm->frameCount; frameIndex++) {
        KTN_CallFrame* frame = &vm->frames[frameIndex];
        bool foundRecursiveness = false;

        for (int internalIndex = 0; internalIndex < frameIndex; internalIndex++) {
            KTN_CallFrame* internalFrame = &vm->frames[internalIndex];

            if (
                frame->closure->function == internalFrame->closure->function &&
                (frame->ip - frame->closure->function->chunk.code) == (internalFrame->ip - internalFrame->closure->function->chunk.code)
            ) {
                cycleStart = internalIndex;
                cycleLength = frameIndex - internalIndex;
                cycleRepetitions = (vm->frameCount - frameIndex) / cycleLength;

                foundRecursiveness = true;
                break;
            }
        }

        if (foundRecursiveness) break;
    }

    for (int frameIndex = 0; frameIndex < vm->frameCount; frameIndex++) {
        KTN_CallFrame* frame = &vm->frames[frameIndex];
        KTN_ObjShiki* function = frame->closure->function;
        size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);

        int line = KTN_ChunkGetLine(&function->chunk, (int)instruction);
        char* content = KTN_ChunkGetSource(&function->chunk, (int)instruction);

        if (frameIndex == cycleStart + cycleLength) {
            if (cycleLength == 1)
                fprintf(stderr, "[Previous line repeated " COLOR_RED "%d" COLOR_RESET " more times.]\n", cycleRepetitions);
            else
                fprintf(stderr, "[Cycle of " COLOR_CYAN "%d" COLOR_RESET " frames repeated " COLOR_RED "%d" COLOR_RESET " more times.]\n", cycleLength, cycleRepetitions);
            return;
        }

        fprintf(stderr, COLOR_MAGENTA "   %4d " COLOR_RESET "| ", line);
        if (function->name == NULL)
            fprintf(stderr, COLOR_CYAN "<script>" COLOR_RESET);
        else
            fprintf(stderr, COLOR_CYAN "%s()" COLOR_RESET, function->name->chars);
        fprintf(stderr, " | %s\n", content);
    }
}

bool KTN_ThrowValue(KTN_VM* vm, KTN_ObjInstance* exception, bool buildTrace) {
    if (buildTrace) {
        Push(vm, OBJECT_VALUE(exception));

        KTN_Value stackTraceValue = BuildStackTraceObject(vm, NULL);
        Push(vm, stackTraceValue);

        TableSet(vm, &exception->properties, KTN_NAME(vm, KTN_NAME_STACK_TRACE), stackTraceValue);
        TableSet(vm, &exception->properties, KTN_NAME(vm, KTN_NAME_TYPE), OBJECT_VALUE(exception->kata));

        Pop(vm);
        Pop(vm);
    }

    while (true) {
        if (vm->errorCount == 0) {
            fflush(stdout);
            PrintStackTrace(vm);
            fprintf(stderr, "\n" COLOR_RED "%s" COLOR_RESET, exception->kata->className->chars);

            KTN_Value message;

            if (TableGet(&exception->properties, KTN_NAME(vm, KTN_NAME_MESSAGE), &message) && IS_STRING(message)) {
                char* messageChars = AS_CSTRING(message);
                if (messageChars[0] != '\0')
                    fprintf(stderr, ": %s", messageChars);
            }

            fputs("\n", stderr);

            if (vm->frameCount > 0) {
                KTN_CallFrame* frame = &vm->frames[vm->frameCount - 1];
                KTN_ObjShiki* function = frame->closure->function;
                size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);
                int line = KTN_ChunkGetLine(&function->chunk, (int)instruction);
                char* content = KTN_ChunkGetSource(&function->chunk, (int)instruction);

                if (function->name == NULL)
                    fprintf(stderr, "In " COLOR_CYAN "<script>" COLOR_RESET ":\n");
                else
                    fprintf(stderr, "In " COLOR_CYAN "<%s()>" COLOR_RESET ":\n", function->name->chars);

                fprintf(stderr, COLOR_MAGENTA "   %4d" COLOR_RESET " | %s\n", line, content);
            }

            KTN_ResetStack(vm);
            return false;
        }

        KTN_ErrorFrame* errorFrame = ErrorPeek(vm);
        KTN_CallFrame* handlerCallFrame = errorFrame->frame;
        KTN_Value* handlerStackHead = errorFrame->stackHead;

        switch (errorFrame->state) {
            case KTN_EF_WAITING: {
                errorFrame->state = KTN_EF_IN_CATCH;

                vm->frameCount = (int)(handlerCallFrame - vm->frames) + 1;
                vm->currentFrame = handlerCallFrame;
                vm->currentFrame->ip = errorFrame->catchTarget;
                vm->stackTop = handlerStackHead;

                Push(vm, OBJECT_VALUE(exception));

                vm->caughtException = true;
                return true;
            }

            case KTN_EF_IN_FINALLY: {
                ErrorPop(vm);

                if (errorFrame->deferredAction == KTN_DEFERRED_EXCEPTION) {
                    KTN_ObjInstance* original = AS_INSTANCE(errorFrame->deferredValue);
                    KTN_Value suppressed;

                    if (!TableGet(&original->properties, KTN_NAME(vm, KTN_NAME_SUPPRESSED_ERRORS), &suppressed)) {
                        KTN_ObjArray* array = ArrayNew(vm);
                        suppressed = OBJECT_VALUE(array);

                        TableSet(vm, &original->properties, KTN_NAME(vm, KTN_NAME_SUPPRESSED_ERRORS), suppressed);
                    }

                    ValueArrayWrite(vm, &AS_ARRAY(suppressed)->items, OBJECT_VALUE(exception));
                    free(errorFrame);
                    exception = original;
                    continue;
                }

                free(errorFrame);
                continue;
            }

            default:
                break;
        }

        vm->caughtException = false;

        if (errorFrame->finallyTarget != NULL) {
            errorFrame->state = KTN_EF_IN_FINALLY;
            errorFrame->deferredAction = KTN_DEFERRED_EXCEPTION;
            errorFrame->deferredValue = OBJECT_VALUE(exception);
            errorFrame->targetScopeDepth = 0;

            vm->frameCount = (int)(handlerCallFrame - vm->frames) + 1;
            vm->currentFrame = handlerCallFrame;
            KTN_CloseUpvalues(vm, errorFrame->stackHead);
            vm->currentFrame->ip = errorFrame->finallyTarget;
            vm->stackTop = handlerStackHead;

            return true;
        }

        ErrorPop(vm);
        free(errorFrame);
    }
}

bool KTN_RuntimeError(KTN_VM* vm, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char* message = NULL;
    int length = vasprintf(&message, format, args);
    va_end(args);

    Push(vm, OBJECT_VALUE(StringTake(vm, message, length, true)));
    KTN_ObjInstance* exception = KTN_ExceptionCreate(vm, "RuntimeError", AS_STRING(Peek(vm, 0)));
    Pop(vm);

    return KTN_ThrowValue(vm, exception, true);
}

bool KTN_ThrowException(KTN_VM* vm, const char* type, bool isAssert, const char* format, ...) {
    va_list args;
    va_start(args, format);

    char* message = NULL;
    int length = vasprintf(&message, format, args);

    va_end(args);

    const char* exceptionType = (isAssert) ? "AssertionError" : type;
    KTN_ObjInstance* exception = KTN_ExceptionCreate(vm, exceptionType, STRING_TAKE(vm, message, length));

    return KTN_ThrowValue(vm, exception, true);
}

bool KTN_ThrowTypeError(KTN_VM* vm, const char* expectedBuffer, const char* actualName, const char* context) {
    char msgBuffer[512];
    snprintf(msgBuffer, sizeof(msgBuffer), "%s: expected %s, got %s.", context, expectedBuffer, actualName);
    KTN_ObjString* message = STRING_COPY(vm, msgBuffer, (int)strlen(msgBuffer));
    Push(vm, OBJECT_VALUE(message));
    KTN_ObjInstance* exception = KTN_ExceptionCreate(vm, "TypeError", message);
    
    Pop(vm);
    return KTN_ThrowValue(vm, exception, true);
}

// Return the cached primitive ObjClass* for a value, or NULL for object types.
// Used for O(1) primitive matching without any table lookup.
static KTN_ObjKata* PrimitiveClassOf(KTN_VM* vm, KTN_Value value) {
    if (IS_INT(value))      return vm->typeInt;
    if (IS_DOUBLE(value))   return vm->typeFloat;
    if (IS_BOOL(value))     return vm->typeBool;
    if (IS_NULL(value))     return vm->typeNull;
    if (IS_STRING(value))   return vm->typeString;
    if (IS_ARRAY(value))    return vm->typeArray;
    if (IS_MAP(value))      return vm->typeMap;

    return NULL;
}
static void DefineNative(KTN_VM* vm, const char* name, NativeFnEx function, const char* signature, const char* docs) {
    Push(vm, OBJECT_VALUE(STRING_COPY(vm, name, (int)strlen(name))));
    Push(vm, OBJECT_VALUE(NativeNew(vm, function, name, signature, docs)));
    TableSet(vm, &vm->globals, AS_STRING(Peek(vm, 1)), Peek(vm, 0));
    PopN(vm, 2);
}

static DECLARE_NATIVE(Tutorial) {
    EXPECT_ARGC(0)
    KTN_TutorialRun(vm);
    RETURN_NULL;
}

static DECLARE_NATIVE(Exit) {
    if (ARGUMENT_COUNT == 0)
        exit(0);

    EXPECT_ARGC(1);
    EXPECT_ARG_INT(0);

    if (ARGUMENT_COUNT == 1)
        exit((int)AS_INT(ARG(0)));

    RETURN_ERROR(NULL_VALUE);
}

static DECLARE_NATIVE(Input) {
    if (ARGUMENT_COUNT > 0) {
        EXPECT_ARGC(1);
        EXPECT_ARG_STRING(0);

        printf("%s", AS_CSTRING(ARG(0)));
    }

    char input[2048];
    if (!fgets(input, sizeof(input), stdin))
        input[0] = '\0';
    input[strcspn(input, "\n")] = '\0';

    return (KTN_NativeResult){true, OBJECT_VALUE(STRING_COPY(vm, input, (int)strlen(input)))};
}

static DECLARE_NATIVE(Clock) {
    return (KTN_NativeResult){true, DOUBLE_VALUE((double)clock() / CLOCKS_PER_SEC)};
}

static DECLARE_NATIVE(System) {
    EXPECT_ARGC(1);
    EXPECT_ARG_STRING(0);

    return (KTN_NativeResult){true, INT_VALUE(system(AS_CSTRING(ARG(0))))};
}

static DECLARE_NATIVE(ReadFile) {
    EXPECT_ARGC(1);
    EXPECT_ARG_STRING(0);

    size_t size = 0;
    char* filePath = AS_CSTRING(ARG(0));
    char* file = KTN_FileRead(filePath, &size);
    if (!file) {
        KTN_RuntimeError(vm, "Couldn't read file \"%s\".\n", filePath);
        RETURN_ERROR(NULL_VALUE);
    }

    KTN_ObjString* fileString = StringTake(vm, file, size, false);

    RETURN_VALUE(OBJECT_VALUE(fileString));
}

static DECLARE_NATIVE(Length) {
    if (ARGUMENT_COUNT != 1) {
        (void)KATANE_RUNTIME_ERROR("length expected 1 argument, got %d.", COLOR_MAGENTA "Eep~" COLOR_RESET " len wants exactly one argument... " COLOR_CYAN "%d" COLOR_RESET " is too many tails for me to count~ ♡", ARGUMENT_COUNT);
        RETURN_ERROR(NULL_VALUE);
    }

    if (IS_STRING(ARG(0)))
        return (KTN_NativeResult){true, INT_VALUE(AS_STRING(ARG(0))->charLength)};

    if (IS_ARRAY(ARG(0)))
        return (KTN_NativeResult){true, INT_VALUE(AS_ARRAY(ARG(0))->items.count)};

    KTN_RuntimeError(vm, "length expected a string or array.");
    return (KTN_NativeResult){false, NULL_VALUE};
}

static DECLARE_NATIVE(Credits) {
    EXPECT_MAX_ARGC(0);

    printf(COLOR_CUSTOM "Katane" COLOR_RESET " %s ", KATANE_VERSION);
    printf("(%s, %s) [%s] on %s\n", __DATE__, __TIME__, COMPILER, COMPILED_OS);
    printf(KATANE_COPYRIGHT "\n\n");
    printf("Thank you for using Katane! ♡ \n");
    RETURN_NULL;
}

static DECLARE_NATIVE(Help) {
    EXPECT_MAX_ARGC(1);

    if (ARGUMENT_COUNT == 0) {
        printf(COLOR_MAGENTA "✦ Katane Interactive Help System ✦" COLOR_RESET "\n"
               "   Ara ara~ You called me without offering anything?\n"
               "   How cheeky~ Let this cute fox guide you properly ♡\n\n"

               "   " COLOR_CYAN "help(shiki)" COLOR_RESET "       Show function signature and docs.\n"
               "   " COLOR_CYAN "help(kata)" COLOR_RESET "        Show class information.\n"
               "   " COLOR_CYAN "help(mochi)" COLOR_RESET "       Show info about any value.\n"
               "   " COLOR_CYAN "help(module)" COLOR_RESET "     List module contents.\n\n"

               "   " COLOR_CYAN "Examples:" COLOR_RESET "\n"
               "       help(print)          Peek at a built-in shiki.\n"
               "       help(MyKata)         Explore a class.\n"
               "       help(42)             See info about a number.\n"
               "       help(\"hello\")      Learn about strings.\n\n"

               COLOR_MAGENTA "   Be nice to me and I'll be extra helpful~ " COLOR_RESET "\n");
        RETURN_NULL;
    }

    KTN_Value value = ARG(0);

    if (IS_NATIVE(value)) {
        KTN_ObjNative* native = AS_NATIVE(value);
        printf(COLOR_CYAN "shiki " COLOR_RESET);

        if (native->signature)
            printf("%s\n", native->signature);
        else if (native->name)
            printf("%s(...)\n", native->name);
        else
            printf("anonymous(...)\n");

        if (native->docs != NULL && strlen(native->docs) > 0)
            printf("   %s\n", native->docs);
        else
            printf("   " COLOR_MAGENTA "No documentation yet… I'll write something special for you later~ ♡\n" COLOR_RESET);

        RETURN_NULL;
    }

    if (IS_CLOSURE(value)) {
        KTN_ObjShiki* fn = AS_CLOSURE(value)->function;
        const char* sig = (fn->signature && fn->signature->display)
                ? fn->signature->display->chars
                : NULL;
        const char* name = fn->name ? fn->name->chars : "<anonymous>";

        if (sig)
            printf(COLOR_CYAN "shiki " COLOR_RESET "%s\n", sig);
        else
            printf(COLOR_CYAN "shiki " COLOR_RESET "%s(...)\n", name);

        if (fn->docs && fn->docs->length > 0)
            printf("   %s\n", fn->docs->chars);
        else
            printf("   " COLOR_MAGENTA "No documentation attached~ ♡\n" COLOR_RESET);

        RETURN_NULL;
    }

    if (IS_CLASS(value)) {
        KTN_ObjKata* k = AS_CLASS(value);

        printf(COLOR_CYAN "kata " COLOR_RESET "%s\n", k->className->chars);

        if (k->docs && k->docs->length > 0)
            printf("   %s\n", k->docs->chars);
        else
            printf("   " COLOR_MAGENTA "No documentation available for this kata yet~ ♡\n" COLOR_RESET);

        // Constructor
        if (IS_NATIVE(k->constructor)) {
            KTN_ObjNative* ctor = AS_NATIVE(k->constructor);
            printf("   " COLOR_CYAN "constructor:" COLOR_RESET " ");
            printf("%s\n", ctor->signature ? ctor->signature : "...");
        } else if (IS_CLOSURE(k->constructor)) {
            printf("   " COLOR_CYAN "constructor:" COLOR_RESET " %s(...)\n", k->className->chars);
        }

        printf("\n");

        // TODO: add proper iteration on tables.
        for (int i = 0; i < k->properties.capacity; i++) {
            KTN_TableEntry* entry = &k->properties.entries[i];

            if (entry == NULL) continue;
            if (entry->Key == NULL) continue;

            printf("   " COLOR_MAGENTA "mochi" COLOR_RESET " %s", entry->Key->chars);

            if (!IS_NULL(entry->value)) {
                printf(" = ");
                ValuePrint(entry->value);
            }

            printf(";\n");
        }

        for (int i = 0; i < k->staticProperties.capacity; i++) {
            KTN_TableEntry* entry = &k->staticProperties.entries[i];

            if (entry == NULL) continue;
            if (entry->Key == NULL) continue;

            printf("   " COLOR_MAGENTA "static mochi" COLOR_RESET " %s", entry->Key->chars);

            if (!IS_NULL(entry->value)) {
                printf(" = ");
                ValuePrint(entry->value);
            }

            printf(";\n");
        }

        for (int i = 0; i < k->methods.capacity; i++) {
            KTN_TableEntry* entry = &k->methods.entries[i];

            if (entry == NULL) continue;
            if (entry->Key == NULL) continue;
            if (IS_NULL(entry->value)) continue;

            if (IS_ACCESSOR(entry->value)) {
                KTN_ObjAccessor* accessor = AS_ACCESSOR(entry->value);

                if (IS_CLOSURE(accessor->getter)) {
                    KTN_ObjClosure* getter = AS_CLOSURE(accessor->getter);

                    if (getter->function->signature != NULL && getter->function->signature->display != NULL)
                        printf("   " COLOR_CYAN "get" COLOR_RESET " %s\n", getter->function->signature->display->chars);
                }

                if (IS_CLOSURE(accessor->setter)) {
                    KTN_ObjClosure* setter = AS_CLOSURE(accessor->setter);
                    
                    if (setter->function->signature != NULL && setter->function->signature->display != NULL)
                        printf("   " COLOR_CYAN "set" COLOR_RESET " %s\n", setter->function->signature->display->chars);
                }

                continue;
            }

            if (!IS_CLOSURE(entry->value)) continue;

            KTN_ObjSignature* signature = AS_CLOSURE(entry->value)->function->signature;

            if (signature == NULL || signature->display == NULL) {
                printf("No signature...");
                continue;
            }

            printf("   " COLOR_CYAN "shiki" COLOR_RESET " %s\n", signature->display->chars);
        }

        RETURN_NULL;
    }

    KTN_ObjKata* primitiveKata = PrimitiveClassOf(vm, value);
    const char* typeName = KTN_ValueTypeName(value);

    if (primitiveKata) {
        printf(COLOR_CYAN "mochi " COLOR_RESET "%s\n", typeName);

        if (primitiveKata->docs && primitiveKata->docs->length > 0)
            printf("   %s\n", primitiveKata->docs->chars);
        else
            printf("   " COLOR_MAGENTA "A built-in primitive type~ Soft and simple.\n" COLOR_RESET);

        printf("   Value: ");
        ObjectRepr(value);
        printf("\n");

        RETURN_NULL;
    }

    printf(COLOR_CYAN "mochi " COLOR_RESET);
    ObjectRepr(value);
    printf("\n");

    if (IS_ARRAY(value)) {
        printf("   A playful array with %d tails inside~ ♡\n", AS_ARRAY(value)->items.count);
    }
    else if (IS_MAP(value)) {
        printf("   A mysterious map holding %d little secrets~\n", AS_MAP(value)->map.count);
    }
    else if (IS_INSTANCE(value)) {
        KTN_ObjKata* k = AS_INSTANCE(value)->kata;
        printf("   Instance of kata " COLOR_CYAN "%s" COLOR_RESET "\n", k->className->chars);

        if (k->docs && k->docs->length > 0)
            printf("   %s\n", k->docs->chars);
        else
            printf("   " COLOR_MAGENTA "No documentation available for this kata yet~ ♡\n" COLOR_RESET);

        // Constructor
        if (IS_NATIVE(k->constructor)) {
            KTN_ObjNative* ctor = AS_NATIVE(k->constructor);
            printf("   " COLOR_CYAN "constructor:" COLOR_RESET " ");
            printf("%s\n", ctor->signature ? ctor->signature : "...");
        } else if (IS_CLOSURE(k->constructor)) {
            printf("   " COLOR_CYAN "constructor:" COLOR_RESET " %s(...)\n", k->className->chars);
        }
    }
    else {
        printf("   " COLOR_MAGENTA "No special documentation available for this type yet~ " COLOR_RESET "ฅ^•ﻌ•^ฅ\n");
    }

    RETURN_NULL;
}

static DECLARE_NATIVE(MapGetKeys) {
    EXPECT_ARGC(1);
    EXPECT_ARG_MAP(0);

    KTN_ObjMap* map = AS_MAP(ARG(0));
    KTN_Value array;
    if (!MapGetKeys(vm, map, &array)) {
        KTN_RuntimeError(vm, "Something went wrong.");
        RETURN_ERROR(NULL_VALUE);
    }

    RETURN_VALUE(array);
}

static DECLARE_NATIVE(MapGetValues) {
    EXPECT_ARGC(1);
    EXPECT_ARG_MAP(0);

    KTN_ObjMap* map = AS_MAP(ARG(0));
    KTN_Value array;
    if (!MapGetValues(vm, map, &array)) {
        KTN_RuntimeError(vm, "Something went wrong.");
        RETURN_ERROR(NULL_VALUE);
    }

    RETURN_VALUE(array);
}

static DECLARE_NATIVE(Stringify) {
    EXPECT_ARGC(1);

    KTN_ObjString* str = ObjectToString(vm, ARG(0));

    if (str == NULL) {
        KTN_RuntimeError(vm, "Something went wrong.");
        RETURN_ERROR(NULL_VALUE);
    }

    RETURN_VALUE(OBJECT_VALUE(str));
}

static DECLARE_NATIVE(RemoveFile) {
    EXPECT_ARGC(1);
    EXPECT_ARG_STRING(0);

    const char* str = AS_CSTRING(ARG(0));

    int result = remove(str);

    RETURN_VALUE(INT_VALUE(result));
}

static void PrintCallFrame(KTN_VM* vm, const KTN_CallFrame* frame) {
    KTN_ObjShiki* function = frame->closure->function;
    size_t ipOffset = (size_t)(frame->ip - function->chunk.code);
    int slotIndex = (int)(frame->slots - vm->stack);

    printf(
        "--- [CallFrame @ stack[" COLOR_CYAN "%d" COLOR_RESET "]] ---\n"
        "Function : \"%s\"\n"
        "Arity    : %d\n"
        "IP Offset: %zu\n",
        slotIndex,
        (function->name) ? function->name->chars : "<script>",
        function->arity,
        ipOffset
    );

    if (function->arity > 0) {
        printf("Locals / Args:");
        for (int i = 0; i < function->arity; i++) {
            printf(" ");
            ObjectRepr(frame->slots[i]);
        }
        printf("\n");
    }
    printf("===========================\n");
}

static bool CanAccessMember(KTN_VM* vm, KTN_ObjKata* kata, KTN_ObjString* name, KTN_Value* isHidden) {
    if (!TableGet(&kata->privateMembers, name, isHidden)) {
        return true;
    }

    return (vm->currentFrame->owner == kata);
}

typedef enum {
    FINAL_WRITE_ALLOWED,
    FINAL_WRITE_BLOCKED,
    FINAL_WRITE_FAILED
} FinalWriteStatus;

static FinalWriteStatus CanAssignFinalProperty(KTN_VM* vm, KTN_Value receiver, KTN_Value currentValue, uint8_t flags, KTN_ObjString* name, bool isStatic) {
    if (!(flags & KTN_TABLE_ENTRY_FINAL))
        return FINAL_WRITE_ALLOWED;

    if (IS_EMPTY(currentValue)) {
        if (isStatic)
            return FINAL_WRITE_ALLOWED;

        if (vm->currentFrame != NULL &&
            vm->currentFrame->closure->function->type == TYPE_CONSTRUCTOR &&
            ValuesEqual(receiver, vm->currentFrame->slots[0])) {
            return FINAL_WRITE_ALLOWED;
        }
    }

    if (!ThrowException(vm, "AccessError", false, "Cannot assign to final property \"%s\".", name->chars))
        return FINAL_WRITE_FAILED;

    return FINAL_WRITE_BLOCKED;
}

void VMInit(KTN_VM* vm) {
    KTN_ResetStack(vm);
    srand(time(NULL));
    vm->parentVM = NULL;
    KTN_ResetStack(vm);

    vm->kataneMode = true;

    vm->id = 0;
    vm->objects = NULL;
    vm->exceptionClass = NULL;
    vm->stackTraceClass = NULL;
    vm->caughtException = false;
    vm->currentFrame = NULL;
    vm->rootFile = NULL;

    vm->typeInt = NULL;
    vm->typeFloat = NULL;
    vm->typeBool = NULL;
    vm->typeString = NULL;
    vm->typeNull = NULL;
    vm->typeArray = NULL;
    vm->typeMap = NULL;

    vm->showWarnings = false;
    vm->shouldPrintBytecode = false;
    vm->shouldExitAfterBytecode = false;

    vm->stdArgs = NULL;
    vm->stdArgsCount = 0;

    vm->finalResult = NULL_VALUE;

    vm->allocatedBytes = 0;
    vm->nextCollection = 1024 * 1024;

    vm->grayCapacity = 0;
    vm->grayCount = 0;
    vm->grayStack = NULL;

    vm->errorCount = 0;
    for (int i = 0; i < MAX_ERRORS; i++) vm->errors[i] = NULL;

    // As you can guess, we allocate a small safeguard piece of memory to make sure the GC can run.
    vm->safeguardStack = malloc(sizeof(KTN_Object*) * 4);
    if (vm->safeguardStack == NULL)
        fprintf(stderr, "[ERROR]: Failed to allocate safeguard stack.\n");

    TableInit(&vm->strings);
    TableInit(&vm->globals);
    TableInit(&vm->modules);

    KTN_WellKnownNamesInit(vm, &vm->wellKnownNames);

    KTN_DescriptorSetInit(&vm->typeDescriptors);
    TableInit(&vm->compilerState.globalTypes);
    TableInit(&vm->compilerState.declaredGlobals);

    vm->initString = NULL;

    DefineNative(vm, "clock", GET_NATIVE(Clock), "clock()", "Returns the current timestamp.");
    DefineNative(vm, "input", GET_NATIVE(Input), "input(message: String = "")", "Gets input from stdin, showing an optional prompt message.");
    DefineNative(vm, "exit", GET_NATIVE(Exit), "exit(exitCode: Int = 0)", "Exits the program with the given exit code.");
    DefineNative(vm, "system", GET_NATIVE(System), "system(command: String)", "Runs the given command in the shell.");
    DefineNative(vm, "help", GET_NATIVE(Help), "help(value?)", "Print documentation for a function, class, or module.");
    DefineNative(vm, "readFile", GET_NATIVE(ReadFile), "readFile(filename: String): String", "Reads a file and returns its contents.");
    DefineNative(vm, "length", GET_NATIVE(Length), "length(value: String | Array): Int", "Returns the length of a string or array.");
    DefineNative(vm, "tutorial", GET_NATIVE(Tutorial), "tutorial()", "Start the interactive Katane tutorial.");
    DefineNative(vm, "credits", GET_NATIVE(Credits), "credits()", "Credits of everyone who has worked in the Katane Programming Language!");
    DefineNative(vm, "mapGetKeys", GET_NATIVE(MapGetKeys), "mapGetKeys(map: Map)", "Returns the keys of the map as a list.");
    DefineNative(vm, "mapGetValues", GET_NATIVE(MapGetValues), "mapGetValues(map: Map)", "Returns a list containing all of the values in the map.");
    DefineNative(vm, "stringify", GET_NATIVE(Stringify), "stringify(value)", "Converts any object into its string representation.");
}

void VMFree(KTN_VM* vm) {
    if (vm->id == 0) {
        TableFree(vm, &vm->modules);
        TableFree(vm, &vm->globals);

        KTN_DescriptorSetFree(vm, &vm->typeDescriptors);
        TableFree(vm, &vm->compilerState.globalTypes);
        TableFree(vm, &vm->compilerState.declaredGlobals);
    }

    TableFree(vm, &vm->strings);
    vm->initString = NULL;
    KTN_MemoryFreeObjects(vm);

    for (int i = 0; i < vm->errorCount; ++i) {
        if (vm->errors[i] == NULL) continue;

        free(vm->errors[i]);
    }

    free(vm);
}

inline void Push(KTN_VM* vm, KTN_Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

inline KTN_Value Pop(KTN_VM* vm) {
    //printf("Pop called.\n");
    vm->stackTop--;
    return *vm->stackTop;
}

inline KTN_Value PopN(KTN_VM* vm, int n) {
    //printf("PopN called with n: %d.\n", n);
    vm->stackTop -= n;
    return *vm->stackTop;
}

inline KTN_Value Peek(KTN_VM* vm, int distance) {
    return vm->stackTop[-1 - distance];
}

inline void ErrorPush(KTN_VM* vm, KTN_ErrorFrame* frame) {
    if (vm->errorCount >= MAX_ERRORS) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Catch frame stack overflow.\n");
        exit(11);
        return;
    }

    vm->errors[vm->errorCount++] = frame;
}

inline KTN_ErrorFrame* ErrorPop(KTN_VM* vm) {
    KTN_ErrorFrame* error = vm->errors[--vm->errorCount];
    error->frame = NULL;
    error->stackHead = NULL;
    error->offset = 0;
    return error;
}

inline KTN_ErrorFrame* ErrorPeek(KTN_VM* vm) {
    return vm->errors[vm->errorCount - 1];
}

static bool Call(KTN_VM* vm, KTN_ObjClosure* closure, int argumentCount, KTN_ObjKata* owner) {
    if (argumentCount != closure->function->arity) {
        if (!KATANE_RUNTIME_ERROR("Expected %d arguments but got %d instead.", COLOR_MAGENTA "Ara ara~" COLOR_RESET " I was craving " COLOR_CYAN "%d" COLOR_RESET " sweet arguments to spoil me with... but you only brought " COLOR_CYAN "%d" COLOR_RESET "? How disappointing, darling~ ♡", closure->function->arity, argumentCount)) return false;
        return true;
    }

    if (vm->frameCount == MAX_FRAMES) {
        if (!KATANE_RUNTIME_ERROR("Stack Overflow. Limit is %d.", COLOR_MAGENTA "Kyaa~" COLOR_RESET " My fluffy tail can only wrap around " COLOR_CYAN "%d" COLOR_RESET " frames... you're pushing me way too deep, naughty~! 🦊", MAX_FRAMES)) return false;
        return true;
    }

    KTN_CallFrame* frame = &vm->frames[vm->frameCount++];
    frame->owner = owner;
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    frame->slots = vm->stackTop - argumentCount - 1;
#ifdef DEBUG_PRINT_CODE
    PrintCallFrame(vm, frame);
#endif
    return true;
}

static bool CallValue(KTN_VM* vm, KTN_Value callee, int argumentCount) {
    if (IS_OBJECT(callee)) {
        switch (OBJECT_TYPE(callee)) {
            case OBJ_CLASS: {
                KTN_ObjKata* kata = AS_CLASS(callee);
                vm->stackTop[-argumentCount - 1] = OBJECT_VALUE(InstanceNew(vm, kata));
                if (IS_CLOSURE(kata->constructor)) {
                    return Call(vm, AS_CLOSURE(kata->constructor), argumentCount, kata);
                } else if (IS_NATIVE(kata->constructor)) {
                    KTN_CallArgs callArgs = {
                        .positional = vm->stackTop - argumentCount - 1,
                        .positionalCount = argumentCount + 1,
                        .variadic = NULL,
                        .named = NULL,
                        .namedCount = 0
                    };
                    NativeFnEx constructorFn = AS_NATIVE(kata->constructor)->function;
                    KTN_NativeResult result = constructorFn(vm, callArgs);

                    if (!result.success) return false;

                    vm->stackTop -= argumentCount;
                    return true;
                } else if (argumentCount != 0) {
                    if (!KATANE_RUNTIME_ERROR(
                            "Constructor expected 0 arguments but got %d.",
                            COLOR_MAGENTA "Ara ara~" COLOR_RESET " Constructors shouldn't take arguments, yet we have " COLOR_CYAN "%d" COLOR_RESET "!",
                            argumentCount))
                        return false;
                    return true;
                }
                return true;
            }
            case OBJ_NATIVE: {
                KTN_CallArgs callArgs = {
                    .positional = vm->stackTop - argumentCount,
                    .positionalCount = argumentCount,
                    .variadic = NULL,
                    .named = NULL,
                    .namedCount = 0
                };

                NativeFnEx native = AS_NATIVE(callee)->function;
                KTN_NativeResult result = native(vm, callArgs);

                if (result.success) {
                    vm->stackTop -= argumentCount + 1;
                    Push(vm, result.value);
                }

                return result.success;
            }
            case OBJ_CLOSURE: {
                KTN_ObjClosure* closure = AS_CLOSURE(callee);
                return Call(vm, closure, argumentCount, closure->owner);
            }

            case OBJ_BOUND_METHOD: {
                KTN_ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm->stackTop[-argumentCount - 1] = bound->receiver;
                return Call(vm, bound->method, argumentCount, bound->owner);
            }
            default:
                break;
        }
    }
    if (!KATANE_RUNTIME_ERROR("Can only call functions and classes.", COLOR_MAGENTA "Nyaa~" COLOR_RESET " Only functions and classes are callable~!")) return false;
    return true;
}

static bool InvokeFromClass(KTN_VM* vm, KTN_ObjKata* kata, KTN_ObjString* name, int argumentCount) {
    if (strcmp(name->chars, kata->className->chars) == 0) {
        if (IS_CLOSURE(kata->constructor))
            return Call(vm, AS_CLOSURE(kata->constructor), argumentCount, kata);

        if (!KTN_RuntimeError(vm, "Class \"%s\" has no constructor.", kata->className->chars))
            return false;
        return true;
    }

    KTN_Value method;
    if (!TableGet(&kata->methods, name, &method)) {
        if (!KTN_RuntimeError(vm, "\"%s\" object has no method \"%s\".", kata->className->chars, name->chars))
            return false;
        return true;
    }

    KTN_Value isHidden;
    if (!CanAccessMember(vm, kata, name, &isHidden)) {
        if (AS_BOOL(isHidden)) {
            if (!ThrowException(vm, "PropertyError", false, "\"%s\" object has no member \"%s\".", kata->className->chars, name->chars))
                return false;
            return true;
        }

        if (!ThrowException(vm, "PropertyError", false, "\"%s\" object has no method \"%s\".", kata->className->chars, name->chars))
            return false;
        return true;
    }

    if (IS_NATIVE(method)) {
        KTN_CallArgs callArgs = {
            .positional = vm->stackTop - argumentCount - 1,
            .positionalCount = argumentCount + 1,
            .variadic = NULL,
            .named = NULL,
            .namedCount = 0
        };

        NativeFnEx nativeFn = AS_NATIVE(method)->function;
        KTN_NativeResult result = nativeFn(vm, callArgs);

        if (!result.success) return false;

        vm->stackTop -= argumentCount + 1;
        Push(vm, result.value);

        return true;
    }

    if (!IS_CLOSURE(method)) {
        if (!KTN_RuntimeError(vm, "\"%s\" object has no method \"%s\".", kata->className->chars, name->chars))
            return false;
        return true;
    }

    KTN_ObjClosure* closure = AS_CLOSURE(method);
    return Call(vm, closure, argumentCount, closure->owner);
}

static bool Invoke(KTN_VM* vm, KTN_ObjString* name, int argumentCount) {
    KTN_Value receiver = Peek(vm, argumentCount);

    if (!IS_INSTANCE(receiver)) {
        if (!KATANE_RUNTIME_ERROR("Only instances have methods.", COLOR_MAGENTA "Tsk~" COLOR_RESET " Only instances have methods~!"))
            return false;

        return true;
    }

    KTN_ObjInstance* instance = AS_INSTANCE(receiver);

    KTN_Value value;
    if (TableGet(&instance->properties, name, &value)) {
        vm->stackTop[-argumentCount - 1] = value;
        return CallValue(vm, value, argumentCount);
    }

    return InvokeFromClass(vm, instance->kata, name, argumentCount);
}

static bool BindMethod(KTN_VM* vm, KTN_ObjKata* kata, KTN_ObjString* name) {
    KTN_Value method;
    KTN_Value isHidden;

    bool canAccess = CanAccessMember(vm, kata, name, &isHidden);

    if (!TableGet(&kata->methods, name, &method) || (!canAccess && AS_BOOL(isHidden))) {
        if (!ThrowException(vm, "PropertyError", false, "\"%s\" object has no member \"%s\".", kata->className->chars, name->chars))
            return false;
        return true;
    }


    if (!canAccess) {
        if (!ThrowException(vm, "PropertyError", false, "Method \"%s\" is private and cannot be accessed in the current scope.", name->chars))
            return false;
        return true;
    }

    if (IS_ACCESSOR(method)) {
        KTN_ObjAccessor* accessor = AS_ACCESSOR(method);

        if (IS_EMPTY(accessor->getter)) {
            if (!ThrowException(vm, "PropertyError", false, "Kata \"%s\" has no getter \"%s\".", kata->className->chars, name->chars))
                return false;
            return true;
        }

        if (!CallValue(vm, accessor->getter, 0)) {
            if (vm->caughtException) {
                vm->caughtException = false;
                vm->currentFrame = &vm->frames[vm->frameCount - 1];

                return true;
            }

            return false;
        }

        vm->currentFrame = &vm->frames[vm->frameCount - 1];
        return true;
    }

    KTN_ObjBoundMethod* bound = BoundMethodNew(vm, Peek(vm, 0), AS_CLOSURE(method), kata);

    Pop(vm);
    Push(vm, OBJECT_VALUE(bound));
    return true;
}

static KTN_InterpretResult DoReturn(KTN_VM* vm, int exitFrame) {
    KTN_Value result = Pop(vm);
    KTN_CloseUpvalues(vm, vm->currentFrame->slots);

    int returnDescriptor = vm->currentFrame->closure->function->returnTypeDescriptor;

    if (returnDescriptor >= 0) {
        KTN_Value typeDescriptorValue = vm->currentFrame->closure->function->chunk.constants.values[returnDescriptor];

        if (IS_TYPE_DESCRIPTOR(typeDescriptorValue)) {
            KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptorValue);

            if (!KTN_TypeDescriptorCheck(vm, result, descriptor)) {
                char expectedBuffer[256];

                KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(result), "Return type error"))
                    return RUNTIME_ERROR(NULL_VALUE);

                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                return RUNTIME_OK(NULL_VALUE);
            }
        }
    }

    // Free all catch frames that belong to this call frame (handles early returns).
    while (vm->errorCount > 0 && vm->errors[vm->errorCount - 1]->frame == vm->currentFrame) {
        KTN_ErrorFrame* errorFrame = ErrorPop(vm);
        free(errorFrame);
    }

    vm->frameCount--;

    if (vm->frameCount == 0) {
        KTN_InterpretResult interpretResult = RUNTIME_OK(vm->finalResult);
        PopN(vm, 2);
        return interpretResult;
    }

    vm->stackTop = vm->currentFrame->slots;
    Push(vm, result);

    vm->currentFrame = &vm->frames[vm->frameCount - 1];

    if (vm->frameCount == exitFrame) {
        return RUNTIME_OK(vm->finalResult);
    }

    return RUNTIME_OK(NULL_VALUE);
}

static bool DefineMethod(KTN_VM* vm, KTN_ObjString* name) {
    KTN_Value method = Peek(vm, 0);
    KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));
    KTN_ObjClosure* closure = AS_CLOSURE(method);
    KTN_ShikiType type = closure->function->type;

    if (strcmp(name->chars, kata->className->chars) == 0) {
        if (!IS_NULL(kata->constructor)) {
            if (!KTN_RuntimeError(vm, "Duplicate constructor defined for kata \"%s\".", kata->className->chars))
                return false;
            return true;
        }

        if (type != TYPE_CONSTRUCTOR) {
            if (!KTN_RuntimeError(vm, "Cannot use class name \"%s\" for a %s.", kata->className->chars, (type == TYPE_GETTER) ? "getter" : "setter"))
                return false;
            return true;
        }

        kata->constructor = method;

        Pop(vm);
        return true;
    }

    closure->owner = kata;

    if (TableContains(&kata->properties, name)) {
        if (!KTN_RuntimeError(vm, "Cannot create method \"%s\" for \"%s\" because a property already exists.", name->chars, kata->className->chars))
            return false;
        return true;
    }

    KTN_Value existing;

    if (TableGet(&kata->methods, name, &existing)) {
        if (IS_CLOSURE(existing)) {
            if (type == TYPE_METHOD) {
                TableSet(vm, &kata->methods, name, method);
                Pop(vm);
                return true;
            }

            if (type == TYPE_GETTER) {
                if (!KTN_RuntimeError(vm, "Cannot create getter \"%s\" for \"%s\" because a method already exists.", name->chars, kata->className->chars))
                    return false;
                return true;
            }

            if (type == TYPE_SETTER) {
                if (!KTN_RuntimeError(vm, "Cannot create setter \"%s\" for \"%s\" because a method already exists.", name->chars, kata->className->chars))
                    return false;
                return true;
            }

            KTN_VMPanic(vm, "DefineMethod called with a function that is neither a method nor an accesor.");
        }

        // Accessors can have two definitions: one for setter and one for getter.
        //  We check if the one we're creating doesn't exist and act accordingly.
        if (IS_ACCESSOR(existing)) {
            KTN_ObjAccessor* accessor = AS_ACCESSOR(existing);

            if (type == TYPE_GETTER) {
                if (!IS_EMPTY(accessor->getter)) {
                    if (!KTN_RuntimeError(vm, "Duplicate getter \"%s\" for \"%s\".", name->chars, kata->className->chars))
                        return false;
                    return true;
                }

                accessor->getter = OBJECT_VALUE(closure);

                Pop(vm);
                return true;
            } else if (type == TYPE_SETTER) {
                if (!IS_EMPTY(accessor->setter)) {
                    if (!KTN_RuntimeError(vm, "Duplicate setter \"%s\" for \"%s\".", name->chars, kata->className->chars))
                        return false;
                    return true;
                }

                accessor->setter = OBJECT_VALUE(closure);

                Pop(vm);
                return true;
            } else {
                if (!KTN_RuntimeError(vm, "Cannot create method \"%s\" for \"%s\" because a property accessor already exists.", name->chars, kata->className->chars))
                    return false;
                return true;
            }
        }

        KTN_VMPanic(vm, "Method table contains a value that is neither a closure nor an accessor.");
    }

    if (type == TYPE_METHOD) {
        TableSet(vm, &kata->methods, name, method);
    } else {
        KTN_ObjAccessor* accessor = AccessorNew(vm);

        if (type == TYPE_GETTER)
            accessor->getter = OBJECT_VALUE(closure);
        else if (type == TYPE_SETTER)
            accessor->setter = OBJECT_VALUE(closure);
        else
            KTN_VMPanic(vm, "DefineMethod called with function that is neither a closure nor an accessor.");

        TableSet(vm, &kata->methods, name, OBJECT_VALUE(accessor));
    }

    Pop(vm);
    return true;
}

static bool DefineStaticMethod(KTN_VM* vm, KTN_ObjString* name) {
    KTN_Value method = Peek(vm, 0);
    KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));
    KTN_ObjClosure* closure = AS_CLOSURE(method);
    KTN_ShikiType type = closure->function->type;

    closure->owner = kata;

    if (TableContains(&kata->staticProperties, name)) {
        if (!KTN_RuntimeError(vm, "Cannot create method \"%s\" for \"%s\" because a property already exists.", name->chars, kata->className->chars))
            return false;
        return true;
    }

    KTN_Value existing;

    if (TableGet(&kata->staticMethods, name, &existing)) {
        // We check for a duplicate closure. If it is, we error out.
        if (IS_CLOSURE(existing)) {
            if (type == TYPE_METHOD) {
                if (!KTN_RuntimeError(vm, "Duplicate \"%s\" static method found for \"%s\".", name->chars, kata->className->chars))
                    return false;
                return true;
            }

            if (type == TYPE_GETTER) {
                if (!KTN_RuntimeError(vm, "Cannot create static getter \"%s\" for \"%s\" because a method already exists.", name->chars, kata->className->chars))
                    return false;
                return true;
            }

            if (type == TYPE_SETTER) {
                if (!KTN_RuntimeError(vm, "Cannot create static setter \"%s\" for \"%s\" because a method already exists.", name->chars, kata->className->chars))
                    return false;
                return true;
            }

            KTN_VMPanic(vm, "DefineMethod called with a function that is neither a method nor an accesor.");
        }

        // Accessors can have two definitions: one for setter and one for getter.
        //  We check if the one we're creating doesn't exist and act accordingly.
        if (IS_ACCESSOR(existing)) {
            KTN_ObjAccessor* accessor = AS_ACCESSOR(existing);

            if (type == TYPE_GETTER) {
                if (!IS_EMPTY(accessor->getter)) {
                    if (!KTN_RuntimeError(vm, "Duplicate static getter \"%s\" for \"%s\".", name->chars, kata->className->chars))
                        return false;
                    return true;
                }

                accessor->getter = OBJECT_VALUE(closure);

                Pop(vm);
                return true;
            } else if (type == TYPE_SETTER) {
                if (!IS_EMPTY(accessor->setter)) {
                    if (!KTN_RuntimeError(vm, "Duplicate static setter \"%s\" for \"%s\".", name->chars, kata->className->chars))
                        return false;
                    return true;
                }

                accessor->setter = OBJECT_VALUE(closure);

                Pop(vm);
                return true;
            } else {
                if (!KTN_RuntimeError(vm, "Cannot create static method \"%s\" for \"%s\" because a property accessor already exists.", name->chars, kata->className->chars))
                    return false;
                return true;
            }
        }

        KTN_VMPanic(vm, "Method table contains a value that is neither a closure nor an accessor.");
    }

    if (type == TYPE_METHOD) {
        TableSet(vm, &kata->staticMethods, name, method);
    } else {
        KTN_ObjAccessor* accessor = AccessorNew(vm);

        if (type == TYPE_GETTER)
            accessor->getter = OBJECT_VALUE(closure);
        else if (type == TYPE_SETTER)
            accessor->setter = OBJECT_VALUE(closure);
        else
            KTN_VMPanic(vm, "DefineStaticMethod called with function that is neither a closure nor an accessor.");

        TableSet(vm, &kata->staticMethods, name, OBJECT_VALUE(accessor));
    }

    Pop(vm);
    return true;
}

static KTN_MAYBE_UNUSED void DefineProperty(KTN_VM* vm, KTN_ObjString* name, bool isStatic) {
    KTN_Value property = Peek(vm, 0);
    KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));

    if (!isStatic)
        TableSet(vm, &kata->properties, name, property);
    else
        TableSet(vm, &kata->staticProperties, name, property);

    if (IS_CLOSURE(property)) {
        AS_CLOSURE(property)->owner = kata;
    }

    Pop(vm);
}

static bool IsFalsey(KTN_Value value) {
    return (IS_NULL(value) || (IS_BOOL(value) && !AS_BOOL(value)));
}

static void Concatenate(KTN_VM* vm) {
    KTN_ObjString* b = AS_STRING(Peek(vm, 0));
    KTN_ObjString* a = AS_STRING(Peek(vm, 1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);

    chars[length] = '\0';

    KTN_ObjString* Result = StringTake(vm, chars, length, false);
    PopN(vm, 2);
    Push(vm, OBJECT_VALUE(Result));
}

static KTN_MAYBE_UNUSED void dumpVMState(KTN_VM* vm, const char* reason) {
    KTN_CallFrame* frame = &vm->frames[vm->frameCount - 1];
    KTN_ObjShiki* function = frame->closure->function;
    size_t instruction = (size_t)(frame->ip - function->chunk.code - 1);

    int line = KTN_ChunkGetLine(&function->chunk, (int)instruction);
    char* content = KTN_ChunkGetSource(&function->chunk, (int)instruction);

    fprintf(stderr, "\n--- VM State Dump (%s) ---\n", reason);
    fprintf(stderr, "Line: %d | %s\n", line, content);
    fprintf(stderr, "Stack pointer: %lld (stack top index)\n", vm->stackTop - vm->stack);
    fprintf(stderr, "Stack count: %lld\n", vm->stackTop - vm->stack);
    fprintf(stderr, "Frame count: %d\n", vm->frameCount);

    // Dump stack slots from bottom to top
    fprintf(stderr, "Stack contents (bottom to top):\n");
    for (KTN_Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        fprintf(stderr, "  [%lld] ", slot - vm->stack);
        ObjectRepr(*slot);
        fprintf(stderr, "\n");
    }

    // Dump call frames
    for (int i = 0; i < vm->frameCount; i++) {
        KTN_CallFrame* frame = &vm->frames[i];
        KTN_ObjShiki* func = frame->closure->function;
        fprintf(stderr, "Frame %d: %s (ip=%d)\n", i,
                func->name ? func->name->chars : "<script>",
                (int)(frame->ip - func->chunk.code));
        // Optionally dump locals for this frame? You'd need to know the frame's base.
        // You can calculate the locals by looking at the stack slots starting at frame->slots.
    }

    // If there's an active exception (e.g., in catch handler), print it
    if (vm->caughtException && vm->errorCount != 0) {
        fprintf(stderr, "Pending exception: ");
        ObjectRepr(OBJECT_VALUE(vm->errors[-1]->value));
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "--- End of dump ---\n\n");
}

static void dumpPanicState(KTN_VM* vm) {
    fprintf(stderr, "\n========== PANIC VM STATE ==========\n");

    // Stack
    fprintf(stderr, "Stack top index: %lld\n", vm->stackTop - vm->stack);
    fprintf(stderr, "Stack contents (bottom to top):\n");
    for (KTN_Value* slot = vm->stack; slot < vm->stackTop; slot++) {
        fprintf(stderr, "  [%lld] ", slot - vm->stack);
        ObjectRepr(*slot);
        fprintf(stderr, "\n");
    }

    // Call frames
    fprintf(stderr, "Frame count: %d\n", vm->frameCount);
    for (int i = 0; i < vm->frameCount && i < MAX_FRAMES; i++) {
        KTN_CallFrame* frame = &vm->frames[i];
        fprintf(stderr, "Frame %d: ", i);
        if (frame->closure && frame->closure->function) {
            KTN_ObjShiki* func = frame->closure->function;
            fprintf(stderr, "%s (ip=%d)",
                func->name ? func->name->chars : "<script>",
                (int)(frame->ip - func->chunk.code));
        } else {
            fprintf(stderr, "<invalid frame>");
        }
        fprintf(stderr, "\n");
    }

    // Upvalues (open)
    fprintf(stderr, "Open upvalues: ");
    for (KTN_ObjUpvalue* uv = vm->openUpvalues; uv; uv = uv->next) {
        fprintf(stderr, "%p -> ", (void*)uv);
    }
    fprintf(stderr, "NULL\n");

    // Error frames
    fprintf(stderr, "Error frames count: %d\n", vm->errorCount);
    for (int i = 0; i < vm->errorCount && i < MAX_ERRORS; i++) {
        KTN_ErrorFrame* ef = vm->errors[i];
        if (ef) {
            fprintf(stderr, "  %d: frame=%p offset=%d stackHead=%p\n",
                i, (void*)ef->frame, ef->offset, (void*)ef->stackHead);
        }
    }

    fprintf(stderr, "=====================================\n");
}

void KTN_VMPanic(KTN_VM* vm, const char* format, ...) {
    fprintf(stderr, "\n" COLOR_RED "!!! INTERNAL VM PANIC !!!" COLOR_RESET "\n");

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");

    dumpPanicState(vm);

    // Force abort (or exit)
    exit(EXIT_FAILURE);
}

static KTN_InterpretResult Run(KTN_VM* vm, int exitFrame) {
    vm->currentFrame = &vm->frames[vm->frameCount - 1];

    #define READ_BYTE() (*vm->currentFrame->ip++)
    #define READ_CONSTANT() (vm->currentFrame->closure->function->chunk.constants.values[READ_BYTE()])
    #define READ_CONSTANT_LONG() ( \
        (void)(vm->currentFrame->ip += 4), \
        vm->currentFrame->closure->function->chunk.constants.values[ \
            ((uint32_t)vm->currentFrame->ip[-4] << 24) | \
            ((uint32_t)vm->currentFrame->ip[-3] << 16) | \
            ((uint32_t)vm->currentFrame->ip[-2] <<  8) | \
            ((uint32_t)vm->currentFrame->ip[-1]      ) \
        ] \
    )
    #define READ_SHORT() (vm->currentFrame->ip += 2, (uint16_t)((vm->currentFrame->ip[-2] << 8) | vm->currentFrame->ip[-1]))
    #define READ_STRING() AS_STRING(READ_CONSTANT_LONG())
    #define READ_CONSTANT_BY_INDEX(idx) (vm->currentFrame->closure->function->chunk.constants.values[(idx)])
    #define BINARY_OP(KTN_ValueType, op) \
        do { \
            if ((!IS_DOUBLE(Peek(vm, 0)) && !IS_BOOL(Peek(vm, 0))) || (!IS_DOUBLE(Peek(vm, 1)) && !IS_BOOL(Peek(vm, 1)))) { \
                if (!KATANE_RUNTIME_ERROR("Operands must be numbers.", COLOR_MAGENTA "Nyaa~" COLOR_RESET " Modulo only wants to play with numbers... what are you trying to %%%% me with, naughty~? (⁄ ⁄>⁄ ▽ ⁄<⁄ ⁄)")) \
                    return RUNTIME_ERROR(NULL_VALUE); \
                break; \
            } \
            KTN_Value first = Pop(vm); \
            KTN_Value second = Pop(vm); \
            double b = (IS_BOOL(first)) ? (double)AS_BOOL(first) : AS_DOUBLE(first); \
            double a = (IS_BOOL(second)) ? (double)AS_BOOL(second) : AS_DOUBLE(second); \
            Push(vm, KTN_ValueType(a op b)); \
        } while (false)

    // For arithmetic operators that should preserve int type
    //  when both operands are int and the result fits in int32.
    #define ARITH_OP(int_result_fn, float_result_fn, c_op)                         \
    do {                                                                        \
        if ((!IS_NUMERIC(Peek(vm, 0)) && !IS_BOOL(Peek(vm, 0))) ||             \
            (!IS_NUMERIC(Peek(vm, 1)) && !IS_BOOL(Peek(vm, 1)))) {             \
            if (!KATANE_RUNTIME_ERROR("Operands are of invalid types.",               \
                COLOR_MAGENTA "Nyaa~" COLOR_RESET " Your inputs are of invalid types here~ ♡"))     \
                return RUNTIME_ERROR(NULL_VALUE);                               \
            break;                                                              \
        }                                                                       \
        KTN_Value first = Pop(vm);   /* right operand (top) */                  \
        KTN_Value second = Pop(vm);  /* left operand  */                        \
        if (IS_INT(second) && IS_INT(first)) {                                  \
            int64_t a = (int64_t)AS_INT(second);                                \
            int64_t b = (int64_t)AS_INT(first);                                 \
            int64_t r = a c_op b;                                               \
            if (r >= INT32_MIN && r <= INT32_MAX)                               \
                Push(vm, int_result_fn((int32_t)r));                            \
            else                                                                \
                Push(vm, float_result_fn((double)r));                           \
        } else {                                                                \
            double a = IS_BOOL(second) ? (double)AS_BOOL(second) : AS_NUMERIC(second); \
            double b = IS_BOOL(first)  ? (double)AS_BOOL(first)  : AS_NUMERIC(first);  \
            Push(vm, float_result_fn(a c_op b));                                \
        }                                                                       \
    } while (false)

    // Comparison ops always produce bool so we keep the existing BINARY_OP for those,
    // but updated to use IS_NUMERIC / AS_NUMERIC:
    #define COMPARE_OP(c_op)                                                        \
    do {                                                                        \
        if ((!IS_NUMERIC(Peek(vm, 0)) && !IS_BOOL(Peek(vm, 0))) ||             \
            (!IS_NUMERIC(Peek(vm, 1)) && !IS_BOOL(Peek(vm, 1)))) {             \
            if (!KATANE_RUNTIME_ERROR("Operands must be numbers.",               \
                COLOR_MAGENTA "Nyaa~" COLOR_RESET " Only numbers here~ ♡"))     \
                return RUNTIME_ERROR(NULL_VALUE);                               \
            break;                                                              \
        }                                                                       \
        KTN_Value first = Pop(vm);   /* right operand */                        \
        KTN_Value second = Pop(vm);  /* left operand  */                        \
        double a = IS_BOOL(second) ? (double)AS_BOOL(second) : AS_NUMERIC(second); \
        double b = IS_BOOL(first)  ? (double)AS_BOOL(first)  : AS_NUMERIC(first);  \
        Push(vm, BOOL_VALUE(a c_op b));                                         \
    } while (false)

    for (;;) {
        if (vm->frameCount == 0) {
            return RUNTIME_ERROR(NULL_VALUE);
        }

#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        printf("( ");
        for (KTN_Value* slot = vm->stack; slot < vm->stackTop; slot++) {
            printf("[");
            ObjectRepr(*slot);
            printf(" ]");
        }
        printf(" )");
        printf("\n");
        DisassembleInstruction(&vm->currentFrame->closure->function->chunk, (int)(vm->currentFrame->ip - vm->currentFrame->closure->function->chunk.code));
#endif
        uint8_t Instruction;
        switch(Instruction = READ_BYTE()) {
            case OP_CONSTANT: { // RIP. You did good. You will be remembered.
                KTN_Value Constant = READ_CONSTANT();
                Push(vm, Constant);
                break;
            }
            case OP_CONSTANT_LONG: {
                KTN_Value Constant = READ_CONSTANT_LONG();
                Push(vm, Constant);
                break;
            }
            case OP_NULL:       Push(vm, NULL_VALUE);                     break;
            case OP_TRUE:       Push(vm, BOOL_VALUE(true));               break;
            case OP_FALSE:      Push(vm, BOOL_VALUE(false));              break;
            case OP_MAYBE:      Push(vm, BOOL_VALUE((bool)(rand() % 2))); break;
            case OP_POP: {
                if (vm->stackTop <= vm->stack + 2) {
                    KTN_VMPanic(vm, "OP_POP would underflow.");
                }

                Pop(vm);
                break;
            }
            case OP_POP_RESULT: {
                vm->finalResult = Pop(vm);
                break;
            }
            case OP_DUPLICATE:  Push(vm, Peek(vm, 0));              break;
            case OP_SWAP: {
                KTN_Value top = vm->stackTop[-1];
                vm->stackTop[-1] = vm->stackTop[-2];
                vm->stackTop[-2] = top;
                break;
            }
            case OP_DEFINE_GLOBAL: {
                KTN_ObjString* name = READ_STRING();
                TableSet(vm, &vm->globals, name, Peek(vm, 0));
                Pop(vm);
                break;
            }
            case OP_DEFINE_GLOBAL_TYPED: {
                KTN_ObjString* name = AS_STRING(READ_CONSTANT_LONG());
                KTN_Value typeDescriptor = READ_CONSTANT_LONG();

                if (IS_TYPE_DESCRIPTOR(typeDescriptor)) {
                    KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);
                    if (!KTN_TypeDescriptorCheck(vm, Peek(vm, 0), descriptor)) {
                        char expectedBuffer[256];

                        KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                        if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(Peek(vm, 0)), "Global variable type error"))
                            return RUNTIME_ERROR(NULL_VALUE);

                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }
                }

                TableSet(vm, &vm->globals, name, Peek(vm, 0));
                Pop(vm);
                break;
            }
            case OP_GET_GLOBAL: {
                KTN_ObjString* name = READ_STRING();
                KTN_Value value;
                if (!TableGet(&vm->globals, name, &value)) {
                    if (!KATANE_RUNTIME_ERROR("Global variable '%s' not set before reading it.", COLOR_MAGENTA "Eep~" COLOR_RESET " You reached for '%s' but I haven't even let you touch it yet~ Where did you find that name, naughty? ฅ^•ﻌ•^ฅ", name->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                }
                Push(vm, value);
                break;
            }
            case OP_SET_GLOBAL: {
                KTN_ObjString* name = READ_STRING();
                uint8_t flags = TableGetFlags(&vm->globals, name);

                if (flags & KTN_TABLE_ENTRY_CONST) {
                    if (!ThrowException(vm, "AccessError", false, "Cannot assign to const mochi \"%s\".", name->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                if (flags & KTN_TABLE_ENTRY_FINAL) {
                    if (!ThrowException(vm, "AccessError", false, "Cannot assign to final mochi \"%s\" after it has been assigned.", name->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                if (TableSet(vm, &vm->globals, name, Peek(vm, 0))) {
                    TableDelete(&vm->globals, name);
                    if (!KATANE_RUNTIME_ERROR("Global variable '%s' not set before reading it.", COLOR_MAGENTA "Eep~" COLOR_RESET " You reached for '%s' but I haven't even let you touch it yet~ Where did you find that name, naughty? ฅ^•ﻌ•^ฅ", name->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                }
                break;
            }
            case OP_SET_GLOBAL_TYPED: {
                KTN_ObjString* name = READ_STRING();
                KTN_Value typeDescriptor = READ_CONSTANT_LONG();
                KTN_Value value = Peek(vm, 0);

                if (IS_TYPE_DESCRIPTOR(typeDescriptor)) {
                    KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);
                    if (!KTN_TypeDescriptorCheck(vm, value, descriptor)) {
                        char expectedBuffer[256];

                        KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                        if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(value), "Global variable type error"))
                            return RUNTIME_ERROR(NULL_VALUE);

                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }
                }

                if (TableSet(vm, &vm->globals, name, value)) {
                    TableDelete(&vm->globals, name);
                    if (!KATANE_RUNTIME_ERROR("Global variable '%s' not set before reading it.", COLOR_MAGENTA "Eep~" COLOR_RESET " You reached for '%s' but I haven't even let you touch it yet~ Where did you find that name, naughty? ฅ^•ﻌ•^ฅ", name->chars)) return RUNTIME_ERROR(NULL_VALUE);
                }
                break;
            }
            case OP_MARK_GLOBAL_FLAGS: {
                KTN_ObjString* name = READ_STRING();
                uint8_t flags = READ_BYTE();
                TableSetFlags(&vm->globals, name, flags);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vm->currentFrame->slots[slot] = Peek(vm, 0);
                break;
            }
            case OP_SET_LOCAL_TYPED: {
                uint8_t slot = READ_BYTE();
                KTN_Value typeDescriptor = READ_CONSTANT_LONG();
                KTN_Value value = Peek(vm, 0);

                if (IS_TYPE_DESCRIPTOR(typeDescriptor)) {
                    KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);
                    if (!KTN_TypeDescriptorCheck(vm, value, descriptor)) {
                        char expectedBuffer[256];

                        KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                        if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(value), "Local variable type error"))
                            return RUNTIME_ERROR(NULL_VALUE);

                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }
                }

                vm->currentFrame->slots[slot] = value;
                break;
            }
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                Push(vm, vm->currentFrame->slots[slot]);
                break;
            }
            case OP_SET_INDEX: {
                if (!IS_OBJECT(Peek(vm, 2))) {
                    if (!KATANE_RUNTIME_ERROR("Cannot access the index of a non-object.", COLOR_MAGENTA "Ara ara~" COLOR_RESET " Only my special fluffy objects get to be indexed... what are you trying to poke at, silly~? ♡"))
                        return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_Value value = Peek(vm, 0);

                switch(OBJECT_TYPE(Peek(vm, 2))) {
                    case OBJ_ARRAY: {
                        KTN_ObjArray* array = AS_ARRAY(Peek(vm, 2));

                        if (!KTN_ArraySet(vm, array, Peek(vm, 1), value)) {
                            if (!KATANE_RUNTIME_ERROR("Invalid array setting.", COLOR_MAGENTA "Ehehe~" COLOR_RESET " That's not how you stroke my array, darling~ You're poking in the wrong spot... be gentler next time~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                        }

                        break;
                    }

                    case OBJ_MAP: {
                        KTN_ObjMap* map = AS_MAP(Peek(vm, 2));
                        KTN_HashMapSet(vm, &map->map, Peek(vm, 1), value);
                        // if (!KATANE_RUNTIME_ERROR("Invalid map setting.", COLOR_MAGENTA "Ehehe~" COLOR_RESET " That's not how you stroke my map, darling~ You're poking in the wrong spot... be gentler next time~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    default: {
                        if (!KATANE_RUNTIME_ERROR("Cannot access the index of a non-object.", COLOR_MAGENTA "Ara ara~" COLOR_RESET " Only my special fluffy objects get to be indexed... what are you trying to poke at, silly~? ♡"))
                            return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }
                }

                PopN(vm, 3);    // We pop out the value, the index and the list from the stack.
                Push(vm, value);
                break;
            }
            case OP_GET_INDEX: {
                if (!IS_OBJECT(Peek(vm, 1))) {
                    if (!KATANE_RUNTIME_ERROR("Cannot access the index of a non-object.", COLOR_MAGENTA "Ara ara~" COLOR_RESET " Only my special fluffy objects get to be indexed... what are you trying to poke at, silly~? ♡")) return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_Value value;
                switch(OBJECT_TYPE(Peek(vm, 1))) {
                    case OBJ_ARRAY: {
                        KTN_ObjArray* array = AS_ARRAY(Peek(vm, 1));
                        if (!KTN_ArrayGet(array, Peek(vm, 0), &value)) {
                            if (!KATANE_RUNTIME_ERROR("Invalid array access.", COLOR_MAGENTA "Nuh-uh~" COLOR_RESET " That index is way beyond my tail's reach... reach softer and sweeter next time~! ฅ(>ω<)ฅ")) return RUNTIME_ERROR(NULL_VALUE);
                        }
                        break;
                    }

                    case OBJ_MAP: {
                        KTN_ObjMap* map = AS_MAP(Peek(vm, 1));

                        if (!KTN_HashMapGet(vm, &map->map, Peek(vm, 0), &value)) {
                            if (!KATANE_RUNTIME_ERROR("Invalid map access.", COLOR_MAGENTA "Ara ara~" COLOR_RESET " My maps are shy and picky... that key doesn't belong to you yet~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                        }
                        break;
                    }

                    case OBJ_STRING: {
                        // Index into a string by Unicode codepoint, not by byte.
                        KTN_ObjString* str = AS_STRING(Peek(vm, 1));
                        KTN_Value idxVal   = Peek(vm, 0);

                        if (!IS_NUMERIC(idxVal)) {
                            if (!KATANE_RUNTIME_ERROR("String index must be an integer.",
                                COLOR_MAGENTA "Nyaa~" COLOR_RESET " String indices must be integers, not whatever that was~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                        }

                        int cpIdx = (int)AS_NUMERIC(idxVal);
                        // Support negative indices (Python-style)
                        if (cpIdx < 0) cpIdx += str->charLength;

                        if (cpIdx < 0 || cpIdx >= str->charLength) {
                            if (!KATANE_RUNTIME_ERROR("String index %d out of range (length %d).",
                                COLOR_MAGENTA "Eep~" COLOR_RESET " Index " COLOR_CYAN "%d" COLOR_RESET " is out of my string's reach~! It only goes up to " COLOR_CYAN "%d" COLOR_RESET " ♡",
                                cpIdx, str->charLength, cpIdx, str->charLength - 1)) return RUNTIME_ERROR(NULL_VALUE);
                        }

                        Utf8Char cp = Utf8CodepointAt(str->chars, str->length, cpIdx);
                        // Encode the single codepoint into a temporary buffer
                        char buf[5];
                        int  encLen = Utf8Encode(cp.codepoint, buf);
                        buf[encLen] = '\0';
                        value = OBJECT_VALUE(STRING_COPY(vm, buf, encLen));
                        break;
                    }

                    default: {
                        if (!KATANE_RUNTIME_ERROR("Cannot index into this type.", COLOR_MAGENTA "Ara~" COLOR_RESET " This type doesn't support indexing~ ♡"))
                            return RUNTIME_ERROR(NULL_VALUE);
                        value = EMPTY_VALUE;
                    }
                }

                PopN(vm, 2);
                Push(vm, value);
                break;
            }
            case OP_GET_INDEX_RANGED: {
                if (!IS_OBJECT(Peek(vm, 3)) || !IS_ARRAY(Peek(vm, 3))) {
                    KTN_RuntimeError(vm, "Cannot access the indexes of a non-object.");
                    return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_ObjArray* array = AS_ARRAY(Peek(vm, 3));
                KTN_Value newArray;
                if (!KTN_ArrayGetRange(vm, array, Peek(vm, 2), Peek(vm, 1), Peek(vm, 0), &newArray)) {
                    if (!KATANE_RUNTIME_ERROR("Invalid array access.", COLOR_MAGENTA "Nuh-uh~" COLOR_RESET " That index is way beyond my tail's reach... reach softer and sweeter next time~! ฅ(>ω<)ฅ")) return RUNTIME_ERROR(NULL_VALUE);
                }

                PopN(vm, 4);
                Push(vm, newArray);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *vm->currentFrame->closure->upvalues[slot]->location = Peek(vm, 0);
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                Push(vm, *vm->currentFrame->closure->upvalues[slot]->location);
                break;
            }
            case OP_INIT_PROPERTY: {
                if (!IS_CLASS(Peek(vm, 1))) {
                    if (!KATANE_RUNTIME_ERROR("Only classes have properties.", COLOR_MAGENTA "Tsk~" COLOR_RESET " Only classes have properties~!")) return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));
                KTN_ObjString* string = READ_STRING();
                bool isStatic = (READ_BYTE() == 1);
                uint8_t flags = READ_BYTE();

                if (isStatic) {
                    if (TableContains(&kata->staticProperties, string)) {
                        if (!KATANE_RUNTIME_ERROR("Duplicate property \"%s\" on kata \"%s\".", COLOR_MAGENTA "Tsk tsk~" COLOR_RESET " You already gave me a \"%s\" property on \"%s\"... I don't like sharing my private spots, darling~ ♡", string->chars, kata->className->chars)) return RUNTIME_ERROR(NULL_VALUE);
                    }

                    TableSetFlagged(vm, &kata->staticProperties, string, Peek(vm, 0), flags);
                    Pop(vm);
                    break;
                }

                if (TableContains(&kata->properties, string)) {
                    if (!KATANE_RUNTIME_ERROR("Duplicate property \"%s\" on kata \"%s\".", COLOR_MAGENTA "Tsk tsk~" COLOR_RESET " You already gave me a \"%s\" property on \"%s\"... I don't like sharing my private spots, darling~ ♡", string->chars, kata->className->chars)) return RUNTIME_ERROR(NULL_VALUE);
                }

                TableSetFlagged(vm, &kata->properties, string, Peek(vm, 0), flags);
                Pop(vm);
                break;
            }
            case OP_INIT_PROPERTY_TYPED: {
                KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));
                KTN_ObjString* name = READ_STRING();
                bool isStatic = (READ_BYTE() == 1);
                KTN_Value typeDescriptor = READ_CONSTANT_LONG();
                uint8_t flags = READ_BYTE();

                if (!IS_TYPE_DESCRIPTOR(typeDescriptor)) {
                    KTN_VMPanic(vm, "Type descriptor object is not a type descriptor in OP_INIT_PROPERTY_TYPED");
                }

                KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);

                if (!KTN_TypeDescriptorCheck(vm, Peek(vm, 0), descriptor)) {
                    char expectedBuffer[256];

                    KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                    if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(Peek(vm, 0)), "Property type error"))
                        return RUNTIME_ERROR(NULL_VALUE);

                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                    break;
                }

                if (isStatic) {
                    if (TableContains(&kata->staticProperties, name)) {
                        if (!KATANE_RUNTIME_ERROR("Duplicate property \"%s\" on kata \"%s\".", COLOR_MAGENTA "Tsk tsk~" COLOR_RESET " You already gave me a \"%s\" property on \"%s\"... I don't like sharing my private spots, darling~ ♡", name->chars, kata->className->chars))
                            return RUNTIME_ERROR(NULL_VALUE);
                    }

                    TableSetFlagged(vm, &kata->staticProperties, name, Peek(vm, 0), flags);
                    TableSet(vm, &kata->staticFieldTypes, name, typeDescriptor);

                    Pop(vm);
                    break;
                }

                if (TableContains(&kata->properties, name)) {
                    if (!KATANE_RUNTIME_ERROR("Duplicate property \"%s\" on kata \"%s\".", COLOR_MAGENTA "Tsk tsk~" COLOR_RESET " You already gave me a \"%s\" property on \"%s\"... I don't like sharing my private spots, darling~ ♡", name->chars, kata->className->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                }

                TableSet(vm, &kata->fieldTypes, name, typeDescriptor);
                TableSetFlagged(vm, &kata->properties, name, Peek(vm, 0), flags);

                kata->hasTypedFields = true;
                Pop(vm);
                break;
            }
            case OP_SET_PROPERTY: {
                // vm stack:
                // 0. Value to set.
                // 1. The receiver.

                if (IS_CLASS(Peek(vm, 1))) {
                    KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));
                    KTN_ObjString* name = READ_STRING();

                    if (TableContains(&kata->staticProperties, name)) {
                        KTN_Value currentValue;
                        uint8_t flags = TableGetFlags(&kata->staticProperties, name);
                        FinalWriteStatus finalStatus;

                        TableGet(&kata->staticProperties, name, &currentValue);

                        finalStatus = CanAssignFinalProperty(vm, Peek(vm, 1), currentValue, flags, name, true);
                        if (finalStatus == FINAL_WRITE_FAILED)
                            return RUNTIME_ERROR(NULL_VALUE);

                        if (finalStatus == FINAL_WRITE_BLOCKED)
                            break;

                        if (kata->hasTypedFields) {
                            KTN_Value typeDescriptor;

                            if (TableGet(&kata->staticFieldTypes, name, &typeDescriptor) && IS_TYPE_DESCRIPTOR(typeDescriptor)) {
                                KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);

                                if (!KTN_TypeDescriptorCheck(vm, Peek(vm, 0), descriptor)) {
                                    char expectedBuffer[256];

                                    KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                                    if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(Peek(vm, 0)), "Local variable type error"))
                                        return RUNTIME_ERROR(NULL_VALUE);

                                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                                    break;
                                }
                            }
                        }

                        TableSet(vm, &kata->staticProperties, name, Peek(vm, 0));

                        KTN_Value value = Pop(vm);
                        Pop(vm);
                        Push(vm, value);
                        break;
                    }

                    KTN_Value value;

                    if (!TableGet(&kata->staticMethods, name, &value)) {
                        if (!ThrowException(vm, "PropertyError", false, "Kata \"%s\" has no static member \"%s\".", kata->className->chars, name->chars))
                            return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    if (!IS_ACCESSOR(value)) {
                        if (!ThrowException(vm, "PropertyError", false, "Cannot assign to method \"%s\" in kata \"%s\".", name->chars, kata->className->chars))
                            return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    KTN_ObjAccessor* accessor = AS_ACCESSOR(value);

                    if (IS_EMPTY(accessor->setter)) {
                        if (!ThrowException(vm, "PropertyError", false, "Kata \"%s\" has no static setter for \"%s\".", kata->className->chars, name->chars))
                            return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    if (!CallValue(vm, accessor->setter, 1)) {
                        if (vm->caughtException) {
                            vm->caughtException = false;
                            vm->currentFrame = &vm->frames[vm->frameCount - 1];
                            break;
                        }

                        return RUNTIME_ERROR(NULL_VALUE);
                    }
                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                    break;
                }

                if (!IS_INSTANCE(Peek(vm, 1))) {
                    if (!KATANE_RUNTIME_ERROR("Only kata instances have properties.", COLOR_MAGENTA "Ara~" COLOR_RESET " Only proper kata instances get my private properties... what are you trying to peek at, naughty~? ♡")) return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_ObjInstance* instance = AS_INSTANCE(Peek(vm, 1));
                KTN_ObjString* string = READ_STRING();

                if (TableContains(&instance->properties, string)) {
                    KTN_Value currentValue;
                    uint8_t flags = TableGetFlags(&instance->kata->properties, string);
                    FinalWriteStatus finalStatus;

                    TableGet(&instance->properties, string, &currentValue);

                    finalStatus = CanAssignFinalProperty(vm, Peek(vm, 1), currentValue, flags, string, false);
                    if (finalStatus == FINAL_WRITE_FAILED)
                        return RUNTIME_ERROR(NULL_VALUE);

                    if (finalStatus == FINAL_WRITE_BLOCKED)
                        break;

                    if (instance->kata->hasTypedFields) {
                        KTN_Value typeDescriptor;

                        if (TableGet(&instance->kata->fieldTypes, string, &typeDescriptor) && IS_TYPE_DESCRIPTOR(typeDescriptor)) {
                            KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);

                            if (!KTN_TypeDescriptorCheck(vm, Peek(vm, 0), descriptor)) {
                                char expectedBuffer[256];

                                KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                                if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(Peek(vm, 0)), "Local variable type error"))
                                    return RUNTIME_ERROR(NULL_VALUE);

                                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                                break;
                            }
                        }
                    }

                    TableSet(vm, &instance->properties, string, Peek(vm, 0));

                    KTN_Value value = Pop(vm);
                    Pop(vm);
                    Push(vm, value);
                    break;
                }

                KTN_Value value;
                if (!TableGet(&instance->kata->methods, string, &value)) {
                    if (!KATANE_RUNTIME_ERROR("Instance of kata \"%s\" has no field \"%s\".", COLOR_MAGENTA "Hmm~?" COLOR_RESET " Instance of kata \"%s\" doesn't have a \"%s\" spot for you to fondle... did you forget to declare it, silly~? ♡", instance->kata->className->chars, string->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                KTN_ObjAccessor* accessor = AS_ACCESSOR(value);

                if (IS_EMPTY(accessor->setter)) {
                    if (!ThrowException(vm, "PropertyError", false, "Instance of kata \"%s\" has no setter for \"%s\".", instance->kata->className->chars, string->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                if (!CallValue(vm, accessor->setter, 1)) {
                    if (vm->caughtException) {
                        vm->caughtException = false;
                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }

                    return RUNTIME_ERROR(NULL_VALUE);
                }

                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                break;
            }
            case OP_GET_PROPERTY: {
                // If the element is a kata, we check for static properties and members.
                if (IS_CLASS(Peek(vm, 0))) {
                    KTN_ObjKata* kata = AS_CLASS(Peek(vm, 0));
                    KTN_ObjString* name = READ_STRING();

                    KTN_Value value;

                    // If we have either a property or a method, we simply check if we can access it and we push it to the stack.
                    // No need for calling stuff, since that is handled by OP_CALL and OP_INVOKE.
                    // Similarly, because, in the case of a method, the method would be static, there is no instance to bind to, and thus we are set.
                    if (TableGet(&kata->staticProperties, name, &value) || TableGet(&kata->staticMethods, name, &value)) {
                        KTN_Value isHidden;

                        if (!CanAccessMember(vm, kata, name, &isHidden)) {
                            if (AS_BOOL(isHidden)) {
                                if (!ThrowException(vm, "PropertyError", false, "Kata \"%s\" has no static member \"%s\".", kata->className->chars, name->chars))
                                    return RUNTIME_ERROR(NULL_VALUE);
                                break;
                            }

                            if (!ThrowException(vm, "PropertyError", false, "Static member \"%s\" is private and cannot be accessed in the current scope.", name->chars))
                                return RUNTIME_ERROR(NULL_VALUE);
                            break;
                        }

                        if (IS_ACCESSOR(value)) {
                            KTN_ObjAccessor* accessor = AS_ACCESSOR(value);

                            if (IS_EMPTY(accessor->getter)) {
                                if (!ThrowException(vm, "PropertyError", false, "Kata \"%s\" has no static getter \"%s\".", kata->className->chars, name->chars))
                                    return RUNTIME_ERROR(NULL_VALUE);
                                break;
                            }

                            if (!CallValue(vm, accessor->getter, 0)) {
                                if (vm->caughtException) {
                                    vm->caughtException = false;
                                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                                    break;
                                }

                                return RUNTIME_ERROR(NULL_VALUE);
                            }
                            vm->currentFrame = &vm->frames[vm->frameCount - 1];
                            break;
                        }

                        Pop(vm);
                        Push(vm, value);
                        break;
                    }

                    // If we got here, there is neither a static property nor static method with the given name, so we throw a PropertyError.
                    if (!ThrowException(vm, "PropertyError", false, "Kata \"%s\" has no static member \"%s\".", kata->className->chars, name->chars))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                if (!IS_INSTANCE(Peek(vm, 0))) {
                    if (!KATANE_RUNTIME_ERROR("Only objects have properties.", COLOR_MAGENTA "Tsk~" COLOR_RESET " Only my cute objects have properties you can touch... stop trying with primitives, darling~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_ObjInstance* instance = AS_INSTANCE(Peek(vm, 0));
                KTN_ObjString* name = READ_STRING();

                KTN_Value value;
                if (TableGet(&instance->properties, name, &value)) {
                    KTN_Value isHidden;

                    if (!CanAccessMember(vm, instance->kata, name, &isHidden)) {
                        if (AS_BOOL(isHidden)) {
                            if (!ThrowException(vm, "PropertyError", false, "\"%s\" object has no member \"%s\".", instance->kata->className->chars, name->chars))
                                return RUNTIME_ERROR(NULL_VALUE);
                            break;
                        }

                        if (!ThrowException(vm, "PropertyError", false, "Member \"%s\" is private and cannot be accessed in the current scope.", name->chars))
                            return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    Pop(vm);
                    Push(vm, value);
                    break;
                }

                if (!BindMethod(vm, instance->kata, name)) {
                    return RUNTIME_ERROR(NULL_VALUE);
                }
                break;
            }
            case OP_GET_SUPER: {
                KTN_ObjString* name = READ_STRING();
                KTN_ObjKata* sokata = AS_CLASS(Pop(vm));

                if (!BindMethod(vm, sokata, name)) {
                    return RUNTIME_ERROR(NULL_VALUE);
                }
                break;
            }
            case OP_EQUAL: {
                KTN_Value b = Pop(vm);
                KTN_Value a = Pop(vm);
                Push(vm, BOOL_VALUE(ValuesEqual(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                KTN_Value b = Pop(vm);
                KTN_Value a = Pop(vm);
                Push(vm, BOOL_VALUE(!ValuesEqual(a, b)));
                break;
            }
            case OP_GREATER:    COMPARE_OP(>);  break;
            case OP_SMALLER:    COMPARE_OP(<);  break;
            case OP_GREATER_EQ: {
                KTN_Value b = Peek(vm, 0);
                KTN_Value a = Peek(vm, 1);

                bool valuesAreEqual = ValuesEqual(a, b);

                if (valuesAreEqual) {
                    Pop(vm);
                    Pop(vm);
                    Push(vm, BOOL_VALUE(valuesAreEqual));
                    break;
                }

                COMPARE_OP(>);
                break;
            }
            case OP_SMALLER_EQ: {
                KTN_Value b = Peek(vm, 0);
                KTN_Value a = Peek(vm, 1);
                bool valuesAreEqual = ValuesEqual(a, b);
                if (valuesAreEqual) {
                    Pop(vm);
                    Pop(vm);
                    Push(vm, BOOL_VALUE(valuesAreEqual));
                    break;
                }

                COMPARE_OP(<);
                break;
            }
            case OP_IS: {
                KTN_Value b = Peek(vm, 0);
                KTN_Value a = Peek(vm, 1);

                if ((!IS_OBJECT(a) || !IS_OBJECT(b)) || (IS_STRING(a) && IS_STRING(b))) {
                    Push(vm, BOOL_VALUE(ValuesEqual(a, b)));
                    break;
                }

                // No need to compare if we already know they are of different types.
                if (OBJECT_TYPE(a) != OBJECT_TYPE(b)) {
                    Push(vm, BOOL_VALUE(false));
                    break;
                }

                bool comparison = (AS_OBJECT(a) == AS_OBJECT(b));

                PopN(vm, 2);
                Push(vm, BOOL_VALUE(comparison));
                break;
            }
            case OP_ADD: {
                if (IS_STRING(Peek(vm, 0)) && IS_STRING(Peek(vm, 1))) {
                    Concatenate(vm);
                    break;
                }

                ARITH_OP(INT_VALUE, DOUBLE_VALUE, +);
                break;
            }
            case OP_SUBTRACT:   ARITH_OP(INT_VALUE, DOUBLE_VALUE, -); break;
            case OP_MULTIPLY:   ARITH_OP(INT_VALUE, DOUBLE_VALUE, *); break;

            case OP_DIVIDE: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);
                if (!IS_NUMERIC(rhs) && !IS_BOOL(rhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands must be numbers.",
                        COLOR_MAGENTA "Nyaa~" COLOR_RESET " Division needs numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                double b = AS_NUMERIC(rhs);
                double a = AS_NUMERIC(lhs);

                if (b == 0) {
                    if (!KATANE_RUNTIME_ERROR("Integer division by zero.",
                        COLOR_MAGENTA "Eep~" COLOR_RESET " Can't divide by zero~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                PopN(vm, 2);
                Push(vm, DOUBLE_VALUE(a / b));
                break;
            }

            case OP_FLOOR_DIV: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);
                if (!IS_NUMERIC(rhs) && !IS_BOOL(rhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands must be numbers.",
                        COLOR_MAGENTA "Ara~" COLOR_RESET " Floor division needs numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                if (IS_INT(lhs) && IS_INT(rhs)) {
                    int32_t b = AS_INT(rhs);
                    int32_t a = AS_INT(lhs);
                    if (b == 0) {
                        if (!KATANE_RUNTIME_ERROR("Integer division by zero.",
                            COLOR_MAGENTA "Eep~" COLOR_RESET " Can't divide by zero~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    int32_t q = a / b;

                    if ((a % b) != 0 && ((a ^ b) < 0)) q -= 1;

                    PopN(vm, 2);
                    Push(vm, INT_VALUE(q));
                } else {
                    double b = AS_NUMERIC(rhs);
                    double a = AS_NUMERIC(lhs);
                    PopN(vm, 2);
                    Push(vm, DOUBLE_VALUE(floor(a / b)));
                }
                break;
            }

            case OP_POW: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);
                if (!IS_NUMERIC(rhs) && !IS_BOOL(rhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands must be numbers.",
                        COLOR_MAGENTA "Ara~" COLOR_RESET " Power needs numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                if (IS_INT(lhs) && IS_INT(rhs)) {
                    int32_t exp = AS_INT(rhs);
                    if (exp >= 0) {
                        // Integer fast path via repeated squaring.
                        int64_t base   = AS_INT(lhs);
                        int64_t result = 1;
                        int64_t b      = base;
                        int32_t e      = exp;
                        bool    overflow = false;
                        while (e > 0) {
                            if (e & 1) {
                                result *= b;
                                if (result > INT32_MAX || result < INT32_MIN) {
                                    overflow = true; break;
                                }
                            }
                            e >>= 1;
                            if (e > 0) {
                                b *= b;
                                if (b > INT32_MAX || b < INT32_MIN) {
                                    overflow = true; break;
                                }
                            }
                        }
                        PopN(vm, 2);
                        if (!overflow && result >= INT32_MIN && result <= INT32_MAX)
                            Push(vm, INT_VALUE((int32_t)result));
                        else
                            Push(vm, DOUBLE_VALUE(pow((double)AS_INT(lhs),
                                                      (double)AS_INT(rhs))));
                    } else {
                        double a = (double)AS_INT(lhs);
                        double b = (double)AS_INT(rhs);

                        PopN(vm, 2);
                        Push(vm, DOUBLE_VALUE(pow(a, b)));
                    }
                } else {
                    double a = AS_NUMERIC(lhs);
                    double b = AS_NUMERIC(rhs);

                    PopN(vm, 2);
                    Push(vm, DOUBLE_VALUE(pow(a, b)));
                }
                break;
            }
            case OP_POSTINCREASE: {
                if (!IS_NUMERIC(Peek(vm, 0))) {
                    if (!KATANE_RUNTIME_ERROR("Cannot post-increase a non-number.",
                        COLOR_MAGENTA "Nyaa~" COLOR_RESET " '++' loves only numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                KTN_Value a = Peek(vm, 0);

                Pop(vm);
                Push(vm, a);   // push old value (expression result)
                Push(vm, (IS_INT(a)) ? INT_VALUE(AS_INT(a) + 1) : DOUBLE_VALUE(AS_DOUBLE(a) + 1));
                break;
            }
            case OP_PREINCREASE: {
                if (!IS_NUMERIC(Peek(vm, 0))) {
                    if (!KATANE_RUNTIME_ERROR("Cannot pre-increase a non-number.",
                        COLOR_MAGENTA "Tsk~" COLOR_RESET " '++' only for numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                KTN_Value a = Peek(vm, 0);
                KTN_Value b = IS_INT(a) ? INT_VALUE(AS_INT(a) + 1) : DOUBLE_VALUE(AS_DOUBLE(a) + 1);
                Pop(vm);
                Push(vm, b);
                Push(vm, b);
                break;
            }
            case OP_POSTDECREASE: {
                if (!IS_NUMERIC(Peek(vm, 0))) {
                    if (!KATANE_RUNTIME_ERROR("Cannot post-decrease a non-number.",
                        COLOR_MAGENTA "Eep~" COLOR_RESET " '--' loves only numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                KTN_Value a = Peek(vm, 0);
                Push(vm, IS_INT(a) ? INT_VALUE(AS_INT(a) - 1) : DOUBLE_VALUE(AS_DOUBLE(a) - 1));
                break;
            }
            case OP_PREDECREASE: {
                if (!IS_NUMERIC(Peek(vm, 0))) {
                    if (!KATANE_RUNTIME_ERROR("Cannot pre-decrease a non-number.",
                        COLOR_MAGENTA "Ohoho~" COLOR_RESET " '--' only for numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                KTN_Value a = Peek(vm, 0);
                KTN_Value b = IS_INT(a) ? INT_VALUE(AS_INT(a) - 1) : DOUBLE_VALUE(AS_DOUBLE(a) - 1);
                Pop(vm);
                Push(vm, b);
                Push(vm, b);
                break;
            }
            case OP_MOD: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);
                if (!IS_NUMERIC(rhs) || !IS_NUMERIC(lhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands must be numbers.",
                        COLOR_MAGENTA "Nyaa~" COLOR_RESET " Modulo only plays with numbers~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                if (IS_INT(lhs) && IS_INT(rhs)) {
                    int32_t b = AS_INT(rhs);
                    if (b == 0) {
                        if (!KATANE_RUNTIME_ERROR("Modulo by zero.",
                            COLOR_MAGENTA "Eep~" COLOR_RESET " Can't mod by zero~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                        break;
                    }

                    int32_t a = AS_INT(lhs);
                    int32_t r = a % b;

                    if (r != 0 && ((r ^ b) < 0)) r += b;

                    PopN(vm, 2);
                    Push(vm, INT_VALUE(r));
                } else {
                    double b = AS_NUMERIC(rhs);
                    double a = AS_NUMERIC(lhs);
                    PopN(vm, 2);
                    Push(vm, DOUBLE_VALUE(fmod(a, b)));
                }
                break;
            }
            case OP_BITWISE_AND: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);

                if (!IS_NUMERIC(rhs) || !IS_NUMERIC(lhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands to & must be integers.", COLOR_MAGENTA "Ara~" COLOR_RESET " Bitwise & only for integers~ ♡"))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                int32_t a = (int32_t)(int64_t)AS_NUMERIC(lhs);
                int32_t b = (int32_t)(int64_t)AS_NUMERIC(rhs);
                PopN(vm, 2);
                Push(vm, INT_VALUE(a & b));
                break;
            }
            case OP_BITWISE_OR: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);
                if (!IS_NUMERIC(rhs) || !IS_NUMERIC(lhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands to | must be integers.", COLOR_MAGENTA "Eep~" COLOR_RESET " Bitwise | only for integers~ ♡"))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                int32_t a = (int32_t)(int64_t)AS_NUMERIC(lhs);
                int32_t b = (int32_t)(int64_t)AS_NUMERIC(rhs);
                PopN(vm, 2);
                Push(vm, INT_VALUE(a | b));
                break;
            }
            case OP_BITWISE_XOR: {
                KTN_Value rhs = Peek(vm, 0);
                KTN_Value lhs = Peek(vm, 1);
                if (!IS_NUMERIC(rhs) || !IS_NUMERIC(lhs)) {
                    if (!KATANE_RUNTIME_ERROR("Operands to ^ must be integers.", COLOR_MAGENTA "Nyaa~" COLOR_RESET " XOR only for integers~ ♡"))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                int32_t a = (int32_t)(int64_t)AS_NUMERIC(lhs);
                int32_t b = (int32_t)(int64_t)AS_NUMERIC(rhs);

                PopN(vm, 2);
                Push(vm, INT_VALUE(a ^ b));
                break;
            }
            case OP_BITWISE_NOT: {
                KTN_Value v = Peek(vm, 0);
                if (!IS_NUMERIC(v)) {
                    if (!KATANE_RUNTIME_ERROR("Operand to ~ must be an integer.", COLOR_MAGENTA "Ara~" COLOR_RESET " ~ only for integers~ ♡"))
                        return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }
                int32_t a = (int32_t)(int64_t)AS_NUMERIC(v);
                Pop(vm);
                Push(vm, INT_VALUE(~a));
                break;
            }

            case OP_SHIFT_LEFT: {
                KTN_Value right = Peek(vm, 0);
                KTN_Value left = Peek(vm, 1);

                if (!IS_INT(right) || !IS_INT(left)) {
                    if (!KATANE_RUNTIME_ERROR("Bit shifting can only be performed with integers.",
                        COLOR_MAGENTA "Nuh uh~" COLOR_RESET " Only integers can be bit shifted~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                int32_t a = AS_INT(left);
                int32_t b = AS_INT(right);

                if (b < 0) {
                    if (!KATANE_RUNTIME_ERROR("Negative shifting is not allowed.",
                        COLOR_MAGENTA "Tsk~" COLOR_RESET " Tried to slip in a negative shift! So naughty~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                PopN(vm, 2);
                Push(vm, INT_VALUE(a << b));
                break;
            }

            case OP_SHIFT_RIGHT: {
                KTN_Value right = Peek(vm, 0);
                KTN_Value left = Peek(vm, 1);

                if (!IS_INT(right) || !IS_INT(left)) {
                    if (!KATANE_RUNTIME_ERROR("Bit shifting can only be performed with integers.",
                        COLOR_MAGENTA "Nuh uh~" COLOR_RESET " Only integers can be bit shifted~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                int32_t a = AS_INT(left);
                int32_t b = AS_INT(right);

                if (b < 0) {
                    if (!KATANE_RUNTIME_ERROR("Negative shifting is not allowed.",
                        COLOR_MAGENTA "Tsk~" COLOR_RESET " Tried to slip in a negative shift! So naughty~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                    break;
                }

                PopN(vm, 2);
                Push(vm, INT_VALUE(a >> b));

                break;
            }
            case OP_NOT:        Push(vm, BOOL_VALUE(IsFalsey(Pop(vm)))); break;
            case OP_NEGATE: {
                KTN_Value v = Peek(vm, 0);
                if (!IS_NUMERIC(v) && !IS_BOOL(v)) {
                    if (!KATANE_RUNTIME_ERROR("Operand to unary minus must be a number.",
                        COLOR_MAGENTA "Tsk~" COLOR_RESET " Only numbers get negated~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                }
                KTN_Value popped = Pop(vm);
                if (IS_INT(popped))
                    Push(vm, INT_VALUE(-AS_INT(popped)));
                else
                    Push(vm, DOUBLE_VALUE(-AS_NUMERIC(popped)));
                break;
            }
            case OP_PRINT: {
                ValuePrint(Pop(vm));
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t Offset = READ_SHORT();
                vm->currentFrame->ip += Offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t Offset = READ_SHORT();
                if (IsFalsey(Peek(vm, 0)))
                    vm->currentFrame->ip += Offset;
                break;
            }
            case OP_LOOP: {
                uint16_t Offset = READ_SHORT();
                vm->currentFrame->ip -= Offset;
                break;
            }
            case OP_CALL: {
                int argumentCount = READ_BYTE();
                if (!CallValue(vm, Peek(vm, argumentCount), argumentCount)) {
                    if (vm->caughtException) {
                        vm->caughtException = false;
                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }
                    return RUNTIME_ERROR(NULL_VALUE);
                }
                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                break;
            }
            case OP_INVOKE: {
                KTN_ObjString* method = READ_STRING();
                int argumentCount = READ_BYTE();

                if (!Invoke(vm, method, argumentCount)) {
                    if (vm->caughtException) {
                        vm->caughtException = false;
                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }
                    return RUNTIME_ERROR(NULL_VALUE);
                }

                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                break;
            }
            case OP_SUPER_INVOKE: {
                KTN_ObjString* method = READ_STRING();
                int argumentCount = READ_BYTE();
                KTN_ObjKata* sokata = AS_CLASS(Pop(vm));

                if (strcmp(method->chars, "sokata") == 0) {
                    if (!IS_CLOSURE(sokata->constructor)) {
                        if (!KATANE_RUNTIME_ERROR("Cannot call super since sokata has no constructor", COLOR_MAGENTA "Ara ara~" COLOR_RESET " 'super' call failed: missing constructor~!")) return RUNTIME_ERROR(NULL_VALUE);
                    }

                    if (!Call(vm, AS_CLOSURE(sokata->constructor), argumentCount, sokata)) {
                        return RUNTIME_ERROR(NULL_VALUE);
                    }

                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                    break;
                }

                if (!InvokeFromClass(vm, sokata, method, argumentCount)) {
                    return RUNTIME_ERROR(NULL_VALUE);
                }
                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                KTN_ObjShiki* function = AS_FUNCTION(READ_CONSTANT_LONG());
                KTN_ObjClosure* closure = ClosureNew(vm, function);
                Push(vm, OBJECT_VALUE(closure));

                closure->owner = vm->currentFrame->owner;

                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal)
                        closure->upvalues[i] = CaptureUpvalue(vm, vm->currentFrame->slots + index);
                    else
                        closure->upvalues[i] = vm->currentFrame->closure->upvalues[index];
                }
                break;
            }
            case OP_ARRAY: {
                int numOfItems = READ_SHORT();
                KTN_ObjArray* array = ArrayNew(vm);

                // We jump over all of the item values and move to the NULL placeholder value.
                vm->stackTop[-numOfItems - 1] = OBJECT_VALUE(array);

                for (int i = numOfItems - 1; i >= 0; i--) {
                    KTN_ArrayAdd(vm, array, Peek(vm, i));
                }
                PopN(vm, numOfItems);
                break;
            }

            case OP_MAP: {
                int numOfPairs = READ_SHORT();
                int numOfItems = numOfPairs * 2;

                KTN_ObjMap* map = MapNew(vm);

                vm->stackTop[-numOfItems - 1] = OBJECT_VALUE(map);

                for (int i = numOfItems - 1; i >= 0; i -= 2) {
                    KTN_Value key = Peek(vm, i);
                    KTN_Value value = Peek(vm, i - 1);

                    KTN_HashMapSet(vm, &map->map, key, value);
                }

                PopN(vm, numOfPairs * 2);
                break;
            }

            case OP_CLASS: {
                Push(vm, OBJECT_VALUE(KataNew(vm, READ_STRING())));
                break;
            }
            case OP_INHERIT: {
                KTN_Value sokata = Peek(vm, 1);

                if (!IS_CLASS(sokata)) {
                    if (!KATANE_RUNTIME_ERROR("Classes can only inherit from other classes.", COLOR_MAGENTA "Tsk tsk~" COLOR_RESET " Classes can only inherit elegance from other classes... stop trying to mix with peasants, darling~ ♡")) return RUNTIME_ERROR(NULL_VALUE);
                }

                KTN_ObjKata* subclass = AS_CLASS(Peek(vm, 0));
                TableAddAll(vm, &AS_CLASS(sokata)->methods, &subclass->methods);
                TableAddAll(vm, &AS_CLASS(sokata)->properties, &subclass->properties);
                subclass->sokata = AS_CLASS(sokata);
                Pop(vm);
                break;
            }
            case OP_MARK_PRIVATE: {
                KTN_ObjString* name = READ_STRING();
                KTN_Value isHidden = Pop(vm);
                KTN_ObjKata* kata = AS_CLASS(Peek(vm, 1));

                TableSet(vm, &kata->privateMembers, name, isHidden);

                break;
            }

            case OP_METHOD: {
                KTN_ObjString* name = READ_STRING();
                bool isStatic = (READ_BYTE() == 1);

                if (isStatic) {
                    if (!DefineStaticMethod(vm, name)) {
                        return RUNTIME_ERROR(NULL_VALUE);
                    }
                    break;
                }

                if (!DefineMethod(vm, name)) {
                    return RUNTIME_ERROR(NULL_VALUE);
                }
                break;
            }
            case OP_CLOSE_UPVALUE: {
                KTN_CloseUpvalues(vm, vm->stackTop - 1);
                Pop(vm);
                break;
            }
            case OP_RETURN: {
                KTN_InterpretResult result = DoReturn(vm, exitFrame);

                if (result.status == INTERPRET_OK && vm->frameCount > exitFrame)
                    break;

                return result;
            }

            case OP_DEFER_ACTION: {
                uint8_t type = READ_BYTE();
                uint8_t targetScopeDepth = READ_BYTE();

                KTN_Value returnValue = NULL_VALUE;
                uint8_t* jumpTarget = NULL;

                // 0x00: Return, 0x01: Jump forwards, 0x02: Jump backwards.
                if (type == 0x00) {
                    returnValue = Pop(vm);
                } else {
                    uint16_t offset = READ_SHORT();
                    jumpTarget = (type == 0x01) ? vm->currentFrame->ip + offset : vm->currentFrame->ip - offset;
                }

                bool shouldBreak = false;

                while (vm->errorCount > 0 && vm->errors[vm->errorCount - 1]->frame == vm->currentFrame) {
                    KTN_ErrorFrame* errorFrame = vm->errors[vm->errorCount - 1];

                    if (errorFrame->catchScopeDepth <= (int)targetScopeDepth) {
                        break;
                    }

                    if (errorFrame->finallyTarget != NULL && errorFrame->state != KTN_EF_IN_FINALLY) {
                        errorFrame->state = KTN_EF_IN_FINALLY;
                        errorFrame->targetScopeDepth = (int)targetScopeDepth;

                        if (type == 0x00) {
                            errorFrame->deferredAction = KTN_DEFERRED_RETURN;
                            errorFrame->deferredValue = returnValue;
                        } else {
                            errorFrame->deferredAction = KTN_DEFERRED_JUMP;
                            errorFrame->deferredJumpTarget = jumpTarget;
                        }

                        KTN_CloseUpvalues(vm, errorFrame->stackHead);
                        vm->stackTop = errorFrame->stackHead;
                        vm->currentFrame->ip = errorFrame->finallyTarget;

                        shouldBreak = true;
                        break;
                    }

                    ErrorPop(vm);
                    free(errorFrame);
                }

                if (shouldBreak) break;

                if (type == 0x00) {
                    Push(vm, returnValue);
                    KTN_InterpretResult result = DoReturn(vm, exitFrame);

                    if (result.status == INTERPRET_OK && vm->frameCount > exitFrame)
                        break;

                    return result;
                }

                vm->currentFrame->ip = jumpTarget;
                break;
            }

            case OP_CALL_IMPORT:
                (void)READ_CONSTANT_LONG();
                break;

            case OP_NATIVE_MODULE:          { break; }
            case OP_SELECT_IMPORT:          { break; }
            case OP_SELECT_NATIVE_IMPORT:   { break; }
            case OP_IMPORT_ALL:             { break; }
            case OP_IMPORT_ALL_NATIVE:      { break; }
            case OP_EJECT_IMPORT:           { break; }
            case OP_EJECT_NATIVE_IMPORT:    { break; }

            case OP_BEGIN_CATCH: {
                uint16_t catchOffset = READ_SHORT();
                uint8_t* catchBase = vm->currentFrame->ip;
                uint16_t finallyOffset = READ_SHORT();
                uint8_t* finallyBase = vm->currentFrame->ip;

                KTN_ErrorFrame* errorFrame = malloc(sizeof(KTN_ErrorFrame));

                if (errorFrame == NULL) {
                    fprintf(stderr, "[FATAL] Out of memory allocating catch frame.\n");
                    exit(1);
                }

                errorFrame->frame = vm->currentFrame;
                errorFrame->catchTarget = (catchBase + catchOffset);
                errorFrame->finallyTarget = (finallyOffset == 0) ? NULL : (finallyBase + finallyOffset);
                errorFrame->stackHead = vm->stackTop;
                errorFrame->catchScopeDepth = 0;
                errorFrame->state = KTN_EF_WAITING;

                errorFrame->deferredAction = KTN_DEFERRED_NONE;
                errorFrame->deferredValue = NULL_VALUE;
                errorFrame->deferredJumpTarget = NULL;
                errorFrame->targetScopeDepth = 0;

                errorFrame->value = NULL_VALUE;

                ErrorPush(vm, errorFrame);
                break;
            }

            case OP_END_CATCH: {
                if (
                    vm->errorCount > 0 &&
                    vm->errors[vm->errorCount - 1]->frame == vm->currentFrame &&
                    vm->errors[vm->errorCount - 1]->state != KTN_EF_IN_FINALLY
                ) {
                    KTN_ErrorFrame* errorFrame = ErrorPop(vm);
                    free(errorFrame);
                }
                break;
            }

            case OP_END_FINALLY: {
                KTN_VMPanic(vm, "OP_BEGIN_FINALLY not yet implemented...");

                if (vm->errorCount > 0 && vm->errors[vm->errorCount - 1]->frame == vm->currentFrame) {
                    KTN_ErrorFrame* errorFrame = ErrorPop(vm);
                    free(errorFrame);
                }

                if (!vm->hasPendingException) break;

                vm->hasPendingException = false;
                KTN_Value thrownValue = vm->pendingException;
                vm->pendingException = NULL_VALUE;

                if (!KTN_ThrowValue(vm, AS_INSTANCE(thrownValue), true))
                    return RUNTIME_ERROR(NULL_VALUE);

                break;
            }

            case OP_RAISE: {
                bool buildTrace = (bool)READ_BYTE();
                KTN_Value thrownValue = Pop(vm);

                if (!IS_INSTANCE(thrownValue)) {
                    if (!KTN_RuntimeError(vm, "Ohoho~! You can only throw instances of Exception subclasses, not whatever that was~ ♡"))
                        return RUNTIME_ERROR(NULL_VALUE);
                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                    break;
                }

                KTN_ObjInstance* thrownInstance = AS_INSTANCE(thrownValue);
                if (vm->exceptionClass != NULL && !IsInstanceOfKata(thrownInstance->kata, vm->exceptionClass)) {
                    if (!KTN_RuntimeError(vm, "Tsk~! \"%s\" does not inherit from Exception and cannot be thrown.", thrownInstance->kata->className->chars))
                        return RUNTIME_ERROR(NULL_VALUE);

                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                    break;
                }

                if (!KTN_ThrowValue(vm, thrownInstance, buildTrace))
                    return RUNTIME_ERROR(NULL_VALUE);

                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_RETHROW: {
                if (!vm->caughtException) {
                    if (!ThrowException(vm, "RuntimeError", false, "\"rethrow\" used outside of a catch block."))
                        return RUNTIME_ERROR(NULL_VALUE);

                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                    break;
                }

                vm->caughtException = false;
                KTN_Value thrownInstance = Pop(vm);

                if (!KTN_ThrowValue(vm, AS_INSTANCE(thrownInstance), true))
                    return RUNTIME_ERROR(NULL_VALUE);

                vm->currentFrame = &vm->frames[vm->frameCount - 1];
                break;
            }

            case OP_ASSERT: {
                KTN_Value message = Pop(vm);
                KTN_Value condition = Pop(vm);

                if (IsFalsey(condition)) {
                    KTN_ObjString* messageString;
                    if (IS_STRING(message)) {
                        messageString = AS_STRING(message);
                    } else {
                        messageString = STRING_COPY(vm, "Assertion failed.", 17);
                    }

                    Push(vm, OBJECT_VALUE(messageString));
                    KTN_ObjInstance* assertion = KTN_ExceptionCreate(vm, "AssertionError", messageString);
                    Pop(vm);

                    if (!KTN_ThrowValue(vm, assertion, true))
                        return RUNTIME_ERROR(NULL_VALUE);

                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                }
                break;
            }

            case OP_INSTANCEOF: {
                KTN_Value classValue = Peek(vm, 0);
                KTN_Value target = Peek(vm, 1);

                if (!IS_CLASS(classValue)) {
                    PopN(vm, 2);
                    Push(vm, BOOL_VALUE(false));
                    break;
                }
                
                KTN_ObjKata* targetClass = AS_CLASS(classValue);
                KTN_ObjKata* instanceClass = (IS_INSTANCE(target)) ? AS_INSTANCE(target)->kata : PrimitiveClassOf(vm, target);

                PopN(vm, 2);
                Push(vm, BOOL_VALUE(IsInstanceOfKata(instanceClass, targetClass)));
                break;
            }

            case OP_BUILD_STACK_TRACE: {
                // Peek at TOS (the caught exception instance), push its stackTrace property.
                KTN_Value exceptionValue = Peek(vm, 0);
                KTN_Value stackTraceValue = NULL_VALUE;

                if (IS_INSTANCE(exceptionValue)) {
                    TableGet(&AS_INSTANCE(exceptionValue)->properties,
                        STRING_COPY_AUTO("stackTrace"), &stackTraceValue);
                }

                Push(vm, stackTraceValue);
                break;
            }

            case OP_CHECK_PARAMS: {
                uint8_t parameterCount = READ_BYTE();

                for (int i = 0; i < parameterCount; i++) {
                    uint8_t slot = READ_BYTE();
                    KTN_Value typeDescriptor = READ_CONSTANT_LONG();

                    if (!IS_TYPE_DESCRIPTOR(typeDescriptor))
                        continue;

                    KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(typeDescriptor);
                    KTN_Value argument = vm->currentFrame->slots[slot];

                    if (!KTN_TypeDescriptorCheck(vm, argument, descriptor)) {
                        char expectedBuffer[256];

                        KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                        if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(argument), "Parameter type error"))
                            return RUNTIME_ERROR(NULL_VALUE);

                        vm->currentFrame = &vm->frames[vm->frameCount - 1];
                        break;
                    }
                }

                break;
            }

            case OP_CHECK_TYPE: {
                KTN_Value descriptorValue = READ_CONSTANT_LONG();

                if (!IS_TYPE_DESCRIPTOR(descriptorValue)) break;

                KTN_ObjTypeDescriptor* descriptor = AS_TYPE_DESCRIPTOR(descriptorValue);
                KTN_Value topValue = Peek(vm, 0);

                if (!KTN_TypeDescriptorCheck(vm, topValue, descriptor)) {
                    char expectedBuffer[256];

                    KTN_TypeDescriptorFormat(descriptor, expectedBuffer, sizeof(expectedBuffer));

                    if (!KTN_ThrowTypeError(vm, expectedBuffer, KTN_ValueTypeName(topValue), "Type error"))
                        return RUNTIME_ERROR(NULL_VALUE);

                    vm->currentFrame = &vm->frames[vm->frameCount - 1];
                }
                break;
            }

            default:
                break;
        }
    }

    #undef READ_BYTE
    #undef READ_CONSTANT
    #undef READ_CONSTANT_LONG
    #undef READ_CONSTANT_BY_INDEX
    #undef READ_SHORT
    #undef READ_STRING
    #undef BINARY_OP
}

void registerModuleFile(KTN_VM* vm, KTN_ObjModule* module) {
    Push(vm, OBJECT_VALUE(STRING_COPY_AUTO("ktnIsMain")));
    Push(vm, BOOL_VALUE(module->isMain));
    TableSet(vm, &module->values, AS_STRING(Peek(vm, 1)), Peek(vm, 0));
    PopN(vm, 2);

    Push(vm, OBJECT_VALUE(STRING_COPY_AUTO("ktnFile")));
    if (module->file)
        Push(vm, OBJECT_VALUE(STRING_COPY(vm, module->file, (int)strlen(module->file))));
    else
        Push(vm, NULL_VALUE);

    TableSet(vm, &module->values, AS_STRING(Peek(vm, 1)), Peek(vm, 0));
    PopN(vm, 2);
}

void registerRoot(KTN_VM* vm) {
    if (!vm->rootFile) return;
    Push(vm, OBJECT_VALUE(STRING_COPY_AUTO("ktnRoot")));
    Push(vm, OBJECT_VALUE(STRING_COPY(vm, vm->rootFile, (int)strlen(vm->rootFile))));
    TableSet(vm, &vm->globals, AS_STRING(Peek(vm, 1)), Peek(vm, 0));
    PopN(vm, 2);
}

void registerStdArgs(KTN_VM* vm) {
    Push(vm, OBJECT_VALUE(STRING_COPY_AUTO("ktnArgs")));
    Push(vm, OBJECT_VALUE(ArrayNew(vm)));

    KTN_ObjArray* array = AS_ARRAY(Peek(vm, 0));

    for (int i = 0; i < vm->stdArgsCount; i++) {
        KTN_ArrayAdd(vm, array, OBJECT_VALUE(STRING_COPY_AUTO(vm->stdArgs[i])));
    }

    TableSet(vm, &vm->globals, AS_STRING(Peek(vm, 1)), Peek(vm, 0));
    PopN(vm, 2);
}

KTN_InterpretResult KTN_Interpret(KTN_VM* vm, KTN_ObjModule* module, const char* source) {
    vm->finalResult = NULL_VALUE;

    Push(vm, OBJECT_VALUE(module));

    if (vm->exceptionClass == NULL)
        KTN_InitializeExceptions(vm, module);

    KTN_TypeDescriptorPreResolvePrimitives(vm);

    KTN_ObjShiki* function = KTN_Compile(vm, source);

    if (function == NULL) {
        Pop(vm);
        return COMPILE_ERROR(NULL_VALUE);
    }

    if (vm->shouldExitAfterBytecode) {
        Pop(vm);
        return RUNTIME_OK(NULL_VALUE);
    }

    Push(vm, OBJECT_VALUE(function));
    KTN_ObjClosure* closure = ClosureNew(vm, function);
    Pop(vm);
    Push(vm, OBJECT_VALUE(closure));

    registerModuleFile(vm, module);
    registerStdArgs(vm);

    Call(vm, closure, 0, NULL);

    KTN_InterpretResult topLevel = Run(vm, 0);

    if (!module->isMain)
        return topLevel;

    return topLevel;
}
