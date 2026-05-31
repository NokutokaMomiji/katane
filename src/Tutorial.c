#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "Tutorial.h"
#include "Object.h"
#include "VM.h"
#include "Common.h"   // COLOR_* macros

// ─────────────────────────────────────────────────────────────────────────────
//  Forward declarations (defined in VM.c)
// ─────────────────────────────────────────────────────────────────────────────
KTN_InterpretResult KTN_Interpret(KTN_VM* vm, KTN_ObjModule* module,
                                  const char* source);
void                ModuleAdd(KTN_VM* vm, KTN_ObjModule* module, char* name);
void                registerModuleFile(KTN_VM* vm, KTN_ObjModule* module);
void                registerRoot(KTN_VM* vm);

// ─────────────────────────────────────────────────────────────────────────────
//  Lesson data
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    const char* title;
    const char* explanation;
    const char* default_code;
    const char* tip;
    bool        is_stub;
} KTN_Lesson;

#define LESSON(t, e, c, tip_) { (t), (e), (c), (tip_), false }
#define STUB(t, e)             { (t), (e), "", "", true  }

static const KTN_Lesson LESSONS[] = {

    LESSON(
        "Hello, Katane!",

        "Every language starts here. print() writes a value to stdout.\n"
        "Strings use double quotes. Semicolons are optional.",

        "print(\"Hello, Katane!\");",

        "Try changing the message. What happens if you pass a number instead?"
    ),

    LESSON(
        "Variables & types",

        "Declare variables with mochi. Katane is dynamically typed by default,\n"
        "but you can add an optional type annotation: mochi x: Int = 10.\n"
        "Built-in types: Int, Float, String, Bool, null.",

        "mochi name   = \"Alice\";\n"
        "mochi age    = 30;\n"
        "mochi active = true;\n"
        "mochi score: Float = 9.5;\n"
        "\n"
        "print(name);\n"
        "print(age);\n"
        "print(active);\n"
        "print(score);",

        "Try re-assigning name to a number. With a type annotation it will error."
    ),

    LESSON(
        "Operators",

        "Arithmetic: + - * / ** (power) /~ (floor division) % (floored mod).\n"
        "Comparison: == != < > <= >=.  Identity: is.\n"
        "Logical: and  or  !.  Compound assignment: += -= *= /~= **=.\n"
        "Integer literals: 0xFF  0b1010  0o17  1_000_000.",

        "mochi a = 17;\n"
        "mochi b = 5;\n"
        "\n"
        "print(a + b);             // 22\n"
        "print(a /~ b);            // floor div  => 3\n"
        "print(a % b);             // floored mod => 2\n"
        "print(a ** 2);            // exponent    => 289\n"
        "print(a > b and b != 0);  // true",

        "Change the values and predict the output before running."
    ),

    LESSON(
        "Control flow",

        "if / else if / else, for(init; cond; step), while(cond).\n"
        "break exits a loop. continue skips to the next iteration.\n"
        "switch uses implicit isolation — no break needed between cases.\n"
        "choice is an expression form of switch that returns a value.",

        "mochi sum = 0;\n"
        "for (mochi i = 1; i <= 10; i++) {\n"
        "    if (i % 2 == 0) continue;   // skip evens\n"
        "    sum = sum + i;\n"
        "}\n"
        "print(sum);   // 1+3+5+7+9 = 25\n"
        "\n"
        "mochi label = choice (sum /~ 10) {\n"
        "    case 3: \"thirty-something\",\n"
        "    case 2: \"twenty-something\",\n"
        "    default: \"other\"\n"
        "};\n"
        "print(label);",

        "Try changing the loop to include evens and predict the new sum."
    ),

    LESSON(
        "Functions",

        "Declare functions with shiki. Type annotations on parameters and the\n"
        "return type are optional. The fat-arrow => form returns the expression\n"
        "directly. Functions are first-class values.",

        "shiki greet(name: String): String {\n"
        "    return \"Hello, \" + name + \"!\";\n"
        "}\n"
        "\n"
        "shiki square(x) => x * x;\n"
        "\n"
        "mochi double = shiki(n) => n * 2;\n"
        "\n"
        "print(greet(\"World\"));\n"
        "print(square(9));\n"
        "print(double(21));",

        "Try writing a recursive factorial function using shiki."
    ),

    LESSON(
        "Closures",

        "Inner functions capture variables from their enclosing scope.\n"
        "Each call to the outer function creates an independent captured variable —\n"
        "c1 and c2 below do not share their count.",

        "shiki makeCounter(start) {\n"
        "    mochi count = start;\n"
        "    return shiki() {\n"
        "        count++;\n"
        "        return count;\n"
        "    };\n"
        "}\n"
        "\n"
        "mochi c1 = makeCounter(0);\n"
        "mochi c2 = makeCounter(100);\n"
        "\n"
        "print(c1());   // 1\n"
        "print(c1());   // 2\n"
        "print(c2());   // 101\n"
        "print(c1());   // 3 — c2 did not affect c1",

        "Try creating a makeAdder(n) that returns a function adding n to its argument."
    ),

    LESSON(
        "Classes",

        "Define a class with kata. Fields use mochi. The constructor shares the\n"
        "class name. Use this to access instance members.\n"
        "Inherit with kata Child : Parent. Call the parent constructor with sokata().\n"
        "Override a method by redefining it in the subclass.",

        "kata Animal {\n"
        "    mochi name = \"\";\n"
        "\n"
        "    Animal(name) { this.name = name; }\n"
        "\n"
        "    shiki speak() {\n"
        "        return this.name + \" makes a sound.\";\n"
        "    }\n"
        "}\n"
        "\n"
        "kata Dog : Animal {\n"
        "    Dog(name) { sokata(name); }\n"
        "\n"
        "    shiki speak() {\n"
        "        return this.name + \" barks!\";\n"
        "    }\n"
        "}\n"
        "\n"
        "mochi a = Animal(\"Cat\");\n"
        "mochi d = Dog(\"Rex\");\n"
        "\n"
        "print(a.speak());\n"
        "print(d.speak());",

        "Try adding a Cat subclass that meows, or a static field to count instances."
    ),

    LESSON(
        "Error handling",

        "throw ErrorType(message) raises an exception.\n"
        "try { ... } catch on TypeName (e) { ... } catches a specific type.\n"
        "A bare catch (e) catches anything. finally always runs.\n"
        "Built-in types: RuntimeError, ValueError, TypeError,\n"
        "RangeError, ArgumentError, PropertyError, AssertionError.",

        "shiki safeDivide(a, b) {\n"
        "    if (b == 0) throw ValueError(\"b must not be zero\");\n"
        "    return a / b;\n"
        "}\n"
        "\n"
        "try {\n"
        "    print(safeDivide(10, 2));   // 5\n"
        "    print(safeDivide(5, 0));    // throws\n"
        "} catch on ValueError (e) {\n"
        "    print(\"Caught: \" + e.message);\n"
        "} finally {\n"
        "    print(\"Done.\");\n"
        "}",

        "Try throwing a RangeError and catching it separately from ValueError."
    ),

    STUB("Enums & modules",       "enum, summon, and using blocks — coming soon."),
    STUB("Async programming",     "Vow, async/await — coming soon."),
    STUB("File I/O",              "fs.readFile, File objects, using — coming soon."),
    STUB("Reflection",            "typeOf, hasField, callMethod — coming soon."),
    STUB("Next steps",            "Where to go from here — coming soon."),
};

