CC=clang
CFLAGS=-g -O0 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined,address -fsanitize-trap=all -I..
LDFLAGS=-L.. -lruntime
LIBRUNTIME=../libruntime.so

all: test-persistent hybrid-client

test-persistent: test-persistent.c
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS)

hybrid-client: hybrid-client.c
	test -n "$(AFL_PATH)"
	$(AFL_PATH)/afl-clang-fast -o $@ -I../include -g -O0 $< $(LDFLAGS)

$(LIBRUNTIME):
	$(MAKE) -C ..
