// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

#include <cstdio>
#include <vector>

namespace angelsea {

struct JitConfig {
	struct LogTargets {
		asEMsgType verbose             = asEMsgType(-1);
		asEMsgType info                = asMSGTYPE_INFORMATION;
		asEMsgType performance_warning = asMSGTYPE_WARNING;
		asEMsgType warning             = asMSGTYPE_WARNING;
		asEMsgType error               = asMSGTYPE_ERROR;
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
		/// How many times should a module have any of its JIT entry points for any function hit before it is compiled
		/// from bytecode, to C, to unoptimized MIR. Afterwards, `hits_before_func_compile` is used for all functions in
		/// that module, to determine which should have code generated for it.
		std::size_t hits_before_module_compile = 0;

		/// How many times should a function has any of its JIT entry points (usually many times per function,
		/// especially hot ones) be hit before it triggers code optimization and generation.
		/// This avoids compiling cold functions unnecessarily, or even functions that are never called, which can be
		/// surprisingly common for code that relies a lot on `#include`.
		std::size_t hits_before_func_compile = 0;
	};
	CompileTriggers triggers;

	/// MIR optimization level, as passed to `MIR_gen_set_optimize_level`, to
	/// balance between runtime speed and compile times (higher improves
	/// codegen).
	/// MIR default is `2`. Meaningful values are 0 through 3.
	int mir_optimization_level = 3;

	// TODO: determine what exactly the compilation mode does -- AFAIK it is when MIR functions call other MIR functions
	// that have not be codegen'd yet.
	enum class MirCompilationMode {
		/// Regular compilation mode
		Normal,
		/// Lazy generation: Functions are generated on the first call of the
		/// function; functions that are never executed are never generated
		Lazy,
		/// Lazy BB generation: Basic blocks are compiled on first execution of
		/// the BB
		LazyBB,
	};
	MirCompilationMode mir_compilation_mode = MirCompilationMode::Lazy;

	struct CGeneratorConfig {
		/// Enables C generation that uses the GNU C "label as values" extension, see:
		/// https://gcc.gnu.org/onlinedocs/gcc-4.3.4/gcc/Labels-as-Values.html
		/// This enables slightly more efficient generated code.
		/// Requires compiler support (includes: C2MIR, gcc, clang, but not MSVC).
		bool use_gnu_label_as_value = true;
		bool human_readable         = true;
		bool copyright_header       = false;
	};
	CGeneratorConfig c;
};

} // namespace angelsea