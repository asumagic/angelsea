// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

class asCScriptFunction;

// NOTE: all of these function declarations should be:
// - mirrored in the generated header in bytecode2c.cpp
// - registered to MIR in jitcompiler.cpp

extern "C" {
/// \brief Calls a script function by index (in m_engine->scriptFunctions).
///
/// The caller must ensure that the VM registers are saved before calling.
/// The JIT function should always return to the VM after calling this function.
int asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn);
}