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
#include <as_texts.h>
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

// this is its own function so that we can legally make it inline and tag it as always_inline. otherwise, the compiler
// chooses not to inline asea_prepare_script_stack from the _and_vars variant, which in this case is wasteful and adds
// unnecessary stack overhead
[[gnu::always_inline]]
inline int asea_prepare_script_stack_common(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
) {
	asCContext& ctx = asea_get_context(vm_registers);

	auto& engine      = asea_get_engine(vm_registers);
	auto& callstack   = ctx.m_callStack;
	auto& script_data = *fn.scriptData;

	// update stack size if needed
	asUINT old_length = callstack.GetLength();
	if (old_length >= callstack.GetCapacity()) [[unlikely]] {
		if (engine.ep.maxCallStackSize > 0 && old_length >= engine.ep.maxCallStackSize * CALLSTACK_FRAME_SIZE) {
			// the call stack is too big to grow further
			ctx.SetInternalException(TXT_STACK_OVERFLOW);
			return 1;
		}
		callstack.AllocateNoConstruct(old_length + (10 * CALLSTACK_FRAME_SIZE), true);
	}
	callstack.SetLengthNoAllocate(old_length + CALLSTACK_FRAME_SIZE);

	asPWORD* target = callstack.AddressOf() + old_length;

	// store call state
	target[0] = std::bit_cast<asPWORD>(fp);
	target[1] = std::bit_cast<asPWORD>(ctx.m_currentFunction);
	target[2] = std::bit_cast<asPWORD>(pc);
	target[3] = std::bit_cast<asPWORD>(sp);
	target[4] = ctx.m_stackIndex;

	ctx.m_currentFunction = &fn;

	// pc and fp registers are not manipulated by stack block logic, don't bother storing them.
	// sp is, though, and we need to write it either way as the caller *does* want us to commit sp
	vm_registers->stackPointer = sp;

	angelsea_assert(ctx.m_stackBlocks.GetLength() != 0);

	auto* new_sp = sp;

	if (sp - (script_data.stackNeeded + RESERVE_STACK) < ctx.m_stackBlocks[ctx.m_stackIndex]) [[unlikely]] {
		if (!ctx.ReserveStackSpace(script_data.stackNeeded)) { // may update sp register
			return 1;
		}

		if (vm_registers->stackPointer != sp) {
			int num_dwords = fn.GetSpaceNeededForArguments() + (fn.objectType != nullptr ? AS_PTR_SIZE : 0)
			    + (fn.DoesReturnOnStack() ? AS_PTR_SIZE : 0);
			memcpy(vm_registers->stackPointer, sp, sizeof(asDWORD) * num_dwords);
		}

		new_sp = vm_registers->stackPointer;
	}

	vm_registers->stackPointer -= script_data.variableSpace;
	vm_registers->programPointer    = script_data.byteCode.AddressOf();
	vm_registers->stackFramePointer = new_sp;

	return 0;
}

int asea_prepare_script_stack(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
) {
	return asea_prepare_script_stack_common(vm_registers, fn, pc, sp, fp);
}

int asea_prepare_script_stack_and_vars(
    asSVMRegisters*    vm_registers,
    asCScriptFunction& fn,
    asDWORD*           pc,
    asDWORD*           sp,
    asDWORD*           fp
) {
	if (asea_prepare_script_stack_common(vm_registers, fn, pc, sp, fp) != 0) {
		return 1;
	}

	memset(vm_registers->stackPointer, 0, fn.scriptData->variableSpace * sizeof(asDWORD));

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

void* asea_new_script_object(asCObjectType* obj_type) {
	auto* mem = static_cast<asCScriptObject*>(asea_alloc(obj_type->size));
	ScriptObject_Construct(obj_type, mem);
	return mem;
}

void* asea_alloc(asQWORD size) { return userAlloc(size); }
void  asea_free(void* ptr) { userFree(ptr); }
}
