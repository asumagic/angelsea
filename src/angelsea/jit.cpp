// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/jit.hpp>

namespace angelsea
{

int JitCompiler::CompileFunction(asIScriptFunction* function, asJITFunction* output)
{
    return 1;
}

void JitCompiler::ReleaseJITFunction(asJITFunction func)
{

}

}