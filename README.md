# angelsea

> [!IMPORTANT]
> Angelsea is alpha quality software.

Angelsea is a **JIT compiler for AngelScript** written in C++20 which leverages
the lightweight [MIR](https://github.com/vnmakarov/mir) JIT runtime and its C11
compiler.

## Current status

We don't consider it stable yet, but it works. The test suite is constantly
checked and it is regularly tested against a test development build of
[King Arthur's Gold](https://store.steampowered.com/app/219830/King_Arthurs_Gold).

It is still in a bit of a "move fast, break things" phase, but we're trying to
keep unstable features out of the main branch.

### Performance

Compared to the [BlindMindStudios JIT](https://github.com/BlindMindStudios/AngelScript-JIT-Compiler),
you should hopefully get similar or exceeding (+30%) performance in real world
usecases.  
However, we haven't looked at performance too closely yet; and since instruction
support is not complete and there are differences in ABI support YMMV.

#### Compiler performance

The compiler itself isn't very fast or memory efficient but we mitigate those
concerns as well as we can:

- Lazy compilation is used by default, so cold functions can be ignored.
- Asynchronous compilation can be enabled using `Jit::SetCompileCallback`.
Currently, this can bottleneck on a single thread, but it will not block the
main thread.
- Huge functions (typically thousands of lines) are ignored by default due to
exploding compute and memory costs.

## Supported platforms

MIR supports many other platforms and CPU architectures, and Angelsea should be
generating fairly portable C code, but very little of it is tested (please don't
bother trying big-endian).

We would [like to support](https://github.com/asumagic/angelsea/issues?q=is%3Aissue%20state%3Aopen%20label%3Atargets):

- ✅ **Linux x86-64** (CI pass, tested on real app)
- ✅ MinGW x86-64 (CI pass, tested on real app)
- ✅ MSVC x86-64 (CI pass, not tested on real app)
- ❌ Linux aarch64 (major breakage, need to investigate ABI issues)
- ❌ macOS aarch64 (major breakage, need to investigate ABI issues)
- ❌ macOS x86-64 (broken native calling convention for certain signatures + requires macOS 14 due to upstream AS issue with macOS 15)

32-bit support is not planned because MIR supports no such platform.
[BlindMindStudio's JIT](https://github.com/BlindMindStudios/AngelScript-JIT-Compiler) supports 32-bit and 64-bit x86.

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
- [**nanobench**](https://github.com/martinus/nanobench/) (for benchmarking in testing)

> [!NOTE]
> In theory, if you want to avoid building via CMake, you could pull Angelsea
> into your trunk to avoid the build process, and you would just have to ensure
> the include directories are right. We do not test or support this usecase.

### 1. Use vendored versions

**By default, these dependencies are vendored via git submodules.**  
When you add Angelsea as a CMake subdirectory, you can also use the targets it
provides for those libraries to link against them.

If you use CMake to build your project, you can choose to use the AngelScript
version we vendor for your own project and link against the `asea_angelscript`
target.

If you want to provide any of dependencies yourself (e.g. you also use them and
don't want to rely on Angelsea building it), see the optional step.

> [!WARNING]
> As an user, you should know that upstream MIR has not seen activity in a year.
> I provide a downstream fork of MIR to work around specific problems, and we
> hope the defaults to be fully stable.
>
> The contents of the fork is:
> - The load/store optimizations of GVN pass broke on various occasions with our
> generated code. It just seems to intensely dislike the constant AS stack back
> and forth we are doing.
> We worked around some issues (see below), but it is frankly more trouble than
> it is worth.
> Thus we moved it to a "-O3" level; and we default to
> `config.mir_optimization_level = 2`, and strongly discourage changing this.
> - Solve [Integer sign-extension miscompile](https://github.com/vnmakarov/mir/issues/423)
> (workaround if using upstream: set `config.mir_optimization_level = 1;`)
> - Solve [Jump optimization can cause use-after-free when using label references](https://github.com/vnmakarov/mir/issues/424)
> (workaround if using upstream: set `config.mir_optimization_level = 1;`)
> - Solve high memory usage: implemented a hack; see `config.hack_mir_minimize` (defaults to true)

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

- `build/libasea_mir.a`
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

Note that cache variables tend to be sticky; clear the workspace if the build
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
engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);

// example config
angelsea::JitConfig config;
angelsea::Jit jit(config, *engine);
assert(engine->SetJITCompiler(&jit) >= 0);
```

It is strongly encouraged to check [`JitConfig` tunables](include/angelsea/config.hpp)
and to adjust it accordingly for your application.  
The defaults are overall tuned for the needs of a semi-heavily scripted
commercial application.

For instance, to disable lazy compilation:

```cpp
angelsea::JitConfig config {
    .triggers = {
        .hits_before_func_compile = 0,
    }
};
```

> [!WARNING]
> The JIT compiler itself is not currently thread-safe with regards to
> multithreaded AngelScript contexts or engines.

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

### How does the JIT compiler work?

AngelScript uses a bytecode virtual machine, so a JIT compiler has to take this
bytecode and translate some or all of it to native code.  
Bytecode functions have one or more "JIT entry points" from which a JIT function
can start. This effectively allows JIT functions to drop down to the VM for
unsupported instructions, but still making it possible to be called again.

Angelsea compiles AngelScript bytecode to C functions. c2mir compiles that to
MIR in memory, and MIR ultimately emits machine code.

Lazy compilation works by giving AngelScript dummy JIT functions that merely
count how often they were called. Once the configurable threshold is reached,
compilation will be triggered, potentially asynchronously.

### What is the best supported calling convention?

Currently, the `asCALL_GENERIC` calling convention is the best supported (though
some things are not covered).

There is experimental support for the native calling convention (on by default).
Many cases should be covered, but some are omitted.

Supported system calls are much faster with the JIT with either the generic or
native calling convention.

There is also an experimental "stack elision" optimization
(`experimental_stack_elision`) that can improve the native calling convention
performance by bypassing stack pushes entirely when possible.

As of writing, `asIScriptGeneric` is not a particularly efficient interface (see
below), but when the time comes, we may try to contribute back design
improvements for it.

<details>
<summary>Elaborating on the generic calling convention</summary>

`asIScriptGeneric` is a rather slow interface by design. You might think that
the native calling convention (e.g. `asCALL_CDECL` and all) might be faster, but
it's not actually obvious why that would be true. In stock AngelScript (i.e. no
JIT), the native convention is actually fairly slow as it needs to go through
native shims that are fairly complex due to needing to handle arbitrary
signatures, resulting in a rather impressive pile of C++ ABI emulation for each
unique platform.

AngelScript tries to be clever about this in some cases (e.g. `asBC_Thiscall1`),
but native calls otherwise carry more overhead than you might think.

In essence, the native calling convention actually fundamentally has to do
_more_ work than the generic calling convention! In both cases, all arguments
live on the AS stack either way, and have to be pulled out of it sooner or
later. Whether AS itself or your app need to look up those arguments doesn't
really matter.

That being said, the generic calling convention *is* actually somewhat slow
out-of-the-box in AngelScript, but this is not an unsolvable problem.  
The callee (your code) needs to do, for _every_ argument or other call to the
generic, a virtual function call. This is usually not outrageously expensive,
but it prevents inlining despite the functions being (overall) not very
expensive each.  
Worse, some functions also do a lot of work and e.g. need to look up the
script function type information and then loop over arguments,
_for every argument lookup you do_.

When automatically wrapping functions for the generic calling convention, it
actually is feasible to hack a lot of that complexity away by directly poking at
`asCGeneric`.

Angelsea is able to make generic calls much faster by doing a lot less work than
the AngelScript VM, though, largely thanks to the fact we can generate code
tailored for each function, skipping steps and branches we know are
unnecessary. It also has some hacks like pooling the `asCGeneric` objects at
function level to skip reinitialization of fields that never change.  
This allows angelsea to give a ~5x performance uplift for a 1 million generic
calls benchmark.

</details>

### Why generate C instead of MIR?

It actually makes a lot of sense to take this "lazy" approach.

1. Our bytecode2c compiler generates standard (enough) C code. Entry points use
the `asJITFunction` signature. We also try to resolve all references to pointers
baked in the bytecode and forward detailed information via a callback.  
Fundamentally, nothing about the codegen is really specific to JIT or even to
MIR...
2. ... So nothing really prevents you from AOT compiling AngelScript code to C
using bytecode2c for release builds, which might be interesting for consoles and
certain platforms (e.g. iOS) which notoriously ban JITs. You still would need
the interpreter (if only because Angelsea will fallback to it), but in theory,
all you would need to do is use bytecode2c with appropriate symbol callbacks,
and add some glue code by implementing your own `asIJITCompiler` that map JIT
entry points to C++. (This isn't really an easy task yet, and there are _still_
some prerequisite refactors before this is viable, but the overall design
fundamentally allows it.)
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
often (in our usecase), and contains subtle bugs.
- I previously worked on [asllvm](https://github.com/asumagic/asllvm).
It was a functional proof-of-concept (in fact some of angelsea's infrastructure
originates from it), but it was way too large in scope. It more or less intended
to take over the entire interpreter, which meant total coverage of *all* of AS'
low-level semantics before it was any useful. This is doubly a problem, because
to speak the C ABI -- let alone the C++ ABI -- you basically need to do it
yourself AFAIK. Besides, LLVM is _huge_, breaks API compatibility on almost
every major update, and is rather unreasonable for an embeddable language.
- Hazelight's UnrealEngine-AngelScript is way more involved than I thought and
includes a [C++ AOT transpiler](https://github.com/Hazelight/UnrealEngine-Angelscript/tree/e1bdb40e97da880ae907030dda65639d5a4b7b3d/Engine/Plugins/Angelscript/Source/AngelscriptCode/Private/StaticJIT).
To my understanding it is tightly coupled to that project and its fork of AS,
and does not feature an actual JIT compiler.
- There is an [AOT compiler](https://github.com/quarnster/asaot), but it is
unmaintained and I don't know what it is worth nowadays. Due to its approach, if
AOT is fine for you, it might actually make sense to use.

To my knowledge, there is no other (public...) project of that kind.

### Why this name?

I was looking for puns with "mir" or "mimir" and miserably failed, so all you
get is Angel→C, which seems memorable enough.