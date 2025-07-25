// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

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

	/// Whether to dump optimized MIR code to stdout.
	bool dump_mir_code = false;
};

} // namespace angelsea