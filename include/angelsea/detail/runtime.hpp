// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <as_context.h>
#include <as_objecttype.h>
#include <as_scriptobject.h>

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

/// \brief Same as \ref asea_prepare_script_stack but also makes space for variables by bumping the stack pointer and
/// clears out whatever variables needs to be. This function variant is useful when the concrete function is only known
/// at runtime.
int asea_prepare_script_stack_and_vars(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
);

/// \brief Prints a debug message via the engine, only enabled when debugging.
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);

/// \brief Prints a debug message via the engine, only enabled when debugging.
void asea_debug_int(asSVMRegisters* vm_registers, asPWORD x);

/// \brief Wrapper for asCContext::SetInternalException. `text` should typically
/// be use one of the TXT_* AngelScript macros for the relevant exception.
void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text);

/// \brief Performs cleanup for arguments of a function. This generally amounts
/// to calling ref release or destruct behaviors.
void asea_clean_args(asSVMRegisters* vm_registers, asCScriptFunction& fn, asDWORD* args);

/// \brief Casts script object \ref obj to the requested \ref type_id; stores result in object register
void asea_cast(asSVMRegisters* vm_registers, asCScriptObject* obj, asDWORD type_id);

/// \brief Heap-allocate a new script object and construct it, then return the pointer to it. The caller should still be
/// calling the scripted constructor for that object.
void* asea_new_script_object(asCObjectType* obj_type);

void* asea_alloc(asQWORD size);
void  asea_free(void* ptr);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
// yes, it's not great to rely on offsetof given this is not a POD type; but AS does this all over the place and the
// involved types don't require multiple inheritance
static constexpr asPWORD asea_offset_ctx_callstack  = offsetof(asCContext, m_callStack);
static constexpr asPWORD asea_offset_ctx_status     = offsetof(asCContext, m_status);
static constexpr asPWORD asea_offset_ctx_currentfn  = offsetof(asCContext, m_currentFunction);
static constexpr asPWORD asea_offset_ctx_stackindex = offsetof(asCContext, m_stackIndex);
static constexpr asPWORD asea_offset_ctx_engine     = offsetof(asCContext, m_engine);

static constexpr asPWORD asea_offset_scriptfn_scriptdata = offsetof(asCScriptFunction, scriptData);
static constexpr asPWORD asea_offset_scriptdata_jitfunction
    = offsetof(asCScriptFunction::ScriptFunctionData, jitFunction);

static constexpr asPWORD asea_offset_scriptobj_objtype = offsetof(asCScriptObject, objType);

static constexpr asPWORD asea_offset_objtype_vtable = offsetof(asCObjectType, virtualFunctionTable);
#pragma GCC diagnostic pop
}