// The main file for ncurses_tetris.
#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tetris.h"

// This will be the maximum amount of time to wait for a keypress before
// updating the screen anyway. Basically, we can achieve a higher "frames per
// second" if there's faster input, but we'll always have about 30 FPS.
#define MAX_MS_PER_FRAME (33)

// Calls our internal function to print the location of an error and exit if
// a curses function returns ERR.
#define CheckCursesError(val) InternalCheckCursesError((val), #val, __FILE__, __LINE__)

// Calls our internal function to print an error and exit if a value is NULL.
#define CheckNULL(val) InternalCheckNULL((val), #val, __FILE__, __LINE__)

// Returns the current time, in seconds.
static double CurrentSeconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
    endwin();
    printf("Error getting time.\n");
    exit(1);
  }
  return ((double) ts.tv_sec) + (((double) ts.tv_nsec) / 1e9);
}

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

// Clears the status line if something is there. Doesn't refresh the screen.
static void ClearStatusLine(TetrisDisplay *windows) {
  WINDOW *w = windows->top_window;
  int i;
  // Move the cursor and write the first char.
  CheckCursesError(mvwaddch(w, 1, 2, ' '));
  // Remove the remaining characters.
  for (i = 1; i < (sizeof(windows->status_message) - 1); i++) {
    CheckCursesError(waddch(w, ' '));
  }
  windows->status_message[0] = 0;
}

// Writes the status message to the game display, or clears it if the timeout
// has elapsed. To set a new status message, write the message to
// windows->status_message and set windows->status_start_time to
// CurrentSeconds().
static void WriteStatusMessage(TetrisDisplay *windows) {
  WINDOW *w = windows->top_window;
  double displayed_duration;
  // We have no current status message.
  if (windows->status_message[0] == 0) return;

  // Clear the status message if it's been displayed for over 5 seconds.
  displayed_duration = CurrentSeconds() - windows->status_start_time;
  if (displayed_duration >= 5.0) {
    ClearStatusLine(windows);
    return;
  }

  // We expect the typical behavior to being *not* displaying a status message,
  // so I don't care if this runs every update while a status is being
  // displayed.
  windows->status_message[sizeof(windows->status_message) - 1] = 0;
  CheckCursesError(mvwprintw(w, 1, 2, "%s", windows->status_message));
  // I don't understand why yet, but mvwprintw deletes the right border
  // character after the status line.
  CheckCursesError(mvwaddch(w, 1, getmaxx(w) - 1, '|'));
}

// Handles the ncurses calls to flush the content to the terminal.
static void RefreshAllWindows(TetrisDisplay *windows) {
  WriteStatusMessage(windows);
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

// Prints a status message to the top of the game display's main window.
static void StatusPrintf(TetrisDisplay *windows, const char *format, ...) {
  va_list args;
  ClearStatusLine(windows);
  va_start(args, format);
  memset(windows->status_message, 0, sizeof(windows->status_message));
  vsnprintf(windows->status_message, sizeof(windows->status_message) - 1,
    format, args);
  va_end(args);
  windows->status_start_time = CurrentSeconds();
  WriteStatusMessage(windows);
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
  int piece_x, piece_y, screen_x, screen_y;
  char c;
  for (piece_y = 0; piece_y < 4; piece_y++) {
    screen_y = 5 - piece_y;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      c = p[piece_y * 4 + piece_x];
      screen_x = (piece_x * 2) + 3;
      CheckCursesError(mvwprintw(w, screen_y, screen_x, "%c%c", c, c));
    }
  }
}

// Returns 1 if a space is available for a falling block, and 0 if not. Returns
// 0 if the given position is either out of bounds, or overlapping with an
// existing block. The x, y coordinate is in game cells, NOT ncurses rows/cols.
static int SpaceAvailable(uint8_t *board, int x, int y) {
  char c;
  if ((x < 0) || (x >= BLOCKS_WIDE)) return 0;
  // We always count space above the board as OK.
  if (y < 0) return 1;
  if (y >= BLOCKS_TALL) return 0;
  c = board[y * BLOCKS_WIDE + x];
  return (c <= ' ') || (c >= '~');
}

static void DrawFallingPiece(WINDOW *w, TetrisGameState *s) {
  const char *p = tetris_pieces[s->current_piece];
  int piece_x, piece_y, board_x, board_y, screen_x, screen_y;
  char c;

  for (piece_y = 0; piece_y < 4; piece_y++) {
    board_y = s->piece_y - piece_y;
    if (board_y < 0) continue;
    screen_y = board_y + 1;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      c = p[piece_y * 4 + piece_x];
      if (c == ' ') continue;
      board_x = s->piece_x + piece_x;
      screen_x = board_x * 2 + 1;
      CheckCursesError(mvwprintw(w, screen_y, screen_x, "%c%c", c, c));
    }
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
  CheckCursesError(mvwprintw(windows->score, 1, 2, "% 11d", s->score));
  CheckCursesError(mvwprintw(windows->line_count, 1, 2, "% 11d", s->lines));
  RefreshAllWindows(windows);
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
  StatusPrintf(windows, "Quickload not yet implemented!");
  return 0;
}

