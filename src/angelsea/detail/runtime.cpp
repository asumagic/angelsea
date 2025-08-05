// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/runtime.hpp>

#include <angelscript.h>
#include <angelsea/detail/debug.hpp>
#include <as_context.h>
#include <as_objecttype.h>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>
#include <cmath>

static asCContext&      asea_get_context(asSVMRegisters* regs) { return static_cast<asCContext&>(*regs->ctx); }
static asCScriptEngine& asea_get_engine(asSVMRegisters* regs) {
	return static_cast<asCScriptEngine&>(*asea_get_context(regs).GetEngine());
}

extern "C" {
void asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn) {
	asea_get_context(vm_registers).CallScriptFunction(&fn);
}

int asea_prepare_script_stack(asSVMRegisters* vm_registers, asCScriptFunction& fn) {
	asCContext& ctx = asea_get_context(vm_registers);
	if (ctx.PushCallState() < 0) {
		return 1;
	}

	ctx.m_currentFunction     = &fn;
	ctx.m_regs.programPointer = fn.scriptData->byteCode.AddressOf();

	asDWORD* oldStackPointer = ctx.m_regs.stackPointer;
	asUINT   needSize        = fn.scriptData->stackNeeded;

	// TODO: move this to native codegen
	// With a quick check we know right away that we don't need to call ReserveStackSpace and do other checks inside it
	if (ctx.m_stackBlocks.GetLength() == 0
	    || oldStackPointer - (needSize + RESERVE_STACK) < ctx.m_stackBlocks[ctx.m_stackIndex]) {
		if (!ctx.ReserveStackSpace(needSize)) {
			return 1;
		}

		if (ctx.m_regs.stackPointer != oldStackPointer) {
			int numDwords = fn.GetSpaceNeededForArguments() + (fn.objectType ? AS_PTR_SIZE : 0)
			    + (fn.DoesReturnOnStack() ? AS_PTR_SIZE : 0);
			memcpy(ctx.m_regs.stackPointer, oldStackPointer, sizeof(asDWORD) * numDwords);
		}
	}

	// Update framepointer
	ctx.m_regs.stackFramePointer = ctx.m_regs.stackPointer;

	return 0;
}

void asea_debug_message(asSVMRegisters* vm_registers, const char* text) {
	asea_get_engine(vm_registers).WriteMessage("<angelsea_debug>", 0, 0, asMSGTYPE_INFORMATION, text);
}

void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text) {
	asea_get_context(vm_registers).SetInternalException(text);
}

float asea_fmodf(float a, float b) { return fmodf(a, b); }
float asea_fmod(float a, float b) { return fmod(a, b); }
}
