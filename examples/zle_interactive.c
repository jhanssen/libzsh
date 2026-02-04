/*
 * zle_interactive.c - Interactive ZLE line editor example
 *
 * This example demonstrates using the ZLE line buffer and manipulation
 * functions for an interactive line editor. It uses a simplified input
 * loop rather than the full ZLE keymap system.
 *
 * Features:
 * - Character insertion
 * - Backspace/Delete
 * - Arrow keys for cursor movement
 * - Ctrl+A/E for beginning/end of line
 * - Ctrl+K to kill to end of line
 * - Ctrl+U to kill entire line
 * - Enter to accept line
 * - Ctrl+C/Ctrl+D to quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <termios.h>
#include <unistd.h>

#include "zsh.mdh"
#include "zle.mdh"

/* Forward declarations */
extern void createoptiontable(void);
extern void createaliastables(void);
extern void createreswdtable(void);
extern void init_thingies(void);
extern void init_keymaps(void);

extern unsigned char *cmdstack;
extern int cmdsp;
extern int strin;
#define CMDSTACKSZ 256

/* ZLE globals */
extern ZLE_STRING_T zleline;
extern int zlecs;  /* cursor position */
extern int zlell;  /* line length */
extern int linesz; /* allocated size */

/* ZLE functions */
extern void sizeline(int sz);
extern char *zlelineasstring(ZLE_STRING_T instr, int inll, int incs,
                             int *outllp, int *outcsp, int useheap);
extern ZLE_STRING_T stringaszleline(char *instr, int incs,
                                    int *outll, int *outsz, int *outcs);
extern void spaceinline(int ct);
extern void foredel(int ct, int flags);
extern void backdel(int ct, int flags);

/* Terminal state */
static struct termios orig_termios;
static int raw_mode = 0;

static void disable_raw_mode(void)
{
    if (raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode = 0;
    }
}

static void enable_raw_mode(void)
{
    static int atexit_registered = 0;
    struct termios raw;

    if (raw_mode)
        return;

    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        return;

    raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        return;

    raw_mode = 1;
    if (!atexit_registered) {
        atexit(disable_raw_mode);
        atexit_registered = 1;
    }
}

static void init_zle_subsystem(void)
{
    int t0;

    setlocale(LC_ALL, "");

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

    /* Initialize ZLE */
    init_thingies();
    init_keymaps();
}

/* Refresh the display */
static void refresh_line(const char *prompt)
{
    int outll, outcs;
    char *str = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);

    /* Move to beginning of line, clear line */
    printf("\r\033[K");

    /* Print prompt and line */
    printf("%s%s", prompt, str);

    /* Move cursor to correct position */
    if (zlecs < zlell) {
        printf("\033[%dD", zlell - zlecs);
    }

    fflush(stdout);
}

/* Insert a character at cursor position */
static void insert_char(int c)
{
    spaceinline(1);
    zleline[zlecs] = (ZLE_CHAR_T)c;
    zlecs++;
}

