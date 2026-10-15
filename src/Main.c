#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <locale.h>
#include "Common.h"
#include "Config.h"
#include "VM.h"
#include "Utilities.h"

#define COMPILE_ERROR_CODE 65
#define RUNTIME_ERROR_CODE 70
#define OOM_ERROR_CODE 74
#define INVALID_PARAM_ERROR_CODE 64

// Defined in VM.c
void registerModuleFile(KTN_VM* vm, KTN_ObjModule* module);
void registerRoot(KTN_VM* vm);

#ifdef _WIN32
#include <windows.h>
#endif

int returnCode = 0;

static void printHeader() {
    printf(COLOR_CUSTOM "Katane" COLOR_RESET " %s ", KATANE_VERSION);
    printf("(%s, %s) [%s] on %s\n", __DATE__, __TIME__, COMPILER, COMPILED_OS);
    printf("Run \"exit()\" to quit or \"credits()\" for more information\n");
}

static void printVersion() {
    printf("Katane %s\n", KATANE_VERSION);
}

static void printBytecodeVersion() {
    printf("Katane bytecode %s", KATANE_BYTECODE_VERSION);
}

static void showUsage(const char* executableName, bool fail) {
    FILE* stream = (fail) ? stderr : stdout;

    fprintf(stream,
        "Usage: %s [options] [script.ktn] [script-args...]\n"
        "\n"
        "Options:\n"
        "  --help, -h                 Show this help text\n"
        "  --version, -v              Show the Katane version\n"
        "  --bytecode-version         Show the Katane bytecode version\n"
        "  --repl                     Start the interactive REPL\n"
        "  --eval, -e <code>          Execute inline Katane source\n"
        "  --interactive, -i          Execute the code then start the REPL\n"
        "  --dump-bytecode            Print compiled bytecode before execution\n"
        "  --dump-bytecode-only       Print compiled bytecode and exit\n"
        "  --boring-mode              Make Katane be more \"professional\"\n",
        executableName);
}

static KTN_ObjModule* generateModule(KTN_VM* vm, const char* name, const char* rootFile) {
    char* duplicateName = strdup(name);
    char* duplicateRootFile = strdup(rootFile);
    
    KTN_ObjModule* module = ModuleNew(vm, duplicateName, duplicateRootFile, NULL);

    module->isMain = true;

    ModuleAdd(vm, module, NULL);

    vm->rootFile = duplicateRootFile;

    registerModuleFile(vm, module);
    registerRoot(vm);

    return module;
}

static bool hasUnclosed(const char* src, size_t length) {
    int parenthesis = 0;
    int braces = 0;
    int squares = 0;

    bool inString = false;

    for (size_t i = 0; i < length; i++) {
        if (inString && src[i] == '\\') {
            i++;
            continue;
        }

        if (src[i] == '"') {
            inString = !inString;
            continue;
        }

        if (inString) continue;

        switch (src[i]) {
            case '(': parenthesis++; break;
            case ')': parenthesis--; break;
            case '{': braces++; break;
            case '}': braces--; break;
            case '[': squares++; break;
            case ']': squares--; break;
            default: break;
        }
    }

    return (parenthesis > 0 || braces > 0 || squares > 0);
}

static void configureProgramArgs(KTN_VM* vm, int count, char* argv[]) {
    vm->stdArgs = argv;
    vm->stdArgsCount = count;
}

