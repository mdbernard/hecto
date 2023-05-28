/*** includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

/** To store terminal settings at program start time. */
struct termios orig_termios;

/*** terminal ***/

/** Print an error message and exit the program. */
void die (const char *s) {
    perror(s);
    exit(1);
}

/** Disable raw mode (return terminal to startup settings). */
void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
}

/** Enable raw mode (process 1 character at a time, raw). */
void enable_raw_mode() {
    // save the original terminal settings
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");

    atexit(disable_raw_mode);

    struct termios raw = orig_termios;

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

/*** init ***/

int main() {
    enable_raw_mode();

    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");

        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }

        if (c == 'q') break;
    }

    return 0;
}