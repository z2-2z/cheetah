CC=clang
CFLAGS=-g -O0 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined,address -fsanitize-trap=all -I..
LDFLAGS=-L../runtime -lruntime
LIBRUNTIME=../runtime/libruntime.so

BINARIES=test-persistent hybrid-client shmem-echo test-forkserver

all: $(BINARIES)

shmem-echo: shmem-echo.c
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS)

test-persistent: test-persistent.c
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS)
	
test-forkserver: test-forkserver.c
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS)

hybrid-client: hybrid-client.c
	test -n "$(AFL_PATH)"
	$(AFL_PATH)/afl-clang-fast -o $@ -I../include -g -O0 $< $(LDFLAGS)

$(LIBRUNTIME):
	$(MAKE) -C ../runtime

.PHONY: clean
clean:
	@rm -fv $(BINARIES)
