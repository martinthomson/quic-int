bench: bench.c
	gcc -m64 -std=gnu99 -O3 -o $@ $^
