CC ?= clang
INTERNAL_CFLAGS=-Wall -Wextra -Wpedantic -O3 -march=native -flto -fomit-frame-pointer -fno-stack-protector -fvisibility=hidden -s -fPIC -shared

C_SOURCES=$(wildcard *.c)
H_SOURCES=$(wildcard *.h)

libruntime.so: $(C_SOURCES) $(H_SOURCES)
	$(CC) -o $@ $(INTERNAL_CFLAGS) $(CFLAGS) $(C_SOURCES)

.PHONY: clean
clean:
	@rm -fv libruntime.so