// Attempts to save the game state to the quicksave file. If any error occurs,
// this will *not* exit or crash, but instead will print an error message to
// the game display and return 0.
static void DoQuicksave(TetrisDisplay *windows, TetrisGameState *s) {
  // TODO: Implement DoQuicksave.
  StatusPrintf(windows, "Quicksave not yet implemented!");
}

// Takes the game window and empties it.
static void ClearGameBoard(TetrisDisplay *windows) {
  // For now, we will *not* clear the score and lines, so players can see them
  // after exiting games but before starting a new one.
  CheckCursesError(werase(windows->game));
  WinBox(windows->game);
}

// Sets up a new game, clearing the board, setting score and lines to 0, and
// setting up the current and next piece.
static void InitializeNewGame(TetrisGameState *s) {
  memset(s, 0, sizeof(*s));
  memset(s->board, ' ', sizeof(s->board));
  s->next_piece = RandomNewPiece();
  s->current_piece = RandomNewPiece();
  // The piece starts at the top, in the middle.
  s->piece_y = 0;
  s->piece_x = BLOCKS_WIDE / 2;
}

// Attempts to move a falling piece down by one spot. Returns 0 if the piece is
// blocked, otherwise moves the piece down by 1 row.
static int TryMovingDown(TetrisGameState *s) {
  const char *p = tetris_pieces[s->current_piece];
  int piece_x, piece_y, board_x, board_y;

  // NOTE: Can be more efficient.
  for (piece_y = 0; piece_y < 4; piece_y++) {
    board_y = s->piece_y - piece_y;
    if (board_y < -1) continue;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      if (p[piece_y * 4 + piece_x] == ' ') continue;
      board_x = s->piece_x + piece_x;
      // Note that the falling piece isn't part of the "board" yet, so we don't
      // need to worry about it overlapping with itself.
      if (!SpaceAvailable(s->board, board_x, board_y + 1)) return 0;
    }
  }

  // Move the piece down.
  s->piece_y++;
  return 1;
}

// Attempts to move a falling piece left by one column. Doesn't move the piece
// if the movement is blocked.
static void TryMovingLeft(TetrisGameState *s) {
  const char *p = tetris_pieces[s->current_piece];
  int piece_x, piece_y, board_x, board_y;
  // Can't move left at all if we're already on the left edge.
  if (s->piece_x == 0) return;

  for (piece_y = 0; piece_y < 4; piece_y++) {
    board_y = s->piece_y - piece_y;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      board_x = s->piece_x + piece_x;
      if (p[piece_y * 4 + piece_x] == ' ') continue;
      if (!SpaceAvailable(s->board, board_x - 1, board_y)) return;
    }
  }
  s->piece_x--;
}

// Similar to TryMovingLeft, but attempts to move the falling piece right.
static void TryMovingRight(TetrisGameState *s) {
  const char *p = tetris_pieces[s->current_piece];
  int piece_x, piece_y, board_x, board_y;
  // Unfortunately, we can't stop early here, because the rightmost column
  // depends on how far right the piece is (most pieces aren't 4 wide!).

  for (piece_y = 0; piece_y < 4; piece_y++) {
    board_y = s->piece_y - piece_y;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      board_x = s->piece_x + piece_x;
      if (p[piece_y * 4 + piece_x] == ' ') continue;
      if (!SpaceAvailable(s->board, board_x + 1, board_y)) return;
    }
  }
  s->piece_x++;
}

static void TryRotating(TetrisGameState *s) {
  // TODO: Implement TryRotating.
}

// Must be called after the falling piece can't fall any more, but *before*
// FinishFallingPiece. Returns 1 if any of the falling piece is above the top
// of the board.
static int IsGameOver(TetrisGameState *s) {
  const char *p = tetris_pieces[s->current_piece];
  int piece_x, piece_y, board_y;
  for (piece_y = 0; piece_y < 4; piece_y++) {
    board_y = s->piece_y - piece_y;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      if (p[piece_y * 4 + piece_x] == ' ') continue;
      // We found a non-space part of the current piece that ended up above the
      // board.
      if (board_y < 0) return 1;
    }
  }
  return 0;
}

