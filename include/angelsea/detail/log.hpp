// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <fmt/format.h>
#include <utility>

namespace angelsea::detail {

enum class LogSeverity { VERBOSE, INFO, WARNING, PERF_WARNING, ERROR };

template<typename... Ts>
void log_at(
    const JitConfig&          config,
    asIScriptEngine&          engine,
    const char*               section,
    int                       row,
    int                       col,
    LogSeverity               severity,
    fmt::format_string<Ts...> format_string,
    Ts&&... fmt_args
) {
	int type;

	const auto& targets = config.log_targets;
	switch (severity) {
	case LogSeverity::VERBOSE: {
		type = targets.verbose;
		break;
	}
	case LogSeverity::INFO: {
		type = targets.info;
		break;
	}
	case LogSeverity::PERF_WARNING: {
		type = targets.performance_warning;
		break;
	}
	case LogSeverity::WARNING: {
		type = targets.warning;
		break;
	}
	case LogSeverity::ERROR: {
		type = targets.error;
		break;
	}
	}

	if (type < 0) {
		return;
	}

	engine.WriteMessage(
	    section,
	    row,
	    col,
	    asEMsgType(type),
	    fmt::format(format_string, std::forward<Ts>(fmt_args)...).c_str()
	);
}

template<typename... Ts>
void log(
    const JitConfig&          config,
    asIScriptEngine&          engine,
    asIScriptFunction&        script_func,
    LogSeverity               severity,
    fmt::format_string<Ts...> format_string,
    Ts&&... fmt_args
) {
	const char* decl_section;
	int         decl_row, decl_col;
	script_func.GetDeclaredAt(&decl_section, &decl_row, &decl_col);
	log_at(
	    config,
	    engine,
	    decl_section != nullptr ? decl_section : "",
	    decl_row,
	    decl_col,
	    severity,
	    format_string,
	    std::forward<Ts>(fmt_args)...
	);
}

template<typename... Ts>
void log(
    const JitConfig&          config,
    asIScriptEngine&          engine,
    LogSeverity               severity,
    fmt::format_string<Ts...> format_string,
    Ts&&... fmt_args
) {
	log_at(config, engine, "", 0, 0, severity, format_string, std::forward<Ts>(fmt_args)...);
}

} // namespace angelsea::detail