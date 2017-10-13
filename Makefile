bench: bench.c
	$(CC) -m64 -std=gnu99 -O3 -o $@ $^
