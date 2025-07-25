## angelsea

> [!IMPORTANT]
> Angelsea is in very early stages of development and is not suitable for use.

Angelsea is a **JIT compiler for AngelScript** which leverages the lightweight
[MIR](https://github.com/vnmakarov/mir) JIT runtime and its C11 compiler.

### Design

Angelsea compiles AngelScript bytecode to C functions that are JIT compiled by
c2mir and invoked by the usual JIT entry points in AngelScript.

C code generation amounts to pasting simple C that closely matches
AngelScript behavior.  
It can fallback to the interpreter at any point, and may be invoked from any JIT
entry point (e.g. inserted by AS at the start of the function or after calls).

### Use

We implement the [`asIJITCompilerV2`](https://www.angelcode.com/angelscript/sdk/docs/manual/classas_i_j_i_t_compiler_v2.html)
JIT interface.

> [!IMPORTANT]
> angelsea is not ready for production yet; and we have not documented (or
> tested) build steps or made it any convenient to drop in in an existing
> project -- stay tuned

Enabling the JIT engine amounts to:

```cpp
engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, true);
engine->SetEngineProperty(asEP_JIT_INTERFACE_VERSION, 2);

// optional, but recommended
engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);

angelsea::JitConfig config;
angelsea::Jit jit(config, *engine);
assert(engine->SetJITCompiler(&jit) >= 0);
```

> [!NOTE]
> The `vendor/angelscript` submodule points to the
> [unofficial AngelScript](https://github.com/codecat/angelscript-mirror) git repository
> mirror by codecat.

#### License notice

As of writing:

- **Angelsea is licensed under the [BSD-2-Clause license](LICENSE)**.
- MIR is licensed under the [MIT license](https://github.com/vnmakarov/mir/blob/master/LICENSE).
- AngelScript is licensed under the zlib license.
- libfmt is licensed under the [MIT license](https://github.com/fmtlib/fmt/blob/master/LICENSE) (with optional exception).

Those all have very similar permissive terms, but remember to give attribution
in source **and** binary distributions!

### Q&A

#### What is the best supported calling convention?

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

#### Why generate C instead of MIR?

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
3. JIT compilation mostly amounts to copy-pasting bits of the interpreter with
some changes (such as baking in constants, etc.) -- this is stupid fast to do,
and surprisingly human-readable even to people with no prior compilation
experience.
4. The fact we can just speak C greatly simplifies interfacing with script
engine structures. Dealing with the C++ ABI (such as for certain native function
calls, or to call virtual AS engine functions) would be annoying, but we can
work around it to some extent.
5. In theory, it does mean we can leverage native tooling such as
AddressSanitizer and static analysis to debug JIT bugs.

#### What about other JIT compilers?

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

To my knowledge, other than some simple C++ AOT compilation wrappers, there is
no other (public...) project of that kind.

#### Do you support multi-threaded code generation?

Not yet, but it seems like this is something MIR lets you do pretty easily at a
module level.