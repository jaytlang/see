// see text editor
// the gateway drug to vim. without colons.  // jay lang, 2019

/* Include */

#include <unistd.h>  // read(), stdin fd, 
#include <string.h>  // memcpy()
#include <termios.h> // termios struct, tcget/set, tcflags 
#include <stdlib.h>  // atexit(), exit(), realloc(), free()
#include <sys/ioctl.h> // TIOCGWINSZ, ioctl routines
#include <stdio.h>   // printf(), perror(), snprintf()
#include <ctype.h>   // iscntrl()
#include <errno.h>   // errno, err flags

#define SEEVER "1.0X"  // version history!

/* Global data */

struct seeConfig {
  struct termios oldConfig;                                   // termios struct describes current term config
  int rows;                                                   // window size descriptors
  int cols;
  int xcursor, ycursor;                                       // Cursor position
};

enum kPress {                                                 // Helps with arrow key / WASD / fxn aliasing
  ARROW_LEFT = 'a',
  ARROW_RIGHT = 'd',
  ARROW_UP = 'w',
  ARROW_DOWN = 's',
  PAGE_UP = 'z',
  PAGE_DOWN = 'c',
  DEL_KEY = 'x',
  HOME_KEY = '1',
  END_KEY = '3',
};

struct seeConfig config;


/* Terminal Interface */

void suicide(const char *s) {                                 // Self kill function in the event of fatal err
  write(STDOUT_FILENO, "\x1b[2J", 4);                         // x1b J2 to clear the screen
  write(STDOUT_FILENO, "\x1b[H", 3);                          // Put the cursor at the top left (no args to H)
  perror(s);                                                  // Print out error with errno checking
  exit(1);                                                    // Exit with err code 1
} 

void rawModeOff() {
  int statrt = tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.oldConfig);// Restore the backed up original config 
  if (statrt == -1) suicide("rawModeOff.tcsetattr");          // Error checking
}

void rawModeOn() {
  int statrt = tcgetattr(STDIN_FILENO, &config.oldConfig);   // get this by giving stdin fd pointed @ struct
  if (statrt == -1) suicide("rawmodeOn.tcgetattr");          // Error handling
  struct termios newConfig = config.oldConfig;               // back up the original config 
  
  /// Config changes. These are like bitmask opts.
  /// C_IFLAGS are input-specific flags
  newConfig.c_iflag &= (~IXON);                               // off goes software flow control
  newConfig.c_iflag &= (~ICRNL);                              // fix ctrl-M to be read as 13, no \r\n translates
  newConfig.c_iflag &= (~BRKINT);                             // break condition fixes (applicable on olds)
  newConfig.c_iflag &= (~INPCK);                              // Parity checking enabled for accurate data trans
  newConfig.c_iflag &= (~ISTRIP);                             // 8th bit of each input byte shouldn't be stripped

  /// C_CFLAG is a bitmask controlling basic features
  newConfig.c_cflag |= CS8;                                   // Input byte size set to 8 bytes

  /// C_OFLAGS are output/processing flags. Turn em off!
  newConfig.c_oflag &= (~OPOST);                              // No carriage returns on \n, disable outprocs.

  /// C_LFLAGS are general/misc. state flags
  newConfig.c_lflag &= (~ECHO);                               // echo flag echoes keypresses. gets in the way. 
  newConfig.c_lflag &= (~ICANON);                             // read byte by byte instead of by line. canon. off
  newConfig.c_lflag &= (~ISIG);                               // disable interrupt signals. quit yourself.
  newConfig.c_lflag &= (~IEXTEN);                             // literal character escapes off + Ctrl-O fix

  /// Set read timeout times with c_cc/ctrl chars
  newConfig.c_cc[VMIN] = 0;                                   // Bytes of input req'd before read returns
  newConfig.c_cc[VTIME] = 1;                                  // Allowing a timeout. After .1 seconds.

  statrt = tcsetattr(STDIN_FILENO, TCSAFLUSH, &newConfig);    // set new atts. TCSAFLUSH waits, flushes new ins.
  if (statrt == -1) suicide("rawModeOn.tcsetattr");           // Error handling
  atexit(rawModeOff);                                         // be nice to the user. runs on exit() or ret main
}

char readKey() {                                              // Wait for a keypress then send it back. EZ$
  int nrStat;                                                 // Return code for the read / err handling
  char buf;                                                   // The boi

  nrStat = read(STDIN_FILENO, &buf, 1);                       // o b t a i n
  if (nrStat == -1 && errno != EAGAIN) suicide("readKey.read");// death and destruction / error handling 

  if (buf == '\x1b') {                                        // Detect escape key -> arrow keys?
    char seq[3];                                              // If we got one, IMMEDIATELY read new bytes
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';   // Timeout?
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';   // Maybe this just up and doesn't happen, needed
    
    if (seq[0] == '[') {                                      // Correct kind of start sequence? If so
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';// Read in aanother boi and hope it's a ~ for pgops
        if (seq[2] ==  '~') {                                 // If we have a match, return the correct keys
          switch (seq[1]) {                                   // And multiplex the controls
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;                        // Multiple possibilities dependent on emulator
            case '8': return END_KEY;
          }
        }
      } else {                                                // This fails, still might be an arrow key
        switch (seq[1]) {                                     // If arrow key combos, return the right kind
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {                               // Yet further HOME possibilities...
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      } 
    }
    return '\x1b';                                            // Something else. Who knows what else is in there?
  } else {
    return buf;                                               // Normal key handling
  }
}

int windowSize(int *rows, int *cols) {                        // pass destination mem locations for us to load
  struct winsize ws;                                          // has fields for us to fill rows n cols

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 ||  ws.ws_col == 0) {// account for erroneous ioctl syscalls
    return -1;
  } else {
    *cols = ws.ws_col;                                        // dump the results and get out
    *rows = ws.ws_row;
    return 0;
  }
}

