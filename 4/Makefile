.PHONY: all test.o thread_pool.o

all: test.o thread_pool.o
	gcc test.o thread_pool.o ../utils/heap_help/heap_help.c

test.o: test.c
	gcc -Wall -c test.c -o test.o -I ../utils

thread_pool.o: thread_pool.c
	gcc -Wall -c thread_pool.c -o thread_pool.o
