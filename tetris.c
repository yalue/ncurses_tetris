// The main file for ncurses_tetris.
#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tetris.h"

// These are used for bounds checks when drawing to the screen. They account
// for the 1 character of border and the 2-character cell width.
#define BOARD_MAX_X (1 + (BLOCKS_WIDE * 2))
#define BOARD_MAX_Y (1 + BLOCKS_TALL)

// Calls our internal function to print the location of an error and exit if
// a curses function returns ERR.
#define CheckCursesError(val) InternalCheckCursesError((val), #val, __FILE__, __LINE__)

// Calls our internal function to print an error and exit if a value is NULL.
#define CheckNULL(val) InternalCheckNULL((val), #val, __FILE__, __LINE__)

static void InternalCheckCursesError(int value, const char *fn,
    const char *file, int line) {
  if (value != ERR) return;
  endwin();
  printf("File %s, line %d: %s returned an error\n", file, line, fn);
  exit(1);
}

static void InternalCheckNULL(void *value, const char *fn,
    const char *file, int line) {
  if (value) return;
  endwin();
  printf("File %s, line %d: %s was NULL\n", file, line, fn);
  exit(1);
}

// Initially called to set up the curses settings for character-at-a-time
// control. Exits on error.
static void SetupCurses(void) {
  CheckNULL(initscr());
  // This causes a "break" after every character input, rather than every line
  CheckCursesError(cbreak());
  // Don't print the characters that are input
  CheckCursesError(noecho());
  // Don't translate LF -> CRLF
  CheckCursesError(nonl());
  // Changes behavior of interrupt keys on the keyboard. Not sure if relevant,
  // but recommended by the man pages.
  CheckCursesError(intrflush(stdscr, FALSE));
  // Enables sending keycodes, i.e. KEY_LEFT, via getch()
  CheckCursesError(keypad(stdscr, TRUE));
  // Hide the location of the cursor
  CheckCursesError(curs_set(0));
}

// We replace ncurses' box(..) function with this, because it works properly in
// putty without extra env stuff to override attempts to draw nicer borders.
// The main downside is that it prevents the nicer borders on terminals that
// properly support the character set, but w/e. To change this behavior back,
// just replace wborder(window, ...) with box(window, 0, 0) in this function.
static void WinBox(WINDOW *window) {
  CheckCursesError(wborder(window, '|', '|', '-', '-', '+', '+', '+', '+'));
}

// Prints the title at the top of a window, centered.
static void PrintWindowTitle(WINDOW *window, const char *s) {
  int start_x = (getmaxx(window) - strlen(s)) >> 1;
  CheckCursesError(mvwprintw(window, 0, start_x, s));
}

// Prints the "Controls: " lines in the main window. Exits on error.
static void PrintControls(WINDOW *w, int row, int col) {
  CheckCursesError(mvwprintw(w, row, col, "Controls:"));
  CheckCursesError(mvwprintw(w, row + 1, col, "q: quit"));
  CheckCursesError(mvwprintw(w, row + 2, col, "l: quick load"));
  CheckCursesError(mvwprintw(w, row + 3, col, "s: quick save"));
  CheckCursesError(mvwprintw(w, row + 4, col, "space: pause"));
  CheckCursesError(mvwprintw(w, row + 5, col, "arrow keys:"));
  CheckCursesError(mvwprintw(w, row + 6, col, "  move/rotate"));
}

// Initializes the layout and empty display. Exits on error.
static void CreateWindows(TetrisDisplay *windows) {
  int chars_wide, chars_tall, status_window_width, main_window_width;
  memset(windows, 0, sizeof(*windows));
  // The width of the play area = 2 chars per block, plus a character on each
  // side for the border.
  chars_wide = (BLOCKS_WIDE * 2) + 2;
  // The height of the play area = 1 char per block, plus a character on each
  // side for the border.
  chars_tall = BLOCKS_TALL + 2;
  // Give each "status" window enough space for 11 characters of text, and one
  // space of padding and border on each side.
  status_window_width = 15;
  // The main window needs to hold the side-by-side game window with the status
  // windows, along with a space between the two windows, and 2 characters of
  // border + padding on each side.
  main_window_width = chars_wide + status_window_width + 8;

  windows->top_window = newwin(chars_tall + 4, main_window_width, 0, 0);
  CheckNULL(windows->top_window);
  WinBox(windows->top_window);
  PrintWindowTitle(windows->top_window, " Tetris ");
  PrintControls(windows->top_window, chars_tall - 4, chars_wide + 5);

  windows->game = subwin(windows->top_window, chars_tall, chars_wide, 2, 2);
  CheckNULL(windows->game);
  WinBox(windows->game);
  windows->score = subwin(windows->top_window, 3, status_window_width, 11,
    chars_wide + 5);
  CheckNULL(windows->score);
  WinBox(windows->score);
  PrintWindowTitle(windows->score, " Score ");
  windows->line_count = subwin(windows->top_window, 3, 15, 15, chars_wide + 5);
  CheckNULL(windows->line_count);
  WinBox(windows->line_count);
  PrintWindowTitle(windows->line_count, " Lines ");
  windows->next_piece = subwin(windows->top_window, 8, 15, 2, chars_wide + 5);
  CheckNULL(windows->next_piece);
  WinBox(windows->next_piece);
  PrintWindowTitle(windows->next_piece, " Next ");
}

