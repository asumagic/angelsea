// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

#include <cstdio>

namespace angelsea {

struct JitConfig {
	// TODO: implement

	/// If the compiler is destroyed without ever having compiled a function,
	/// emit a warning
	bool warn_if_never_compiled = true;

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

	/// MIR optimization level, as passed to `MIR_gen_set_optimize_level`, to
	/// balance between runtime speed and compile times (higher improves
	/// codegen).
	/// MIR default is `2`. Meaningful values are 0 through 3.
	int mir_optimization_level = 3;
};

} // namespace angelsea