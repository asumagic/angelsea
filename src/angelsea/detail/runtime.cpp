// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/runtime.hpp>

#include <angelsea/detail/debug.hpp>

#include <angelscript.h>
#include <as_context.h>
#include <as_objecttype.h>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>

static asCContext&      asea_get_context(asSVMRegisters* regs) { return static_cast<asCContext&>(*regs->ctx); }
static asCScriptEngine& asea_get_engine(asSVMRegisters* regs) {
	return static_cast<asCScriptEngine&>(*asea_get_context(regs).GetEngine());
}

extern "C" {
void asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn) {
	asea_get_context(vm_registers).CallScriptFunction(&fn);
}

void asea_debug_message(asSVMRegisters* vm_registers, const char* text) {
	asea_get_engine(vm_registers).WriteMessage("<angelsea_debug>", 0, 0, asMSGTYPE_INFORMATION, text);
}

void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text) {
	asea_get_context(vm_registers).SetInternalException(text);
}
}
