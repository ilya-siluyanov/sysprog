.PHONY: all test.o thread_pool.o

all: test.o thread_pool.o
	gcc -Wall -g -D NEED_TIMED_JOIN test.o thread_pool.o ../utils/heap_help/heap_help.c

test.o: test.c
	gcc -Wall -g -D NEED_TIMED_JOIN -c test.c -o test.o -I ../utils

thread_pool.o: thread_pool.c
	gcc -Wall -g -D NEED_TIMED_JOIN -c thread_pool.c -o thread_pool.o
