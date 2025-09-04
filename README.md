# cheetah

My personal, high-performance fuzzer runtime that implements a forkserver, persistent mode and testcase-passing
over shared memory.

## Features
- Persistent mode with 75% less overhead than AFL++ while maintaining the same feature set
- Compatible with AFL++'s coverage instrumentation
- Rust bindings for LibAFL integration

## Usage
Create `libruntime.so` by invoking
```
make
```
optionally with custom `CC` and `CFLAGS` environment variables.

Add `./include/` to your include path when compiling your fuzz target and link
with `-lruntime`.

## API
To use `libruntime.so`, include the header file:
```
#include <fuzzer-runtime.h>
```

Then, you can make use of the following API:

| Function | Explanation |
|----------|-------------|
| `void spawn_forkserver (void)` | Equivalent to `__AFL_INIT` |
| `int spawn_persistent_loop (size_t iterations)` | Equivalent to `__AFL_LOOP` but no prior `spawn_forkserver()` is necessary |
| `MAX_ITERATIONS` | Convenience definition to do as many iterations as possible in `spawn_persistent_loop()` |
| `unsigned char* fuzz_input_ptr (void)` | Get a pointer to the fuzz input when it is being passed over shared memory. Equivalent to `__AFL_FUZZ_TESTCASE_BUF` |
| `size_t fuzz_input_len (void)` | Length of fuzz input. Equivalent to `__AFL_FUZZ_TESTCASE_LEN`. Must be called AFTER `spawn_forkserver()` or `spawn_persistent_loop()` |
| `size_t fuzz_input_max_len (void)` | Maximum length that a fuzz input can have |
| `size_t fuzz_input_capacity (void)` | Size of shared memory mapping for fuzz input |

## Benchmark
On my `Intel(R) Core(TM) i5-10210U CPU @ 1.60GHz` I get the following results when measuring the overhead of the
following persistent loop implementations:

![](./tests/results.png)

The benchmark measures the `exec/sec` of targets with an empty persistent loop:
```c
__AFL_INIT();
while (__AFL_LOOP((unsigned int) 0xFFFFFFFF));
```
and
```c
while (spawn_persistent_loop(MAX_ITERATIONS));
```
respectively.

The fuzzers for each target are built with LibAFL and are designed to be
as similar as possible. They only differ in the `Executor` in use.
For the AFL++ setup, a `ForkserverExecutor` with shared-memory testcases
was used and for the Cheetah setup, an `InProcessExecutor` was used that
utilized Cheetah's rust bindings directly. Both fuzzers were given the
same seed and were prevented from doing any input generation.
A `NopMutator` was created that leaves the seed inputs unmodified and
a corpus was built that consists of a single, zero-length file.

The exact setup is:
```
make
AFL_PATH=<your afl path> make -C tests
```
and
```
cd bindings
CORES=<core speficiation> cargo test --release bench_afl -- --nocapture
CORES=<core speficiation> cargo test --release bench_libruntime -- --nocapture
```
