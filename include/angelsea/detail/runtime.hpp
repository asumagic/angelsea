// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

// NOTE: all of these function declarations should be:
// - mirrored in the generated header in bytecode2c.cpp
// - registered to MIR in jitcompiler.cpp

extern "C"
{

/// \brief Calls a script function by index (in m_engine->scriptFunctions).
///
/// The caller must ensure that the VM registers are updated before calling.
///
/// This returns the value of `m_status`, which describes whether execution was
/// successful. Should it be `!= asEXECUTION_FINISHED`, the JIT function must
/// return to the VM.
int asea_call_script_function(asSVMRegisters* vm_registers, int function_idx);

}