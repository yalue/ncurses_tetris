// The main file for ncurses_tetris.
#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tetris.h"

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

static void ClearGameWindow(TetrisDisplay *windows) {
  CheckCursesError(werase(windows->game));
  WinBox(windows->game);
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
    ClearGameWindow(&windows);
    CheckCursesError(mvwprintw(windows.game, 9, 6, "Press space"));
    CheckCursesError(mvwprintw(windows.game, 10, 7, "to start!"));
    RefreshAllWindows(&windows);
    // TODO (next):
    //  - Clear all window centers
    //  - Write "Press space to start"
    //  - Write "Press q to quit"
    input_key = getch();
    switch (input_key) {
    case (' '):
      addstr("Space pressed\n");
      // TODO: Run the game here!
      //  - Draw the game window on the new game screen with "press space to start"
      //    in the middle.
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
  return 0;
}
