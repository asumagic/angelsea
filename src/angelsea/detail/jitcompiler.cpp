// SPDX-License-Identifier: BSD-2-Clause

extern "C"
{
#include <c2mir/c2mir.h>
#include <mir.h>
#include <mir-gen.h>
}

#include <angelsea/detail/jitcompiler.hpp>
#include <angelsea/detail/log.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/bytecode2c.hpp>
#include <string>
#include <unordered_map>

namespace angelsea::detail
{

void JitCompiler::register_function(asIScriptFunction& script_function)
{
    JitFunction& jit_function = get_or_create_jit_function(script_function);
}

void JitCompiler::unregister_function(asIScriptFunction& script_function)
{
    const auto it = m_functions.find(&script_function);
    angelsea_assert(it != m_functions.end());
    m_functions.erase(it);
}

void JitCompiler::compile_all()
{
    detail::log(*this, LogSeverity::VERBOSE, "Requesting compilation for {} functions", m_functions.size());

    MIR_context_t mir = MIR_init();
    c2mir_init(mir);

    const char* include_dirs[] = {nullptr};

    c2mir_options c_options {
        .message_file = stderr, // TODO: optional
        .debug_p = 0,
        .verbose_p = 0,
        .ignore_warnings_p = 1,
        .no_prepro_p = 0,
        .prepro_only_p = 0,
        .syntax_only_p = 0,
        .pedantic_p = 1,
        .asm_p = 0,
        .object_p = 0,
        .module_num = 0, // ?
        .prepro_output_file = nullptr,
        .output_file_name = nullptr,
        .macro_commands_num = 0,
        .macro_commands = nullptr,
        .include_dirs = include_dirs
    };

    BytecodeToC c_generator{*this};

    auto modules = compute_module_map();

    // TODO: bad names that contain __angelsea_ or something

    for (auto& [script_module, functions] : modules)
    {
        if (script_module == nullptr)
        {
            // in the "anonymous" module?
            for (std::size_t i = 0; i < functions.size(); ++i)
            {
                // convert each function as their own virtual module
                // (is this useful? should reconsider)
                c_generator.prepare_new_context();
                c_generator.translate_module(
                    "<anon>",
                    script_module,
                    std::span{functions}.subspan(i, 1)
                );

                // c2mir_compile(mir, &c_options, TODO, TODO, "<anon>", nullptr);
            }
        }
        else
        {
            c_generator.prepare_new_context();
            c_generator.translate_module(
                script_module->GetName(),
                script_module,
                std::span{functions}
            );

            fmt::print("Compiled function:\n{}", c_generator.source());
        }
    }

    MIR_gen_init(mir);
    // MIR_link(mir, TODO, TODO);

    MIR_gen_finish(mir);

    // c

    c2mir_finish(mir);
    MIR_finish(mir);
}

JitFunction* JitCompiler::get_jit_function(asIScriptFunction& function)
{
    const auto it = m_functions.find(&function);
    return (it != m_functions.end()) ? &it->second : nullptr;
}

JitFunction& JitCompiler::get_or_create_jit_function(asIScriptFunction& function)
{
    const auto it = m_functions.find(&function);

    if (it != m_functions.end())
    {
        return it->second;
    }

    // insert key &func, value JITFunction(*this, function))
    const auto [ins_it, success] = m_functions.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(&function),
        std::forward_as_tuple(*this, function)
    );
    return ins_it->second;
}

std::unordered_map<asIScriptModule*, std::vector<JitFunction*>> JitCompiler::compute_module_map()
{
    std::unordered_map<asIScriptModule*, std::vector<JitFunction*>> ret;
    
    for (auto& [script_function, function] : m_functions)
    {
        ret[script_function->GetModule()].push_back(&function);
    }

    return ret;
}

}
