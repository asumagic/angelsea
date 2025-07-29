# angelsea

> [!IMPORTANT]
> Angelsea is alpha quality software.

Angelsea is a **JIT compiler for AngelScript** written in C++20 which leverages
the lightweight [MIR](https://github.com/vnmakarov/mir) JIT runtime and its C11
compiler.

## Design

Angelsea compiles AngelScript bytecode to C functions that are JIT compiled by
c2mir and invoked by the usual JIT entry points in AngelScript.

C code generation amounts to pasting simple C that closely matches
AngelScript behavior.  
It can fallback to the interpreter at any point, and may be invoked from any JIT
entry point (e.g. inserted by AS at the start of the function or after calls).

## Supported platforms

Currently, only x86-64 Linux is being developed on and tested. However, MIR
supports many other platforms and CPU architectures, and Angelsea should be
generating fairly portable C code, so it shouldn't be hard to port.

(Ideally, we would setup CI to automatically test other platforms; perhaps even
using qemu integration with containers to test aarch64 etc.)

## Clone & Build

Start by cloning the repository itself:

```bash
git clone https://github.com/asumagic/angelsea.git
cd angelsea
```

Or, if you want to vendor it as a Git submodule to your repository:

```bash
cd whatever/vendor/directory/
git submodule add https://github.com/asumagic/angelsea.git
cd angelsea
```

**Example project:** [`example/hello/`](example/hello)

Angelsea has hard dependencies on:

- [**AngelScript**](https://www.angelcode.com/angelscript/) v2.37.0+(\*)
- [**fmt**](https://github.com/fmtlib/fmt),
- [**MIR**](https://github.com/vnmakarov/mir) v1.0(\*\*)

(\*): The version requirement stems from the use of the `asIJITCompilerV2`
interface.  
(\*\*): We require [a downstream fork](https://github.com/asumagic/mir) for now,
see rationale further below.

It has optional dependencies on:
- [**Catch2**](https://github.com/catchorg/Catch2/tree/devel) (for testing)

> [!NOTE] In theory, if you want to avoid building via CMake, you could pull
> Angelsea into your trunk to avoid the build process, and you would just have
> to ensure the include directories are right. We do not test or support this
> usecase.

### 1. Use vendored versions

**By default, these dependencies are vendored via git submodules.**  
When you add Angelsea as a CMake subdirectory, you can also use the targets it
provides for those libraries to link against them.

If you use CMake to build your project, you can choose to use the AngelScript
version we vendor for your own project and link against the `asea_angelscript`
target.

If you want to provide any of dependencies yourself (e.g. you also use them and
don't want to rely on Angelsea building it), see the optional step.

> [!NOTE]
> The `vendor/angelscript` submodule points to the
> [unofficial AngelScript](https://github.com/codecat/angelscript-mirror) git repository
> mirror by codecat.

> [!NOTE]
> We provide a downstream fork of MIR to work around the following issues:
> - [Integer sign-extension miscompile](https://github.com/vnmakarov/mir/issues/423)
> (workaround if using upstream: set `config.mir_optimization_level = 1;`)

```bash
git submodule update --init --recursive vendor/angelscript
git submodule update --init --recursive vendor/mir
git submodule update --init --recursive vendor/fmt
# if you want to run angelsea tests
git submodule update --init --recursive tests/vendor/*
```

### Optional: Provide specific dependencies yourself

TODO: the current steps work for static lib builds, but won't for dynamic libs
and doesn't work for tests, and it should provide a way to provide the paths to
built dependencies in that case.

#### AngelScript

Pass to CMake:

- `-DASEA_ANGELSCRIPT_SYSTEM=1`
- `-DASEA_ANGELSCRIPT_ROOT=/path/to/angelscript/sdk/` **(defaults to `vendor/angelscript/sdk`)**

#### fmt

Either:

- Pass `-DASEA_FMT_SYSTEM=1` to CMake to rely on `find_package(fmt)`.
- Pass the following to point headers to a known directory:
    - `-DASEA_FMT_SYSTEM=1`
    - `-DASEA_FMT_EXTERNAL=1`
    - `-DASEA_FMT_ROOT=/path/to/fmt` **(defaults to `vendor/fmt`)**

#### MIR

Pass to CMake:

- `-DASEA_MIR_SYSTEM=1`
- `-DASEA_MIR_ROOT=/path/to/mir/` **(defaults to `vendor/mir`)**

### 2a. Build the standalone library statically

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

On Linux anyway, the resulting library will be available as
`build/libangelsea.a`, which you can now link against in your build. For
vendored dependencies, make sure you link against them:

- `build/libmir.a`
- `build/vendor/fmt/libfmt.a` (`libfmtd.a` in debug)
- `build/vendor/angelscript/sdk/angelscript/projects/cmake/libangelscript.a`

### 2b. Include as a CMake submodule

If you use CMake to build your project, you can do the following:

```cmake
add_subdirectory(path/to/angelsea)
target_link_libraries(yourexecutable PRIVATE angelsea)
```

This will automatically build any vendored dependencies. You can also achieve
the steps in 1b with this setup by adding the following **BEFORE** the
`add_subdirectory` step:

```cmake
set(ASEA_FMT_SYSTEM OFF CACHE BOOL "")
```

Note that cache variable tend to be sticky; clear the workspace if the build
errors are confusing.

## Use

We implement the [`asIJITCompilerV2`](https://www.angelcode.com/angelscript/sdk/docs/manual/classas_i_j_i_t_compiler_v2.html)
JIT interface.

Enabling the JIT engine amounts to:

```cpp
#include <angelsea.hpp>

// ...

engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, true);
engine->SetEngineProperty(asEP_JIT_INTERFACE_VERSION, 2);

// optional, but recommended
engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);

angelsea::JitConfig config;
angelsea::Jit jit(config, *engine);
assert(engine->SetJITCompiler(&jit) >= 0);
```

**angelsea requires you to explicitly trigger native compilation.**
This is done by calling `jit.CompileModules();`.

### License notice

As of writing:

- **Angelsea is licensed under the [BSD-2-Clause license](LICENSE)**.
- MIR is licensed under the [MIT license](https://github.com/vnmakarov/mir/blob/master/LICENSE).
- AngelScript is licensed under the zlib license.
- libfmt is licensed under the [MIT license](https://github.com/fmtlib/fmt/blob/master/LICENSE) (with optional exception).

Those all have very similar permissive terms, but remember to give attribution
in source **and** binary distributions!

## Documentation

- [Running the test suite](doc/TESTS.md)

## Q&A

### What is the best supported calling convention?

> [!NOTE]
> As of writing, this is not true yet; asCALL_GENERIC calls are not yet handled.

For now, special care is given to the `asCALL_GENERIC` convention
performance-wise, but others will work (although they will fall back to the
interpreter).  
This is because it is the easiest to support without falling back to the
interpreter, because it can be made reasonably fast, and because the primary
downstream user of angelsea uses it exclusively.  
As of writing, `asIScriptGeneric` is not a particularly efficient interface, but
when the time comes, we may contribute back design improvements for it.

### Why generate C instead of MIR?

It actually makes a lot of sense to take this "lazy" approach.

1. Our bytecode2c compiler generates fairly standard C code. Entry points use
the `asJITFunction` signature, but nothing about it is really specific to JIT or
even to MIR...
2. ... So nothing really prevents you from AOT compiling AngelScript code to C
using bytecode2c for release builds, which might be interesting for consoles and
certain platforms (e.g. iOS) which notoriously ban JITs. You still would need
the interpreter (if only because Angelsea will fallback to it), but in theory,
all you would need to do is to add some glue code by implementing your own
`asIJITCompiler` that map JIT entry points to C++.
3. In theory, it enables the ability to inject native C code. Because MIR is
capable of inlining functions, they could be made to implement
performance-sensitive things like some array calls and avoid a native function
call. (This would still be feasible even if we generated MIR directly, but it is
an advantage of using C.)
4. Generated C code is a lot like the VM code. This is fairly quick to do and is
surprisingly human-readable even to people with no prior compilation experience.
    - We do take more care with strict aliasing rules than AS does, though.
5. The fact we can just speak C greatly simplifies interfacing with script
engine structures. Dealing with the C++ ABI (such as for certain native function
calls, or to call virtual AS engine functions) would be annoying, but we can
work around it to some extent.
6. In theory, it does mean we can leverage native tooling such as
AddressSanitizer and static analysis to debug JIT bugs.

### What about other JIT compilers?

- [BlindMindStudio's JIT compiler](https://github.com/BlindMindStudios/AngelScript-JIT-Compiler)
served its purpose, but it is unmaintained (although people have forked it), has
way suboptimal and x86-only code generation, falls back to the interpreter
often, and contains subtle bugs.
- I previously worked on [asllvm](https://github.com/asumagic/asllvm).
It was a functional proof-of-concept (in fact some of angelsea's infrastructure
originates from it), but it was way too large in scope. It more or less intended
to take over the entire interpreter, which meant total coverage of *all* of AS'
low-level semantics before it was any useful. Besides, LLVM is _huge_, breaks on
almost every major update, and is rather unreasonable for an embeddable
language.
- There is an [AOT compiler](https://github.com/quarnster/asaot), but it is
unmaintained and I don't know what it is worth nowadays. Due to its approach, if
AOT is fine for you, it might actually make sense to use.

To my knowledge, there is no other (public...) project of that kind.

### Do you support multi-threaded code generation?

Not yet, but it seems like this is something MIR lets you do pretty easily at a
module level.

### Why this name?

I was looking for puns with "mir" or "mimir" and miserably failed, so all you
get is Angel→C, which seems memorable enough.