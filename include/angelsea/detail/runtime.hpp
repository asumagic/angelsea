// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

class asCScriptFunction;

// NOTE: all of these function declarations should be:
// - mirrored in the generated header in runtimeheader.hpp
// - registered to MIR in jitcompiler.cpp

extern "C" {
/// \brief Calls a script function by pointer (from m_engine->scriptFunctions).
///
/// The caller must ensure that the VM registers are saved before calling.
/// The JIT function should always return to the VM after calling this function.
void asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn);

int asea_prepare_script_stack(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
);

/// \brief Prints a debug message via the engine, only enabled when debugging.
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);

/// \brief Wrapper for asCContext::SetInternalException. `text` should typically
/// be use one of the TXT_* AngelScript macros for the relevant exception.
void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text);

float asea_fmodf(float a, float b);
float asea_fmod(float a, float b);
}