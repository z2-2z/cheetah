CC=clang
CFLAGS=-g -O0 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined,address -fsanitize-trap=all -I..
LDFLAGS=-L.. -lruntime
LIBRUNTIME=../libruntime.so

all: test-persistent

test-persistent: test-persistent.c $(LIBRUNTIME)
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS)

$(LIBRUNTIME):
	$(MAKE) -C ..
