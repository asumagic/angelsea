// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/jitcompiler.hpp>
#include <angelsea/detail/log.hpp>
#include <angelsea/detail/debug.hpp>

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

}
