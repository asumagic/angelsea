// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/runtime.hpp>

#include <angelsea/detail/debug.hpp>

#include <angelscript.h>
#include <as_context.h>
#include <as_objecttype.h>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>

extern "C" {
void asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn) {
	auto& context = static_cast<asCContext&>(*vm_registers->ctx);
	context.CallScriptFunction(&fn);
}

void asea_debug_message(asSVMRegisters* vm_registers, const char* text) {
	auto& context = static_cast<asCContext&>(*vm_registers->ctx);
	context.GetEngine()->WriteMessage("<angelsea_debug>", 0, 0, asMSGTYPE_INFORMATION, text);
}
}
