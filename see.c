//               SEE TEXT EDITOR                           //
// AKA: what if we thought HJKL through a little further?  //
// Credit goes to lots of online help and to Torvalds...   //
// He was crazy enough to use his own text editor, as am I //
//                  Jay Lang 2019                          //

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios termstate;

void activateRawMode() {
    tcgetattr(STDIN_FILENO, &termstate);
    termstate.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termstate); 
}

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termstate);
}

int main() {
    atexit(disableRawMode);
    activateRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}
