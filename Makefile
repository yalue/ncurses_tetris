.PHONY: all clean

CFLAGS := -Wall -Werror -O3 -g

all: tetris

tetris: tetris.c tetris.h
	gcc $(CFLAGS) -o tetris tetris.c -lcurses

clean:
	rm -f tetris

