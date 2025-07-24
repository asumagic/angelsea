// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/detail/jitcompiler.hpp>
#include <string>
#include <string_view>
#include <span>
#include <functional>
#include <fmt/format.h>

namespace angelsea::detail
{

using ModuleId = std::size_t;
using FunctionId = std::size_t;

class BytecodeToC
{
    public:
    using OnMapFunctionCallback = std::function<void(JitFunction&, const std::string& name)>;

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

    /// Configure the callback to be invoked when a function is mapped to a C
    /// function name. This is useful to track the generated entry points in
    /// the source code.
    void set_map_function_callback(OnMapFunctionCallback callback)
    {
        m_on_map_function_callback = callback;
    }

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

    OnMapFunctionCallback m_on_map_function_callback;
};

}