static void Repl(KTN_VM* vm) {
    char* source = NULL;
    size_t capacity = 0;
    size_t length = 0;
    bool firstLine = true;

    printHeader();

    /*if (returnCode == COMPILE_ERROR_CODE) {
        printf("[ " COLOR_CYAN "Note" COLOR_RESET " ]: Script failed to compile. No state is available\n");
    } else if (returnCode == RUNTIME_ERROR_CODE) {
        printf("[ " COLOR_CYAN "Note" COLOR_RESET " ]: Program state available.");
    }*/

    if (returnCode == COMPILE_ERROR_CODE) {
        printf(COLOR_CYAN "->" COLOR_RESET " Script failed to compile. No state is available\n");
    } else if (returnCode == RUNTIME_ERROR_CODE) {
        printf(COLOR_CYAN "->" COLOR_RESET " Program state available.\n");
    }

    KTN_ObjModule* module = generateModule(vm, "<script>", "<repl>");

    while (1) {
        printf((firstLine) ? COLOR_MAGENTA ">>> " COLOR_RESET : COLOR_GRAY "... " COLOR_RESET);

        char line[1024];
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        size_t lineLen = strlen(line);
        bool isBlank = (lineLen == 1 && line[0] == '\n');

        if (isBlank && length > 0 && !hasUnclosed(source, length)) {
            source[length] = '\0';

            (void)KTN_Interpret(vm, module, source);
            
            length = 0;
            firstLine = true;
            
            fflush(stdout);
            continue;
        }

        if (length + lineLen + 1 > capacity) {
            size_t oldCap = capacity;
            
            capacity = ((oldCap == 0) ? 1024 : oldCap * 2);
            source = (char*)realloc(source, capacity + 1);

            if (!source) {
                fprintf(stderr, "[ERROR]: Out of memory in REPL.\n");
                exit(OOM_ERROR_CODE);
            }
        }
        
        memcpy(source + length, line, lineLen);
        length += lineLen;

        if (!hasUnclosed(source, length)) {
            source[length] = '\0';
            KTN_InterpretResult result = KTN_Interpret(vm, module, source);

            if (result.status == INTERPRET_OK && !IS_NULL(result.value)) {
                ObjectRepr(result.value, false);
                printf("\n");
            }

            length = 0;
            firstLine = true;
            fflush(stdout);
            continue;
        }

        firstLine = false;
    }

    free(source);
}

static char* ReadFileContents(const char* path) {
    FILE* sourceFile = fopen(path, "rb");
    
    if (!sourceFile) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Could not open file \"%s\": %s\n", path, strerror(errno));
        exit(74);
    }

    fseek(sourceFile, 0L, SEEK_END);
    
    size_t fileSize = ftell(sourceFile);

    rewind(sourceFile);
    
    char* dataBuffer = (char*)malloc(fileSize + 1);
    
    if (!dataBuffer) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Not enough memory to read \"%s\".", path);
        exit(OOM_ERROR_CODE);
    }
    
    size_t bytesRead = fread(dataBuffer, sizeof(char), fileSize, sourceFile);
    
    if (bytesRead == 0 && fileSize > 0) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Failed to read file \"%s\".", path);
        exit(OOM_ERROR_CODE);
    }
    
    dataBuffer[bytesRead] = '\0';
    fclose(sourceFile);
    
    return dataBuffer;
}

static bool FileExists(const char* path) {
    FILE* sourceFile = fopen(path, "rb");

    if (!sourceFile) {
        if (errno == EACCES) return false;

        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Could not open file \"%s\": %s\n", path, strerror(errno));
        exit(OOM_ERROR_CODE);
    }

    return true;
}

// Cross-platform absolute path resolution.
static char* ResolvePath(const char* path) {
#ifdef _WIN32
    return _fullpath(NULL, path, _MAX_PATH);
#else
    char* resolved = realpath(path, NULL);
    return resolved ? resolved : strdup(path);
#endif
}

static int RunFile(KTN_VM* vm, const char* path) {
    const char* actualPath = path;
    char* allocatedPath = NULL;

    if (!FileExists(path)) {
        allocatedPath = StringAppend(strdup(path), "/" LIBRARY_DIRECTORY_INDEX KATANE_EXTENSION);
        actualPath = allocatedPath;
    }

    char* source = ReadFileContents(actualPath);
    char* resolvedPath = ResolvePath(actualPath);

    KTN_ObjModule* module = generateModule(vm, "", resolvedPath);

    KTN_InterpretResult result = KTN_Interpret(vm, module, source);

    fflush(stdout);
    free(source);
    free(resolvedPath);

    if (allocatedPath) free(allocatedPath);
    if (result.status == INTERPRET_COMPILE_ERROR) return COMPILE_ERROR_CODE;
    if (result.status == INTERPRET_RUNTIME_ERROR) return RUNTIME_ERROR_CODE;

    return 0;
}

