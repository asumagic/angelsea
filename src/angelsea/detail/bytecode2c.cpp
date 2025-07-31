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
#include <fmt/format.h>

namespace angelsea::detail {

BytecodeToC::BytecodeToC(const JitConfig& config, asIScriptEngine& engine, std::string jit_fn_prefix) :
    m_config(config), m_script_engine(engine), m_jit_fn_prefix(std::move(jit_fn_prefix)) {
	m_state.buffer.reserve(1024 * 64);
}

void BytecodeToC::prepare_new_context() {
	m_state.fallback_count      = 0;
	m_state.string_constant_idx = 0;

	m_state.buffer.clear();
	m_state.buffer += angelsea_c_header;
}

void BytecodeToC::translate_module(
    std::string_view              internal_module_name,
    asIScriptModule*              script_module,
    std::span<asIScriptFunction*> functions
) {
	// NOTE: module name and section names are separate concepts, and there may
	// be several script sections in a module
	emit(
	    R"___(
/* MODULE: {module_name} */ 
)___",
	    fmt::arg("module_name", internal_module_name)
	);

	for (asIScriptFunction* fn : functions) {
		translate_function(internal_module_name, *fn);
	}
}

void BytecodeToC::translate_function(std::string_view internal_module_name, asIScriptFunction& fn) {
	const auto func_name = entry_point_name(fn);
	if (m_on_map_function_callback) {
		m_on_map_function_callback(fn, func_name);
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
	emit("void {name}(asSVMRegisters *_regs, asPWORD entryLabel) {{\n", fmt::arg("name", func_name));

	// HACK: which we would prefer not to do; but accessing valueRegister is
	// going to be pain with strict aliasing either way
	emit("\tasea_vm_registers *regs = (asea_vm_registers *)_regs;\n");

	// We define l_sp/l_fp as void* instead of asea_stack_var* to avoid doing
	// plain arithmetic over it directly
	emit(
	    // "#ifdef __MIRC__\n"
	    // "\tasDWORD *l_bc __attribute__((antialias(bc_sp)));\n"
	    // "\tvoid *l_sp __attribute__((antialias(bc_sp)));\n"
	    // "\tvoid *l_fp;\n"
	    // "#else\n"
	    "\tasDWORD *l_bc;\n"
	    "\tvoid *l_sp;\n"
	    "\tvoid *l_fp;\n"
	    // "#endif\n"
	);
	emit_load_vm_registers();

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
		    "\tasea_debug_message((asSVMRegisters*)regs, \"TRACE FUNCTION: module "
		    "{}: {}:{}:{}: {}\");\n\n",
		    internal_module_name,
		    escape_c_literal(section != nullptr ? section : "<anon>"),
		    row,
		    col,
		    escape_c_literal(fn.GetDeclaration(true, true, true))
		);
	}

	emit_entry_dispatch(fn);

	FunctionTranslationState state;
	for (BytecodeInstruction ins : get_bytecode(fn)) {
		translate_instruction(fn, ins, state);
	}

	emit("}}\n");
}

std::string BytecodeToC::entry_point_name(asIScriptFunction& fn) const {
	angelsea_assert(fn.GetId() != 0 && "Did not expect a delegate function");

	std::string mangled_module = "anon";

	if (const char* module_name = fn.GetModuleName(); module_name != nullptr) {
		mangled_module = "module_";
		for (char c : std::string_view{module_name}) {
			if (is_alpha_numerical(c)) {
				mangled_module += c;
			} else {
				mangled_module += fmt::format("_{:02X}_", c);
			}
		}
	}

	return fmt::format("{}{}_{}", m_jit_fn_prefix, fn.GetId(), mangled_module);
}

