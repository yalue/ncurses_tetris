#ifndef TETRIS_H
#define TETRIS_H
// We'll use this file to contain commonly-accessed struct definitions from
// tetris.c.

// The width and height of the area where the blocks go, in blocks rather than
// characters.
#define BLOCKS_WIDE (10)
#define BLOCKS_TALL (20)

#include <curses.h>
#include <stdint.h>

// This struct keeps track of the tetris display windows from ncurses.
typedef struct {
  // The top-level curses "window", containing all of the other windows.
  WINDOW *top_window;
  // The "game" area, where the pieces are dropped and moved around.
  // Should be 20 characters wide (for 10 blocks) and 20 character tall (for
  // 10 blocks), plus one char of padding around the border; a total of 22x22
  // characters.
  WINDOW *game;
  // The window showing only the player's current score.
  WINDOW *score;
  // The window showing the number of lines the player has completed.
  WINDOW *line_count;
  // The window showing a preview of the next piece.
  WINDOW *next_piece;
} TetrisDisplay;

// This holds everything we need to know about the current state of an ongoing
// game.
typedef struct {
  // Holds the state of the entire board. If a cell is empty, it must be either
  // 0 or a space character. In any other case, the cell is occupied, and this
  // must contain the character to draw to the screen when drawing the cell.
  // (Note that cells are drawn as two characters wide, so each nonzero
  // character in this array will be printed twice when drawing the board.)
  char board[BLOCKS_WIDE * BLOCKS_TALL];

  // The ID of the next piece that will be generated.
  char next_piece;

  // The x and y location of the current piece being dropped into the board, in
  // a cell coordinate rather than a window character.
  int piece_x;
  int piece_y;

  // The piece that is currently "falling".
  char current_piece;

  // The player's current score.
  int score;

  // The number of lines the player has completed.
  int lines;
} TetrisGameState;

#endif  // TETRIS_H

