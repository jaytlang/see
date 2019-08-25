// see text editor
// the gateway drug to vim. without colons.  // jay lang, 2019

/* Include */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>  // read(), stdin fd, 
#include <string.h>  // memcpy()
#include <termios.h> // termios struct, tcget/set, tcflags 
#include <stdlib.h>  // atexit(), exit(), realloc(), free(), malloc()
#include <sys/ioctl.h> // TIOCGWINSZ, ioctl routines
#include <sys/types.h> // ssize_t
#include <stdio.h>   // printf(), perror(), snprintf(), FILE n friends
#include <ctype.h>   // iscntrl()
#include <errno.h>   // errno, err flags

#define TABSTOP 8


/* Global data */

typedef struct textRow {                                      // Struct for each row. Has a size n stuff.
  int size;
  int rsize;
  char *chars;
  char *render;
} textRow;

struct seeConfig {
  struct termios oldConfig;                                   // termios struct describes current term config
  int rows;                                                   // window size descriptors
  int cols;
  int render;                                                 // what's actually gonna be displayed (tabs)
  int rsize;                                                  // sizeof(above)
  int xcursor, ycursor;                                       // Cursor position
  int rcursor;                                                // Rendered cursor x position
  int yoffset;                                                // Offset used with scrolling
  int xoffset;                                                // " but horizontal scrolling
  int numRows;                                                // Number of rows in an opened file
  textRow *row;                                               // Actual array of rows in question
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
  newConfig.c_iflag &= (~ICRNL);                              // fix ctrl-M to be read as 13, no \n->\r\n 
  newConfig.c_iflag &= (~BRKINT);                             // break condition fixes (applicable on olds)
  newConfig.c_iflag &= (~INPCK);                              // Parity checking enabled
  newConfig.c_iflag &= (~ISTRIP);                             // 8th bit of each input byte shouldn't strip

  /// C_CFLAG is a bitmask controlling basic features
  newConfig.c_cflag |= CS8;                                   // Input byte size set to 8 bytes

  /// C_OFLAGS are output/processing flags. Turn em off!
  newConfig.c_oflag &= (~OPOST);                              // No carriage returns on \n, disable outprocs.

  /// C_LFLAGS are general/misc. state flags
  newConfig.c_lflag &= (~ECHO);                               // echo flag echoes keypresses. gets in the way 
  newConfig.c_lflag &= (~ICANON);                             // read byte by byte instead of by line. rip cn.
  newConfig.c_lflag &= (~ISIG);                               // disable interrupt signals. quit yourself.
  newConfig.c_lflag &= (~IEXTEN);                             // literal character escapes off + Ctrl-O fix

  /// Set read timeout times with c_cc/ctrl chars
  newConfig.c_cc[VMIN] = 0;                                   // Bytes of input req'd before read returns
  newConfig.c_cc[VTIME] = 1;                                  // Allowing a timeout. After .1 seconds.

