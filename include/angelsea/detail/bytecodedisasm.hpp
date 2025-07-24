// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelsea/detail/bytecodeinstruction.hpp>
#include <string>

namespace angelsea::detail
{

class JitCompiler;

std::string disassemble(
    asIScriptEngine& engine,
    BytecodeInstruction instruction
);

}
