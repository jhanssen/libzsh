/*
 * zle_example.c - Example program demonstrating ZLE (Zsh Line Editor)
 *
 * This example shows how to:
 * - Initialize the ZLE subsystem
 * - Set up keymaps
 * - Manipulate the line buffer programmatically
 * - Execute ZLE widgets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#include "zsh.mdh"
#include "zle.mdh"

/* Forward declarations */
extern void init_jobs(char **argv, char **envp);
extern void createoptiontable(void);
extern void createaliastables(void);
extern void createreswdtable(void);

/* ZLE initialization functions */
extern void init_thingies(void);
extern void init_keymaps(void);

extern unsigned char *cmdstack;
extern int cmdsp;
extern int strin;
#define CMDSTACKSZ 256

/* ZLE globals we need */
extern ZLE_STRING_T zleline;
extern int zlecs;  /* cursor position */
extern int zlell;  /* line length */
extern int linesz; /* allocated size */
extern Keymap curkeymap;
extern HashTable keymapnamtab;

/* ZLE functions */
extern void sizeline(int sz);
extern void zleaddtoline(int chr);
extern char *zlelineasstring(ZLE_STRING_T instr, int inll, int incs,
                             int *outllp, int *outcsp, int useheap);
extern ZLE_STRING_T stringaszleline(char *instr, int incs,
                                    int *outll, int *outsz, int *outcs);
extern void setline(char *s, int flags);
extern void spaceinline(int ct);
extern void foredel(int ct, int flags);
extern void backdel(int ct, int flags);

static char *fake_argv[] = { "zle-example", NULL };
static char *fake_envp[] = { "PATH=/bin:/usr/bin", "HOME=/tmp", "TERM=xterm", NULL };

/*
 * Initialize zsh/ZLE subsystems
 */
static void init_zle_subsystem(void)
{
    int t0;

    setlocale(LC_ALL, "");

    /* Initialize job control structures */
    init_jobs(fake_argv, fake_envp);

    /* Set up metafication type table */
    typtab['\0'] |= IMETA;
    typtab[STOUC(Meta)] |= IMETA;
    typtab[STOUC(Marker)] |= IMETA;
    for (t0 = (int)STOUC(Pound); t0 <= (int)STOUC(Nularg); t0++)
        typtab[t0] |= ITOK | IMETA;

    /* Set up file descriptor table */
    fdtable_size = zopenmax();
    fdtable = zshcalloc(fdtable_size * sizeof(*fdtable));
    fdtable[0] = fdtable[1] = fdtable[2] = FDT_EXTERNAL;

    /* Create option table */
    createoptiontable();

    /* Initialize lexer tables */
    initlextabs();

    /* Initialize hash tables */
    createreswdtable();
    createaliastables();

    /* Initialize command stack */
    cmdstack = (unsigned char *)zalloc(CMDSTACKSZ);
    cmdsp = 0;

    /* Initialize parser */
    init_parse();

    /* Initialize history */
    strin = 1;
    hbegin(0);

    /* Initialize ZLE thingies (widget table) */
    init_thingies();

    /* Initialize ZLE keymaps */
    init_keymaps();

    printf("ZLE subsystem initialized.\n\n");
}

/*
 * Demonstrate line buffer manipulation
 */
static void demo_line_buffer(void)
{
    printf("=== Line Buffer Demo ===\n\n");

    /* Allocate line buffer */
    sizeline(256);
    zlell = 0;
    zlecs = 0;

    /* Set initial line content (ZSL_COPY=1 to copy the string since setline modifies it) */
    setline("echo hello world", ZSL_COPY);

    /* Convert to string and print */
    int outll, outcs;
    char *str = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    printf("Initial line: \"%s\"\n", str);
    printf("  Length: %d, Cursor: %d\n\n", zlell, zlecs);

    /* Move cursor to position 5 */
    zlecs = 5;
    printf("After moving cursor to position 5:\n");
    printf("  Cursor: %d (at '%c')\n\n", zlecs, (char)zleline[zlecs]);

    /* Insert text at cursor */
    printf("Inserting ' beautiful' at cursor...\n");

    /* Make space for new text (using mutable array since stringaszleline modifies input) */
    char insert[] = " beautiful";
    int insert_len = strlen(insert);

    /* Convert insert string to ZLE format and insert */
    int new_ll, new_sz, new_cs;
    ZLE_STRING_T zle_insert = stringaszleline(insert, 0, &new_ll, &new_sz, &new_cs);

    /* Make room in the line */
    spaceinline(new_ll);

    /* Copy the new text */
    ZS_memcpy(zleline + zlecs, zle_insert, new_ll);
    zlecs += new_ll;

    str = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    printf("After insert: \"%s\"\n", str);
    printf("  Length: %d, Cursor: %d\n\n", zlell, zlecs);

    /* Delete "hello " (6 chars) */
    printf("Deleting 'hello ' (6 chars forward from position 16)...\n");
    zlecs = 16;  /* position at 'h' in 'hello' */
    foredel(6, 0);

    str = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    printf("After delete: \"%s\"\n", str);
    printf("  Length: %d, Cursor: %d\n\n", zlell, zlecs);
}

/*
 * Demonstrate keymap inspection
 */
static void demo_keymaps(void)
{
    printf("=== Keymap Demo ===\n\n");

    if (!keymapnamtab) {
        printf("Keymap table not initialized.\n");
        return;
    }

    printf("Available keymaps:\n");

    /* Try to open some standard keymaps */
    const char *keymap_names[] = { "main", "emacs", "viins", "vicmd", ".safe", NULL };

    for (int i = 0; keymap_names[i]; i++) {
        Keymap km = openkeymap((char *)keymap_names[i]);
        if (km) {
            printf("  - %s (found)\n", keymap_names[i]);
        } else {
            printf("  - %s (not found)\n", keymap_names[i]);
        }
    }
    printf("\n");

    /* Show current keymap */
    if (curkeymap) {
        printf("Current keymap is set.\n");
    } else {
        printf("No current keymap selected.\n");
    }
    printf("\n");
}

/*
 * Demonstrate widget/thingy lookup
 */
static void demo_widgets(void)
{
    printf("=== Widget Demo ===\n\n");

    /* Some built-in widget names to look up */
    const char *widget_names[] = {
        "self-insert",
        "backward-delete-char",
        "forward-char",
        "backward-char",
        "beginning-of-line",
        "end-of-line",
        "accept-line",
        "up-line-or-history",
        "down-line-or-history",
        NULL
    };

    printf("Looking up built-in widgets:\n");
    for (int i = 0; widget_names[i]; i++) {
        /* In ZLE, widgets are stored as "thingies" */
        Thingy t = (Thingy)gethashnode(thingytab, widget_names[i]);
        if (t) {
            printf("  - %-25s (found)\n", widget_names[i]);
        } else {
            printf("  - %-25s (not found)\n", widget_names[i]);
        }
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    printf("ZLE (Zsh Line Editor) Example\n");
    printf("==============================\n\n");

    /* Initialize */
    init_zle_subsystem();

    /* Run demos */
    demo_keymaps();
    demo_widgets();
    demo_line_buffer();

    printf("=== Done ===\n");
    return 0;
}
