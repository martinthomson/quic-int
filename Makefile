.PHONY: all
all: bench64 bench32
bench64: bench.c
	$(CC) -m64 -std=gnu99 -O3 -o $@ $^
bench32: bench.c
	$(CC) -m32 -std=gnu99 -O3 -o $@ $^