// Removes the given row from the board, shifting down everything above it.
static void RemoveRowAndShift(uint8_t *board, int row) {
  int x, y, row_start, i;
  for (y = row; y > 0; y--) {
    row_start = y * BLOCKS_WIDE;
    for (x = 0; x < BLOCKS_WIDE; x++) {
      i = row_start + x;
      board[i] = board[i - BLOCKS_WIDE];
    }
  }
  // Clear the top row.
  for (x = 0; x < BLOCKS_WIDE; x++) {
    board[x] = ' ';
  }
}

// This function checks for completed lines, removes any complete lines, and
// scores points for lines completed. This must be called after a falling piece
// has landed and after FinishFallingPiece has finished. However,
// FinishFallingPiece will modify s->piece_y, so the caller is responsible for
// keeping track of the y position that just landed, and provide it.
static void CheckForCompleteLines(TetrisDisplay *w, TetrisGameState *s,
  int fallen_piece_y) {
  int completed_row_count = 0;
  int completed_rows[4];
  int board_x, board_y, row_ok, i;

  for (board_y = fallen_piece_y - 3; board_y <= fallen_piece_y; board_y++) {
    row_ok = 1;
    for (board_x = 0; board_x < BLOCKS_WIDE; board_x++) {
      if (!SpaceAvailable(s->board, board_x, board_y)) continue;
      row_ok = 0;
      break;
    }
    if (!row_ok) continue;
    completed_rows[completed_row_count] = board_y;
    completed_row_count++;
  }
  if (completed_row_count == 0) return;

  // Now that we've identified the completed rows, clear the pieces and shift
  // everything above it down.
  for (i = 0; i < completed_row_count; i++) {
    RemoveRowAndShift(s->board, completed_rows[i]);
  }
  ClearGameBoard(w);
  s->lines += completed_row_count;
  switch (completed_row_count) {
  case 1:
    s->score += 100;
    break;
  case 2:
    s->score += 400;
    break;
  case 3:
    s->score += 1600;
    break;
  case 4:
    s->score += 6400;
    break;
  default:
    break;
  }
}

// Makes the falling piece "land"; adding its cells to the game board, and
// generating a new falling piece.
static void FinishFallingPiece(TetrisGameState *s) {
  const char *p = tetris_pieces[s->current_piece];
  int piece_x, piece_y, board_x, board_y;
  char c;
  for (piece_y = 0; piece_y < 4; piece_y++) {
    board_y = s->piece_y - piece_y;
    for (piece_x = 0; piece_x < 4; piece_x++) {
      c = p[piece_y * 4 + piece_x];
      board_x = s->piece_x + piece_x;
      if (c == ' ') continue;
      s->board[board_y * BLOCKS_WIDE + board_x] = c;
    }
  }

  // Next, get the new falling piece.
  s->current_piece = s->next_piece;
  s->next_piece = RandomNewPiece();
  s->piece_x = BLOCKS_WIDE / 2;
  s->piece_y = -4;
}

// This function happens every time either a keypress occurs or
// MAX_MS_PER_FRAME has elapsed. Takes the time elapsed since the last call to
// UpdateGameState, the input key that has been pressed (which may be ERR if no
// key was pressed), and the timer (accumulator) to keep track of when the
// falling piece needs to be moved down next. Returns 0 on game over.
static int UpdateGameState(TetrisDisplay *w, TetrisGameState *s, double delta,
  int input_key, double *down_movement_timer) {
  int done_falling = 0;
  int fallen_piece_y = 0;
  // This is the number of seconds after which the piece moves down regardless
  // of what has been pressed.
  double down_movement_threshold = 0.7;
  // Every 10 lines, the piece speeds up by 1 ms, until it gets to the point of
  // moving down every frame at ~666 lines. When the threshold hits 0, it's
  // going to move down every single input event or frame, whichever comes
  // first!
  down_movement_threshold -= ((double) (s->lines / 10)) * 0.001;

  // Handle movement or rotation.
  switch (input_key) {
  case (KEY_LEFT):
    TryMovingLeft(s);
    break;
  case (KEY_RIGHT):
    TryMovingRight(s);
    break;
  case (KEY_UP):
    TryRotating(s);
    break;
  default:
    // This occurs if no key was pressed (input_key == ERR).
    break;
  }

  // Process downward movement after side-to-side movements or rotations.
  if (down_movement_threshold < 0.0) down_movement_threshold = 0.0;
  *down_movement_timer += delta;
  if ((input_key == KEY_DOWN) ||
    (*down_movement_timer > down_movement_threshold)) {
    // We'll get a point every time the piece moves down.
    s->score++;
    done_falling = !TryMovingDown(s);
    *down_movement_timer = 0.0;
  }
  if (done_falling) {
    // It's a game over if the falling piece is at all above the board.
    if (IsGameOver(s)) return 0;
    fallen_piece_y = s->piece_y;
    FinishFallingPiece(s);
    CheckForCompleteLines(w, s, fallen_piece_y);
  }
  return 1;
}