static void RefreshAllWindows(TetrisDisplay *windows) {
  CheckCursesError(refresh());
  CheckCursesError(wrefresh(windows->top_window));
  CheckCursesError(wrefresh(windows->game));
  CheckCursesError(wrefresh(windows->score));
  CheckCursesError(wrefresh(windows->line_count));
  CheckCursesError(wrefresh(windows->next_piece));
}

static void DestroyWindows(TetrisDisplay *windows) {
  // We're not going to bother checking errors on these cleanup functions.
  // Technically, they return an error if any of the windows are NULL, but at
  // least they shouldn't segfault in that case, so we won't bother checking.
  delwin(windows->next_piece);
  delwin(windows->line_count);
  delwin(windows->score);
  delwin(windows->game);
  // We must delete this last, after the sub-windows.
  delwin(windows->top_window);
  memset(windows, 0, sizeof(*windows));
}

// If c is a non-printable character (or just something we don't want to print
// to the board, returns a ' '.  Otherwise, returns c.
static char FixNonPrintable(char c) {
  if ((c < ' ') || (c > '~')) return ' ';
  return c;
}

// Takes a pointer to the game window, and the board array in the game state,
// and draws the contents of the board.
static void DrawBoard(WINDOW *w, uint8_t *board) {
  // We'll use these as the coordinates in the ncurses window.
  // The coordinates into the ncurses window.
  int y, x;
  // The index into the board array.
  int i = 0;
  uint8_t c;
  // Note that we start at y = 1 and x = 1 to skip the window border.
  for (y = 1; y <= BLOCKS_TALL; y++) {
    for (x = 1; x <= (BLOCKS_WIDE * 2); x += 2) {
      c = FixNonPrintable(board[i]);
      // NOTE: Replace with something more efficient than format strings.
      CheckCursesError(mvwprintw(w, y, x, "%c%c", c, c));
      i++;
    }
  }
}

// Takes a pointer to the next piece window, and the index of the piece in the
// tetris_pieces array. Draws the piece in the window.
static void DrawNextPiece(WINDOW *w, uint8_t piece) {
  const char *p = tetris_pieces[piece];
  // Follows a very similar pattern to DrawBoard
  int x, y, i = 0;
  char c;
  for (y = 3; y < 7; y++) {
    for (x = 4; x < 12; x += 2) {
      c = p[i];
      CheckCursesError(mvwprintw(w, y, x, "%c%c", c, c));
      i++;
    }
  }
}

// Returns 1 if a space is available for a falling block, and 0 if not. Returns
// 0 if the given position is either out of bounds, or overlapping with an
// existing block. The x, y coordinate is in game cells, NOT ncurses rows/cols.
static int SpaceAvailable(uint8_t *board, int x, int y) {
  char c;
  if ((x < 0) || (y < 0)) return 0;
  if ((x >= BLOCKS_WIDE) || (y >= BLOCKS_TALL)) return 0;
  c = board[y * BLOCKS_WIDE + x];
  return (c <= ' ') || (c >= '~');
}

// TODO: Use SpaceAvailable when moving pieces, either side to side, rotation,
// or falling.

