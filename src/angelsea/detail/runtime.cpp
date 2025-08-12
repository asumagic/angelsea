// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/runtime.hpp>

#include <angelscript.h>
#include <angelsea/detail/debug.hpp>
#include <as_context.h>
#include <as_memory.h>
#include <as_objecttype.h>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>
#include <as_scriptobject.h>
#include <bit>
#include <fmt/core.h>

static asCContext&      asea_get_context(asSVMRegisters* regs) { return static_cast<asCContext&>(*regs->ctx); }
static asCScriptEngine& asea_get_engine(asSVMRegisters* regs) {
	return static_cast<asCScriptEngine&>(*asea_get_context(regs).GetEngine());
}

extern "C" {
void asea_call_script_function(asSVMRegisters* vm_registers, asCScriptFunction& fn) {
	asea_get_context(vm_registers).CallScriptFunction(&fn);
}

int asea_call_system_function(asSVMRegisters* vm_registers, int fn) {
	return CallSystemFunction(fn, &asea_get_context(vm_registers));
}

void asea_call_object_method(asSVMRegisters* vm_registers, void* obj, int fn) {
	asea_get_engine(vm_registers).CallObjectMethod(obj, fn);
}

int asea_prepare_script_stack(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
) {
	asCContext& ctx              = asea_get_context(vm_registers);
	ctx.m_regs.programPointer    = pc;
	ctx.m_regs.stackPointer      = sp;
	ctx.m_regs.stackFramePointer = fp;

	if (ctx.PushCallState() < 0) {
		return 1;
	}

	ctx.m_currentFunction = &fn;

	const asUINT needSize = fn.scriptData->stackNeeded;

	// TODO: move this to native codegen?
	// With a quick check we know right away that we don't need to call ReserveStackSpace and do other checks inside it
	if (ctx.m_stackBlocks.GetLength() == 0 || sp - (needSize + RESERVE_STACK) < ctx.m_stackBlocks[ctx.m_stackIndex]) {
		if (!ctx.ReserveStackSpace(needSize)) {
			return 1;
		}

		if (ctx.m_regs.stackPointer != sp) {
			int numDwords = fn.GetSpaceNeededForArguments() + (fn.objectType != nullptr ? AS_PTR_SIZE : 0)
			    + (fn.DoesReturnOnStack() ? AS_PTR_SIZE : 0);
			memcpy(ctx.m_regs.stackPointer, sp, sizeof(asDWORD) * numDwords);
		}
	}

	ctx.m_regs.programPointer    = fn.scriptData->byteCode.AddressOf();
	ctx.m_regs.stackFramePointer = ctx.m_regs.stackPointer;

	return 0;
}

void asea_debug_message(asSVMRegisters* vm_registers, const char* text) {
	asea_get_engine(vm_registers).WriteMessage("<angelsea_debug>", 0, 0, asMSGTYPE_INFORMATION, text);
}

void asea_debug_int(asSVMRegisters* vm_registers, asPWORD x) {
	asea_get_engine(vm_registers)
	    .WriteMessage(
	        "<angelsea_debug>",
	        0,
	        0,
	        asMSGTYPE_INFORMATION,
	        fmt::format("0x{:0>16x} / {} / '{}'", x, x, char(x)).c_str()
	    );
}

void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text) {
	asea_get_context(vm_registers).SetInternalException(text);
}

void asea_clean_args(asSVMRegisters* vm_registers, asCScriptFunction& fn, asDWORD* args) {
	asCScriptEngine& engine = asea_get_engine(vm_registers);

	auto& clean_args = fn.sysFuncIntf->cleanArgs;
	for (std::size_t i = 0; i < clean_args.GetLength(); ++i) {
		void** addr = std::bit_cast<void**>(&args[clean_args[i].off]);
		if (clean_args[i].op == 0) {
			if (*addr != nullptr) {
				engine.CallObjectMethod(*addr, clean_args[i].ot->beh.release);
				*addr = nullptr;
			}
		} else {
			if (clean_args[i].op == 2) {
				engine.CallObjectMethod(*addr, clean_args[i].ot->beh.destruct);
			}

			engine.CallFree(*addr);
		}
	}
}

void asea_cast(asSVMRegisters* vm_registers, asCScriptObject* obj, asDWORD type_id) {
	asCScriptEngine& engine = asea_get_engine(vm_registers);

	asCObjectType& type = *obj->objType;
	asCObjectType& to   = *engine.GetObjectTypeFromTypeId(type_id);

	if (type.Implements(&to) || type.DerivesFrom(&to)) {
		vm_registers->objectType     = nullptr;
		vm_registers->objectRegister = obj;
		obj->AddRef();
	}
}

void* asea_alloc(asQWORD size) { return userAlloc(size); }
void  asea_free(void* ptr) { userFree(ptr); }
}
