/*
 * test_main.c - Tests for libzsh functionality
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>

#include "zsh.mdh"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("  Testing %s... ", #name); \
        fflush(stdout); \
        tests_run++; \
        if (test_##name()) { \
            printf("PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("FAILED\n"); \
        } \
    } while (0)

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "\n    Assertion failed: %s (line %d)\n", #cond, __LINE__); \
            return 0; \
        } \
    } while (0)

/*
 * Forward declarations for functions not in .epro headers
 */
extern void createoptiontable(void);
extern void createaliastables(void);
extern void createreswdtable(void);

/* Global variables we need to initialize */
extern unsigned char *cmdstack;
extern int cmdsp;
extern int strin;  /* flag: reading from string, not stdin */
#define CMDSTACKSZ 256

/*
 * Minimal initialization for testing parser/lexer
 * Note: We intentionally skip job control since it's not needed for parsing.
 */
static int initialized = 0;

static void init_for_tests(void)
{
    if (initialized)
        return;

#ifdef USE_LOCALE
    setlocale(LC_ALL, "");
#endif

    /* Set up metafication type table */
    int t0;
    typtab['\0'] |= IMETA;
    typtab[STOUC(Meta)] |= IMETA;
    typtab[STOUC(Marker)] |= IMETA;
    for (t0 = (int)STOUC(Pound); t0 <= (int)STOUC(Nularg); t0++)
        typtab[t0] |= ITOK | IMETA;

    /* Set up file descriptor table */
    fdtable_size = zopenmax();
    fdtable = zshcalloc(fdtable_size * sizeof(*fdtable));
    fdtable[0] = fdtable[1] = fdtable[2] = FDT_EXTERNAL;

    /* Create option table (needed for parser) */
    createoptiontable();

    /* Initialize lexer tables */
    initlextabs();

    /* Initialize hash tables needed for parsing */
    createreswdtable();   /* reserved words: if, then, else, fi, etc. */
    createaliastables();  /* aliases */

    /* Initialize command stack for parser */
    cmdstack = (unsigned char *)zalloc(CMDSTACKSZ);
    cmdsp = 0;

    /* Initialize parser state */
    init_parse();

    /* Initialize history mechanism (sets up hgetc, hungetc, etc.) */
    hbegin(0);  /* 0 = don't save history */

    initialized = 1;
}

/*
 * Test: Memory allocation with heap
 */
static int test_heap_allocation(void)
{
    init_for_tests();

    pushheap();

    /* Allocate some memory on the heap */
    char *str1 = zhalloc(100);
    ASSERT(str1 != NULL);

    char *str2 = zhalloc(200);
    ASSERT(str2 != NULL);

    /* Write to the memory */
    strcpy(str1, "Hello");
    strcpy(str2, "World");

    ASSERT(strcmp(str1, "Hello") == 0);
    ASSERT(strcmp(str2, "World") == 0);

    popheap();

    return 1;
}

/*
 * Test: Reserved word hash table lookup
 */
static int test_reswdtab(void)
{
    init_for_tests();

    /* Look up a reserved word */
    HashNode node = gethashnode(reswdtab, "if");
    ASSERT(node != NULL);

    node = gethashnode(reswdtab, "then");
    ASSERT(node != NULL);

    node = gethashnode(reswdtab, "fi");
    ASSERT(node != NULL);

    /* Non-reserved words should not be found */
    node = gethashnode(reswdtab, "echo");
    ASSERT(node == NULL);

    return 1;
}

/*
 * Test: Lexer tokenization
 */
static int test_lexer_simple(void)
{
    init_for_tests();

    pushheap();

    /* Initialize lexer state */
    lexinit();
    strin = 1;  /* reading from string, not stdin */
    hbegin(0);  /* initialize history mechanism */

    /* Push a simple command string */
    char *input = ztrdup("echo hello");
    inpush(input, 0, NULL);

    /* Get first token - should be a word "echo" */
    zshlex();
    ASSERT(tok == STRING || tok == TYPESET);
    ASSERT(tokstr != NULL);
    ASSERT(strcmp(tokstr, "echo") == 0);

    /* Get second token - should be "hello" */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(tokstr != NULL);
    ASSERT(strcmp(tokstr, "hello") == 0);

    /* Get end token */
    zshlex();
    ASSERT(tok == ENDINPUT || tok == NEWLIN);

    inpop();
    popheap();

    return 1;
}

/*
 * Test: Lexer with pipe
 */
static int test_lexer_pipe(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;
    hbegin(0);

    char *input = ztrdup("ls | grep foo");
    inpush(input, 0, NULL);

    /* ls */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(strcmp(tokstr, "ls") == 0);

    /* | */
    zshlex();
    ASSERT(tok == BAR);

    /* grep */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(strcmp(tokstr, "grep") == 0);

    /* foo */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(strcmp(tokstr, "foo") == 0);

    inpop();
    popheap();

    return 1;
}

/*
 * Test: Lexer with redirection
 */