  statrt = tcsetattr(STDIN_FILENO, TCSAFLUSH, &newConfig);    // set new atts. TCSAFLUSH waits, flushes news
  if (statrt == -1) suicide("rawModeOn.tcsetattr");           // Error handling
  atexit(rawModeOff);                                         // be nice to the user. runs on exit() or ret
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
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';// Read in aanother boi and check if ~/pg. opt
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
    return '\x1b';                                            // Something else. Who knows what else is there?
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

/* Row opts */

/// Properly calculate the value of rcursor
int cursorToRenderHelper(textRow *row, int xcursor) {
  int rcursor = 0;
  int i;
  for (i = 0; i < xcursor; i++) {                           // Move the cursor properly
    if (row->chars[i] == '\t') rcursor += ((TABSTOP-1) - (rcursor & TABSTOP));
    rcursor++;
  }
  return rcursor;
}

/// Use the chars string of the textRow to fill in render
/// For now, directly copy things over and fix tabs
void updateRow(textRow *row) {
  int tabs = 0;                                             // Simple enough, do tab determination
  int i;
  for (i = 0; i < row->size; i++) {
    if (row->chars[i] == '\t') tabs++;
  }

  free(row->render);                                        // Free whatever might already be there
  row->render = malloc(row->size + 1 + tabs*(TABSTOP-1));   // Allocate space, row->size is one tab

  int idx = 0;
  for (i = 0; i < row->size; i++) {
    if (row->chars[i] == '\t') {                            // If there's a tab add spaces until stop
      row->render[idx++] = ' ';                             // Divisible by 8 implies stop
      while (idx % TABSTOP != 0) row->render[idx++] = ' ';        
    } else {
      row->render[idx++] = row->chars[i];                   // Normal copy
    }
  }

  row->render[idx] = '\0';                                  // After copying suppend terminator
  row->rsize = idx;
}

void addRow(char *line, size_t lineLength) {
  /// Allocate space for a new row = textRowSize * numberOfRows
  config.row = realloc(config.row, sizeof(textRow)*(config.numRows+1));

  int current = config.numRows;                             // This will dynamically change as we add more
  config.row[current].size = lineLength;                    // Update the config - for loop this later?
  config.row[current].chars = malloc(lineLength+1);         // Leave length for C-string terminator!!
  memcpy(config.row[current].chars, line, lineLength);      // Copy in the line into our config file
  config.row[current].chars[lineLength] = '\0';

  config.row[current].rsize = 0;                            // Initialize the new render
  config.row[current].render = 0;                           // Nothing is here yet
  updateRow(&config.row[current]);                          // Fix tab rendering during generation

  config.numRows++;                                         // Do as mentioned and add the boi
}

/* File I/O Stuffs */

void openFile(char *fname) {                                  // Should handle most file open operations
  FILE *fptr = fopen(fname, "r");                             // Open in read mode, get a ptr to the thing.
  if (!fptr) suicide("openFile.fopen");                       // Check that fp != 0

  char *line = 0;                                             // Make a pointer and explicitly 0 initialize
  size_t lineCap = 0;                                         // How long is it? No u. Make me memory+show me.
  int lineLength;
 /// getLine(**buf, *size_t, file *fptr) 
                                                                  // pause for pointerception ********
  while ((lineLength = getline(&line, &lineCap, fptr)) != -1) {// No error right? returns -1 at EOF
    while (lineLength > 0 && ( line[lineLength - 1] == '\n' ||    // Trim off the following special characters
                               line[lineLength - 1] == '\r' ))
      lineLength--;
    addRow(line, lineLength);
  }
  free(line);                                                 // Done, we read a line. Free that n close.
  fclose(fptr);                                               

  
}

/* String buffer / unified writes */
struct sbuf {                                                 // Super simple string constructor we can add to
  char *buf;                                                  // The buffer we're gonna use during making
  int len;                                                    // Length of our boi
};

#define SBUFSTART {NULL, 0}                                   // Constructor we can invoke for this struct

void sbufAdd(struct sbuf *sb, const char *nsin, int len) {
  char *new = realloc(sb->buf, sb->len + len);                // Autoset enough memory to handle our string
  if (new == NULL) return;                                    // No change? Yeet.

  memcpy(&new[sb->len], nsin, len);                           // Copy the new thing nsin to OLD EOB
  sb->buf = new;                                              // Then update the ptrs - sb->buf could move
  sb->len += len;                                             // Incrementing length also essential
}

void sbufKill(struct sbuf *sb) {
  // happy big kill sbuf death time
  free(sb->buf);  
}

/* Output/display handling */

void padWelcome(struct sbuf *sbptr, const char *msg) {
  char welcome[100];                                           // Set up a buffer we'll snprint to for welcome
    int welcomeWr = snprintf(welcome, sizeof(welcome), msg, "");
      if (welcomeWr > config.cols) welcomeWr = config.cols;    // Truncation in the event of tiny terminals
        int pad = (config.cols-welcomeWr) / 2;                 // Pick a nice center point for further padding
        if (pad) {                                             // Make sure we actually have room to do things
          sbufAdd(sbptr, "-", 1);                              // Beginning boyo
          pad--;                                               // Proper decrementation
        }
        while (pad) {                                          // While there's still room chuck on spaces
          sbufAdd(sbptr, " ", 1);
          pad--;
        }
        sbufAdd(sbptr, welcome, welcomeWr);                    // Add the whole deal to the buffer
}

void checkScroll() {
  config.rcursor = 0;
  if (config.ycursor < config.numRows) {
    config.rcursor = cursorToRenderHelper(&config.row[config.ycursor], config.xcursor);
  }

  /// Vertical scrolling
  if (config.ycursor < config.yoffset) {                       // Above the window? Just set equal and we done
    config.yoffset = config.ycursor;
  }
  if (config.ycursor >= config.yoffset+config.rows) {          // Below is slightly more complicated
    config.yoffset = config.ycursor - config.rows + 1;         // Increment what's at the top of the screen
  }
  
  /// Horizontal scrolling
  if (config.rcursor < config.xoffset) {                       // Is cursor position less than offset?
    config.xoffset = config.rcursor;                           // If so scroll one back
  }
  if (config.rcursor >= config.xoffset+config.cols) {          // If we're over, drop the width off and add
    config.xoffset = config.rcursor - config.cols + 1;         // Parallel to vertical scrolling code
  }
}

void drawDash(struct sbuf *sbptr) {
  int i;                          
  for (i = 0; i < config.rows; i++) {                          // loop through all rows to set this up...
    int frow = i+config.yoffset;                               // Tack on the current offset as we go
    if (frow >= config.numRows) {                              // Oof, we're outta range
    /// WELCOME: let's be nice! 
      if (i >= config.numRows) {
        if (config.numRows == 0) {
          if (i == config.rows / 3 - 1) {
            padWelcome(sbptr, "welcome to see text editor");       // This is exceedingly tedious.
          } else if (i == config.rows / 3) {
            padWelcome(sbptr, "written by Jay Lang, (c) 2019");
          } else if (i == config.rows / 2 - 1) {
            padWelcome(sbptr, "find the controls on the bar below as you type");
          } else if (i ==  config.rows / 2) {
            padWelcome(sbptr, "or, use arrow keys! you can do both here.");
          } else if (i == config.rows / 2 + 1) {
            padWelcome(sbptr, "special thanks to jake for inspiring the project :D");
          } else if (i == config.rows / 2 + 2) {
            padWelcome(sbptr, "you should send him chipotle. donation link on my git @jaytlang");
          }
        }
        sbufAdd(sbptr, "-", 1);                                  // Add new dashes to the buffer for all rows
      }
    } else {
      int len = config.row[frow].rsize - config.xoffset;         // From-file row display time. Len-off...
      if (len < 0) len = 0;                                      // If we scrolled too far, nothing displays
      if (len > config.cols) len = config.cols;                  // ...and trim it in case things get bigboi
      sbufAdd(sbptr, &config.row[frow].render[config.xoffset], len);  // Chuck her in
    }

    /// Prepare for the next line
    sbufAdd(sbptr, "\x1b[K", 3);                              // Clear the following row into the buffer,
    if (i < config.rows-1) {                                  // If we haven't hit the EOS, also do \r\n
      sbufAdd(sbptr, "\r\n", 2);                                 
    }
  }
}

void refreshScreen() {                                        

  /// Use of VT100 0x1b[ escape sequences to the terminal begins
  /// Objective: clear the entire screen and do (re)setup
  checkScroll();                                              // Is scrolling necessary?
  struct sbuf sb = SBUFSTART;                                 // Make the buffer we'll continuously do things

  sbufAdd(&sb, "\x1b[?25l", 6);                               // Disable the cursor on newer terminals, fancy
  sbufAdd(&sb, "\x1b[H", 3);                                  // Top left the cursor
  drawDash(&sb);                                              // Dashes interface setting upping

  /// Move the cursor based on config values.
  /// Terminal uses 1 indexed values, we'll need to convert
  /// Use snprint to attack escape sequences via a buffer
  char buf[32]; 

  /// Note: cursor offset should refer to position on screen, not in text file
  int lenWr = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", config.ycursor-config.yoffset+1, config.rcursor-config.xoffset+1);
  sbufAdd(&sb, buf, lenWr);
  
  sbufAdd(&sb, "\x1b[?25h", 6);                               // Re-enable the cursor

  write(STDOUT_FILENO, sb.buf, sb.len);                       // Do one big write to STDOUT w buffer contents
  sbufKill(&sb);                                              // Make it die
}

/* Input delegation */

void mvCursor(char keyPressed) {                              // Update cursor position based on WASD keys
  /// Ensure that we're on an actual line...
  /// because yoffset is allowed to be one line past the last line
  textRow *row = (config.xcursor >= config.rows) ? NULL : &config.row[config.ycursor];

  switch (keyPressed) {
    case ARROW_LEFT:
      if (config.xcursor != 0) config.xcursor--;              // Do bounds checking for all cases
      else if (config.ycursor > 0) {                          // xc = 0 but yc is valid and nonzero
        config.ycursor--;
        config.xcursor = config.row[config.ycursor].size;     // Decrement + end of last line
      }
      break;
    case ARROW_RIGHT:
      if (row && config.xcursor < row->size) config.xcursor++;// Does the current row exist? Are we in it?
      else if (row && config.xcursor == row->size) {
        config.ycursor++;
        config.xcursor = 0;
      }
      break;
    case ARROW_UP:
      if (config.ycursor != 0) config.ycursor--;
      break;
    case ARROW_DOWN:
      if (config.ycursor < config.numRows) config.ycursor++;  // If we have file left to go down, do it
      break;
  }

  /// ycursor now could point to a different row. Reset it.
  row = (config.ycursor >= config.rows) ? NULL : &config.row[config.ycursor];
  int rowLength = row ? row->size : 0;                                        // Get rowLength
  if (config.xcursor > rowLength) config.xcursor = rowLength;                 // Compare + snap if bad
}

void procKeypress() {
  char c = readKey();                                         // Get the next keypress, blocking til it here 
 
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
    /// I will never actually remember zero indexing
    case PAGE_UP:
    case PAGE_DOWN: {
      if (c == PAGE_UP) {                                     // Scrolling up an entire page...
        config.ycursor = config.yoffset;                      // Set the cursor to the scroll offset, then up
      } else if (c == PAGE_DOWN) {
        config.ycursor = config.yoffset+config.rows-1;        // Move to the bottom of the screen, -1
        if (config.ycursor > config.numRows) config.ycursor = config.numRows;// Snap
      }
      
      int lc = config.rows;
      while (lc--) mvCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);//Delegate actual positioning to this
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
  config.xcursor = 0; config.ycursor = 0;                     // Setup the cursor, and originally no rows
  config.rcursor = 0;                                         
  config.yoffset = 0;                                         // Row offset is obviously initially zero,zero
  config.xoffset = 0;
  config.numRows = 0;
  config.row = 0;                                             // Zero initialize the pointer

  if (windowSize(&config.rows, &config.cols) == -1) suicide("initConfig.windowSize");// do the big wsize set
}

int main(int argc, char *argv[]) {
  rawModeOn();                                                // Customize terminal settings for our interface
  initConfig();                                               // Set up the seeConfiguration, struct is above
  if (argc >= 2) {
    openFile(argv[1]);                                        // Open something!
  }

  /// Continuously process keypresses until 'q' is pressed
  while (1) {
    refreshScreen();
    procKeypress(); 
  }
}
