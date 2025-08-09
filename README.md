# cheetah

My high-performance fuzzer runtime that implements a forkserver, persistent mode and passing fuzz inputs
over shared memory.

### Features
- Persistent mode with 85% less overhead than AFL++ while maintaining the same feature set
- Rust bindings for LibAFL integration

## Usage
Create `libruntime.so` by invoking
```
make
```
Optionally with custom `CC` and `CFLAGS` environment variables.

Add `./include/` to your include path when compiling your fuzz target.

## API
To use `libruntime.so`, include the header file
```
#include <fuzzer-runtime.h>
```

Then you can make use of the following API:

todo