static void DrawFallingPiece(WINDOW *w, TetrisGameState *s) {
  // TODO: Need DrawFallingPiece.
  //  - If partially above the top of the screen, just don't draw what's above.
  const char *p = tetris_pieces[s->current_piece];
  int x, y, screen_x, init_screen_x, i = 0;
  char c;
  screen_x = (2 * s->piece_x) + 1;
  init_screen_x = screen_x;
  for (y = s->piece_y; y < (s->piece_y + 4); y++) {
    for (x = s->piece_x; x < (s->piece_x + 4); x++) {
      c = p[i];
      // We ignore empty parts of the piece, as we wouldn't want to overwrite
      // existing blocks with empty space.
      if (c == ' ') {
        i++;
        screen_x += 2;
        continue;
      }
      // Since we're dealing with a part of the block that's actually there,
      // ensure that it's within the board boundaries and not overlapping any
      // existing piece.
      if (!SpaceAvailable(s->board, x, y)) {
        endwin();
        printf("The current piece is in an invalid location!\n");
        exit(1);
      }
      CheckCursesError(mvwprintw(w, y + 1, screen_x, "%c%c", c, c));
      screen_x += 2;
      i++;
    }
    screen_x = init_screen_x;
  }
}

// TODO: Sanity checks for quickload:
//  - Make sure there are no completed rows in the board (would be impossible
//    in a quicksave).
//  - Score > lines
//  - lines not negative
//  - Next piece a valid index.
//  - Current piece a valid index.
//  - piece_x and piece_y are valid.

// Writes the score, piece, and so on, in the game window.
static void DisplayGameState(TetrisDisplay *windows, TetrisGameState *s) {
  DrawBoard(windows->game, s->board);
  DrawNextPiece(windows->next_piece, s->next_piece);
  DrawFallingPiece(windows->game, s);
  CheckCursesError(mvwprintw(windows->score, 1, 2, "%011d", s->score));
  CheckCursesError(mvwprintw(windows->line_count, 1, 2, "%011d", s->lines));
}

// Returns a random piece to drop down (i.e., an index into tetris_pieces).
static uint8_t RandomNewPiece(void) {
  // Since some pieces have up to four rotations, this allows us to select a
  // random piece rotation without weighting pieces with more rotations over
  // pieces with fewer, as every piece has four entries.
  static const uint8_t piece_ids[] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4,
    5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
  const uint8_t max_choice = sizeof(piece_ids) / sizeof(uint8_t);
  return piece_ids[rand() % max_choice];
}

// Attempts to quickload a game state from the quicksave file. If any error
// occurs in loading or validating the file, this will *not* exit or crash;
// instead it will just print an error message to the game display and return
// 0. If any error occurs, this will *not* modify s.
static int TryQuickload(TetrisDisplay *windows, TetrisGameState *s) {
  // TODO: Implement TryQuickload.
  return 0;
}

// Sets up a new game, clearing the board, setting score and lines to 0, and
// setting up the current and next piece.
static void InitializeNewGame(TetrisGameState *s) {
  memset(s, 0, sizeof(*s));
  memset(s->board, ' ', sizeof(s->board));
  s->next_piece = RandomNewPiece();
  s->current_piece = RandomNewPiece();
  // The piece starts at the top, in the middle.
  s->piece_y = 14; //////////////////////////////////////////// DEBUG
  s->piece_x = BLOCKS_WIDE / 2;
}

// Runs a game until a quit or a game over occurs. Returns 0 on a game over,
// and nonzero on a quit. The initial_quickload argument should be zero to
// start a new game, and nonzero if we should attempt to load a quicksave
// immediately.
static int RunGame(TetrisDisplay *windows, int initial_quickload) {
  TetrisGameState s;
  if (initial_quickload) {
    if (!TryQuickload(windows, &s)) {
      InitializeNewGame(&s);
    }
  } else {
    InitializeNewGame(&s);
  }
  DisplayGameState(windows, &s);
  return 0;
}

int main(int argc, char **argv) {
  TetrisDisplay windows;
  int input_key, should_exit;
  if (!setlocale(LC_ALL, "")) {
    printf("Failed setting locale: %s\n", strerror(errno));
    return 1;
  }
  SetupCurses();
  CreateWindows(&windows);
  // This loop controls the game over / new game screen, where we initially
  // start.
  should_exit = 0;
  while (!should_exit) {
    CheckCursesError(mvwprintw(windows.game, 9, 6, "Press space"));
    CheckCursesError(mvwprintw(windows.game, 10, 7, "to start!"));
    RefreshAllWindows(&windows);
    input_key = getch();
    switch (input_key) {
    case (' '):
      should_exit = RunGame(&windows, 0);
      break;
    case ('l'):
      // Here, we just call RunGame like normal, except instruct it to
      // immediately try loading the quicksave if it exists.
      should_exit = RunGame(&windows, 1);
      break;
    case ('q'):
      should_exit = 1;
      break;
    default:
      break;
    }
  }
  DestroyWindows(&windows);
  endwin();
  printf("Tetris exited normally!\n");
  return 0;
}
