CXXFLAGS = -g

clean:
	rm -rf testdir
	rm -f a.out
	rm -rf a.out.dSYM

build:
	# gcc -Wall solution.c parser.h array.h types.h ../utils/heap_help/heap_help.h ../utils/heap_help/heap_help.c
	gcc -Wall solution.c models.h stack.h libcoro.h libcoro.c ../utils/heap_help/heap_help.h ../utils/heap_help/heap_help.c

build-sanitize:
	gcc -fPIE -pie -fsanitize=address -fno-omit-frame-pointer -Wall solution.c parser.h array.h types.h

run:
	$(MAKE) clean
	$(MAKE) build
	./a.out -t 100 -n 10 -o out.txt file1.txt file2.txt file3.txt file4.txt file5.txt

test:
	$(MAKE) build
	python checker.py

run-s:
	$(MAKE) clean
	$(MAKE) build-sanitize
	./a.out