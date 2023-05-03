.PHONY: all test.o userfs.o


all: test.o userfs.o
	gcc -g -Wall -D NEED_OPEN_FLAGS -D NEED_RESIZE test.o userfs.o ../utils/heap_help/heap_help.c -ldl

test.o: test.c
	gcc -g -Wall -D NEED_OPEN_FLAGS -D NEED_RESIZE -c test.c -o test.o -I ../utils

userfs.o: userfs.c
	gcc -g -Wall -D NEED_OPEN_FLAGS -D NEED_RESIZE -c userfs.c -o userfs.o
