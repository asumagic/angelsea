// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

#include <cstdio>
#include <vector>

namespace angelsea {

enum class AbiMask {
	LINUX_GCC_X86_64     = 1 << 0,
	WINDOWS_MSVC_X86_64  = 1 << 1,
	WINDOWS_MINGW_X86_64 = 1 << 2,
	MACOS_X86_64         = 1 << 3,
	LINUX_GCC_AARCH64    = 1 << 4,
	MACOS_AARCH64        = 1 << 5,
};

struct JitConfig {
	struct LogTargets {
		asEMsgType verbose          = asEMsgType(-1);
		asEMsgType info             = asMSGTYPE_INFORMATION;
		asEMsgType performance_hint = asMSGTYPE_INFORMATION;
		asEMsgType warning          = asMSGTYPE_WARNING;
		asEMsgType error            = asMSGTYPE_ERROR;
	};

	/// Logging configuration for different severities.
	/// angelsea will use the usual AngelScript message mechanism to log various
	/// things.
	/// You can control what message type is used for different message
	/// severities.
	/// Negative values mean messages will not be printed.
	LogTargets log_targets = {};

	struct Debug {
		/// Whether to dump generated C code to stdout.
		bool dump_c_code = false;

		/// What file to dump the C file into, if dump_c_code is set.
		/// This is more intended for debugging than for processing the output.
		FILE* dump_c_code_file = stdout;

		/// Whether to dump optimized MIR code to stdout.
		bool dump_mir_code = false;

		/// What file to dump the MIR output into, if dump_mir_code is set.
		/// This is more intended for debugging than for processing the output.
		FILE* dump_mir_code_file = stdout;

		/// What file to dump C compile errors into.
		FILE* c2mir_diagnostic_file = stderr;

		/// MIR debugging level, as passed to `MIR_gen_set_debug_level`, to dump
		/// verbose information on the commandline.
		/// -1 disables them. As of writing this, meaningful values are -1, 0, 2, 4.
		int mir_debug_level = -1;

		/// What file to dump MIR debug logging into, if mir_debug_level >= 0.
		FILE* mir_diagnostic_file = stderr;

		/// Bytecode instructions that should emit a VM fallback; for debugging
		/// miscompiles and such.
		std::vector<asEBCInstr> blacklist_instructions;

		/// Emit a debug message via the engine on every function call.
		bool trace_functions = false;

		/// Generate a VM fallback after the given instruction, *after*
		/// the content of its handler. This can be useful to diagnose crashes
		/// caused by an instruction in some cases, as using
		/// `blacklist_instructions` may prevent some instructions from ever
		/// being reached.
		/// Might not be valid for all handlers.
		asEBCInstr fallback_after_instruction = asEBCInstr(-1);
	};
	Debug debug;

	struct CompileTriggers {
		/// How many times should a function has any of its JIT entry points (usually many times per function,
		/// especially hot ones) be hit before it triggers code generation.
		/// This avoids compiling cold functions unnecessarily, or even functions that are never called, which can be
		/// surprisingly common for code that relies a lot on `#include`.
		std::size_t hits_before_func_compile = 15000;
	};
	CompileTriggers triggers;

	/// MIR optimization level, as passed to `MIR_gen_set_optimize_level`, to balance between runtime speed and compile
	/// times (higher improves codegen).
	///
	/// MIR default is `2`. Meaningful values are 0 through 3, but `3` is known broken and *very* discouraged.
	/// `3` is not actually a meaningful option in upstream MIR. The Angelsea fork of MIR neutralizes Global Value
	/// Numbering memory optimizations for anything but level `3`, should you really want to try. The reason for it
	/// is that it has caused numerous complex bugs (interactions across several correct instructions), and still has
	/// unresolved issues that can result in corruption and crashes. From some testing, our generated code doesn't seem
	/// to care all that much performance-wise.
	int mir_optimization_level = 2;

	/// Maximum number of bytecode size in bytes for a function to be considered by the JIT compiler. This is to limit
	/// the effect of extremely large functions that take disproportionately much memory and compute time when compiled
	/// with MIR.
	///
	/// It can also avoid needlessly triggering compilation for long functions that are cold and very long to the point
	/// they hit enough JIT entry points to trigger compilation.
	std::size_t max_bytecode_bytes = 25000;

	/// Gross hack that frees a bunch of memory internally used by MIR that is not really used after the code generation
	/// of a function. This reduces RES memory usage very significantly in real applications.
	bool hack_mir_minimize = true;

