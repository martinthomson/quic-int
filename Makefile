CFLAGS := -Wall -Werror -std=gnu99 -O3

.PHONY: all
all: bench64 bench32
bench64: bench.c
	$(CC) -m64 $(CFLAGS) -o $@ $^
bench32: bench.c
	$(CC) -m32 $(CFLAGS) -o $@ $^
clean:
	git clean -fX
