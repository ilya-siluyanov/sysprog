clean:
	rm a.out || exit 0
	rm out.txt || exit 0

build:
	make clean
	gcc -Wall solution.c libcoro.c libcoro.h stack.h timings.h timings.c models.h ../utils/heap_help/heap_help.c

run:
	make build
	HHREPORT=1 ./a.out -t 10 -n 2 -o out.txt file1.txt file2.txt file3.txt file4.txt file5.txt

test:
	make run
	python checker.py -f out.txt