static int RunEval(KTN_VM* vm, const char* source) {
    KTN_ObjModule* module = generateModule(vm, "", "<eval>");
    KTN_InterpretResult result = KTN_Interpret(vm, module, source);

    if (result.status == INTERPRET_COMPILE_ERROR) return COMPILE_ERROR_CODE;
    if (result.status == INTERPRET_RUNTIME_ERROR) return RUNTIME_ERROR_CODE;

    if (result.status == INTERPRET_OK && !IS_NULL(result.value) && !vm->shouldExitAfterBytecode) {
        ObjectRepr(result.value, false);
        printf("\n");
    }

    return 0;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, ".UTF-8");
    
    KTN_VM* vm = (KTN_VM*)malloc(sizeof(KTN_VM));

    if (vm == NULL) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Out of memory.");
        return OOM_ERROR_CODE;
    }

#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    memset(vm, 0, sizeof(KTN_VM));
    VMInit(vm);

    const char* scriptPath = NULL;
    const char* evalSource = NULL;
    bool forceRepl = false;
    bool interactive = false;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            showUsage("katane", false);
            VMFree(vm);
            return 0;
        }

        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            printVersion();
            VMFree(vm);
            return 0;
        }

        if (strcmp(arg, "--repl") == 0) {
            forceRepl = true;
            continue;
        }

        if (strcmp(arg, "--interactive") == 0 || strcmp(arg, "-i") == 0) {
            interactive = true;
            continue;
        }

        if (strcmp(arg, "--dump-bytecode") == 0) {
            vm->shouldPrintBytecode = true;
            continue;
        }

        if (strcmp(arg, "--dump-bytecode-only") == 0) {
            vm->shouldPrintBytecode = true;
            vm->shouldExitAfterBytecode = true;
            continue;
        }

        if (strcmp(arg, "--boring-mode") == 0) {
            vm->kataneMode = false;
            continue;
        }

        if (strcmp(arg, "--eval") == 0 || strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) {
                showUsage(argv[0], true);
                VMFree(vm);
                return INVALID_PARAM_ERROR_CODE;
            }

            evalSource = argv[++i];
            configureProgramArgs(vm, argc - (i + 1), &argv[i + 1]);
            break;
        }

        if (arg[0] == '-' && scriptPath == NULL) {
            fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Unknown option \"%s\".\n", arg);
            showUsage(argv[0], true);
            VMFree(vm);
            return INVALID_PARAM_ERROR_CODE;
        }

        scriptPath = arg;
        configureProgramArgs(vm, argc - (i + 1), &argv[i + 1]);
        break;
    }

    if (evalSource != NULL && scriptPath != NULL) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: Cannot use --eval together with a script path.\n");
        VMFree(vm);
        return INVALID_PARAM_ERROR_CODE;
    }

    if (forceRepl && (evalSource != NULL || scriptPath != NULL)) {
        fprintf(stderr, "[" COLOR_RED "ERROR" COLOR_RESET "]: --repl cannot be combined with --eval or a script path.\n");
        VMFree(vm);
        return INVALID_PARAM_ERROR_CODE;
    }

    if (forceRepl || (scriptPath == NULL && evalSource == NULL)) {
        Repl(vm);
    } else if (evalSource != NULL) {
        returnCode = RunEval(vm, evalSource);

        if (interactive) Repl(vm);
        else if (returnCode) return returnCode;
    } else {
        returnCode = RunFile(vm, scriptPath);

        if (interactive) Repl(vm);
        else if (returnCode) return returnCode;
    }

    printf("Freeing VM.");

    VMFree(vm);
    return 0;
}
