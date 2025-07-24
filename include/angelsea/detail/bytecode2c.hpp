// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/detail/jitcompiler.hpp>
#include <string>
#include <string_view>
#include <span>
#include <unordered_map>
#include <fmt/format.h>

namespace angelsea::detail
{

using ModuleId = std::size_t;
using FunctionId = std::size_t;

class BytecodeToC
{
    public:
    BytecodeToC(JitCompiler& compiler);

    void prepare_new_context();

    ModuleId translate_module(
        std::string_view internal_module_name,
        asIScriptModule* script_module,
        std::span<JitFunction*> functions
    );

    FunctionId translate_function(
        std::string_view internal_module_name,
        JitFunction& function
    );

    std::string entry_point_name(
        ModuleId module_id,
        FunctionId function_id
    ) const;

    std::string& source() { return m_buffer; }

    bool is_human_readable() const;

    private:
    void write_header();

    template<class... Ts>
    void emit(fmt::format_string<Ts...> format, Ts&&... format_args)
    {
        fmt::format_to(std::back_inserter(m_buffer), format, std::forward<Ts>(format_args)...);
    }

    JitCompiler* m_compiler;
    std::string m_buffer;
    ModuleId m_current_module_id;
    ModuleId m_current_function_id;
};

}