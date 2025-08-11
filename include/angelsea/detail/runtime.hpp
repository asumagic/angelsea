// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <as_context.h>

class asCScriptFunction;
class asCScriptObject;

// NOTE: all of these function declarations should be:
// - mirrored in the generated header in runtimeheader.hpp
// - registered to MIR in jitcompiler.cpp

extern "C" {
/// \brief Calls a script function by pointer (from m_engine->scriptFunctions).
///
/// The caller must ensure that the VM registers are saved before calling.
/// The JIT function should always return to the VM after calling this function.
void asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn);

/// \brief Shim for CallSystemFunction. Can be a method being called, but typically only for call instructions that deal
/// with the context's stack. If you want to call a specific object method instead and provide your own object pointer,
/// use `asea_call_system_method` instead.
///
/// Returns the number of DWORDs that should be popped from the stack by the caller.
int asea_call_system_function(asSVMRegisters* vm_registers, int fn);

/// \brief Shim for CallObjectMethod.
void asea_call_object_method(asSVMRegisters* vm_registers, void* obj, int fn);

int asea_prepare_script_stack(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
);

/// \brief Prints a debug message via the engine, only enabled when debugging.
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);

/// \brief Prints a debug message via the engine, only enabled when debugging.
void asea_debug_int(asSVMRegisters* vm_registers, int x);

/// \brief Wrapper for asCContext::SetInternalException. `text` should typically
/// be use one of the TXT_* AngelScript macros for the relevant exception.
void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text);

/// \brief Performs cleanup for arguments of a function. This generally amounts
/// to calling ref release or destruct behaviors.
void asea_clean_args(asSVMRegisters* vm_registers, asCScriptFunction& fn, asDWORD* args);

/// \brief Casts script object \ref obj to the requested \ref type_id; stores result in object register
void asea_cast(asSVMRegisters* vm_registers, asCScriptObject* obj, asDWORD type_id);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
// yes, it's not great to rely on offsetof given this is not a POD type; but AS does this all over the place and the
// involved types don't require multiple inheritance
static constexpr asPWORD asea_offset_ctx_callstack  = offsetof(asCContext, m_callStack);
static constexpr asPWORD asea_offset_ctx_status     = offsetof(asCContext, m_status);
static constexpr asPWORD asea_offset_ctx_currentfn  = offsetof(asCContext, m_currentFunction);
static constexpr asPWORD asea_offset_ctx_stackindex = offsetof(asCContext, m_stackIndex);
static constexpr asPWORD asea_offset_ctx_engine     = offsetof(asCContext, m_engine);
#pragma GCC diagnostic pop
}