#define LESSON_COUNT ((int)(sizeof(LESSONS) / sizeof(LESSONS[0])))

// ─────────────────────────────────────────────────────────────────────────────
//  Per-lesson mutable state
// ─────────────────────────────────────────────────────────────────────────────

typedef enum { LS_TODO, LS_DONE, LS_SKIPPED } LessonStatus;

typedef struct {
    LessonStatus status;
    char*        user_code;   // malloc'd; NULL means use default
} LessonState;

static LessonState* s_state   = NULL;
static int          s_current = 0;

// ─────────────────────────────────────────────────────────────────────────────
//  Progress persistence
// ─────────────────────────────────────────────────────────────────────────────

#define PROGRESS_FILE ".katane_tutorial_progress"

static char* progress_path(void) {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    size_t len = strlen(home) + 1 + strlen(PROGRESS_FILE) + 1;
    char* p = (char*)malloc(len);
    snprintf(p, len, "%s/%s", home, PROGRESS_FILE);
    return p;
}

static void save_progress(void) {
    char* path = progress_path();
    FILE* f = fopen(path, "w");
    free(path);
    if (!f) return;

    fprintf(f, "current=%d\n", s_current);
    for (int i = 0; i < LESSON_COUNT; i++) {
        fprintf(f, "lesson[%d].status=%d\n", i, (int)s_state[i].status);
        if (s_state[i].user_code && s_state[i].user_code[0]) {
            fprintf(f, "lesson[%d].code=", i);
            for (const char* c = s_state[i].user_code; *c; c++) {
                if (*c == '\n') fputs("\\n", f);
                else            fputc(*c, f);
            }
            fputc('\n', f);
        }
    }
    fclose(f);
}