void BytecodeToC::emit_entry_dispatch(asIScriptFunction& fn) {
	emit(
	    "\tswitch(entryLabel) {{\n"
	    "\tdefault:\n"
	);

	bool    last_was_jit_entry = false;
	asPWORD jit_entry_id       = 1;

	for (BytecodeInstruction ins : get_bytecode(fn)) {
		if (ins.info->bc != asBC_JitEntry) {
			last_was_jit_entry = false;
			continue; // skip to the next
		}

		if (last_was_jit_entry) {
			// ignore successive JIT entries
			continue;
		}

		ins.pword0() = jit_entry_id;

		emit("\tcase {}: goto bc{};\n", jit_entry_id, ins.offset);
		last_was_jit_entry = true;

		++jit_entry_id;
	}

	emit("\t}}\n\n");
}

bool BytecodeToC::is_instruction_blacklisted(asEBCInstr bc) const {
	return std::find(m_config.debug.blacklist_instructions.begin(), m_config.debug.blacklist_instructions.end(), bc)
	    != m_config.debug.blacklist_instructions.end();
}

void BytecodeToC::translate_instruction(
    asIScriptFunction&        fn,
    BytecodeInstruction       ins,
    FunctionTranslationState& state
) {
	using namespace var_types;

	asCScriptEngine& engine = static_cast<asCScriptEngine&>(m_script_engine);

	if (m_config.c.human_readable) {
		emit("\t/* bytecode: {} */\n", disassemble(m_script_engine, ins));
	}

	emit("\tbc{}: {{\n", ins.offset);

	if (is_instruction_blacklisted(ins.info->bc)) {
		emit_vm_fallback(fn, "instruction blacklisted by config.debug, force fallback");
		emit("\t}}\n");
		return;
	}

	switch (ins.info->bc) {
	case asBC_JitEntry: {
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_STR: {
		emit_vm_fallback(fn, "deprecated instruction");
		break;
	}

	case asBC_SUSPEND: {
		log(m_config,
		    m_script_engine,
		    fn,
		    LogSeverity::PERF_WARNING,
		    "asBC_SUSPEND found; this will fallback to the VM and be slow!");
		emit_vm_fallback(fn, "SUSPEND is not implemented yet");
		break;
	}

	case asBC_PshC4: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -1);\n"
		    "\t\tASEA_STACK_TOP.as_asDWORD = {DWORD0};\n",
		    fmt::arg("DWORD0", ins.dword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}
	case asBC_PshC8: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -2);\n"
		    "\t\tASEA_STACK_TOP.as_asQWORD = {QWORD0};\n",
		    fmt::arg("QWORD0", ins.qword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_PshV4: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -1);\n"
		    "\t\tASEA_STACK_TOP.as_asDWORD = ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}
	case asBC_PshV8: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -2);\n"
		    "\t\tASEA_STACK_TOP.as_asQWORD = ASEA_FRAME_VAR({SWORD0}).as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}
	case asBC_PshVPtr: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = ASEA_FRAME_VAR({SWORD0}).as_asPWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	// V1/V2 are equivalent to V4
	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = (asDWORD){DWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("DWORD0", ins.dword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_SetV8: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asQWORD = (asQWORD){QWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("QWORD0", ins.qword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_CpyVtoR4: {
		emit(
		    "\t\tregs->valueRegister.as_asDWORD = "
		    "ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_CpyRtoV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = "
		    "regs->valueRegister.as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_CpyVtoV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = "
		    "ASEA_FRAME_VAR({SWORD1}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_CpyVtoV8: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asQWORD = "
		    "ASEA_FRAME_VAR({SWORD1}).as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_LDV: {
		emit(
		    "\t\tregs->valueRegister.as_asPWORD = "
		    "(asPWORD)&ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_PSF: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD)&ASEA_FRAME_VAR({SWORD0});\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_PGA: {
		std::string fn_symbol = emit_global_lookup(fn, reinterpret_cast<void**>(ins.pword0()), false);
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD)&{OBJ};\n",
		    fmt::arg("OBJ", fn_symbol)
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_PshGPtr: {
		std::string fn_symbol = emit_global_lookup(fn, reinterpret_cast<void**>(ins.pword0()), false);
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD){OBJ};\n",
		    fmt::arg("OBJ", fn_symbol)
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_PopPtr: {
		emit("\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, AS_PTR_SIZE);\n");
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_VAR: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD){SWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_GETOBJREF: {
		emit(
		    "\t\tasPWORD *dst = &ASEA_STACK_VAR({WORD0}).as_asPWORD;\n"
		    "\t\tasPWORD var_idx = *dst;\n"
		    "\t\tasPWORD var_addr = ASEA_FRAME_VAR(var_idx).as_asPWORD;\n"
		    "\t\tASEA_STACK_VAR({WORD0}).as_asPWORD = var_addr;\n",
		    fmt::arg("WORD0", ins.word0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_RefCpyV: {
		asCObjectType*    type = reinterpret_cast<asCObjectType*>(ins.pword0());
		asSTypeBehaviour& beh  = type->beh;

		if (!(type->flags & (asOBJ_NOCOUNT | asOBJ_VALUE))) {
			emit_vm_fallback(fn, "can't handle release/addref for RefCpyV calls yet");
			break;
		}

		emit(
		    "\t\tasPWORD *dst = &ASEA_FRAME_VAR({SWORD0}).as_asPWORD;\n"
		    "\t\tasPWORD src = ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\t*dst = src;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);

		break;
	}

	case asBC_REFCPY: {
		asCObjectType*    type = reinterpret_cast<asCObjectType*>(ins.pword0());
		asSTypeBehaviour& beh  = type->beh;

		if (!(type->flags & (asOBJ_NOCOUNT | asOBJ_VALUE))) {
			emit_vm_fallback(fn, "can't handle release/addref for RefCpy calls yet");
			break;
		}

		emit(
		    "\t\tasPWORD *dst = (asPWORD*)ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, AS_PTR_SIZE);\n"
		    "\t\tasPWORD src = ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\t*dst = src;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);

		break;
	}

	case asBC_RDR1: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asBYTE = ASEA_VALUEREG_DEREF().as_asBYTE;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}
	case asBC_RDR2: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asWORD = ASEA_VALUEREG_DEREF().as_asWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}
	case asBC_RDR4: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = ASEA_VALUEREG_DEREF().as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}
	case asBC_RDR8: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asQWORD = ASEA_VALUEREG_DEREF().as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_CALL: {
		// TODO: when possible, translate this to a JIT to JIT function call

		int                fn_idx    = ins.int0();
		const std::string  fn_symbol = fmt::format("asea_script_fn{}", fn_idx);
		asCScriptFunction* function  = engine.scriptFunctions[fn_idx];

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(fn_symbol.c_str(), ExternScriptFunction{fn_idx}, function);
		}

		// Call fallback: We initiate the call from JIT, and the rest of the
		// JitEntry handler will branch into the correct instruction.
		emit(
		    "\t\textern char {FN};\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("FN", fn_symbol)
		);
		emit_save_vm_registers();
		emit(
		    "\t\tasea_call_script_function(regs, (asCScriptFunction*)&{FN});\n"
		    "\t\treturn;\n",
		    fmt::arg("FN", fn_symbol),
		    fmt::arg("FN_ID", fn_idx)
		);
		break;
	}

	case asBC_CMPIi: {
		emit(
		    "\t\tint i1 = ASEA_FRAME_VAR({SWORD0}).as_asINT32;\n"
		    "\t\tint i2 = {INT0};\n"
		    "\t\tif( i1 == i2 )     regs->valueRegister.as_asINT64 = 0;\n"
		    "\t\telse if( i1 < i2 ) regs->valueRegister.as_asINT64 = -1;\n"
		    "\t\telse               regs->valueRegister.as_asINT64 = 1;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("INT0", ins.int0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_JMP: {
		emit(
		    "\t\tl_bc += {BRANCH_OFFSET};\n"
		    "\t\tgoto bc{BRANCH_TARGET};\n",
		    fmt::arg("BRANCH_OFFSET", ins.int0() + ins.size),
		    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + ins.size))
		);
		break;
	}

	case asBC_NOT: {
		emit(
		    "\t\tasea_var *var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tasDWORD value = var->as_asDWORD;\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asBYTE = !value;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(ins);
		break;
	}

	case asBC_JZ:           emit_cond_branch_ins(ins, "regs->valueRegister.as_asINT64 == 0"); break;
	case asBC_JLowZ:        emit_cond_branch_ins(ins, "regs->valueRegister.as_asBYTE == 0"); break;
	case asBC_JNZ:          emit_cond_branch_ins(ins, "regs->valueRegister.as_asINT64 != 0"); break;
	case asBC_JLowNZ:       emit_cond_branch_ins(ins, "regs->valueRegister.as_asBYTE != 0"); break;
	case asBC_JS:           emit_cond_branch_ins(ins, "regs->valueRegister.as_asINT64 < 0"); break;
	case asBC_JNS:          emit_cond_branch_ins(ins, "regs->valueRegister.as_asINT64 >= 0"); break;
	case asBC_JP:           emit_cond_branch_ins(ins, "regs->valueRegister.as_asINT64 > 0"); break;
	case asBC_JNP:          emit_cond_branch_ins(ins, "regs->valueRegister.as_asINT64 <= 0"); break;

	case asBC_TZ:           emit_test_ins(ins, "=="); break;
	case asBC_TNZ:          emit_test_ins(ins, "!="); break;
	case asBC_TS:           emit_test_ins(ins, "<"); break;
	case asBC_TNS:          emit_test_ins(ins, ">="); break;
	case asBC_TP:           emit_test_ins(ins, ">"); break;
	case asBC_TNP:          emit_test_ins(ins, "<"); break;

	case asBC_INCi8:        emit_prefixop_valuereg_ins(ins, "++", u8); break;
	case asBC_DECi8:        emit_prefixop_valuereg_ins(ins, "--", u8); break;
	case asBC_INCi16:       emit_prefixop_valuereg_ins(ins, "++", u16); break;
	case asBC_DECi16:       emit_prefixop_valuereg_ins(ins, "--", u16); break;
	case asBC_INCi:         emit_prefixop_valuereg_ins(ins, "++", u32); break;
	case asBC_DECi:         emit_prefixop_valuereg_ins(ins, "--", u32); break;
	case asBC_INCi64:       emit_prefixop_valuereg_ins(ins, "++", u64); break;
	case asBC_DECi64:       emit_prefixop_valuereg_ins(ins, "--", u64); break;
	case asBC_INCf:         emit_prefixop_valuereg_ins(ins, "++", f32); break;
	case asBC_DECf:         emit_prefixop_valuereg_ins(ins, "--", f32); break;
	case asBC_INCd:         emit_prefixop_valuereg_ins(ins, "++", f64); break;
	case asBC_DECd:         emit_prefixop_valuereg_ins(ins, "--", f64); break;

	case asBC_NEGi:         emit_unop_var_inplace_ins(ins, "-", s32); break;
	case asBC_NEGi64:       emit_unop_var_inplace_ins(ins, "-", s64); break;
	case asBC_NEGf:         emit_unop_var_inplace_ins(ins, "-", f32); break;
	case asBC_NEGd:         emit_unop_var_inplace_ins(ins, "-", f64); break;

	case asBC_ADDi:         emit_binop_var_var_ins(ins, "+", s32, s32, s32); break;
	case asBC_SUBi:         emit_binop_var_var_ins(ins, "-", s32, s32, s32); break;
	case asBC_MULi:         emit_binop_var_var_ins(ins, "*", s32, s32, s32); break;
	case asBC_ADDi64:       emit_binop_var_var_ins(ins, "+", s64, s64, s64); break;
	case asBC_SUBi64:       emit_binop_var_var_ins(ins, "-", s64, s64, s64); break;
	case asBC_MULi64:       emit_binop_var_var_ins(ins, "*", s64, s64, s64); break;
	case asBC_ADDf:         emit_binop_var_var_ins(ins, "+", f32, f32, f32); break;
	case asBC_SUBf:         emit_binop_var_var_ins(ins, "-", f32, f32, f32); break;
	case asBC_MULf:         emit_binop_var_var_ins(ins, "*", f32, f32, f32); break;
	case asBC_ADDd:         emit_binop_var_var_ins(ins, "+", f64, f64, f64); break;
	case asBC_SUBd:         emit_binop_var_var_ins(ins, "-", f64, f64, f64); break;
	case asBC_MULd:         emit_binop_var_var_ins(ins, "*", f64, f64, f64); break;

	case asBC_BNOT64:       emit_unop_var_inplace_ins(ins, "~", u64); break;
	case asBC_BAND64:       emit_binop_var_var_ins(ins, "&", u64, u64, u64); break;
	case asBC_BXOR64:       emit_binop_var_var_ins(ins, "^", u64, u64, u64); break;
	case asBC_BOR64:        emit_binop_var_var_ins(ins, "|", u64, u64, u64); break;
	case asBC_BSLL64:       emit_binop_var_var_ins(ins, "<<", u64, u32, u64); break;
	case asBC_BSRL64:       emit_binop_var_var_ins(ins, ">>", u64, u32, u64); break;
	case asBC_BSRA64:       emit_binop_var_var_ins(ins, ">>", s64, u32, s64); break;

	case asBC_BNOT:         emit_unop_var_inplace_ins(ins, "~", u32); break;
	case asBC_BAND:         emit_binop_var_var_ins(ins, "&", u32, u32, u32); break;
	case asBC_BXOR:         emit_binop_var_var_ins(ins, "^", u32, u32, u32); break;
	case asBC_BOR:          emit_binop_var_var_ins(ins, "|", u32, u32, u32); break;
	case asBC_BSLL:         emit_binop_var_var_ins(ins, "<<", u32, u32, u32); break;
	case asBC_BSRL:         emit_binop_var_var_ins(ins, ">>", u32, u32, u32); break;
	case asBC_BSRA:         emit_binop_var_var_ins(ins, ">>", s32, u32, s32); break;

	case asBC_ADDIi:        emit_binop_var_imm_ins(ins, "+", s32, fmt::to_string(ins.int0(1)), s32); break;
	case asBC_SUBIi:        emit_binop_var_imm_ins(ins, "-", s32, fmt::to_string(ins.int0(1)), s32); break;
	case asBC_MULIi:        emit_binop_var_imm_ins(ins, "*", s32, fmt::to_string(ins.int0(1)), s32); break;

	case asBC_iTOf:         emit_primitive_cast_var_ins(ins, s32, f32, true); break;
	case asBC_fTOi:         emit_primitive_cast_var_ins(ins, f32, s32, true); break;
	case asBC_uTOf:         emit_primitive_cast_var_ins(ins, u32, f32, true); break;
	case asBC_fTOu:         emit_primitive_cast_var_ins(ins, f32, u32, true); break;
	case asBC_sbTOi:        emit_primitive_cast_var_ins(ins, s8, s32, true); break;
	case asBC_swTOi:        emit_primitive_cast_var_ins(ins, s16, s32, true); break;
	case asBC_ubTOi:        emit_primitive_cast_var_ins(ins, u8, s32, true); break;
	case asBC_uwTOi:        emit_primitive_cast_var_ins(ins, u16, s32, true); break;
	case asBC_iTOb:         emit_primitive_cast_var_ins(ins, u32, s8, true); break;
	case asBC_iTOw:         emit_primitive_cast_var_ins(ins, u32, s16, true); break;
	case asBC_i64TOi:       emit_primitive_cast_var_ins(ins, s64, s32, false); break;
	case asBC_uTOi64:       emit_primitive_cast_var_ins(ins, u32, s64, false); break;
	case asBC_iTOi64:       emit_primitive_cast_var_ins(ins, s32, s64, false); break;
	case asBC_fTOd:         emit_primitive_cast_var_ins(ins, f32, f64, false); break;
	case asBC_dTOf:         emit_primitive_cast_var_ins(ins, f64, f32, false); break;
	case asBC_fTOi64:       emit_primitive_cast_var_ins(ins, f32, s64, false); break;
	case asBC_dTOi64:       emit_primitive_cast_var_ins(ins, f64, s64, true); break;
	case asBC_fTOu64:       emit_primitive_cast_var_ins(ins, f32, u64, false); break;
	case asBC_dTOu64:       emit_primitive_cast_var_ins(ins, f64, u64, true); break;
	case asBC_i64TOf:       emit_primitive_cast_var_ins(ins, s64, f32, false); break;
	case asBC_u64TOf:       emit_primitive_cast_var_ins(ins, u64, f32, false); break;
	case asBC_i64TOd:       emit_primitive_cast_var_ins(ins, s64, f64, true); break;
	case asBC_u64TOd:       emit_primitive_cast_var_ins(ins, u64, f64, true); break;
	case asBC_dTOi:         emit_primitive_cast_var_ins(ins, f64, s32, false); break;
	case asBC_dTOu:         emit_primitive_cast_var_ins(ins, f64, u32, false); break;
	case asBC_iTOd:         emit_primitive_cast_var_ins(ins, s32, f64, false); break;
	case asBC_uTOd:         emit_primitive_cast_var_ins(ins, u32, f64, false); break;

	case asBC_SwapPtr:
	case asBC_PshG4:
	case asBC_LdGRdR4:
	case asBC_RET:
	case asBC_IncVi:
	case asBC_DecVi:
	case asBC_COPY:
	case asBC_RDSPtr:
	case asBC_CMPd:
	case asBC_CMPu:
	case asBC_CMPf:
	case asBC_CMPi:
	case asBC_CMPIf:
	case asBC_CMPIu:
	case asBC_JMPP:
	case asBC_PopRPtr:
	case asBC_PshRPtr:
	case asBC_CALLSYS:
	case asBC_CALLBND:
	case asBC_ALLOC:
	case asBC_FREE:
	case asBC_LOADOBJ:
	case asBC_STOREOBJ:
	case asBC_GETOBJ:
	case asBC_CHKREF:
	case asBC_GETREF:
	case asBC_PshNull:
	case asBC_ClrVPtr:
	case asBC_OBJTYPE:
	case asBC_TYPEID:
	case asBC_ADDSi:
	case asBC_CpyVtoR8:
	case asBC_CpyVtoG4:
	case asBC_CpyRtoV8:
	case asBC_CpyGtoV4:
	case asBC_WRTV1:
	case asBC_WRTV2:
	case asBC_WRTV4:
	case asBC_WRTV8:
	case asBC_LDG:
	case asBC_CmpPtr:
	case asBC_DIVi:
	case asBC_MODi:
	case asBC_DIVf:
	case asBC_MODf:
	case asBC_DIVd:
	case asBC_MODd:
	case asBC_ADDIf:
	case asBC_SUBIf:
	case asBC_MULIf:
	case asBC_SetG4:
	case asBC_ChkRefS:
	case asBC_ChkNullV:
	case asBC_CALLINTF:
	case asBC_Cast:
	case asBC_DIVi64:
	case asBC_MODi64:
	case asBC_CMPi64:
	case asBC_CMPu64:
	case asBC_ChkNullS:
	case asBC_ClrHi:
	case asBC_CallPtr:
	case asBC_FuncPtr:
	case asBC_LoadThisR:
	case asBC_DIVu:
	case asBC_MODu:
	case asBC_DIVu64:
	case asBC_MODu64:
	case asBC_LoadRObjR:
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
	case asBC_POWu64:
	case asBC_Thiscall1:    {
		emit_vm_fallback(fn, "unsupported instruction");
		break;
	}

	default: {
		emit_vm_fallback(fn, "unknown instruction");
		break;
	}
	}

	if (ins.info->bc == m_config.debug.fallback_after_instruction) {
		emit_vm_fallback(fn, "debug.fallback_after_instruction");
	}

	emit("\t}}\n");
}

void BytecodeToC::emit_auto_bc_inc(BytecodeInstruction ins) { emit("\t\tl_bc += {};\n", ins.size); }

void BytecodeToC::emit_vm_fallback([[maybe_unused]] asIScriptFunction& fn, std::string_view reason) {
	++m_state.fallback_count;

	emit_save_vm_registers();

	if (m_config.c.human_readable) {
		emit("\t\treturn; /* {} */\n", reason);
	} else {
		emit("\t\treturn;\n");
	}
}

void BytecodeToC::emit_save_vm_registers() {
	emit(
	    "\t\tregs->programPointer = l_bc;\n"
	    "\t\tregs->stackPointer = l_sp;\n"
	    "\t\tregs->stackFramePointer = l_fp;\n"
	);
}

void BytecodeToC::emit_load_vm_registers() {
	emit(
	    "\t\tl_bc = regs->programPointer;\n"
	    "\t\tl_sp = regs->stackPointer;\n"
	    "\t\tl_fp = regs->stackFramePointer;\n"
	);
}

void BytecodeToC::emit_primitive_cast_var_ins(BytecodeInstruction ins, VarType src, VarType dst, bool in_place) {
	if (src.byte_count != dst.byte_count && dst.byte_count < 4) {
		emit(
		    "\t\t{DST_TYPE} value = ASEA_FRAME_VAR({SRC}).as_{SRC_TYPE};\n"
		    "\t\tasea_var *dst = &ASEA_FRAME_VAR({DST});\n"
		    "\t\tdst->as_asDWORD = 0;\n"
		    "\t\tdst->as_{DST_TYPE} = value;\n",
		    fmt::arg("DST_TYPE", dst.type),
		    fmt::arg("SRC_TYPE", src.type),
		    fmt::arg("DST", ins.sword0()),
		    fmt::arg("SRC", in_place ? ins.sword0() : ins.sword1())
		);
		emit_auto_bc_inc(ins);
		return;
	}
	emit(
	    "\t\tASEA_FRAME_VAR({DST}).as_{DST_TYPE} = ASEA_FRAME_VAR({SRC}).as_{SRC_TYPE};\n",
	    fmt::arg("DST_TYPE", dst.type),
	    fmt::arg("SRC_TYPE", src.type),
	    fmt::arg("DST", ins.sword0()),
	    fmt::arg("SRC", in_place ? ins.sword0() : ins.sword1())
	);
	emit_auto_bc_inc(ins);
}

std::string BytecodeToC::emit_global_lookup(asIScriptFunction& fn, void** pointer, bool global_var_only) {
	asCScriptEngine& engine = static_cast<asCScriptEngine&>(m_script_engine);

	std::string                            fn_symbol;
	asSMapNode<void*, asCGlobalProperty*>* var_cursor = nullptr;
	if (engine.varAddressMap.MoveTo(&var_cursor, pointer)) {
		asCGlobalProperty* property = engine.varAddressMap.GetValue(var_cursor);
		angelsea_assert(property != nullptr);

		fn_symbol = fmt::format("asea_global{}", property->id);

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(
			    fn_symbol.c_str(),
			    ExternGlobalVariable{.ptr = *pointer, .property = property},
			    pointer
			);
		}
	} else {
		// pointer to a string constant (of an arbitrary registered string type)

		// TODO: deduplicate references to identical strings

		angelsea_assert(!global_var_only);

		fn_symbol = fmt::format("asea_strobj{}_{}", m_state.string_constant_idx, entry_point_name(fn));

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(fn_symbol.c_str(), ExternStringConstant{*pointer}, pointer);
		}

		++m_state.string_constant_idx;
	}

	emit("\t\textern void* {};\n", fn_symbol);
	return fn_symbol;
}

void BytecodeToC::emit_cond_branch_ins(BytecodeInstruction ins, std::string_view test) {
	emit(
	    "\t\tif( {TEST} ) {{\n"
	    "\t\t\tl_bc += {BRANCH_OFFSET};\n"
	    "\t\t\tgoto bc{BRANCH_TARGET};\n"
	    "\t\t}} else {{\n"
	    "\t\t\tl_bc += {INSTRUCTION_LENGTH};\n"
	    "\t\t}}\n",
	    fmt::arg("TEST", test),
	    fmt::arg("INSTRUCTION_LENGTH", ins.size),
	    fmt::arg("BRANCH_OFFSET", ins.int0() + (long)ins.size),
	    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + (long)ins.size))
	);
}

void BytecodeToC::emit_test_ins(BytecodeInstruction ins, std::string_view op_with_rhs_0) {
	emit(
	    "\t\tasINT32 value = regs->valueRegister.as_asINT32;\n"
	    "\t\tregs->valueRegister.as_asQWORD = 0;\n"
	    "\t\tregs->valueRegister.as_asBYTE = (value {OP} 0) ? "
	    "VALUE_OF_BOOLEAN_TRUE : 0;\n",
	    fmt::arg("OP", op_with_rhs_0)
	);
	emit_auto_bc_inc(ins);
}

void BytecodeToC::emit_prefixop_valuereg_ins(BytecodeInstruction ins, std::string_view op, VarType var) {
	emit("\t\t{OP}ASEA_VALUEREG_DEREF().as_{TYPE};\n", fmt::arg("OP", op), fmt::arg("TYPE", var.type));
	emit_auto_bc_inc(ins);
}

void BytecodeToC::emit_unop_var_inplace_ins(BytecodeInstruction ins, std::string_view op, VarType var) {
	emit(
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{TYPE} = {OP} ASEA_FRAME_VAR({SWORD0}).as_{TYPE};\n",
	    fmt::arg("TYPE", var.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0())
	);
	emit_auto_bc_inc(ins);
}

void BytecodeToC::emit_binop_var_var_ins(
    BytecodeInstruction ins,
    std::string_view    op,
    VarType             lhs,
    VarType             rhs,
    VarType             dst
) {
	emit(
	    "\t\t{LHS_TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{LHS_TYPE};\n"
	    "\t\t{RHS_TYPE} rhs = ASEA_FRAME_VAR({SWORD2}).as_{RHS_TYPE};\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{RET_TYPE} = lhs {OP} rhs;\n"
	    "\t\tl_bc += 2;\n",
	    fmt::arg("LHS_TYPE", lhs.type),
	    fmt::arg("RHS_TYPE", rhs.type),
	    fmt::arg("RET_TYPE", dst.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("SWORD2", ins.sword2())
	);
}

void BytecodeToC::emit_binop_var_imm_ins(
    BytecodeInstruction ins,
    std::string_view    op,
    VarType             lhs,
    std::string_view    rhs_expr,
    VarType             dst
) {
	emit(
	    "\t\t{LHS_TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{LHS_TYPE};\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{DST_TYPE} = lhs {OP} ({RHS_EXPR});\n"
	    "\t\tl_bc += 3;\n",
	    fmt::arg("LHS_TYPE", lhs.type),
	    fmt::arg("DST_TYPE", dst.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("RHS_EXPR", rhs_expr)
	);
}

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset) {
	return std::size_t(std::int64_t(base_offset) + std::int64_t(relative_offset));
}

} // namespace angelsea::detail