static int test_lexer_redirect(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;
    hbegin(0);

    char *input = ztrdup("echo test > file.txt");
    inpush(input, 0, NULL);

    /* echo */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(strcmp(tokstr, "echo") == 0);

    /* test */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(strcmp(tokstr, "test") == 0);

    /* > */
    zshlex();
    ASSERT(tok == OUTANG);

    /* file.txt */
    zshlex();
    ASSERT(tok == STRING);
    ASSERT(strcmp(tokstr, "file.txt") == 0);

    inpop();
    popheap();

    return 1;
}

/*
 * Test: Parser with simple command
 */
static int test_parser_simple(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;  /* reading from string */

    char *input = ztrdup("echo hello world\n");
    inpush(input, 0, NULL);

    Eprog prog = parse_list();
    ASSERT(prog != NULL);
    ASSERT(!empty_eprog(prog));

    /* Convert back to text */
    char *text = getpermtext(prog, prog->prog, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "echo") != NULL);
    ASSERT(strstr(text, "hello") != NULL);
    ASSERT(strstr(text, "world") != NULL);

    freeeprog(prog);
    inpop();
    popheap();

    return 1;
}

/*
 * Test: Parser with pipeline
 */
static int test_parser_pipeline(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;

    char *input = ztrdup("cat file | grep pattern | wc -l\n");
    inpush(input, 0, NULL);

    Eprog prog = parse_list();
    ASSERT(prog != NULL);
    ASSERT(!empty_eprog(prog));

    char *text = getpermtext(prog, prog->prog, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "cat") != NULL);
    ASSERT(strstr(text, "grep") != NULL);
    ASSERT(strstr(text, "wc") != NULL);

    freeeprog(prog);
    inpop();
    popheap();

    return 1;
}

/*
 * Test: Parser with if statement
 */
static int test_parser_if(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;

    char *input = ztrdup("if true; then echo yes; else echo no; fi\n");
    inpush(input, 0, NULL);

    Eprog prog = parse_list();
    ASSERT(prog != NULL);
    ASSERT(!empty_eprog(prog));

    char *text = getpermtext(prog, prog->prog, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "if") != NULL);
    ASSERT(strstr(text, "then") != NULL);
    ASSERT(strstr(text, "fi") != NULL);

    freeeprog(prog);
    inpop();
    popheap();

    return 1;
}

/*
 * Test: Parser with for loop
 */
static int test_parser_for(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;

    char *input = ztrdup("for i in a b c; do echo $i; done\n");
    inpush(input, 0, NULL);

    Eprog prog = parse_list();
    ASSERT(prog != NULL);
    ASSERT(!empty_eprog(prog));

    char *text = getpermtext(prog, prog->prog, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "for") != NULL);
    ASSERT(strstr(text, "do") != NULL);
    ASSERT(strstr(text, "done") != NULL);

    freeeprog(prog);
    inpop();
    popheap();

    return 1;
}

/*
 * Test: Parser with function definition
 */
static int test_parser_function(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;

    char *input = ztrdup("myfunc() { echo hello; }\n");
    inpush(input, 0, NULL);

    Eprog prog = parse_list();
    ASSERT(prog != NULL);
    ASSERT(!empty_eprog(prog));

    char *text = getpermtext(prog, prog->prog, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "myfunc") != NULL);

    freeeprog(prog);
    inpop();
    popheap();

    return 1;
}

/*
 * Test: Parser with subshell
 */
static int test_parser_subshell(void)
{
    init_for_tests();

    pushheap();
    lexinit();
    strin = 1;

    char *input = ztrdup("(cd /tmp && ls)\n");
    inpush(input, 0, NULL);

    Eprog prog = parse_list();
    ASSERT(prog != NULL);
    ASSERT(!empty_eprog(prog));

    char *text = getpermtext(prog, prog->prog, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "cd") != NULL || strstr(text, "(") != NULL);

    freeeprog(prog);
    inpop();
    popheap();

    return 1;
}

/*
 * Test: ztrdup string duplication
 */
static int test_ztrdup(void)
{
    init_for_tests();

    const char *original = "test string";
    char *copy = ztrdup(original);

    ASSERT(copy != NULL);
    ASSERT(copy != original);
    ASSERT(strcmp(copy, original) == 0);

    zsfree(copy);

    return 1;
}

/*
 * Test: dupstring on heap
 */
static int test_dupstring(void)
{
    init_for_tests();

    pushheap();

    const char *original = "heap string";
    char *copy = dupstring(original);

    ASSERT(copy != NULL);
    ASSERT(copy != original);
    ASSERT(strcmp(copy, original) == 0);

    popheap();

    return 1;
}

/*
 * Main test runner
 */
int main(int argc, char *argv[])
{
    printf("Running libzsh tests...\n\n");

    printf("Memory tests:\n");
    TEST(heap_allocation);
    TEST(ztrdup);
    TEST(dupstring);

    printf("\nHash table tests:\n");
    TEST(reswdtab);

    printf("\nParser tests:\n");
    TEST(parser_simple);
    TEST(parser_pipeline);
    TEST(parser_if);
    TEST(parser_for);
    TEST(parser_function);
    TEST(parser_subshell);

    printf("\n========================================\n");
    printf("Tests passed: %d/%d\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
