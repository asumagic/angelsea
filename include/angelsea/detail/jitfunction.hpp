// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

namespace angelsea::detail
{

class JitCompiler;

class JitFunction
{
    public:
    JitFunction(JitCompiler& compiler, asIScriptFunction& script_function);

    private:
    JitCompiler& m_compiler;
    asIScriptFunction* m_script_function;
};

}