/* Read a line interactively using ZLE */
static char *readline_zle(const char *prompt)
{
    int c;
    int done = 0;

    /* Initialize line buffer */
    sizeline(256);
    zlell = 0;
    zlecs = 0;
    zleline[0] = ZWC('\0');

    printf("%s", prompt);
    fflush(stdout);

    while (!done) {
        c = getchar();
        if (c == EOF) {
            return NULL;
        }

        switch (c) {
        case '\r':
        case '\n':
            /* Accept line (use \r\n since we're in raw mode) */
            printf("\r\n");
            done = 1;
            break;

        case 3:  /* Ctrl+C */
            printf("^C\r\n");
            return NULL;

        case 4:  /* Ctrl+D */
            if (zlell == 0) {
                printf("\r\n");
                return NULL;
            }
            /* Delete char at cursor */
            if (zlecs < zlell) {
                foredel(1, 0);
                refresh_line(prompt);
            }
            break;

        case 1:  /* Ctrl+A - beginning of line */
            zlecs = 0;
            refresh_line(prompt);
            break;

        case 5:  /* Ctrl+E - end of line */
            zlecs = zlell;
            refresh_line(prompt);
            break;

        case 11:  /* Ctrl+K - kill to end of line */
            if (zlecs < zlell) {
                foredel(zlell - zlecs, 0);
                refresh_line(prompt);
            }
            break;

        case 21:  /* Ctrl+U - kill whole line */
            if (zlell > 0) {
                zlecs = 0;
                foredel(zlell, 0);
                refresh_line(prompt);
            }
            break;

        case 127:  /* Backspace */
        case 8:    /* Ctrl+H */
            if (zlecs > 0) {
                backdel(1, 0);
                refresh_line(prompt);
            }
            break;

        case 27:  /* Escape sequence */
            c = getchar();
            if (c == EOF)
                return NULL;
            if (c == '[') {
                c = getchar();
                if (c == EOF)
                    return NULL;
                switch (c) {
                case 'A':  /* Up arrow - ignored for now */
                case 'B':  /* Down arrow - ignored for now */
                    break;
                case 'C':  /* Right arrow */
                    if (zlecs < zlell) {
                        zlecs++;
                        refresh_line(prompt);
                    }
                    break;
                case 'D':  /* Left arrow */
                    if (zlecs > 0) {
                        zlecs--;
                        refresh_line(prompt);
                    }
                    break;
                case 'H':  /* Home */
                    zlecs = 0;
                    refresh_line(prompt);
                    break;
                case 'F':  /* End */
                    zlecs = zlell;
                    refresh_line(prompt);
                    break;
                case '3':  /* Delete key (followed by ~) */
                    c = getchar();  /* consume the ~ */
                    if (c == EOF)
                        return NULL;
                    if (zlecs < zlell) {
                        foredel(1, 0);
                        refresh_line(prompt);
                    }
                    break;
                }
            }
            break;

        default:
            /* Insert printable characters */
            if (c >= 32 && c < 127) {
                insert_char(c);
                refresh_line(prompt);
            }
            break;
        }
    }

    /* Convert ZLE line to string and return a copy */
    int outll, outcs;
    char *str = zlelineasstring(zleline, zlell, zlecs, &outll, &outcs, 1);
    return ztrdup(str);
}

int main(int argc, char *argv[])
{
    char *line;

    printf("ZLE Interactive Line Editor Example\n");
    printf("====================================\n\n");
    printf("Keys:\n");
    printf("  Ctrl+A      Beginning of line\n");
    printf("  Ctrl+E      End of line\n");
    printf("  Ctrl+K      Kill to end of line\n");
    printf("  Ctrl+U      Kill entire line\n");
    printf("  Backspace   Delete char before cursor\n");
    printf("  Delete      Delete char at cursor\n");
    printf("  Left/Right  Move cursor\n");
    printf("  Enter       Accept line\n");
    printf("  Ctrl+C/D    Quit\n\n");

    /* Initialize */
    init_zle_subsystem();
    enable_raw_mode();

    /* Interactive loop */
    while ((line = readline_zle("zle> ")) != NULL) {
        disable_raw_mode();  /* Temporarily for output */

        if (strlen(line) > 0) {
            printf("You entered: \"%s\"\n", line);

            /* Demo: parse the input */
            pushheap();
            lexinit();
            strin = 1;

            /* Allocate space for line + newline + null */
            size_t len = strlen(line);
            char *input = zalloc(len + 2);
            memcpy(input, line, len);
            input[len] = '\n';
            input[len + 1] = '\0';
            inpush(input, 0, NULL);

            Eprog prog = parse_list();
            if (prog && !empty_eprog(prog)) {
                char *text = getpermtext(prog, prog->prog, 0);
                printf("Parsed as: %s\n", text);
                freeeprog(prog);
            } else {
                printf("(Could not parse)\n");
            }

            inpop();
            popheap();
        }

        zsfree(line);
        enable_raw_mode();
    }

    disable_raw_mode();
    printf("\nGoodbye!\n");
    return 0;
}
