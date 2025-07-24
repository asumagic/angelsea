// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/jitcompiler.hpp>
#include <utility>
#include <fmt/format.h>

namespace angelsea::detail
{

enum class LogSeverity
{
    VERBOSE,
    INFO,
    WARNING,
    PERF_WARNING,
    ERROR
};

template<typename... Ts>
void log_at(
    JitCompiler& jit,
    const char* section,
    int row,
    int col,
    LogSeverity severity,
    fmt::format_string<Ts...> format_string,
    Ts&&... fmt_args
) {
    asEMsgType type;

    // TODO: toggling severity
    switch (severity)
    {
    case LogSeverity::VERBOSE:
        type = asMSGTYPE_INFORMATION;
        break;

    case LogSeverity::INFO:
        type = asMSGTYPE_INFORMATION;
        break;

    case LogSeverity::PERF_WARNING:
    case LogSeverity::WARNING:
        type = asMSGTYPE_WARNING;
        break;

    case LogSeverity::ERROR:
        type = asMSGTYPE_ERROR;
        break;
    }

    jit.engine().WriteMessage(
        section, row, col, type,
        fmt::format(format_string, std::forward<Ts>(fmt_args)...).c_str()
    );
}

template<typename... Ts>
void log(
    JitCompiler& jit,
    asIScriptFunction& script_func,
    LogSeverity severity,
    fmt::format_string<Ts...> format_string,
    Ts&&... fmt_args
) {
    const char* decl_section;
    int decl_row, decl_col;
    script_func.GetDeclaredAt(&decl_section, &decl_row, &decl_col);
    log_at(jit, decl_section != nullptr ? decl_section : "", decl_row, decl_col, severity, format_string, std::forward<Ts>(fmt_args)...);
}

template<typename... Ts>
void log(
    JitCompiler& jit,
    LogSeverity severity,
    fmt::format_string<Ts...> format_string,
    Ts&&... fmt_args
) {
    log_at(jit, "", 0, 0, severity, format_string, std::forward<Ts>(fmt_args)...);
}

}