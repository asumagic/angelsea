## angelsea

Angelsea is a **JIT compiler for AngelScript** which leverages  the lightweight
[MIR](https://github.com/vnmakarov/mir) JIT runtime and its C11 compiler.

### Design

Angelsea compiles AngelScript bytecode to C functions that are JIT compiled by
c2mir and invoked by the usual JIT entry points in AngelScript.

C code generation amounts to pasting simple C that closely matches
AngelScript behavior.  
It can fallback to the interpreter at any point, and may be invoked from any JIT
entry point (e.g. inserted by AS at the start of the function or after calls).

Normally, you could just emit MIR directly, but generating C has few downsides
in this case.

### Use

We implement the [`asIJITCompiler`](https://www.angelcode.com/angelscript/sdk/docs/manual/classas_i_j_i_t_compiler.html)
JIT interface.

#### License notice

As of writing:

- **Angelsea is licensed under the [BSD-2-Clause license](LICENSE)**.
- MIR is licensed under the [MIT license](https://github.com/vnmakarov/mir/blob/master/LICENSE).
- AngelScript is licensed under the zlib license.
- libfmt is licensed under the [MIT license](https://github.com/fmtlib/fmt/blob/master/LICENSE) (with optional exception).

Those all have very similar permissive terms, but remember to give attribution
in source **and** binary distributions!

### Q&A

#### What about other JIT compilers?

- [BlindMindStudio's JIT compiler](https://github.com/BlindMindStudios/AngelScript-JIT-Compiler)
served its purpose, but it is unmaintained (although people have forked it), has
way suboptimal and x86-only code generation, falls back to the interpreter
often, and contains subtle bugs.
- I previously worked on [asllvm](https://github.com/asumagic/asllvm).
It was a functional proof-of-concept, but it was way too large in scope. It
more or less intended to take over the entire interpreter, which meant total
coverage of *all* of AS' low-level semantics before it was any useful. Besides,
LLVM is _huge_, breaks on almost every major update, and is rather unreasonable
for an embeddable language.

To my knowledge, other than some simple C++ AOT compilation wrappers, there is
no other (public...) project of that kind.