	/// Ignore asBC_SUSPEND instructions, and never check for the suspend status in the VM. This is useful even if
	/// `asEP_BUILD_WITHOUT_LINE_CUES` is set, as some suspend instructions may remain, and some instructions implicitly
	/// check for suspend.
	bool hack_ignore_suspend = true;

	/// Ignore C++ exceptions thrown by application functions during direct system calls from JIT functions. If this
	/// hack is disabled, scripts won't be able to perform native calls and will become significantly slower.
	bool hack_ignore_exceptions = true;

	/// Do not update the program pointer, stack pointer and the stack frame pointers on direct system function calls
	/// and some other scenarios. This breaks callees that may rely on the debug interface to inspect script state, but
	/// is safe otherwise.
	bool hack_ignore_context_inspect = true;

	/// Speeds up script calls by replacing complex call runtime logic with code generation. Does not enable inlining
	/// yet. This is subject to breakage with AngelScript updates.
	bool experimental_fast_script_call = true;

	/// Speeds up the generic calling convention by replacing complex call runtime logic with code generation. This is
	/// subject to breakage with AngelScript updates. It also tries to be clever with the C++ ABI (as it has to populate
	/// the vtable pointer for asCGeneric correctly), which could be prone to breakage.
	bool experimental_direct_generic_call = true;

	/// Speeds up the native calling convention by replacing complex call runtime logic with code generation. This is
	/// subject to breakage with AngelScript updates. It is also more complex to support than the generic calling
	/// convention, and more likely to be buggy. It also has to essentially emulate the C++ ABI in some cases, which is
	/// more likely to break on less-tested platforms. Currently, few cases are supported, and native calls will often
	/// fall back to the VM.
	bool experimental_direct_native_call = true;

	/// Speeds up the native calling convention (assuming \ref experimental_direct_native_call is set) by skipping the
	/// AngelScript stack for passing arguments to native functions, when possible.
	bool experimental_stack_elision = false;

	/// Creates native code for the asBC_RET instruction, instead of falling back to the VM. Disabled by default as it
	/// was found to regress performance in microbenchmarks.
	bool experimental_fast_script_return = true;

	/// Speeds up the generic calling convention if \ref experimental_direct_generic_call is true by assuming that the
	/// called system functions will always set the return value. If the callee fails to do so when this function is
	/// set, uninitialized reads can happen script-side, which may result in crashes with pointers.
	bool hack_generic_assume_callee_correctness = false;

	struct CGeneratorConfig {
		/// Enables C generation that uses the GNU C "label as values" extension, see:
		/// https://gcc.gnu.org/onlinedocs/gcc-4.3.4/gcc/Labels-as-Values.html
		/// This enables slightly more efficient generated code in some cases.
		/// Requires compiler support (includes: C2MIR, gcc, clang, but not MSVC).
		/// Disabled by default, because it seems to regress performance with C2MIR.
		bool use_gnu_label_as_value = false;
		/// Enables use of __builtin_expect, which can improve code generation by biasing the code generator to assume
		/// certain branches may or may not get taken. This enables slightly more efficient generated code when using
		/// the MIR JIT. We wouldn't recommend enabling it for smarter AOT compilers, though.
		/// Requires compiler support (includes: C2MIR, gcc, clang, but not MSVC).
		bool use_builtin_expect = true;
		bool human_readable     = false;
		bool copyright_header   = false;

		/// Emits direct values to determine the offset of fields within e.g. `asCContext` as opposed to using external
		/// globals in C code. This results in less portable code (which does not matter for JIT). When using MIR JIT
		/// the only allowed value is `true`.
		bool emit_hardcoded_vm_offsets = true;

		// NOTE: must mirror the ABI in native calling convention in bytecode2c, may need in mirjit in some cases to
		// ensure the proper defines are used.
#if defined(__linux__) && defined(__GNUC__) && defined(__x86_64__)
		AbiMask abi = AbiMask::LINUX_GCC_X86_64;
#elif defined(_WIN32) && defined(__GNUC__) && defined(__x86_64__)
		AbiMask abi = AbiMask::WINDOWS_MINGW_X86_64;
#elif defined(_WIN32) && defined(_MSC_VER)
		AbiMask abi = AbiMask::WINDOWS_MSVC_X86_64;
#elif defined(__APPLE__) && defined(__x86_64__)
		AbiMask abi = AbiMask::MACOS_X86_64;
#elif defined(__linux__) && defined(__GNUC__) && defined(__arch64__)
		AbiMask abi = AbiMask::LINUX_GCC_AARCH64;
#elif defined(__APPLE__) && defined(__aarch64__)
		AbiMask abi = AbiMask::MACOS_AARCH64;
#else
		AbiMask abi;
#endif
	};
	CGeneratorConfig c;
};

} // namespace angelsea