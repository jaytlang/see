// see text editor
// the gateway drug to vim. without colons.
// jay lang, 2019

/* Include */

#include <unistd.h>  // read(), stdin fd, 
#include <termios.h> // termios struct, tcget/set, tcflags 
#include <stdlib.h>  // atexit(), exit()
#include <stdio.h>   // printf(), perror()
#include <ctype.h>   // iscntrl()
#include <errno.h>   // errno, err flags

/* Global data */

struct termios oldConfig;                                     // termios struct describes current term config

/* Terminal Interface */

void suicide(const char *s) {                                 // Self kill function in the event of fatal err
  perror(s);                                                  // Print out error with errno checking
  exit(1);                                                    // Exit with err code 1
} 

void rawModeOff() {
  int statrt = tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldConfig);// Restore the backed up original config 
  if (statrt == -1) suicide("rawModeOff.tcsetattr");              // Error checking
}

void rawModeOn() {
  int statrt = tcgetattr(STDIN_FILENO, &oldConfig);           // get this by giving stdin fd pointed @ struct
  if (statrt == -1) suicide("rawmodeOn.tcgetattr");                // Error handling
  struct termios config = oldConfig;                          // back up the original config for restoration l8r
  
  /// Config changes. These are like bitmask opts.
  /// C_IFLAGS are input-specific flags
  config.c_iflag &= (~IXON);                                  // off goes software flow control
  config.c_iflag &= (~ICRNL);                                 // fix ctrl-M to be read as 13, no \r\n translates
  config.c_iflag &= (~BRKINT);                                // break condition fixes (applicable on olds)
  config.c_iflag &= (~INPCK);                                 // Parity checking enabled for accurate data trans
  config.c_iflag &= (~ISTRIP);                                // 8th bit of each input byte shouldn't be stripped

  /// C_CFLAG is a bitmask controlling basic features
  config.c_cflag |= CS8;                                      // Input byte size set to 8 bytes

  /// C_OFLAGS are output/processing flags. Turn em off!
  config.c_oflag &= (~OPOST);                                 // No carriage returns on \n, disable outprocs.

  /// C_LFLAGS are general/misc. state flags
  config.c_lflag &= (~ECHO);                                  // echo flag echoes keypresses. gets in the way. 
  config.c_lflag &= (~ICANON);                                // read byte by byte instead of by line. canon. off
  config.c_lflag &= (~ISIG);                                  // disable interrupt signals. quit yourself.
  config.c_lflag &= (~IEXTEN);                                // literal character escapes off + Ctrl-O fix

  /// Set read timeout times with c_cc/ctrl chars
  config.c_cc[VMIN] = 0;                                      // Bytes of input req'd before read returns
  config.c_cc[VTIME] = 10;                                    // Allowing a timeout. After 1 second.

  statrt = tcsetattr(STDIN_FILENO, TCSAFLUSH, &config);       // set new atts. TCSAFLUSH waits, flushes new ins.
  if (statrt == -1) suicide("rawModeOn.tcsetattr");               // Error handling
  atexit(rawModeOff);                                         // be nice to the user. runs on exit() or ret main
}

/* Runtime */

int main() {
  rawModeOn();                                                // Customize terminal settings for our interface
  char c;                                                     // buffer for read characters

  /// Continuously read in chars and print em out, less it's q
  while (1) {
    c = '\0';                                                 // Give a default value for c, in case of timeout
    int statrt = read(STDIN_FILENO, &c, 1);                   // Semantics: read(fd, dest, cnt) => cnt
    if (statrt == -1 && errno != EAGAIN) suicide("main.read");    // EAGAIN sometimes fires if timeout on Cygwin, err
    if (iscntrl(c)) {                                         // test whether it's printable or not
      printf("%d\r\n", c);                                    // if not, just print the decimal identifier
    } else {                                                  // if so...
      printf("%d (%c) \r\n", c, c);                           // print out the decimal ID and the char
    }
    if (c == 'q') break;                                      // q instantly quits
  }
}