static void load_progress(void) {
    char* path = progress_path();
    FILE* f = fopen(path, "r");
    free(path);
    if (!f) return;

    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

        int idx, val;
        if (sscanf(line, "current=%d", &val) == 1) {
            if (val >= 0 && val < LESSON_COUNT) s_current = val;
        } else if (sscanf(line, "lesson[%d].status=%d", &idx, &val) == 2) {
            if (idx >= 0 && idx < LESSON_COUNT)
                s_state[idx].status = (LessonStatus)val;
        } else if (sscanf(line, "lesson[%d].code=", &idx) == 1
                   && idx >= 0 && idx < LESSON_COUNT) {
            const char* eq = strchr(line, '=');
            if (!eq) continue;
            const char* enc = eq + 1;
            size_t elen = strlen(enc);
            char* dec = (char*)malloc(elen + 1);
            size_t di = 0;
            for (size_t ei = 0; ei < elen; ei++) {
                if (enc[ei] == '\\' && ei + 1 < elen && enc[ei+1] == 'n') {
                    dec[di++] = '\n'; ei++;
                } else {
                    dec[di++] = enc[ei];
                }
            }
            dec[di] = '\0';
            free(s_state[idx].user_code);
            s_state[idx].user_code = dec;
        }
    }
    fclose(f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bracket balance check (used for multi-line input)
// ─────────────────────────────────────────────────────────────────────────────

static bool has_unclosed(const char* src, size_t len) {
    int parens = 0, braces = 0, squares = 0;
    for (size_t i = 0; i < len; i++) {
        switch (src[i]) {
            case '(': parens++;  break; case ')': parens--;  break;
            case '{': braces++;  break; case '}': braces--;  break;
            case '[': squares++; break; case ']': squares--; break;
            default: break;
        }
    }
    return (parens > 0 || braces > 0 || squares > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input reader
//  Returns a malloc'd string: either a single command word or a code block.
//  Returns NULL on EOF.
// ─────────────────────────────────────────────────────────────────────────────

// Commands that are recognised on a line by themselves
static bool is_command(const char* s) {
    return (strcmp(s, "run")    == 0 ||
            strcmp(s, "reset")  == 0 ||
            strcmp(s, "next")   == 0 ||
            strcmp(s, "n")      == 0 ||
            strcmp(s, "back")   == 0 ||
            strcmp(s, "b")      == 0 ||
            strcmp(s, "skip")   == 0 ||
            strcmp(s, "repeat") == 0 ||
            strcmp(s, "help")   == 0 ||
            strcmp(s, "exit")   == 0 ||
            strcmp(s, "quit")   == 0);
}

static char* read_input(void) {
    char*  buf      = NULL;
    size_t capacity = 0;
    size_t length   = 0;
    bool   first    = true;

    while (1) {
        printf(first
               ? COLOR_MAGENTA "tut> " COLOR_RESET
               : COLOR_GRAY    "...  " COLOR_RESET);
        fflush(stdout);

        char line[2048];
        if (!fgets(line, sizeof(line), stdin)) { free(buf); return NULL; }

        size_t ll = strlen(line);

        // Strip trailing newline to test for commands, then put it back
        char trimmed[2048];
        memcpy(trimmed, line, ll);
        trimmed[ll] = '\0';
        if (ll > 0 && trimmed[ll-1] == '\n') trimmed[--ll] = '\0';

        if (first && is_command(trimmed))
            return strdup(trimmed);

        bool is_blank = (ll == 0);

        // Grow buffer
        size_t raw_ll = strlen(line);   // original length with \n
        if (length + raw_ll + 1 > capacity) {
            capacity = capacity == 0 ? 2048 : capacity * 2;
            buf = (char*)realloc(buf, capacity + 1);
            if (!buf) { fprintf(stderr, "[ERROR]: OOM in tutorial.\n"); exit(74); }
        }
        memcpy(buf + length, line, raw_ll);
        length += raw_ll;

        if (is_blank && length > 0 && !has_unclosed(buf, length)) {
            buf[length] = '\0';
            return buf;
        }
        if (!is_blank && !has_unclosed(buf, length)) {
            buf[length] = '\0';
            return buf;
        }
        first = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  VM helpers
// ─────────────────────────────────────────────────────────────────────────────

static KTN_ObjModule* make_tutorial_module(KTN_VM* vm) {
    char* name = strdup("<tutorial>");
    char* root = strdup("<tutorial>");
    KTN_ObjModule* mod = ModuleNew(vm, name, root, NULL);
    mod->isMain = true;
    ModuleAdd(vm, mod, NULL);
    vm->rootFile = root;
    registerModuleFile(vm, mod);
    registerRoot(vm);
    return mod;
}

static bool execute(KTN_VM* vm, KTN_ObjModule* mod, const char* source) {
    KTN_InterpretResult r = KTN_Interpret(vm, mod, source);
    return r.status == INTERPRET_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI helpers
// ─────────────────────────────────────────────────────────────────────────────

static void print_rule(void) {
    printf(COLOR_GRAY
           "──────────────────────────────────────────────────────"
           COLOR_RESET "\n");
}

static void print_progress(void) {
    printf(COLOR_GRAY "[" COLOR_RESET);
    for (int i = 0; i < LESSON_COUNT; i++) {
        if (i == s_current)
            printf(COLOR_MAGENTA "●" COLOR_RESET);
        else if (s_state[i].status == LS_DONE)
            printf(COLOR_CUSTOM "✓" COLOR_RESET);
        else if (s_state[i].status == LS_SKIPPED)
            printf(COLOR_GRAY "–" COLOR_RESET);
        else
            printf(COLOR_GRAY "·" COLOR_RESET);
    }
    int done = 0;
    for (int i = 0; i < LESSON_COUNT; i++)
        if (s_state[i].status == LS_DONE) done++;
    printf(COLOR_GRAY "]  %d/%d done\n" COLOR_RESET, done, LESSON_COUNT);
}

static void show_code(int idx) {
    const char* code = (s_state[idx].user_code && s_state[idx].user_code[0])
                       ? s_state[idx].user_code
                       : LESSONS[idx].default_code;

    printf(COLOR_GRAY "  ┌─ code ─────────────────────────────────────────\n"
                      COLOR_RESET);
    int ln = 1;
    printf(COLOR_GRAY "  │ %2d  " COLOR_RESET, ln);
    for (const char* p = code; *p; p++) {
        if (*p == '\n') {
            printf("\n");
            if (*(p+1)) printf(COLOR_GRAY "  │ %2d  " COLOR_RESET, ++ln);
        } else {
            fputc(*p, stdout);
        }
    }
    if (code[0] && code[strlen(code)-1] != '\n') printf("\n");
    printf(COLOR_GRAY "  └───────────────────────────────────────────────\n\n"
                      COLOR_RESET);
}

static void show_lesson(int idx) {
    printf("\n");
    print_progress();
    print_rule();
    printf(COLOR_CUSTOM " Lesson %d/%d · %s" COLOR_RESET "\n",
           idx + 1, LESSON_COUNT, LESSONS[idx].title);
    print_rule();
    printf("\n%s\n\n", LESSONS[idx].explanation);

    if (LESSONS[idx].is_stub) {
        printf(COLOR_GRAY "  (This lesson is not yet written — type 'next' to continue.)\n\n"
               COLOR_RESET);
        return;
    }

    show_code(idx);
    printf(COLOR_GRAY
           "  Edit the code above, then type 'run'.  "
           "'help' lists all commands.\n\n"
           COLOR_RESET);
}

static void show_help(void) {
    printf("\n"
           COLOR_CUSTOM "  Tutorial commands\n" COLOR_RESET
           "  run      Execute the code shown above\n"
           "  reset    Restore this lesson's original example\n"
           "  next     Go to next lesson (n)\n"
           "  back     Go to previous lesson (b)\n"
           "  skip     Skip and mark lesson incomplete\n"
           "  repeat   Re-display the current lesson\n"
           "  help     Show this message\n"
           "  exit     Leave the tutorial\n"
           "\n"
           "  You can also type or paste code directly at the tut> prompt.\n"
           "  Multi-line input: a blank line submits when all brackets are balanced.\n\n");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Run the current lesson
// ─────────────────────────────────────────────────────────────────────────────

static void run_lesson(KTN_VM* vm, KTN_ObjModule* mod, int idx) {
    if (LESSONS[idx].is_stub) {
        printf(COLOR_GRAY "  (Stub lesson — nothing to run.)\n\n" COLOR_RESET);
        return;
    }

    const char* code = (s_state[idx].user_code && s_state[idx].user_code[0])
                       ? s_state[idx].user_code
                       : LESSONS[idx].default_code;

    printf("\n");
    fflush(stdout);
    bool ok = execute(vm, mod, code);
    printf("\n");

    if (ok) {
        printf(COLOR_CUSTOM "  ✓ ran successfully\n" COLOR_RESET);
        if (LESSONS[idx].tip[0])
            printf(COLOR_GRAY "  Tip: %s\n" COLOR_RESET, LESSONS[idx].tip);
        printf("\n");
        s_state[idx].status = LS_DONE;
        save_progress();
    } else {
        // KTN_Interpret already printed the error to stderr
        printf(COLOR_GRAY
               "  Fix the code and run again, type 'reset' to restore the\n"
               "  original example, or 'skip' to move on.\n\n"
               COLOR_RESET);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Store user code and immediately run it
// ─────────────────────────────────────────────────────────────────────────────

static void store_and_run(KTN_VM* vm, KTN_ObjModule* mod, int idx,
                           const char* code) {
    free(s_state[idx].user_code);
    s_state[idx].user_code = strdup(code);
    save_progress();
    show_code(idx);
    run_lesson(vm, mod, idx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────

void KTN_TutorialRun(KTN_VM* vm) {
    s_state = (LessonState*)calloc(LESSON_COUNT, sizeof(LessonState));
    if (!s_state) {
        fprintf(stderr, "[ERROR]: Out of memory starting tutorial.\n");
        return;
    }

    load_progress();

    KTN_ObjModule* mod = make_tutorial_module(vm);

    printf("\n" COLOR_CUSTOM "Welcome to the Katane interactive tutorial!\n" COLOR_RESET);
    printf("Type " COLOR_MAGENTA "help" COLOR_RESET " for commands, "
           COLOR_MAGENTA "exit" COLOR_RESET " to quit.\n");
    printf(COLOR_GRAY "Progress is saved to ~/" PROGRESS_FILE ".\n" COLOR_RESET);

    show_lesson(s_current);

    while (1) {
        char* input = read_input();

        if (!input) {
            printf("\n");
            break;
        }

        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            free(input);
            printf(COLOR_GRAY "Exiting tutorial.\n" COLOR_RESET);
            break;
        }
        if (strcmp(input, "help") == 0) {
            free(input); show_help(); continue;
        }
        if (strcmp(input, "run") == 0) {
            free(input); run_lesson(vm, mod, s_current); continue;
        }
        if (strcmp(input, "reset") == 0) {
            free(input);
            free(s_state[s_current].user_code);
            s_state[s_current].user_code = NULL;
            save_progress();
            printf(COLOR_GRAY "  Code restored to default.\n\n" COLOR_RESET);
            show_code(s_current);
            continue;
        }
        if (strcmp(input, "repeat") == 0) {
            free(input); show_lesson(s_current); continue;
        }
        if (strcmp(input, "next") == 0 || strcmp(input, "n") == 0) {
            free(input);
            if (s_current < LESSON_COUNT - 1) {
                s_current++;
                save_progress();
                show_lesson(s_current);
            } else {
                int done = 0;
                for (int i = 0; i < LESSON_COUNT; i++)
                    if (s_state[i].status == LS_DONE) done++;
                printf(COLOR_CUSTOM "  You've reached the end of the tutorial!\n" COLOR_RESET);
                printf("  %d/%d lessons completed. Type 'exit' to return to the REPL.\n\n",
                       done, LESSON_COUNT);
            }
            continue;
        }
        if (strcmp(input, "back") == 0 || strcmp(input, "b") == 0) {
            free(input);
            if (s_current > 0) {
                s_current--;
                save_progress();
                show_lesson(s_current);
            } else {
                printf(COLOR_GRAY "  Already on the first lesson.\n\n" COLOR_RESET);
            }
            continue;
        }
        if (strcmp(input, "skip") == 0) {
            free(input);
            s_state[s_current].status = LS_SKIPPED;
            printf(COLOR_GRAY "  Lesson skipped.\n" COLOR_RESET);
            save_progress();
            if (s_current < LESSON_COUNT - 1) {
                s_current++;
                save_progress();
                show_lesson(s_current);
            }
            continue;
        }

        // Not a command — it's code the user typed.
        store_and_run(vm, mod, s_current, input);
        free(input);
    }

    for (int i = 0; i < LESSON_COUNT; i++) free(s_state[i].user_code);
    free(s_state);
    s_state   = NULL;
    s_current = 0;
}
