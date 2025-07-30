// SPDX-License-Identifier: BSD-2-Clause

#include "angelscript.h"
#include "angelsea/detail/bytecodeinstruction.hpp"
#include "as_property.h"
#include <fmt/format.h>

#include <algorithm>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/bytecodedisasm.hpp>
#include <angelsea/detail/bytecodetools.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/log.hpp>
#include <as_scriptengine.h>

namespace angelsea::detail {

BytecodeToC::BytecodeToC(const JitConfig& config, asIScriptEngine& engine, std::string jit_fn_prefix) :
    m_config(config), m_script_engine(engine), m_jit_fn_prefix(std::move(jit_fn_prefix)) {
	m_state.buffer.reserve(1024 * 64);
}

void BytecodeToC::prepare_new_context() {
	m_state.buffer.clear();
	m_state.fallback_count      = 0;
	m_state.string_constant_idx = 0;
	write_header();
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
	//     bc0: { /* <-- unique C label where the value is the equivalent bytecode offset */
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
		    "\tasea_debug_message((asSVMRegisters*)regs, \"TRACE FUNCTION: module {}: {}:{}:{}: {}\");\n\n",
		    internal_module_name,
		    escape_c_literal(section != nullptr ? section : "<anon>"),
		    row,
		    col,
		    escape_c_literal(fn.GetDeclaration(true, true, true))
		);
	}

	emit_entry_dispatch(fn);

	FunctionTranslationState state;
	walk_bytecode(get_bytecode(fn), [&](BytecodeInstruction ins) { translate_instruction(fn, ins, state); });

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
	// doesn't seem supported by c2mir after all?
	// if (m_config.c.use_gnuc_label_as_address_extension) {
	// 	emit(
	// 	    "\tstatic const void *const dispatch[] = {{\n"
	// 	    "\t\t0,\n" // because JitEntry always starts at 1
	// 	);

	// 	walk_bytecode(get_bytecode(fn), [&](BytecodeInstruction ins) {
	// 		if (ins.info->bc != asBC_JitEntry) {
	// 			return;
	// 		}

	// 		if (ins.pword0() == 0) {
	// 			// codegen eliminated this jit entry
	// 			return;
	// 		}

	// 		emit("\t\t&&bc{},", ins.offset);
	// 	});

	// 	emit(
	// 	    "\t}};\n"
	// 	    "\tgoto *dispatch[entryLabel];\n"
	// 	);

	// }

	emit(
	    "\tswitch(entryLabel) {{\n"
	    "\tdefault:\n"
	);

	bool    last_was_jit_entry = false;
	asPWORD jit_entry_id       = 1;

	walk_bytecode(get_bytecode(fn), [&](BytecodeInstruction ins) {
		if (ins.info->bc != asBC_JitEntry) {
			last_was_jit_entry = false;
			return; // skip to the next
		}

		if (last_was_jit_entry) {
			// ignore successive JIT entries
			return;
		}

		ins.pword0() = jit_entry_id;

		emit("\tcase {}: goto bc{};\n", jit_entry_id, ins.offset);
		last_was_jit_entry = true;

		++jit_entry_id;
	});

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
		emit("\t\tl_bc += 1+AS_PTR_SIZE;\n");
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
		    "\t\tASEA_STACK_TOP.as_asDWORD = {DWORD0};\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("DWORD0", ins.dword0())
		);
		break;
	}
	case asBC_PshC8: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -2);\n"
		    "\t\tASEA_STACK_TOP.as_asQWORD = {QWORD0};\n"
		    "\t\tl_bc += 3;\n",
		    fmt::arg("QWORD0", ins.qword0())
		);
		break;
	}

	case asBC_PshV4: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -1);\n"
		    "\t\tASEA_STACK_TOP.as_asDWORD = ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n"
		    "\t\t++l_bc;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}
	case asBC_PshV8: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -2);\n"
		    "\t\tASEA_STACK_TOP.as_asQWORD = ASEA_FRAME_VAR({SWORD0}).as_asQWORD;\n"
		    "\t\t++l_bc;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}
	case asBC_PshVPtr: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = ASEA_FRAME_VAR({SWORD0}).as_asPWORD;\n"
		    "\t\t++l_bc;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	// V1/V2 are equivalent to V4
	case asBC_SetV1:
	case asBC_SetV2:
	case asBC_SetV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = (asDWORD){DWORD0};\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("DWORD0", ins.dword0())
		);
		break;
	}

	case asBC_SetV8: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asQWORD = (asQWORD){QWORD0};\n"
		    "\t\tl_bc += 3;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("QWORD0", ins.qword0())
		);
		break;
	}

	case asBC_CpyVtoR4: {
		emit(
		    "\t\tregs->valueRegister.as_asDWORD = ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_CpyRtoV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = regs->valueRegister.as_asDWORD;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_CpyVtoV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = ASEA_FRAME_VAR({SWORD1}).as_asDWORD;\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		break;
	}

	case asBC_CpyVtoV8: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asQWORD = ASEA_FRAME_VAR({SWORD1}).as_asQWORD;\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		break;
	}

	case asBC_LDV: {
		emit(
		    "\t\tregs->valueRegister.as_asPWORD = (asPWORD)&ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_PGA: {
		std::string fn_symbol = emit_global_lookup(fn, ins, reinterpret_cast<void**>(ins.pword0()), false);
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD)&{OBJ};\n"
		    "\t\tl_bc += 1+AS_PTR_SIZE;\n",
		    fmt::arg("OBJ", fn_symbol)
		);

		break;
	}

	case asBC_PshGPtr: {
		std::string fn_symbol = emit_global_lookup(fn, ins, reinterpret_cast<void**>(ins.pword0()), false);
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD){OBJ};\n"
		    "\t\tl_bc += 1+AS_PTR_SIZE;\n",
		    fmt::arg("OBJ", fn_symbol)
		);

		break;
	}

	case asBC_PopPtr: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}

	case asBC_VAR: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD){SWORD0};\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_GETOBJREF: {
		emit(
		    "\t\tasPWORD *dst = &ASEA_STACK_VAR({WORD0}).as_asPWORD;\n"
		    "\t\tasPWORD var_idx = *dst;\n"
		    "\t\tasPWORD var_addr = ASEA_FRAME_VAR(var_idx).as_asPWORD;\n"
		    "\t\tASEA_STACK_VAR({WORD0}).as_asPWORD = var_addr;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("WORD0", ins.word0())
		);
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
		    "\t\t*dst = src;\n"
		    "\t\tl_bc += 1+AS_PTR_SIZE;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);

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
		    "\t\t*dst = src;\n"
		    "\t\tl_bc += 1+AS_PTR_SIZE;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);

		break;
	}

	case asBC_RDR1: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asBYTE = ASEA_VALUEREG_DEREF().as_asBYTE;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}
	case asBC_RDR2: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asWORD = ASEA_VALUEREG_DEREF().as_asWORD;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}
	case asBC_RDR4: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = ASEA_VALUEREG_DEREF().as_asDWORD;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}
	case asBC_RDR8: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asQWORD = ASEA_VALUEREG_DEREF().as_asQWORD;\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
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
		    "\t\telse               regs->valueRegister.as_asINT64 = 1;\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("INT0", ins.int0())
		);
		break;
	}

	case asBC_JMP: {
		emit(
		    "\t\tl_bc += {BRANCH_OFFSET};\n"
		    "\t\tgoto bc{BRANCH_TARGET};\n",
		    fmt::arg("BRANCH_OFFSET", ins.int0() + 2),
		    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + 2))
		);
		break;
	}

	case asBC_JZ: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asINT64 == 0");
		break;
	}
	case asBC_JLowZ: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asBYTE == 0");
		break;
	}
	case asBC_JNZ: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asINT64 != 0");
		break;
	}
	case asBC_JLowNZ: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asBYTE != 0");
		break;
	}
	case asBC_JS: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asINT64 < 0");
		break;
	}
	case asBC_JNS: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asINT64 >= 0");
		break;
	}
	case asBC_JP: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asINT64 > 0");
		break;
	}
	case asBC_JNP: {
		emit_cond_branch(ins, 2, "regs->valueRegister.as_asINT64 <= 0");
		break;
	}

	case asBC_TZ: {
		emit_test(ins, "==");
		break;
	}
	case asBC_TNZ: {
		emit_test(ins, "!=");
		break;
	}
	case asBC_TS: {
		emit_test(ins, "<");
		break;
	}
	case asBC_TNS: {
		emit_test(ins, ">=");
		break;
	}
	case asBC_TP: {
		emit_test(ins, ">");
		break;
	}
	case asBC_TNP: {
		emit_test(ins, "<");
		break;
	}

	case asBC_INCi8: {
		emit(
		    "\t\t++ASEA_VALUEREG_DEREF().as_asBYTE;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}
	case asBC_DECi8: {
		emit(
		    "\t\t--ASEA_VALUEREG_DEREF().as_asBYTE;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}

	case asBC_INCi16: {
		emit(
		    "\t\t++ASEA_VALUEREG_DEREF().as_asWORD;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}
	case asBC_DECi16: {
		emit(
		    "\t\t--ASEA_VALUEREG_DEREF().as_asWORD;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}

	case asBC_INCi: {
		emit(
		    "\t\t++ASEA_VALUEREG_DEREF().as_asDWORD;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}
	case asBC_DECi: {
		emit(
		    "\t\t--ASEA_VALUEREG_DEREF().as_asDWORD;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}

	case asBC_INCi64: {
		emit(
		    "\t\t++ASEA_VALUEREG_DEREF().as_asQWORD;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}
	case asBC_DECi64: {
		emit(
		    "\t\t--ASEA_VALUEREG_DEREF().as_asQWORD;\n"
		    "\t\tl_bc++;\n"
		);
		break;
	}

	case asBC_ADDi: {
		emit_arithmetic_simple_stack_stack(ins, "+", var_types::s32, var_types::s32, var_types::s32);
		break;
	}
	case asBC_SUBi: {
		emit_arithmetic_simple_stack_stack(ins, "-", var_types::s32, var_types::s32, var_types::s32);
		break;
	}
	case asBC_MULi: {
		emit_arithmetic_simple_stack_stack(ins, "*", var_types::s32, var_types::s32, var_types::s32);
		break;
	}

	case asBC_ADDi64: {
		emit_arithmetic_simple_stack_stack(ins, "+", var_types::s64, var_types::s64, var_types::s64);
		break;
	}
	case asBC_SUBi64: {
		emit_arithmetic_simple_stack_stack(ins, "-", var_types::s64, var_types::s64, var_types::s64);
		break;
	}
	case asBC_MULi64: {
		emit_arithmetic_simple_stack_stack(ins, "*", var_types::s64, var_types::s64, var_types::s64);
		break;
	}

	case asBC_BNOT64: {
		emit_arithmetic_simple_stack_unary_inplace(ins, "~", var_types::u64);
		break;
	}
	case asBC_BAND64: {
		emit_arithmetic_simple_stack_stack(ins, "&", var_types::u64, var_types::u64, var_types::u64);
		break;
	}
	case asBC_BXOR64: {
		emit_arithmetic_simple_stack_stack(ins, "^", var_types::u64, var_types::u64, var_types::u64);
		break;
	}
	case asBC_BOR64: {
		emit_arithmetic_simple_stack_stack(ins, "|", var_types::u64, var_types::u64, var_types::u64);
		break;
	}
	case asBC_BSLL64: {
		emit_arithmetic_simple_stack_stack(ins, "<<", var_types::u64, var_types::u32, var_types::u64);
		break;
	}
	case asBC_BSRL64: {
		emit_arithmetic_simple_stack_stack(ins, ">>", var_types::u64, var_types::u32, var_types::u64);
		break;
	}
	case asBC_BSRA64: {
		emit_arithmetic_simple_stack_stack(ins, ">>", var_types::s64, var_types::u32, var_types::s64);
		break;
	}

	case asBC_BNOT: {
		emit_arithmetic_simple_stack_unary_inplace(ins, "~", var_types::u32);
		break;
	}
	case asBC_BAND: {
		emit_arithmetic_simple_stack_stack(ins, "&", var_types::u32, var_types::u32, var_types::u32);
		break;
	}
	case asBC_BXOR: {
		emit_arithmetic_simple_stack_stack(ins, "^", var_types::u32, var_types::u32, var_types::u32);
		break;
	}
	case asBC_BOR: {
		emit_arithmetic_simple_stack_stack(ins, "|", var_types::u32, var_types::u32, var_types::u32);
		break;
	}
	case asBC_BSLL: {
		emit_arithmetic_simple_stack_stack(ins, "<<", var_types::u32, var_types::u32, var_types::u32);
		break;
	}
	case asBC_BSRL: {
		emit_arithmetic_simple_stack_stack(ins, ">>", var_types::u32, var_types::u32, var_types::u32);
		break;
	}
	case asBC_BSRA: {
		emit_arithmetic_simple_stack_stack(ins, ">>", var_types::s32, var_types::u32, var_types::s32);
		break;
	}

	case asBC_SUBIi: {
		emit_arithmetic_simple_stack_imm(ins, "-", var_types::s32, fmt::to_string(ins.int0(1)), var_types::s32);
		break;
	}

	case asBC_iTOf: {
		emit_primitive_cast_stack(ins, var_types::s32, var_types::f32, true);
		break;
	}
	case asBC_fTOi: {
		emit_primitive_cast_stack(ins, var_types::f32, var_types::s32, true);
		break;
	}
	case asBC_uTOf: {
		emit_primitive_cast_stack(ins, var_types::u32, var_types::f32, true);
		break;
	}
	case asBC_fTOu: {
		emit_primitive_cast_stack(ins, var_types::f32, var_types::u32, true);
		break;
	}
	case asBC_sbTOi: {
		emit_primitive_cast_stack(ins, var_types::s8, var_types::s32, true);
		break;
	}
	case asBC_swTOi: {
		emit_primitive_cast_stack(ins, var_types::s16, var_types::s32, true);
		break;
	}
	case asBC_ubTOi: {
		emit_primitive_cast_stack(ins, var_types::u8, var_types::s32, true);
		break;
	}
	case asBC_uwTOi: {
		emit_primitive_cast_stack(ins, var_types::u16, var_types::s32, true);
		break;
	}
	case asBC_i64TOi: {
		emit_primitive_cast_stack(ins, var_types::s64, var_types::s32, false);
		break;
	}
	case asBC_uTOi64: {
		emit_primitive_cast_stack(ins, var_types::u32, var_types::s64, false);
		break;
	}
	case asBC_iTOi64: {
		emit_primitive_cast_stack(ins, var_types::s32, var_types::s64, false);
		break;
	}
	case asBC_fTOd: {
		emit_primitive_cast_stack(ins, var_types::f32, var_types::f64, false);
		break;
	}
	case asBC_dTOf: {
		emit_primitive_cast_stack(ins, var_types::f64, var_types::f32, false);
		break;
	}
	case asBC_fTOi64: {
		emit_primitive_cast_stack(ins, var_types::f32, var_types::s64, false);
		break;
	}
	case asBC_dTOi64: {
		emit_primitive_cast_stack(ins, var_types::f64, var_types::s64, true);
		break;
	}
	case asBC_fTOu64: {
		emit_primitive_cast_stack(ins, var_types::f32, var_types::u64, false);
		break;
	}
	case asBC_dTOu64: {
		emit_primitive_cast_stack(ins, var_types::f64, var_types::u64, true);
		break;
	}
	case asBC_i64TOf: {
		emit_primitive_cast_stack(ins, var_types::s64, var_types::f32, false);
		break;
	}
	case asBC_u64TOf: {
		emit_primitive_cast_stack(ins, var_types::u64, var_types::f32, false);
		break;
	}
	case asBC_i64TOd: {
		emit_primitive_cast_stack(ins, var_types::s64, var_types::f64, true);
		break;
	}
	case asBC_u64TOd: {
		emit_primitive_cast_stack(ins, var_types::u64, var_types::f64, true);
		break;
	}
	case asBC_dTOi: {
		emit_primitive_cast_stack(ins, var_types::f64, var_types::s32, false);
		break;
	}
	case asBC_dTOu: {
		emit_primitive_cast_stack(ins, var_types::f64, var_types::u32, false);
		break;
	}
	case asBC_iTOd: {
		emit_primitive_cast_stack(ins, var_types::s32, var_types::f64, false);
		break;
	}
	case asBC_uTOd: {
		emit_primitive_cast_stack(ins, var_types::u32, var_types::f64, false);
		break;
	}

	case asBC_PSF:
	case asBC_SwapPtr:
	case asBC_NOT:
	case asBC_PshG4:
	case asBC_LdGRdR4:
	case asBC_RET:
	case asBC_NEGi:
	case asBC_NEGf:
	case asBC_NEGd:
	case asBC_INCf:
	case asBC_DECf:
	case asBC_INCd:
	case asBC_DECd:
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
	case asBC_STR:
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
	case asBC_ADDf:
	case asBC_SUBf:
	case asBC_MULf:
	case asBC_DIVf:
	case asBC_MODf:
	case asBC_ADDd:
	case asBC_SUBd:
	case asBC_MULd:
	case asBC_DIVd:
	case asBC_MODd:
	case asBC_ADDIi:
	case asBC_MULIi:
	case asBC_ADDIf:
	case asBC_SUBIf:
	case asBC_MULIf:
	case asBC_SetG4:
	case asBC_ChkRefS:
	case asBC_ChkNullV:
	case asBC_CALLINTF:
	case asBC_iTOb:
	case asBC_iTOw:
	case asBC_Cast:
	case asBC_NEGi64:
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
	case asBC_Thiscall1: {
		emit_vm_fallback(fn, "unsupported instruction");
		break;
	}

	default: {
		emit_vm_fallback(fn, "unknown instruction");
		break;
	}
	}

	emit("\t}}\n");
}

void BytecodeToC::emit_vm_fallback(asIScriptFunction& fn, std::string_view reason) {
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

void BytecodeToC::emit_primitive_cast_stack(BytecodeInstruction ins, VarType src, VarType dst, bool in_place) {
	emit(
	    "\t\tASEA_FRAME_VAR({DST}).as_{DST_TYPE} = ASEA_FRAME_VAR({SRC}).as_{SRC_TYPE};\n"
	    "\t\tl_bc += {INSTRUCTION_LENGTH};\n",
	    fmt::arg("DST_TYPE", dst.type),
	    fmt::arg("SRC_TYPE", src.type),
	    fmt::arg("DST", ins.sword0()),
	    fmt::arg("SRC", in_place ? ins.sword0() : ins.sword1()),
	    fmt::arg("INSTRUCTION_LENGTH", in_place ? 1 : 2)
	);
}

std::string
BytecodeToC::emit_global_lookup(asIScriptFunction& fn, BytecodeInstruction ins, void** pointer, bool global_var_only) {
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

void BytecodeToC::emit_cond_branch(BytecodeInstruction ins, std::size_t instruction_length, std::string_view test) {
	emit(
	    "\t\tif( {TEST} ) {{\n"
	    "\t\t\tl_bc += {BRANCH_OFFSET};\n"
	    "\t\t\tgoto bc{BRANCH_TARGET};\n"
	    "\t\t}} else {{\n"
	    "\t\t\tl_bc += {INSTRUCTION_LENGTH};\n"
	    "\t\t}}\n",
	    fmt::arg("TEST", test),
	    fmt::arg("INSTRUCTION_LENGTH", instruction_length),
	    fmt::arg("BRANCH_OFFSET", ins.int0() + (long)instruction_length),
	    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + (long)instruction_length))
	);
}

void BytecodeToC::emit_test(BytecodeInstruction ins, std::string_view op_with_rhs_0) {
	emit(
	    "\t\tasINT32 value = regs->valueRegister.as_asINT32;\n"
	    "\t\tregs->valueRegister.as_asQWORD = 0;\n"
	    "\t\tregs->valueRegister.as_asBYTE = (value {OP} 0) ? VALUE_OF_BOOLEAN_TRUE : 0;\n"
	    "\t\tl_bc++;\n",
	    fmt::arg("OP", op_with_rhs_0)
	);
}

void BytecodeToC::emit_arithmetic_simple_stack_unary_inplace(
    BytecodeInstruction ins,
    std::string_view    op,
    VarType             var
) {
	emit(
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{TYPE} = {OP} ASEA_FRAME_VAR({SWORD0}).as_{TYPE};\n"
	    "\t\t++l_bc;\n",
	    fmt::arg("TYPE", var.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0())
	);
}

void BytecodeToC::emit_arithmetic_simple_stack_stack(
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

void BytecodeToC::emit_arithmetic_simple_stack_imm(
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

void BytecodeToC::write_header() {
	m_state.buffer += R"___(/* start of angelsea static header */

/*
    This generated source file contains macro definitions and references to
    internal structures extracted from the AngelScript scripting library, which
    are licensed under the zlib license (license provided below).

    Very minor modifications may have been applied to formatting or to allow
    compilation via a C compiler.

    This file should NOT be compiled by a C++ compiler, as it relies on type
    punning thru unions in a way that is not legal in C++.

    Generated function definitions are the result of stitching of code stencils
    which are closely based on the definition and internal structure of the
    AngelScript virtual machine.
    Checks and references to variables may be elided at compile time when
    possible.
*/

/*
   AngelCode Scripting Library
   Copyright (c) 2003-2025 Andreas Jonsson

   This software is provided 'as-is', without any express or implied
   warranty. In no event will the authors be held liable for any
   damages arising from the use of this software.

   Permission is granted to anyone to use this software for any
   purpose, including commercial applications, and to alter it and
   redistribute it freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you
      must not claim that you wrote the original software. If you use
      this software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and
      must not be misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
      distribution.

   The original version of this library can be located at:
   http://www.angelcode.com/angelscript/

   Andreas Jonsson
   andreas@angelcode.com
*/

/*
    Definitions normally provided by <angelscript.h>, but that in case of JIT we
    override them with definitions provided by c2mir.
*/
#ifdef ANGELSEA_SUPPORT

typedef __INT8_TYPE__    asINT8;
typedef __INT16_TYPE__   asINT16;
typedef __INT32_TYPE__   asINT32;
typedef __INT64_TYPE__   asINT64;
typedef __UINT8_TYPE__   asBYTE;
typedef __UINT16_TYPE__  asWORD;
typedef __UINT32_TYPE__  asUINT;
typedef __UINT32_TYPE__  asDWORD;
typedef __UINT64_TYPE__  asQWORD;
typedef __UINTPTR_TYPE__ asPWORD;

#define asASSERT

/* TODO: is this ever used in the VM other than AS_PTR_SIZE? */
#if __SIZEOF_POINTER__ == 4
	#define asBCTYPE_PTR_ARG    asBCTYPE_DW_ARG
	#define asBCTYPE_PTR_DW_ARG asBCTYPE_DW_DW_ARG
	#define asBCTYPE_wW_PTR_ARG asBCTYPE_wW_DW_ARG
	#define asBCTYPE_rW_PTR_ARG asBCTYPE_rW_DW_ARG
	#define AS_PTR_SIZE 1
#else
	#define asBCTYPE_PTR_ARG    asBCTYPE_QW_ARG
	#define asBCTYPE_PTR_DW_ARG asBCTYPE_QW_DW_ARG
	#define asBCTYPE_wW_PTR_ARG asBCTYPE_wW_QW_ARG
	#define asBCTYPE_rW_PTR_ARG asBCTYPE_rW_QW_ARG
	#define AS_PTR_SIZE 2
#endif

#define VALUE_OF_BOOLEAN_TRUE 1

typedef enum
{
	asEXECUTION_FINISHED        = 0,
	asEXECUTION_SUSPENDED       = 1,
	asEXECUTION_ABORTED         = 2,
	asEXECUTION_EXCEPTION       = 3,
	asEXECUTION_PREPARED        = 4,
	asEXECUTION_UNINITIALIZED   = 5,
	asEXECUTION_ACTIVE          = 6,
	asEXECUTION_ERROR           = 7,
	asEXECUTION_DESERIALIZATION = 8
} asEContextState;

/*
	Union to provide safe type punning with various AngelScript variables (as
	far as C aliasing rules allow, but not C++'s)
	This is only _fully_ legal and could theoretically break if the compiler can
	see beyond its compile unit (e.g. with LTO) but it should be otherwise
	unproblematic (and AS itself does worse, anyway).
*/
typedef union {
	asINT8 as_asINT8;
	asINT16 as_asINT16;
	asINT32 as_asINT32;
	asINT64 as_asINT64;
	asBYTE as_asBYTE;
	asWORD as_asWORD;
	asDWORD as_asDWORD;
	asQWORD as_asQWORD;
	asPWORD as_asPWORD;
	float as_float;
	double as_double;
	void* as_ptr;
} asea_var;

typedef struct asSVMRegisters asSVMRegisters;
typedef struct asIScriptContext asIScriptContext;
typedef struct asITypeInfo asITypeInfo;
typedef struct asCScriptFunction asCScriptFunction;
typedef struct asCObjectType asCObjectType;
typedef struct asSTypeBehaviour asSTypeBehaviour;

/* Layout exactly mimics asSVMRegisters */
typedef struct
{
	/*
		We rewrite some of the asDWORD* pointers to be void* instead; this is
		across the compile boundary in the case of JIT.
	*/
	asDWORD          *programPointer;     /* points to current bytecode instruction */
	void             *stackFramePointer;  /* function stack frame */
	void             *stackPointer;       /* top of stack (grows downward) */
	asea_var          valueRegister;      /* temp register for primitives */
	void             *objectRegister;     /* temp register for objects and handles */
	asITypeInfo      *objectType;         /* type of object held in object register */
	/* HACK: doProcessSuspend is normally defined as bool in C++; assume int equivalent */
	int              doProcessSuspend;    /* whether or not the JIT should break out when it encounters a suspend instruction */
	asIScriptContext *ctx;                /* the active context */
} asea_vm_registers;

#endif

/*
    The following definitions are part of the angelsea runtime.hpp
*/

void asea_call_script_function(void* vm_registers, void* function);
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);

/*
    The following definitions are additional angelsea helpers
*/

#define ASEA_STACK_DWORD_OFFSET(base, dword_offset) (void*)((char*)(base) + ((dword_offset) * 4))
#define ASEA_FRAME_VAR(dword_offset) (*(asea_var*)(ASEA_STACK_DWORD_OFFSET(l_fp, -(dword_offset))))
#define ASEA_STACK_VAR(dword_offset) (*(asea_var*)(ASEA_STACK_DWORD_OFFSET(l_sp, (dword_offset))))
#define ASEA_STACK_TOP (*(asea_var*)(l_sp))
#define ASEA_VALUEREG_DEREF() (*(asea_var*)(regs->valueRegister.as_ptr))

/* end of angelsea static header */

/* start of code generated by angelsea bytecode2c */
)___";
}

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset) {
	return std::size_t(std::int64_t(base_offset) + std::int64_t(relative_offset));
}

bool is_alpha_numerical(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

std::string escape_c_literal(std::string_view str) {
	std::string ret;
	ret.reserve(str.size()); // always underestimated but that's ok

	std::string_view legal_chars = "!#%&'()*+,-./:;<=>?[]^_{|}~ ";

	for (char c : str) {
		// handle the most common cases (either paste the characters as-is or
		// escape them) for the string to be readable
		if (is_alpha_numerical(c) || legal_chars.find(c) != legal_chars.npos) {
			ret += c;
		} else if (c == '\r') {
			ret += "\\r";
		} else if (c == '\n') {
			ret += "\\n";
		} else if (c == '\t') {
			ret += "\\t";
		} else if (c == '"') {
			ret += "\\\"";
		} else if (c == '\\') {
			ret += "\\\\";
		} else {
			// hex encode the rest
			ret += fmt::format("\\x{:02x}", c);
		}
	}

	return ret;
}

} // namespace angelsea::detail