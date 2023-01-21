Tetris Using Libncurses
=======================

A quick exercise to place myself in the elite cabal of programmers who have
written their own versions of tetris.

Compilation
-----------

 - Ensure that `libncurses5-dev` (or the equivalent) is installed.

 - Navigate to the top-level directory of this project (the one
   containing this README).

 - Run `make`.

Usage
-----

After compiling, run `./tetris`.

Controls:

 - On the initial screen (or after a game over), press space to start a new
   game.

 - Left and right arrow keys: move pieces left and right.

 - Up arrow: rotate the current piece.

 - Down arrow: speed the current piece's descent.

 - Page down: Immediately move the current piece down to its landing position.

 - The 's' key: quick save (will create the file `./tetris_quicksave.bin`).

 - The 'l' key: load last quick save. This will read `./tetris_quicksave.bin`
   if it exists. Otherwise, pressing 'L' does nothing.

 - The 'q' key: quits the game immediately.

Troubleshooting
---------------

If the game windows show up as incorrect sizes, make sure your TERM environment
variable matches the terminal you're actually using.  For example, in older
versions of PuTTY, setting TERM="xterm" would cause problems, but TERM="putty"
was OK.