// Pauses the game; essentially turns off the movement timer, clears the board
// display, and waits for the player to press space or q. Returns 0 if the
// player "unpaused" by pressing 'q' to quit. Quicksaving and quickloading
// are allowed while paused.
static int PauseGame(TetrisDisplay *windows, TetrisGameState *s,
  int immediate_quickload) {
  int input_key;
  if (immediate_quickload) {
    TryQuickload(windows, s);
  }

  // When paused, switch back to waiting indefinitely for inputs.
  timeout(-1);

  // We won't show the piece positions while paused.
  ClearGameBoard(windows);
  CheckCursesError(mvwprintw(windows->game, 8, 8, "Paused!"));
  CheckCursesError(mvwprintw(windows->game, 9, 6, "Press space"));
  CheckCursesError(mvwprintw(windows->game, 10, 7, "to resume"));
  RefreshAllWindows(windows);

  // Wait for keypresses.
  while (1) {
    input_key = getch();
    switch (input_key) {
    case 's':
      DoQuicksave(windows, s);
      break;
    case 'l':
      TryQuickload(windows, s);
      break;
    case ' ':
      timeout(MAX_MS_PER_FRAME);
      return 1;
    case 'q':
      timeout(MAX_MS_PER_FRAME);
      return 0;
    default:
      break;
    }
  }
  // Should be unreachable.
  return 0;
}

// Runs a game until a quit or a game over occurs. Returns 0 on a game over,
// and nonzero on a quit. The initial_quickload argument should be zero to
// start a new game, and nonzero if we should attempt to load a quicksave
// immediately.
static int RunGame(TetrisDisplay *windows, int initial_quickload) {
  TetrisGameState s;
  int input_key, quickload_and_pause, should_exit = 0, game_done = 0;
  double time_delta, last_update_time, pre_pause_delta, down_movement_timer;
  down_movement_timer = 0.0;
  last_update_time = CurrentSeconds();
  if (initial_quickload) {
    if (!TryQuickload(windows, &s)) {
      InitializeNewGame(&s);
    }
  } else {
    InitializeNewGame(&s);
  }
  // We'll enter non-blocking behavior here, so we can move blocks down without
  // waiting for keypresses.
  timeout(MAX_MS_PER_FRAME);
  while (!game_done) {
    DisplayGameState(windows, &s);
    quickload_and_pause = 0;
    input_key = getch();
    time_delta = CurrentSeconds() - last_update_time;
    switch (input_key) {
    case 's':
      DoQuicksave(windows, &s);
      break;
    case 'l':
      quickload_and_pause = 1;
      // We'll fall through here; quickloading while the game is running will
      // always result in pausing.
    case ' ':
      // Keep track of the time prior to the pause keypress to avoid letting
      // people cheat and slow down blocks by pausing to reduce the
      // time_delta going into UpdateGameState.
      pre_pause_delta = CurrentSeconds() - last_update_time;
      game_done = !PauseGame(windows, &s, quickload_and_pause);
      last_update_time = CurrentSeconds() - pre_pause_delta;
      should_exit = game_done;
      timeout(MAX_MS_PER_FRAME);
      break;
    case 'q':
      game_done = 1;
      should_exit = 1;
      break;
    default:
      // Any directional movement, no keypress, or some random keypress will
      // be handled here.
      game_done = !UpdateGameState(windows, &s, time_delta, input_key,
        &down_movement_timer);
      last_update_time = CurrentSeconds();
      break;
    }
  }
  return should_exit;
}

int main(int argc, char **argv) {
  TetrisDisplay windows;
  int input_key, should_exit;
  srand(time(NULL));
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
    // Re-enable blocking behavior for getch if we just exited a game.
    timeout(-1);
    CheckCursesError(mvwprintw(windows.game, 9, 6, "Press space"));
    CheckCursesError(mvwprintw(windows.game, 10, 7, "to start!"));
    RefreshAllWindows(&windows);
    input_key = getch();
    switch (input_key) {
    case (' '):
      should_exit = RunGame(&windows, 0);
      if (!should_exit) {
        StatusPrintf(&windows, "Game over!");
        RefreshAllWindows(&windows);
      }
      ClearGameBoard(&windows);
      break;
    case ('l'):
      // Here, we just call RunGame like normal, except instruct it to
      // immediately try loading the quicksave if it exists.
      should_exit = RunGame(&windows, 1);
      if (!should_exit) {
        StatusPrintf(&windows, "Game over!");
        RefreshAllWindows(&windows);
      }
      ClearGameBoard(&windows);
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
