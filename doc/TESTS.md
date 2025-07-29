# Running Angelsea tests

## Setting up environment

Make sure that the vendored test dependencies are cloned:

```bash
git submodule update --init --recursive tests/vendor/*
```

When configuring the library with CMake, also provide `-DASEA_ENABLE_TESTING=1`.

## Running tests

The test suite uses Catch2, so refer to that for the commandline interface.  
The test executable should be run from the `tests/` directory.

### Building and running all tests on an AddressSanitizer build

```
cd tests/
ninja -C ../build && ASAN_OPTIONS=detect_leaks=0 ../build/tests/angelsea-tests
```

### Filtering tests

Refer to Catch2 docs, but you can do e.g.:

```
cd tests/
../build/tests/angelsea-tests
```

### Debug output

For tests, the `JitConfig` can largely be configured via environment
variables.  
Caution: It's only checked whether the environment variables exist, i.e.
`ASEA_DUMP_MIR=0` will be interpreted incorrectly!

```
ASEA_DUMP_MIR=1 ASEA_DUMP_C=1 ../build/tests/angelsea-tests [fib]
```

See `get_test_jit_config` (in [`common.cpp`](../tests/common.cpp)) for more
options.