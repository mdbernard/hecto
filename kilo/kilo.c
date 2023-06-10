/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) &0x1f)

enum EditorKey {
    ARROW_LEFT = 1000,  // arbitrarily large values out of range for char
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

struct EditorConfig {
    // cursor position
    int cx, cy;

    // terminal screen size
    int screenrows;
    int screencols;

    // terminal settings at program start time
    struct termios orig_termios;
};

struct EditorConfig E;

/*** terminal ***/

/** Print an error message and exit the program. */
void die (const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

/** Disable raw mode (return terminal to startup settings). */
void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

/** Enable raw mode (process 1 character at a time, raw). */
void enable_raw_mode() {
    // save the original terminal settings
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");

    atexit(disable_raw_mode);

    struct termios raw = E.orig_termios;

    // disable a bunch of default terminal settings
    raw.c_iflag &= ~(
        ICRNL   // automatic conversion of '\r' (13) into '\n' (10)
        | IXON  // XON (Ctrl-S) and XOFF (Ctrl-Q) flow control
        // miscellaneous historical flags defining "raw" mode, likely already default
        | BRKINT
        | INPCK
        | ISTRIP
    );
    raw.c_oflag &= ~(OPOST);  // output processing (conversion of '\n' into "\r\n")
    raw.c_lflag &= ~(
        ECHO      // echo
        | ICANON  // canonical mode
        | IEXTEN  // implementation-defined input processing (Ctrl-V)
        | ISIG    // signals like SIGINT (Ctrl-C) and SIGTSTP (Ctrl-Z)
    );

    // set character size to 8 bits per byte (likely already default)
    raw.c_cflag |= (CS8);

    raw.c_cc[VMIN] = 0;  // process any input immediately with `read`
    raw.c_cc[VTIME] = 1;  // timeout `read` after 1/10th of a second

    // Write the modified settings in `raw` to STDIN.
    // TSCAFLUSH == "wait for all output to be written,
    // ignore unread inputs, then apply this change".
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/** Wait for a keypress to the terminal, then return it. */
int editor_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }

            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }

    return c;
}

int get_cursor_position(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != "\x1b" || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int get_window_size(int* rows, int* cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return get_cursor_position(rows, cols);
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/*** appendable string buffer ***/

struct AppendBuffer {
    char* buf;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abuf_append(struct AppendBuffer* abuf, const char* s, int len) {
    char* new = realloc(abuf->buf, abuf->len + len);
    if (new == NULL) return;  // realloc failed
    memcpy(&new[abuf->len], s, len);
    abuf->buf = new;
    abuf->len += len;
}

void abuf_free(struct AppendBuffer* abuf) {
    free(abuf->buf);
}

/*** output ***/

void editor_draw_rows(struct AppendBuffer* abuf) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf(
                welcome,
                sizeof(welcome),
                "Kilo editor -- version %s",
                KILO_VERSION
            );
            if (welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if (padding) {
                abuf_append(abuf, "~", 1);
                padding--;
            }
            while (padding--) abuf_append(abuf, " ", 1);
            abuf_append(abuf, welcome, welcomelen);
        } else {
            abuf_append(abuf, "~", 1);
        }

        // erase the rest of the line
        // https://vt100.net/docs/vt100-ug/chapter3.html#EL
        abuf_append(abuf, "\x1b[K", 3);
        if (y < E.screenrows - 1) {
            abuf_append(abuf, "\r\n", 2);
        }
    }
}

void editor_refresh_screen() {
    struct AppendBuffer abuf = ABUF_INIT;

    // hide the cursor to prevent flickering during refresh
    // https://vt100.net/docs/vt510-rm/DECTCEM.html
    abuf_append(&abuf, "\x1b[?25l", 6);

    // move cursor to top left of screen
    // https://vt100.net/docs/vt100-ug/chapter3.html#CUP
    abuf_append(&abuf, "\x1b[H", 3);

    editor_draw_rows(&abuf);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);  // terminal is 1-indexed
    abuf_append(&abuf, buf, strlen(buf));

    // show the cursor again
    // https://vt100.net/docs/vt510-rm/DECTCEM.html
    abuf_append(&abuf, "\x1b[?25h", 6);

    write(STDOUT_FILENO, abuf.buf, abuf.len);
    abuf_free(&abuf);
}

/*** input ***/

void editor_move_cursor(int key) {
    switch (key) {
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_LEFT:
            if (E.cx != 0) E.cx--;
            break;
        case ARROW_DOWN:
            if (E.cy != E.screencols - 1) E.cy++;
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screenrows - 1) E.cx++;
            break;
    }
}

/** Process a user's keypress to the terminal. */
void editor_process_keypress () {
    int c = editor_read_key();
    switch (c) {
        case CTRL_KEY('c'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
            int times = E.screenrows;
            while (times--)
                editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }

        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editor_move_cursor(c);
            break;
    }
}

/*** init ***/

void init_editor() {
    E.cx = 0;
    E.cy = 0;

    if (get_window_size(&E.screenrows, &E.screencols) == -1) die("get_window_size");
}

int main() {
    enable_raw_mode();
    init_editor();

    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
