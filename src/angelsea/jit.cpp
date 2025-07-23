// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/jit.hpp>
#include <angelsea/detail/log.hpp>

namespace angelsea
{

void Jit::NewFunction(asIScriptFunction* scriptFunc)
{
    detail::log(m_compiler, *scriptFunc, detail::LogSeverity::VERBOSE, "Registered \"{}\" for compilation", scriptFunc->GetDeclaration(true, true, true));
}

void Jit::CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc)
{
}

void Jit::CompileModules()
{
    detail::log(m_compiler, detail::LogSeverity::VERBOSE, "Requested JIT compilation for TODO modules");
}

Jit::~Jit()
{

}

}