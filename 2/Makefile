CXXFLAGS = -g

clean:
	rm -f a.out
	rm -rf a.out.dSYM

build:
	# gcc -Wall solution.c parser.h array.h types.h ../utils/heap_help/heap_help.h
	gcc -Wall solution.c parser.h array.h types.h ../utils/heap_help/heap_help.c

build-sanitize:
	gcc -fPIE -pie -fsanitize=address -fno-omit-frame-pointer -Wall solution.c parser.h array.h types.h

run:
	$(MAKE) clean
	$(MAKE) build
	HHREPORT=1 ./a.out

test:
	$(MAKE) build
	python checker.py --max 20

run-s:
	$(MAKE) clean
	$(MAKE) build-sanitize
	./a.out
