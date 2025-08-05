// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <angelscript.h>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/bytecodedisasm.hpp>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <angelsea/detail/bytecodetools.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/log.hpp>
#include <angelsea/detail/runtimeheader.hpp>
#include <angelsea/detail/stringutil.hpp>
#include <as_property.h>
#include <as_scriptengine.h>
#include <as_texts.h>
#include <fmt/format.h>

namespace angelsea::detail {

// TODO: fix indent level for all of those that use this...
// TODO: format immediate functions instead of using fmt::format/to_string for it

static constexpr std::string_view save_registers_sequence
    = "\t\tregs->pc = pc;\n"
      "\t\tregs->sp = sp;\n"
      "\t\tregs->fp = fp;\n";

static constexpr std::string_view load_registers_sequence
    = "\t\tpc = regs->pc;\n"
      "\t\tsp = regs->sp;\n"
      "\t\tfp = regs->fp;\n";

template<typename T> static std::string imm_int(T v, VarType type) { return fmt::format("({}){}", type.c, v); }

BytecodeToC::BytecodeToC(const JitConfig& config, asIScriptEngine& engine, std::string c_symbol_prefix) :
    m_config(config), m_script_engine(engine), m_c_symbol_prefix(std::move(c_symbol_prefix)), m_module_idx(-1) {
	m_module_state.buffer.reserve(1024 * 64);
}

void BytecodeToC::prepare_new_context() {
	++m_module_idx;
	m_module_state.fallback_count      = 0;
	m_module_state.string_constant_idx = 0;
	m_module_state.fn_idx              = 0;

	m_module_state.buffer.clear();
	if (m_config.c.copyright_header) {
		m_module_state.buffer += angelsea_c_header_copyright;
	}
	m_module_state.buffer += angelsea_c_header;
}

void BytecodeToC::translate_function(std::string_view internal_module_name, asIScriptFunction& fn) {
	m_module_state.fn_name = create_new_entry_point_name(fn);

	if (m_on_map_function_callback) {
		m_on_map_function_callback(fn, m_module_state.fn_name);
	}

	if (m_config.c.human_readable) {
		const char* section_name;
		int         row, col;
		fn.GetDeclaredAt(&section_name, &row, &col);
		emit(
		    "/* {}:{}:{}: {} */\n",
		    section_name != nullptr ? section_name : "<anon>",
		    row,
		    col,
		    fn.GetDeclaration(true, true, true)
		);
	}

	// JIT entry signature is `void(asSVMRegisters *regs, asPWORD jitArg)`
	emit("void {name}(asSVMRegisters *_regs, asPWORD entryLabel) {{\n", fmt::arg("name", m_module_state.fn_name));

	// HACK: which we would prefer not to do; but accessing value is
	// going to be pain with strict aliasing either way
	emit("\tasea_vm_registers *regs = (asea_vm_registers *)_regs;\n");

	emit(
	    // "#ifdef __MIRC__\n"
	    // "\tasDWORD *l_bc __attribute__((antialias(bc_sp)));\n"
	    // "\tvoid *l_sp __attribute__((antialias(bc_sp)));\n"
	    // "\tvoid *l_fp;\n"
	    // "#else\n"
	    "\tasDWORD *pc;\n"
	    "\tasea_var *sp;\n"
	    "\tasea_var *fp;\n"
	    "{}",
	    // "#endif\n",
	    load_registers_sequence
	);

	// Transpiled functions are compiled to be JIT entry points for the
	// AngelScript VM.
	//
	// The conversion process is relatively simple: There is no deep analysis of
	// bytecode; for each bytecode instruction we emit one block of C code,
	// which is largely similar to the equivalent source code in the AngelScript
	// VM (asCContext::ExecuteNext()).
	// If we can't handle an instruction, we rebuild whatever state we need to
	// return to the VM and we `return;` out of the function. This includes
	// instructions we might not be supporting yet, or that are too complex to
	// implement.
	//
	// A script function may have one equivalent JIT function (the one we are
	// emitting here).
	// To differentiate between JIT entry points, we can assign a non-zero
	// asPWORD to each of them.
	// We handle this by simply assigning each asBC_JitEntry a unique increasing
	// number (we will call this an entry ID). We then simply `switch` on that
	// entry ID (see later) to `goto` to the C handler of a given bytecode
	// instruction.
	//
	// A transpiled function looks like this (simplified, with offsets made up,
	// etc.):
	//
	// void asea_jit_mod1_fn3(asSVMRegisters *regs, asPWORD entryLabel) {
	//     switch (entryLabel)
	//     {
	//     case 1: goto bc0;
	//     case 2: goto bc7;
	//     }
	//
	//     /* bytecode: JitEntry 1 */
	//     bc0: { /* <-- unique C label where the value is the equivalent bytecode
	//     offset */
	//         /* <- no useful handler for jit entries */
	//     }
	//     /* fallthrough to the next instruction */
	//
	//     /* bytecode: [DISASSEMBLED INSTRUCTION] */
	//     bc3: {
	//         blah blah do stuff
	//         /* <-- code handling the instruction */
	//     }
	//     /* fallthrough to the next instruction */
	//
	//     /* bytecode: JitEntry 2 */
	//     bc3: {
	//     }
	//
	//     etc.
	// }

	if (m_config.debug.trace_functions) {
		const char* section;
		int         row, col;
		fn.GetDeclaredAt(&section, &row, &col);
		emit(
		    "\tasea_debug_message(_regs, \"TRACE FUNCTION: module "
		    "{}: {}:{}:{}: {}\");\n\n",
		    internal_module_name,
		    escape_c_literal(section != nullptr ? section : "<anon>"),
		    row,
		    col,
		    escape_c_literal(fn.GetDeclaration(true, true, true))
		);
	}

	FnState state{.fn = fn, .ins = {}, .error_handlers = {}};

	configure_jit_entries(state);
	emit_entry_dispatch(state);

	for (BytecodeInstruction ins : get_bytecode(fn)) {
		state.ins = ins;
		translate_instruction(state);
	}

	emit_error_handlers(state);

	emit("}}\n");
}

std::string BytecodeToC::create_new_entry_point_name(asIScriptFunction& fn) {
	angelsea_assert(fn.GetId() != 0 && "Did not expect a delegate function");

	const auto str = fmt::format("{}_mod{}_fn{}", m_c_symbol_prefix, m_module_idx, m_module_state.fn_idx);
	++m_module_state.fn_idx;
	return str;
}

void BytecodeToC::configure_jit_entries(FnState& state) {
	asPWORD jit_entry_id = 1;

	auto bytecode = get_bytecode(state.fn);
	for (auto it = bytecode.begin(), prev = it; it != bytecode.end(); prev = it, ++it) {
		BytecodeInstruction ins = *it;

		if (ins.info->bc != asBC_JitEntry) {
			continue;
		}

		// always clear pword0 as there may be trash data
		ins.pword0() = 0;

		if (it != bytecode.begin()) {
			// consider skipping some JitEntry we believe the VM should never be hitting. this is useful to avoid
			// pessimizing optimizations, so that the optimizer can merge subsequent basic blocks.
			// TODO: we could also eliminate or comment out the label in many cases once we build in some knowledge of
			// branch targets (including switches, so JMPP), which may avoid emitting many basic blocks to start with

			BytecodeInstruction prev_ins    = *prev;
			bool                should_skip = false;

			// TODO: check what are the conditions for AS to insert jit entries to validate below claims

			// check previous instruction
			switch (prev_ins.info->bc) {
			case asBC_SUSPEND: // TODO: falls back as of writing, remove when fixed
				should_skip = m_config.hack_ignore_suspend;
				break;

				// assume asBC_CALL can always fallback
			case asBC_CALL:
				// TODO: all those fall back conditionally as of writing, remove when fixed
			case asBC_RefCpyV:
			case asBC_REFCPY:
			// TODO: all of those are not implemented as of writing, remove when fixed
			case asBC_SwapPtr:
			case asBC_PshG4:
			case asBC_LdGRdR4:
			case asBC_RET:
			case asBC_COPY:
			case asBC_JMPP:
			case asBC_CALLSYS:
			case asBC_CALLBND:
			case asBC_CALLINTF:
			case asBC_Thiscall1:
			case asBC_CallPtr:
			case asBC_ALLOC:
			case asBC_FREE:
			case asBC_GETREF:
			case asBC_ClrVPtr:
			case asBC_OBJTYPE:
			case asBC_CpyVtoR8:
			case asBC_CpyVtoG4:
			case asBC_CpyGtoV4:
			case asBC_ChkRefS:
			case asBC_ChkNullV:
			case asBC_Cast:
			case asBC_ChkNullS:
			case asBC_ClrHi:
			case asBC_FuncPtr:
			case asBC_LoadVObjR:
			case asBC_AllocMem:
			case asBC_SetListSize:
			case asBC_PshListElmnt:
			case asBC_SetListType:
			case asBC_POWi:
			case asBC_POWu:
			case asBC_POWf:
			case asBC_POWd:
			case asBC_POWdi:
			case asBC_POWi64:
			case asBC_POWu64:       should_skip = false; break;

			// only skip if it's a known instruction as of writing
			default:                should_skip = prev_ins.info->bc <= asBC_Thiscall1;
			}

			// NOTE: this doesn't seem to need to care about branch targets: we normally support basically all branching
			// instructions, and AS should always be emitting a jit entry at _some point_ before

			// NOTE: we shouldn't remove the jitentry after an asBC_CALL because it's not unlikely the callee is going
			// to want to return execution to the VM -- in that case, we always immediately return to the VM

			if (should_skip) {
				continue;
			}
		}

		ins.pword0() = jit_entry_id;

		++jit_entry_id;
	}

	state.has_any_late_jit_entries = jit_entry_id > 2; // because of the increment
}

void BytecodeToC::emit_entry_dispatch(FnState& state) {
	if (!state.has_any_late_jit_entries) {
		if (m_config.c.human_readable) {
			emit("\t/* only one jit entry! not generating dispatch */\n");
		}
		return;
	}

	if (m_config.c.use_gnu_label_as_value) {
		emit(
		    "\tstatic const void *const entry[] = {{\n"
		    "\t\t&&bc0,\n" // because index 0 is meaningless
		);
		for (BytecodeInstruction ins : get_bytecode(state.fn)) {
			if (ins.info->bc == asBC_JitEntry && ins.pword0() != 0) {
				emit("\t\t&&bc{},\n", ins.offset);
			}
		}
		emit(
		    "\t}};\n"
		    "\tgoto *entry[entryLabel];\n\n"
		);
	} else {
		emit("\tswitch(entryLabel) {{\n");
		for (BytecodeInstruction ins : get_bytecode(state.fn)) {
			if (ins.info->bc == asBC_JitEntry && ins.pword0() != 0) {
				emit("\tcase {}: goto bc{};\n", ins.pword0(), ins.offset);
			}
		}
		emit("\t}};\n");
	}
}

void BytecodeToC::emit_error_handlers(FnState& state) {
	if (state.error_handlers.null) {
		emit(
		    "\terr_null:\n"
		    "{SAVE_REGS}"
		    "\t\tasea_set_internal_exception(_regs, \"" TXT_NULL_POINTER_ACCESS
		    "\");\n"
		    "\t\treturn;\n"
		    "\t\n",
		    fmt::arg("SAVE_REGS", save_registers_sequence)
		);
	}

	if (state.error_handlers.divide_by_zero) {
		emit(
		    "\terr_divide_by_zero:\n"
		    "{SAVE_REGS}"
		    "\t\tasea_set_internal_exception(_regs, \"" TXT_DIVIDE_BY_ZERO
		    "\");\n"
		    "\t\treturn;\n"
		    "\t\n",
		    fmt::arg("SAVE_REGS", save_registers_sequence)
		);
	}

	if (state.error_handlers.divide_overflow) {
		emit(
		    "\terr_divide_overflow:\n"
		    "{SAVE_REGS}"
		    "\t\tasea_set_internal_exception(_regs, \"" TXT_DIVIDE_OVERFLOW
		    "\");\n"
		    "\t\treturn;\n"
		    "\t\n",
		    fmt::arg("SAVE_REGS", save_registers_sequence)
		);
	}

	if (state.error_handlers.vm_fallback) {
		emit(
		    "\tvm:\n"
		    "{}"
		    "\t\treturn;\n",
		    save_registers_sequence
		);
	}
}

bool BytecodeToC::is_instruction_blacklisted(asEBCInstr bc) const {
	return std::find(m_config.debug.blacklist_instructions.begin(), m_config.debug.blacklist_instructions.end(), bc)
	    != m_config.debug.blacklist_instructions.end();
}

void BytecodeToC::translate_instruction(FnState& state) {
	using namespace var_types;
	auto& ins = state.ins;
	auto& fn  = state.fn;

	asCScriptEngine& engine = static_cast<asCScriptEngine&>(m_script_engine);

	if (m_config.c.human_readable) {
		emit("\t/* bytecode: {} */\n", disassemble(m_script_engine, ins));
	}

	emit("\tbc{}: {{\n", ins.offset);

	if (is_instruction_blacklisted(ins.info->bc)) {
		emit_vm_fallback(state, "instruction blacklisted by config.debug, force fallback");
		emit("\t}}\n");
		return;
	}

	switch (ins.info->bc) {
	case asBC_JitEntry: emit_auto_bc_inc(state); break;
	case asBC_STR:      emit_vm_fallback(state, "deprecated instruction"); break;

	case asBC_SUSPEND:  {
		if (m_config.hack_ignore_suspend) {
			emit_auto_bc_inc(state);
			break;
		}
		emit_vm_fallback(state, "SUSPEND is not implemented yet");
		break;
	}

	case asBC_TYPEID:
	case asBC_PshC4:   emit_stack_push_ins(state, imm_int(ins.dword0(), u32), u32); break;
	case asBC_VAR:     emit_stack_push_ins(state, imm_int(ins.sword0(), pword), pword); break;
	case asBC_PshC8:   emit_stack_push_ins(state, imm_int(ins.qword0(), u64), u64); break;
	case asBC_PshV4:   emit_stack_push_ins(state, frame_var(ins.sword0(), u32), u32); break;
	case asBC_PshV8:   emit_stack_push_ins(state, frame_var(ins.sword0(), u64), u64); break;
	case asBC_PshNull: emit_stack_push_ins(state, "0", pword); break; // TODO: not tested, how to emit?
	case asBC_PshVPtr: emit_stack_push_ins(state, frame_var(ins.sword0(), pword), pword); break;
	case asBC_PshRPtr: emit_stack_push_ins(state, "regs->value.as_asPWORD", pword); break;
	case asBC_PopRPtr: {
		emit(
		    "\t\tregs->value.as_asPWORD = sp->as_asPWORD;\n"
		    "\t\tsp = (asea_var*)((char*)sp + sizeof(asPWORD));\n"
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_PSF: emit_stack_push_ins(state, fmt::format("(asPWORD){}", frame_ptr(ins.sword0())), pword); break;

	case asBC_PGA: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), false);
		emit_stack_push_ins(state, fmt::format("(asPWORD)&{}", symbol), pword);
		break;
	}

	case asBC_PshGPtr: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), false);
		emit_stack_push_ins(state, fmt::format("(asPWORD){}", symbol), pword);
		break;
	}

	case asBC_PopPtr: {
		emit("\t\tsp = (asea_var*)((char*)sp + sizeof(asPWORD));\n");
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_RDSPtr: {
		emit(
		    "\t\tasPWORD* a = (asPWORD*)sp->as_ptr;\n"
		    "\t\tif (a == 0) {{ goto err_null; }}\n"
		    "\t\tsp->as_asPWORD = *a;\n"
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_CHKREF: {
		emit("\t\tif (sp->as_asPWORD == 0) {{ goto err_null; }}\n");
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	// V1/V2 are equivalent to V4
	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4:    emit_assign_ins(state, frame_var(ins.sword0(), u32), imm_int(ins.dword0(), u32)); break;
	case asBC_SetV8:    emit_assign_ins(state, frame_var(ins.sword0(), u64), imm_int(ins.qword0(), u64)); break;

	case asBC_CpyVtoR4: emit_assign_ins(state, "regs->value.as_asDWORD", frame_var(ins.sword0(), u32)); break;
	case asBC_CpyRtoV4: emit_assign_ins(state, frame_var(ins.sword0(), u32), "regs->value.as_asDWORD"); break;
	case asBC_CpyRtoV8: emit_assign_ins(state, frame_var(ins.sword0(), u64), "regs->value.as_asDWORD"); break;
	case asBC_CpyVtoV4: emit_assign_ins(state, frame_var(ins.sword0(), u32), frame_var(ins.sword1(), u32)); break;
	case asBC_CpyVtoV8: emit_assign_ins(state, frame_var(ins.sword0(), u64), frame_var(ins.sword1(), u64)); break;

	case asBC_LDV:
		emit_assign_ins(state, "regs->value.as_asPWORD", fmt::format("(asPWORD){}", frame_ptr(ins.sword0())));
		break;

	case asBC_SetG4: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), true);
		emit_assign_ins(state, fmt::format("*(asDWORD*)&{}", symbol), fmt::to_string(ins.dword0(AS_PTR_SIZE)));
		break;
	}

	case asBC_LDG: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), true);
		emit("\t\tregs->value.as_asPWORD = (asPWORD)&{};\n", symbol);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_RefCpyV: {
		asCObjectType*    type = reinterpret_cast<asCObjectType*>(ins.pword0());
		asSTypeBehaviour& beh  = type->beh;

		if (!(type->flags & (asOBJ_NOCOUNT | asOBJ_VALUE))) {
			emit_vm_fallback(state, "can't handle release/addref for RefCpyV calls yet");
			break;
		}

		emit("\t\t{DST} = sp->as_asPWORD;\n", fmt::arg("DST", frame_var(ins.sword0(), pword)));
		emit_auto_bc_inc(state);

		break;
	}

	case asBC_REFCPY: {
		asCObjectType*    type = reinterpret_cast<asCObjectType*>(ins.pword0());
		asSTypeBehaviour& beh  = type->beh;

		if (!(type->flags & (asOBJ_NOCOUNT | asOBJ_VALUE))) {
			emit_vm_fallback(state, "can't handle release/addref for RefCpy calls yet");
			break;
		}

		emit(
		    "\t\tasPWORD *dst = (asPWORD*)sp->as_asPWORD;\n"
		    "\t\tsp = (asea_var*)((char*)sp + sizeof(asPWORD));\n"
		    "\t\tasPWORD src = sp->as_asPWORD;\n"
		    "\t\t*dst = src;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);

		break;
	}

	case asBC_LOADOBJ: {
		emit(
		    "\t\tvoid **a = &{VARPTR}->as_ptr;\n"
		    "\t\tregs->obj_type = 0;\n"
		    "\t\tregs->obj = *a;\n"
		    "\t\t*a = 0;\n",
		    fmt::arg("VARPTR", frame_ptr(ins.sword0()))
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_STOREOBJ: {
		emit(
		    "\t\t{VARPTR}->as_ptr = regs->obj;\n"
		    "\t\tregs->obj = 0;\n",
		    fmt::arg("VARPTR", frame_ptr(ins.sword0()))
		);
		emit_auto_bc_inc(state);

		break;
	}

	case asBC_GETOBJ: {
		emit(
		    "\t\tasPWORD *a = &{VAR};\n"
		    "\t\tasPWORD offset = *a;\n"
		    "\t\tasPWORD *v = &{OBJPTR};\n"
		    "\t\t*a = *v;\n"
		    "\t\t*v = 0;\n",
		    fmt::arg("VAR", stack_var(ins.word0(), pword)),
		    fmt::arg("OBJPTR", frame_var("offset", pword))
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_GETOBJREF: {
		emit(
		    "\t\tasPWORD *obj = &{VAR};\n"
		    "\t\t{VAR} = {VARDEREF};\n",
		    fmt::arg("VAR", stack_var(ins.word0(), pword)),
		    fmt::arg("VARDEREF", frame_var("*obj", pword))
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_LoadRObjR: {
		emit(
		    "\t\tasPWORD base = {VAR};\n"
		    "\t\tif (base == 0) {{ goto err_null; }}\n"
		    "\t\tregs->value.as_asPWORD = base + {SWORD1};\n",
		    fmt::arg("VAR", frame_var(ins.sword0(), pword)),
		    fmt::arg("SWORD1", ins.sword1())
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_LoadThisR: {
		emit(
		    "\t\tasPWORD base = {THIS};\n"
		    "\t\tif (base == 0) {{ goto err_null; }}\n"
		    "\t\tregs->value.as_asPWORD = base + {SWORD0};\n",
		    fmt::arg("THIS", frame_var(0, pword)),
		    fmt::arg("SWORD0", ins.sword0())
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_WRTV1: emit_assign_ins(state, "regs->value.as_var_ptr->as_asBYTE", frame_var(ins.sword0(), u8)); break;
	case asBC_WRTV2: emit_assign_ins(state, "regs->value.as_var_ptr->as_asWORD", frame_var(ins.sword0(), u16)); break;
	case asBC_WRTV4: emit_assign_ins(state, "regs->value.as_var_ptr->as_asDWORD", frame_var(ins.sword0(), u32)); break;
	case asBC_WRTV8: emit_assign_ins(state, "regs->value.as_var_ptr->as_asQWORD", frame_var(ins.sword0(), u64)); break;

	case asBC_RDR1:  {
		emit(
		    "\t\tasea_var* var = {VARPTR};\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asBYTE = regs->value.as_var_ptr->as_asBYTE;\n",
		    fmt::arg("VARPTR", frame_ptr(ins.sword0()))
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_RDR2: {
		emit(
		    "\t\tasea_var* var = {VARPTR};\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asWORD = regs->value.as_var_ptr->as_asWORD;\n",
		    fmt::arg("VARPTR", frame_ptr(ins.sword0()))
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_RDR4: emit_assign_ins(state, frame_var(ins.sword0(), u32), "regs->value.as_var_ptr->as_asDWORD"); break;
	case asBC_RDR8: emit_assign_ins(state, frame_var(ins.sword0(), u64), "regs->value.as_var_ptr->as_asQWORD"); break;

	case asBC_CALL: {
		// TODO: when possible, translate this to a JIT to JIT function call
		// NOTE: the above is more complicated now because the MIR_cleanup logic
		// destroys inlining information. then again, it's probably more
		// important to create a shim so that we don't have to go through the
		// regular AS functions since we have better knowledge of the callee.

		int                fn_idx    = ins.int0();
		const std::string  fn_symbol = fmt::format("asea_script_fn{}", fn_idx);
		asCScriptFunction* callee    = engine.scriptFunctions[fn_idx];

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(fn_symbol.c_str(), ExternScriptFunction{fn_idx}, callee);
		}

		if (m_config.experimental_fast_script_call && m_config.hack_ignore_suspend) {
			emit(
			    "\t\textern char {FN};\n"
			    "\t\tpc += 2;\n"
			    "\t\tasea_prepare_script_stack(_regs, (asCScriptFunction*)&{FN}, pc, sp, fp);\n",
			    fmt::arg("FN", fn_symbol)
			);

			for (asUINT n = callee->scriptData->variables.GetLength(); n-- > 0;) {
				asSScriptVariable* var = callee->scriptData->variables[n];

				// don't clear the function arguments
				if (var->stackOffset <= 0) {
					continue;
				}

				if (var->onHeap && (var->type.IsObject() || var->type.IsFuncdef())) {
					if (m_config.c.human_readable) {
						emit("\t\t/* arg {} requires clearing @ stack pos {}*/\n", n, -var->stackOffset);
					}

					emit("\t\t((asea_var*)((asDWORD*)(regs->fp) + {}))->as_asPWORD = 0;\n", -var->stackOffset);
				}
			}

			if (callee->scriptData->variableSpace != 0) {
				if (m_config.c.human_readable) {
					emit("\t\t/* make space for variables */\n");
				}
				emit("\t\tregs->sp = (asDWORD*)(regs->sp) - {};\n", callee->scriptData->variableSpace);
			}

			if (callee == &state.fn) {
				if (m_config.c.human_readable) {
					emit("\t\t/* recursive call */\n");
				}
				emit(
				    "\t\t{SELF}(_regs, 1);\n"
				    "\t\treturn;\n",
				    fmt::arg("SELF", m_module_state.fn_name)
				);
			} else {
				// TODO: immediately branch into jitfn if possible
				emit("\t\treturn;\n");
			}
		} else {
			// Call fallback: We initiate the call from JIT, and the rest of the
			// JitEntry handler will branch into the correct instruction.
			emit(
			    "\t\textern char {FN};\n"
			    "\t\tpc += 2;\n"
			    "{SAVE_REGS}"
			    "\t\tasea_call_script_function(_regs, (asCScriptFunction*)&{FN});\n"
			    "\t\treturn;\n",
			    fmt::arg("FN", fn_symbol),
			    fmt::arg("SAVE_REGS", save_registers_sequence)
			);
		}
		break;
	}

	case asBC_JMP: {
		emit(
		    "\t\tpc += {BRANCH_OFFSET};\n"
		    "\t\tgoto bc{BRANCH_TARGET};\n",
		    fmt::arg("BRANCH_OFFSET", ins.int0() + ins.size),
		    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + ins.size))
		);
		break;
	}

	case asBC_NOT: {
		emit(
		    "\t\tasea_var *var = {VARPTR};\n"
		    "\t\tasDWORD value = var->as_asDWORD;\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asBYTE = !value;\n",
		    fmt::arg("VARPTR", frame_ptr(ins.sword0()))
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_ADDSi: {
		// FIXME: concerning wtf: if we store &ASEA_STACK_TOP.as_asPWORD to a temporary and use it, then we get
		// corruption with -O2 (not if disabling load GVN), again.
		emit(
		    "\t\tif (sp->as_asPWORD == 0) {{ goto err_null; }}\n"
		    "\t\tsp->as_asPWORD += {SWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_IncVi: {
		emit("\t\t++{};\n", frame_var(ins.sword0(), u32));
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_DecVi: {
		emit("\t\t--{};\n", frame_var(ins.sword0(), u32));
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_JZ:     emit_cond_branch_ins(state, "regs->value.as_asINT32 == 0"); break;
	case asBC_JLowZ:  emit_cond_branch_ins(state, "regs->value.as_asBYTE == 0"); break;
	case asBC_JNZ:    emit_cond_branch_ins(state, "regs->value.as_asINT32 != 0"); break;
	case asBC_JLowNZ: emit_cond_branch_ins(state, "regs->value.as_asBYTE != 0"); break;
	case asBC_JS:     emit_cond_branch_ins(state, "regs->value.as_asINT32 < 0"); break;
	case asBC_JNS:    emit_cond_branch_ins(state, "regs->value.as_asINT32 >= 0"); break;
	case asBC_JP:     emit_cond_branch_ins(state, "regs->value.as_asINT32 > 0"); break;
	case asBC_JNP:    emit_cond_branch_ins(state, "regs->value.as_asINT32 <= 0"); break;

	case asBC_TZ:     emit_test_ins(state, "=="); break;
	case asBC_TNZ:    emit_test_ins(state, "!="); break;
	case asBC_TS:     emit_test_ins(state, "<"); break;
	case asBC_TNS:    emit_test_ins(state, ">="); break;
	case asBC_TP:     emit_test_ins(state, ">"); break;
	case asBC_TNP:    emit_test_ins(state, "<="); break;

	case asBC_CMPi:   emit_compare_var_var_ins(state, s32); break;
	case asBC_CMPu:   emit_compare_var_var_ins(state, u32); break;
	case asBC_CMPd:   emit_compare_var_var_ins(state, f64); break;
	case asBC_CMPf:   emit_compare_var_var_ins(state, f32); break;
	case asBC_CMPi64: emit_compare_var_var_ins(state, s64); break;
	case asBC_CMPu64: emit_compare_var_var_ins(state, u64); break;
	case asBC_CmpPtr: emit_compare_var_var_ins(state, pword); break;

	case asBC_CMPIi:  emit_compare_var_expr_ins(state, s32, imm_int(ins.int0(), s32)); break;
	case asBC_CMPIu:  emit_compare_var_expr_ins(state, u32, imm_int(ins.dword0(), u32)); break;
	case asBC_CMPIf:
		emit("\t\tasea_i2f rhs_i2f = {{.i={}}};\n", ins.dword0());
		emit_compare_var_expr_ins(state, f32, "rhs_i2f.f");
		break;

	case asBC_INCi8:  emit_prefixop_valuereg_ins(state, "++", u8); break;
	case asBC_DECi8:  emit_prefixop_valuereg_ins(state, "--", u8); break;
	case asBC_INCi16: emit_prefixop_valuereg_ins(state, "++", u16); break;
	case asBC_DECi16: emit_prefixop_valuereg_ins(state, "--", u16); break;
	case asBC_INCi:   emit_prefixop_valuereg_ins(state, "++", u32); break;
	case asBC_DECi:   emit_prefixop_valuereg_ins(state, "--", u32); break;
	case asBC_INCi64: emit_prefixop_valuereg_ins(state, "++", u64); break;
	case asBC_DECi64: emit_prefixop_valuereg_ins(state, "--", u64); break;
	case asBC_INCf:   emit_prefixop_valuereg_ins(state, "++", f32); break;
	case asBC_DECf:   emit_prefixop_valuereg_ins(state, "--", f32); break;
	case asBC_INCd:   emit_prefixop_valuereg_ins(state, "++", f64); break;
	case asBC_DECd:   emit_prefixop_valuereg_ins(state, "--", f64); break;

	case asBC_NEGi:   emit_unop_var_inplace_ins(state, "-", s32); break;
	case asBC_NEGi64: emit_unop_var_inplace_ins(state, "-", s64); break;
	case asBC_NEGf:   emit_unop_var_inplace_ins(state, "-", f32); break;
	case asBC_NEGd:   emit_unop_var_inplace_ins(state, "-", f64); break;

	case asBC_ADDi:   emit_binop_var_var_ins(state, "+", s32, s32, s32); break;
	case asBC_SUBi:   emit_binop_var_var_ins(state, "-", s32, s32, s32); break;
	case asBC_MULi:   emit_binop_var_var_ins(state, "*", s32, s32, s32); break;
	case asBC_ADDi64: emit_binop_var_var_ins(state, "+", s64, s64, s64); break;
	case asBC_SUBi64: emit_binop_var_var_ins(state, "-", s64, s64, s64); break;
	case asBC_MULi64: emit_binop_var_var_ins(state, "*", s64, s64, s64); break;
	case asBC_ADDf:   emit_binop_var_var_ins(state, "+", f32, f32, f32); break;
	case asBC_SUBf:   emit_binop_var_var_ins(state, "-", f32, f32, f32); break;
	case asBC_MULf:   emit_binop_var_var_ins(state, "*", f32, f32, f32); break;
	case asBC_ADDd:   emit_binop_var_var_ins(state, "+", f64, f64, f64); break;
	case asBC_SUBd:   emit_binop_var_var_ins(state, "-", f64, f64, f64); break;
	case asBC_MULd:   emit_binop_var_var_ins(state, "*", f64, f64, f64); break;

	case asBC_DIVi:   emit_divmod_var_int_ins(state, "/", 0x80000000, s32); break;
	case asBC_MODi:   emit_divmod_var_int_ins(state, "%", 0x80000000, s32); break;
	case asBC_DIVu:   emit_divmod_var_unsigned_ins(state, "/", u32); break;
	case asBC_MODu:   emit_divmod_var_unsigned_ins(state, "%", u32); break;
	case asBC_DIVi64: emit_divmod_var_int_ins(state, "/", asINT64(1) << 63, s64); break;
	case asBC_MODi64: emit_divmod_var_int_ins(state, "%", asINT64(1) << 63, s64); break;
	case asBC_DIVu64: emit_divmod_var_unsigned_ins(state, "/", u64); break;
	case asBC_MODu64: emit_divmod_var_unsigned_ins(state, "%", u64); break;

	case asBC_DIVf:   emit_divmod_var_float_ins(state, "ASEA_FDIV", f32); break;
	case asBC_DIVd:   emit_divmod_var_float_ins(state, "ASEA_FDIV", f64); break;
	case asBC_MODf:   emit_divmod_var_float_ins(state, "ASEA_FMOD32", f32); break;
	case asBC_MODd:   emit_divmod_var_float_ins(state, "ASEA_FMOD64", f64); break;

	case asBC_BNOT64: emit_unop_var_inplace_ins(state, "~", u64); break;
	case asBC_BAND64: emit_binop_var_var_ins(state, "&", u64, u64, u64); break;
	case asBC_BXOR64: emit_binop_var_var_ins(state, "^", u64, u64, u64); break;
	case asBC_BOR64:  emit_binop_var_var_ins(state, "|", u64, u64, u64); break;
	case asBC_BSLL64: emit_binop_var_var_ins(state, "<<", u64, u32, u64); break;
	case asBC_BSRL64: emit_binop_var_var_ins(state, ">>", u64, u32, u64); break;
	case asBC_BSRA64: emit_binop_var_var_ins(state, ">>", s64, u32, s64); break;

	case asBC_BNOT:   emit_unop_var_inplace_ins(state, "~", u32); break;
	case asBC_BAND:   emit_binop_var_var_ins(state, "&", u32, u32, u32); break;
	case asBC_BXOR:   emit_binop_var_var_ins(state, "^", u32, u32, u32); break;
	case asBC_BOR:    emit_binop_var_var_ins(state, "|", u32, u32, u32); break;
	case asBC_BSLL:   emit_binop_var_var_ins(state, "<<", u32, u32, u32); break;
	case asBC_BSRL:   emit_binop_var_var_ins(state, ">>", u32, u32, u32); break;
	case asBC_BSRA:   emit_binop_var_var_ins(state, ">>", s32, u32, s32); break;

	case asBC_iTOf:   emit_primitive_cast_var_ins(state, s32, f32); break;
	case asBC_fTOi:   emit_primitive_cast_var_ins(state, f32, s32); break;
	case asBC_uTOf:   emit_primitive_cast_var_ins(state, u32, f32); break;
	case asBC_fTOu:   emit_primitive_cast_var_ins(state, f32, u32); break;
	case asBC_sbTOi:  emit_primitive_cast_var_ins(state, s8, s32); break;
	case asBC_swTOi:  emit_primitive_cast_var_ins(state, s16, s32); break;
	case asBC_ubTOi:  emit_primitive_cast_var_ins(state, u8, s32); break;
	case asBC_uwTOi:  emit_primitive_cast_var_ins(state, u16, s32); break;
	case asBC_iTOb:   emit_primitive_cast_var_ins(state, u32, s8); break;
	case asBC_iTOw:   emit_primitive_cast_var_ins(state, u32, s16); break;
	case asBC_i64TOi: emit_primitive_cast_var_ins(state, s64, s32); break;
	case asBC_uTOi64: emit_primitive_cast_var_ins(state, u32, s64); break;
	case asBC_iTOi64: emit_primitive_cast_var_ins(state, s32, s64); break;
	case asBC_fTOd:   emit_primitive_cast_var_ins(state, f32, f64); break;
	case asBC_dTOf:   emit_primitive_cast_var_ins(state, f64, f32); break;
	case asBC_fTOi64: emit_primitive_cast_var_ins(state, f32, s64); break;
	case asBC_dTOi64: emit_primitive_cast_var_ins(state, f64, s64); break;
	case asBC_fTOu64: emit_primitive_cast_var_ins(state, f32, u64); break;
	case asBC_dTOu64: emit_primitive_cast_var_ins(state, f64, u64); break;
	case asBC_i64TOf: emit_primitive_cast_var_ins(state, s64, f32); break;
	case asBC_u64TOf: emit_primitive_cast_var_ins(state, u64, f32); break;
	case asBC_i64TOd: emit_primitive_cast_var_ins(state, s64, f64); break;
	case asBC_u64TOd: emit_primitive_cast_var_ins(state, u64, f64); break;
	case asBC_dTOi:   emit_primitive_cast_var_ins(state, f64, s32); break;
	case asBC_dTOu:   emit_primitive_cast_var_ins(state, f64, u32); break;
	case asBC_iTOd:   emit_primitive_cast_var_ins(state, s32, f64); break;
	case asBC_uTOd:   emit_primitive_cast_var_ins(state, u32, f64); break;

	case asBC_ADDIi:  emit_binop_var_imm_ins(state, "+", s32, fmt::to_string(ins.int0(1)), s32); break;
	case asBC_SUBIi:  emit_binop_var_imm_ins(state, "-", s32, fmt::to_string(ins.int0(1)), s32); break;
	case asBC_MULIi:  emit_binop_var_imm_ins(state, "*", s32, fmt::to_string(ins.int0(1)), s32); break;

	case asBC_ADDIf:
		emit("\t\tasea_i2f rhs_i2f = {{.i={}}};\n", ins.dword0(1));
		emit_binop_var_imm_ins(state, "+", f32, "rhs_i2f.f", f32);
		break;
	case asBC_SUBIf:
		emit("\t\tasea_i2f rhs_i2f = {{.i={}}};\n", ins.dword0(1));
		emit_binop_var_imm_ins(state, "-", f32, "rhs_i2f.f", f32);
		break;
	case asBC_MULIf:
		emit("\t\tasea_i2f rhs_i2f = {{.i={}}};\n", ins.dword0(1));
		emit_binop_var_imm_ins(state, "*", f32, "rhs_i2f.f", f32);
		break;

	case asBC_SwapPtr:      // TODO: find way to emit
	case asBC_PshG4:        // TODO: find way to emit
	case asBC_LdGRdR4:      // TODO: find way to emit
	case asBC_RET:          // TODO: implement (probably?)
	case asBC_COPY:         // TODO: find way to emit
	case asBC_JMPP:         // TODO: implement (will need a switch pre-pass)
	case asBC_CALLSYS:      // TODO: implement (calls & syscalls)
	case asBC_CALLBND:      // TODO: find way to emit & implement (calls & syscalls)
	case asBC_CALLINTF:     // TODO: implement (calls & syscalls)
	case asBC_Thiscall1:    // TODO: implement (calls & syscalls) -- can probably just do callsys directly?
	case asBC_CallPtr:      // TODO: find way to emit & implement (calls & syscalls) -- probably just functors
	case asBC_ALLOC:        // TODO: implement
	case asBC_FREE:         // TODO: implement
	case asBC_GETREF:       // TODO: implement
	case asBC_ClrVPtr:      // TODO: find way to emit (maybe asOBJ_SCOPED?)
	case asBC_OBJTYPE:      // TODO: implement (seems used in factories)
	case asBC_CpyVtoR8:     // TODO: find way to emit (probably easy and similar to CpyVtoR4)
	case asBC_CpyVtoG4:     // TODO: find way to emit
	case asBC_CpyGtoV4:     // TODO: implement
	case asBC_ChkRefS:      // TODO: find way to emit
	case asBC_ChkNullV:     // TODO: implement
	case asBC_Cast:         // TODO: find way to emit (well. not the hardest to imagine)
	case asBC_ChkNullS:     // TODO: find way to emit
	case asBC_ClrHi:        // TODO: find way to emit
	case asBC_FuncPtr:      // TODO: find way to emit
	case asBC_LoadVObjR:    // TODO: find way to emit
	case asBC_AllocMem:     // TODO: implement (seems used in list factories)
	case asBC_SetListSize:  // TODO: implement
	case asBC_PshListElmnt: // TODO: implement
	case asBC_SetListType:  // TODO: find way to emit
	case asBC_POWi:         // TODO: write tests and implement
	case asBC_POWu:         // TODO: write tests and implement
	case asBC_POWf:         // TODO: write tests and implement
	case asBC_POWd:         // TODO: write tests and implement
	case asBC_POWdi:        // TODO: write tests and implement
	case asBC_POWi64:       // TODO: write tests and implement
	case asBC_POWu64:       // TODO: write tests and implement
	{
		emit_vm_fallback(state, "unsupported instruction");
		break;
	}

	default: {
		emit_vm_fallback(state, "unknown instruction");
		break;
	}
	}

	if (ins.info->bc == m_config.debug.fallback_after_instruction) {
		emit_vm_fallback(state, "debug.fallback_after_instruction");
	}

	emit("\t}}\n");
}

void BytecodeToC::emit_auto_bc_inc(FnState& state) { emit("\t\tpc += {};\n", state.ins.size); }

void BytecodeToC::emit_vm_fallback(FnState& state, std::string_view reason) {
	++m_module_state.fallback_count;
	state.error_handlers.vm_fallback = true;

	if (m_config.c.human_readable) {
		emit("\t\tgoto vm; /* {} */\n", reason);
	} else {
		emit("\t\tgoto vm;\n");
	}
}

void BytecodeToC::emit_primitive_cast_var_ins(FnState& state, VarType src, VarType dst) {
	BytecodeInstruction& ins      = state.ins;
	const bool           in_place = ins.size == 1;

	if (src.size != dst.size && dst.size < 4) {
		emit(
		    "\t\t{DST_TYPE} value = {SRC};\n"
		    "\t\tasea_var *dst = {DSTPTR};\n"
		    "\t\tdst->as_asDWORD = 0;\n"
		    "\t\tdst->as_{DST_TYPE} = value;\n",
		    fmt::arg("DST_TYPE", dst.c),
		    fmt::arg("DSTPTR", frame_ptr(ins.sword0())),
		    fmt::arg("SRC", frame_var(in_place ? ins.sword0() : ins.sword1(), src))
		);
		emit_auto_bc_inc(state);
		return;
	}
	emit_assign_ins(state, frame_var(ins.sword0(), dst), frame_var(in_place ? ins.sword0() : ins.sword1(), src));
}

std::string BytecodeToC::emit_global_lookup(FnState& state, void** pointer, bool global_var_only) {
	asCScriptEngine& engine = static_cast<asCScriptEngine&>(m_script_engine);

	std::string                            fn_symbol;
	asSMapNode<void*, asCGlobalProperty*>* var_cursor = nullptr;
	if (engine.varAddressMap.MoveTo(&var_cursor, pointer)) {
		asCGlobalProperty* property = engine.varAddressMap.GetValue(var_cursor);
		angelsea_assert(property != nullptr);

		fn_symbol = fmt::format("{}_g{}", m_c_symbol_prefix, property->id);

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(
			    fn_symbol.c_str(),
			    ExternGlobalVariable{.ptr = pointer, .property = property},
			    pointer
			);
		}
	} else {
		// pointer to a string constant (of an arbitrary registered string type)

		// TODO: deduplicate references to identical strings

		angelsea_assert(!global_var_only);

		fn_symbol = fmt::format("{}_mod{}_str{}", m_c_symbol_prefix, m_module_idx, m_module_state.string_constant_idx);

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(fn_symbol.c_str(), ExternStringConstant{*pointer}, pointer);
		}

		++m_module_state.string_constant_idx;
	}

	emit("\t\textern void* {};\n", fn_symbol);
	return fn_symbol;
}

void BytecodeToC::emit_stack_push_ins(FnState& state, std::string_view expr, VarType type) {
	if (type == var_types::pword) {
		emit("\t\tsp = (asea_var*)((char*)sp - sizeof(asPWORD));\n");
	} else {
		emit("\t\tsp = (asea_var*)((char*)sp - {});\n", type.size);
	}

	emit("\t\tsp->as_{TYPE} = {EXPR};\n", fmt::arg("TYPE", type.c), fmt::arg("EXPR", expr));
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_assign_ins(FnState& state, std::string_view dst, std::string_view src) {
	emit("\t\t{DST} = {SRC};\n", fmt::arg("DST", dst), fmt::arg("SRC", src));
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_cond_branch_ins(FnState& state, std::string_view test) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\tif( {TEST} ) {{\n"
	    "\t\t\tpc += {BRANCH_OFFSET};\n"
	    "\t\t\tgoto bc{BRANCH_TARGET};\n"
	    "\t\t}}\n"
	    "\t\tpc += {INSTRUCTION_LENGTH};\n",
	    fmt::arg("TEST", test),
	    fmt::arg("INSTRUCTION_LENGTH", ins.size),
	    fmt::arg("BRANCH_OFFSET", ins.int0() + (long)ins.size),
	    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + (long)ins.size))
	);
}

void BytecodeToC::emit_compare_var_var_ins(FnState& state, VarType type) {
	emit_compare_var_expr_ins(state, type, frame_var(state.ins.sword1(), type));
}

void BytecodeToC::emit_compare_var_expr_ins(FnState& state, VarType type, std::string_view rhs_expr) {
	emit(
	    "\t\t{TYPE} lhs = {LHS};\n"
	    "\t\t{TYPE} rhs = {RHS};\n"
	    "\t\tif      (lhs == rhs) regs->value.as_asINT32 = 0;\n"
	    "\t\telse if (lhs < rhs)  regs->value.as_asINT32 = -1;\n"
	    "\t\telse                 regs->value.as_asINT32 = 1;\n",
	    fmt::arg("LHS", frame_var(state.ins.sword0(), type)),
	    fmt::arg("RHS", rhs_expr),
	    fmt::arg("TYPE", type.c)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_test_ins(FnState& state, std::string_view op_with_rhs_0) {
	emit(
	    "\t\tasINT32 value = regs->value.as_asINT32;\n"
	    "\t\tregs->value.as_asQWORD = 0;\n"
	    "\t\tregs->value.as_asBYTE = (value {OP} 0) ? "
	    "VALUE_OF_BOOLEAN_TRUE : 0;\n",
	    fmt::arg("OP", op_with_rhs_0)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_prefixop_valuereg_ins(FnState& state, std::string_view op, VarType type) {
	emit("\t\t{OP}regs->value.as_var_ptr->as_{TYPE};\n", fmt::arg("OP", op), fmt::arg("TYPE", type.c));
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_unop_var_inplace_ins(FnState& state, std::string_view op, VarType type) {
	BytecodeInstruction& ins = state.ins;
	emit("\t\t{VAR} = {OP} {VAR};\n", fmt::arg("OP", op), fmt::arg("VAR", frame_var(ins.sword0(), type)));
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_binop_var_var_ins(FnState& state, std::string_view op, VarType lhs, VarType rhs, VarType dst) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{DST} = {LHS} {OP} {RHS};\n",
	    fmt::arg("OP", op),
	    fmt::arg("DST", frame_var(ins.sword0(), dst)),
	    fmt::arg("LHS", frame_var(ins.sword1(), lhs)),
	    fmt::arg("RHS", frame_var(ins.sword2(), rhs))
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_binop_var_imm_ins(
    FnState&         state,
    std::string_view op,
    VarType          lhs,
    std::string_view rhs_expr,
    VarType          dst
) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{DST} = {LHS} {OP} {RHS};\n",
	    fmt::arg("OP", op),
	    fmt::arg("DST", frame_var(ins.sword0(), dst)),
	    fmt::arg("LHS", frame_var(ins.sword1(), lhs)),
	    fmt::arg("RHS", rhs_expr)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_divmod_var_float_ins(FnState& state, std::string_view op, VarType type) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{TYPE} lhs = {LHS};\n"
	    "\t\t{TYPE} divider = {RHS};\n"
	    "\t\tif (divider == 0) {{ goto err_divide_by_zero; }}\n"
	    "\t\t{DST} = {OP}(lhs, divider);\n",
	    fmt::arg("TYPE", type.c),
	    fmt::arg("DST", frame_var(ins.sword0(), type)),
	    fmt::arg("LHS", frame_var(ins.sword1(), type)),
	    fmt::arg("RHS", frame_var(ins.sword2(), type)),
	    fmt::arg("OP", op)
	);
	state.error_handlers.divide_by_zero = true;
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_divmod_var_int_ins(
    FnState&         state,
    std::string_view op,
    std::uint64_t    lhs_overflow_value,
    VarType          type
) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{TYPE} lhs = {LHS};\n"
	    "\t\t{TYPE} divider = {RHS};\n"
	    "\t\tif (divider == 0) {{ goto err_divide_by_zero; }}\n"
	    "\t\tif (divider == -1 && lhs == ({TYPE}){LHS_OVERFLOW}) {{ goto err_divide_overflow; }}\n"
	    "\t\t{DST} = lhs {OP} divider;\n",
	    fmt::arg("TYPE", type.c),
	    fmt::arg("DST", frame_var(ins.sword0(), type)),
	    fmt::arg("LHS", frame_var(ins.sword1(), type)),
	    fmt::arg("RHS", frame_var(ins.sword2(), type)),
	    fmt::arg("SWORD2", ins.sword2()),
	    fmt::arg("OP", op),
	    fmt::arg("LHS_OVERFLOW", lhs_overflow_value)
	);
	state.error_handlers.divide_by_zero  = true;
	state.error_handlers.divide_overflow = true;
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_divmod_var_unsigned_ins(FnState& state, std::string_view op, VarType type) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{TYPE} divider = {RHS};\n"
	    "\t\tif (divider == 0) {{ goto err_divide_by_zero; }}\n"
	    "\t\t{DST} = {LHS} {OP} divider;\n",
	    fmt::arg("TYPE", type.c),
	    fmt::arg("DST", frame_var(ins.sword0(), type)),
	    fmt::arg("LHS", frame_var(ins.sword1(), type)),
	    fmt::arg("RHS", frame_var(ins.sword2(), type)),
	    fmt::arg("OP", op),
	    fmt::arg("SAVE_REGS", save_registers_sequence)
	);
	state.error_handlers.divide_by_zero = true;
	emit_auto_bc_inc(state);
}

std::string BytecodeToC::frame_ptr(std::string_view expr) {
	return fmt::format("((asea_var*)((asDWORD*)fp - {}))", expr);
}

std::string BytecodeToC::frame_ptr(int offset) {
	if (offset == 0) {
		return "fp";
	}
	return frame_ptr(std::to_string(offset));
}

std::string BytecodeToC::frame_var(std::string_view expr, VarType type) {
	return fmt::format("((asea_var*)((asDWORD*)fp - {}))->as_{}", expr, type.c);
}

std::string BytecodeToC::frame_var(int offset, VarType type) {
	if (offset == 0) {
		return fmt::format("fp->as_{}", type.c);
	}
	return frame_var(std::to_string(offset), type);
}

std::string BytecodeToC::stack_var(int offset, VarType type) {
	return fmt::format("((asea_var*)((asDWORD*)sp + {}))->as_{}", fmt::to_string(offset), type.c);
}

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset) {
	return std::size_t(std::int64_t(base_offset) + std::int64_t(relative_offset));
}

} // namespace angelsea::detail