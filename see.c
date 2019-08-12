//               SEE TEXT EDITOR                           //
// AKA: what if we thought HJKL through a little further?  //
// Credit goes to lots of online help and to Torvalds...   //
// He was crazy enough to use his own text editor, as am I //
//                Jay Lang 2019                            //

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios termstate;

void handleErr(const char *s) {
    perror(s);
    exit(1);
}

void activateRawMode() {
    if (tcgetattr(STDIN_FILENO, &termstate) == -1) handleErr("tcCapForRaw");
    struct termios rterm = termstate;

    rterm.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    rterm.c_oflag &= ~(OPOST);
    rterm.c_cflag |= (CS8);
    rterm.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    rterm.c_cc[VMIN] = 0;
    rterm.c_cc[VTIME] = 5;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &rterm) ==  -1) handleErr("tcSetForRaw"); 
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termstate) == -1) handleErr("tcSetForExitRaw");
}

int main() {
    atexit(disableRawMode);
    activateRawMode();

    char c;
    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) handleErr("read");
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d(%c)\r\n", c, c);
        }
        if (c == 'q') break;
    }
    return 0;
}