/* String buffer / unified writes */
struct sbuf {                                                 // Super simple string constructor we can add to
  char *buf;                                                  // The buffer we're gonna use during construction
  int len;                                                    // Length of our boi
};

#define SBUFSTART {NULL, 0}                                   // Constructor we can invoke for this struct

void sbufAdd(struct sbuf *sb, const char *nsin, int len) {
  char *new = realloc(sb->buf, sb->len + len);               // Autoset enough memory to handle our string
  if (new == NULL) return;                                   // No change? Yeet.

  memcpy(&new[sb->len], nsin, len);                           // Copy the new thing nsin to OLD EOB
  sb->buf = new;                                              // Then update the ptrs - sb->buf could move
  sb->len += len;                                             // Incrementing length also essential
}

void sbufKill(struct sbuf *sb) {
  // happy big kill sbuf death time
  free(sb->buf);  
}

/* Output/display handling */

void drawDash(struct sbuf *sbptr) {
  int i;                          
  for (i = 0; i < config.rows; i++) {                         // Loop through all rows to set this up...
    if (i == config.rows / 3) {
      char welcome[100];                                      // Set up a buffer we'll snprint to for welcoming
      int welcomeWr = snprintf(welcome, sizeof(welcome), "welcome to see text editor: version %s", SEEVER);
      if (welcomeWr > config.cols) welcomeWr = config.cols;   // Truncation in the event of tiny terminals

      int pad = (config.cols-welcomeWr) / 2;                  // Pick a nice center point for further padding
      if (pad) {                                              // Make sure we actually have room to do things
        sbufAdd(sbptr, "-", 1);                               // Beginning boyo
        pad--;                                                // Proper decrementation
      }
      while (pad) {                                           // While there's still room chuck on spaces
        sbufAdd(sbptr, " ", 1);
        pad--;
      }
      sbufAdd(sbptr, welcome, welcomeWr);                     // Add the whole deal to the buffer

    } else { 
      sbufAdd(sbptr, "-", 1);                                 // Add new dashes to the buffer for all rows
    }
    sbufAdd(sbptr, "\x1b[K", 3);                              // Clear the following row into the buffer
    if (i < config.rows-1) {                                  // If we haven't hit the end, also do \r\n
      sbufAdd(sbptr, "\r\n", 2);                                 
    }
  }
}

void refreshScreen() {                                        

  /// Use of VT100 0x1b[ escape sequences to the terminal begins
  /// Objective: clear the entire screen and do (re)setup
  struct sbuf sb = SBUFSTART;                                 // Make the buffer we'll continuously do things to

  sbufAdd(&sb, "\x1b[?25l", 6);                               // Disable the cursor on newer terminals, cosmetics
  sbufAdd(&sb, "\x1b[H", 3);                                  // Top left the cursor
  drawDash(&sb);                                              // Dashes interface setting upping

  /// Move the cursor based on config values.
  /// Terminal uses 1 indexed values, we'll need to convert
  /// Use snprint to attack escape sequences via a buffer
  char buf[32]; 
  int lenWr = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", config.ycursor+1, config.xcursor+1);
  sbufAdd(&sb, buf, lenWr);
  
  sbufAdd(&sb, "\x1b[?25h", 6);                               // Re-enable the cursor

  write(STDOUT_FILENO, sb.buf, sb.len);                       // Do one big write to STDOUT with buffer contents
  sbufKill(&sb);                                              // Make it die
}

/* Input delegation */

void mvCursor(char keyPressed) {                              // Update cursor position based on WASD keys
  switch (keyPressed) {
    case ARROW_LEFT:
      if (config.xcursor != 0) config.xcursor--;              // Do bounds checking for all cases
      break;
    case ARROW_RIGHT:
      if (config.xcursor != (config.cols-1)) config.xcursor++;
      break;
    case ARROW_UP:
      if (config.ycursor != 0) config.ycursor--;
      break;
    case ARROW_DOWN:
      if (config.ycursor != (config.rows-1)) config.ycursor++;
      break;
  }
}

void procKeypress() {
  char c = readKey();                                         // Get the next keypress, blocking til it arrives
 
  // DEBUG
  // printf("Key pressed: %d (%c)", c, c);

  switch (c) {                                                // Processing time - using 'q' as the quit key
    case 'q':                                                 // If it is indeed the quit key...
      write(STDOUT_FILENO, "\x1b[2J", 4);                     // x1b J2 to clear the screen
      write(STDOUT_FILENO, "\x1b[H", 3);                      // Put the cursor at the top left (no args to H)
      exit(0);                                                // Get outta here, we done
      break;                          
  
    /// Generalize the directional keypresses and interface via a subfunction (above)
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      mvCursor(c);
      break;

    /// Pg up / pg down operations consist of consecutive cursor moves to the top/bottom
    case PAGE_UP:
    case PAGE_DOWN: {
      int lc = config.rows;
      while (lc--) mvCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    } break;

    /// HOME AND END or 1/3: move the cursor to the far right or left
    case HOME_KEY:
      config.xcursor = 0;
      break;
    case END_KEY:
      config.xcursor = config.cols - 1;
      break;

  }
}

/* Runtime */

void initConfig() {
  config.xcursor = 0; config.ycursor = 0;
  if (windowSize(&config.rows, &config.cols) == -1) suicide("initConfig.windowSize");// do the big wsize set
}

int main() {
  rawModeOn();                                                // Customize terminal settings for our interface
  initConfig();                                               // Set up the seeConfiguration, struct is above

  /// Continuously process keypresses until 'q' is pressed
  while (1) {
    refreshScreen();
    procKeypress(); 
  }
}
