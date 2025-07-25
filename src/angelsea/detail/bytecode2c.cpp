// SPDX-License-Identifier: BSD-2-Clause

#include "angelscript.h"
#include "angelsea/detail/bytecodeinstruction.hpp"
#define FMT_COMPILE
#include <fmt/format.h>

#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/bytecodedisasm.hpp>
#include <angelsea/detail/bytecodetools.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/log.hpp>

namespace angelsea::detail {

BytecodeToC::BytecodeToC(JitCompiler& compiler) : m_compiler(&compiler) {
	m_buffer.reserve(1024 * 64);
	m_current_module_id   = 0;
	m_current_function_id = 0;
}

void BytecodeToC::prepare_new_context() {
	m_buffer.clear();
	m_fallback_count = 0;
	write_header();
}

ModuleId BytecodeToC::translate_module(
    std::string_view        internal_module_name,
    asIScriptModule*        script_module,
    std::span<JitFunction*> functions
) {
	++m_current_module_id;

	// NOTE: module name and section names are separate concepts, and there may
	// be several script sections in a module
	emit(
	    R"___(
/* MODULE: {module_name} */ 
)___",
	    fmt::arg("module_name", internal_module_name)
	);

	for (JitFunction* function : functions) {
		translate_function(internal_module_name, *function);
	}

	return m_current_module_id;
}

FunctionId BytecodeToC::translate_function(std::string_view internal_module_name, JitFunction& function) {
	++m_current_function_id;

	const auto func_name = entry_point_name(m_current_module_id, m_current_function_id);
	if (m_on_map_function_callback) {
		m_on_map_function_callback(function, func_name);
	}

	if (is_human_readable()) {
		const char* section_name;
		int         row, col;
		function.script_function().GetDeclaredAt(&section_name, &row, &col);
		emit(
		    "/* {}:{}:{}: {} */\n",
		    section_name != nullptr ? section_name : "<anon>",
		    row,
		    col,
		    function.script_function().GetDeclaration(true, true, true)
		);
	}

	// JIT entry signature is `void(asSVMRegisters *regs, asPWORD jitArg)`
	emit("void {name}(asSVMRegisters *regs, asPWORD entryLabel) {{\n", fmt::arg("name", func_name));

	emit(
	    // "#ifdef __MIRC__\n"
	    // "\tasDWORD *l_bc __attribute__((antialias(\"bc-sp\")));\n"
	    // "\tasDWORD *l_sp __attribute__((antialias(\"bc-sp\")));\n"
	    // "\tasDWORD *l_fp;\n"
	    // "#else\n"
	    "\tasDWORD *l_bc;\n"
	    "\tasDWORD *l_sp;\n"
	    "\tasDWORD *l_fp;\n"
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

	emit_entry_dispatch(function);

	walk_bytecode(get_bytecode(function.script_function()), [&](BytecodeInstruction ins) {
		translate_instruction(function, ins);
	});

	emit("}}\n");

	return m_current_function_id;
}

std::string BytecodeToC::entry_point_name(ModuleId module_id, FunctionId function_id) const {
	return fmt::format("asea_jit_mod{}_fn{}", module_id, function_id);
}

void BytecodeToC::emit_entry_dispatch(JitFunction& function) {
	// TODO: (optionally) generate a goto dispatch table, which should be
	// supported by c2mir and probably would at least elide a branch

	// 0 means the JIT entry point will not be used, start at 1
	asPWORD current_entry_id = 1;

	emit("\tswitch(entryLabel) {{\n");

	walk_bytecode(get_bytecode(function.script_function()), [&](BytecodeInstruction ins) {
		if (ins.info->bc != asBC_JitEntry) {
			return; // skip to the next
		}

		// patch the JIT entry with the index we use in the switch
		ins.pword0() = current_entry_id;

		emit("\tcase {}: goto bc{};\n", current_entry_id, ins.offset);

		++current_entry_id;
	});

	emit("\t}}\n\n");
}

void BytecodeToC::translate_instruction(JitFunction& function, BytecodeInstruction ins) {
	if (is_human_readable()) {
		emit("\t/* bytecode: {} */\n", disassemble(m_compiler->engine(), ins));
	}

	emit("\tbc{}: {{\n", ins.offset);

	// TODO: after a fallback don't bother emitting fallback code at all
	// until the next JitEntry

	// TODO: elide jit entries when we're not dropping down to the VM
	// between them (assign their jitarg to 0 to be sure)
	// this does mean having to recompute indices

	// TODO: elide jit entries when they immediately precede an unhandled
	// instruction

	// TODO: if all jit entries were elided from the first jit entry, entirely
	// optimize away the dispatch

	switch (ins.info->bc) {
	case asBC_JitEntry: {
		emit("\t\tl_bc += 1+AS_PTR_SIZE;\n");
		break;
	}

	case asBC_SUSPEND: {
		log(*m_compiler,
		    function.script_function(),
		    LogSeverity::PERF_WARNING,
		    "asBC_SUSPEND found; this will fallback to the VM and be slow!");
		emit_vm_fallback(function, "SUSPEND is not implemented yet");
		break;
	}

	case asBC_PshC4: {
		emit(
		    "\t\t--l_sp;\n"
		    "\t\tASEA_STORE32(l_sp, {DWORD0});\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("DWORD0", ins.dword0())
		);
		break;
	}

	case asBC_PshC8: {
		emit(
		    "\t\tl_sp -= 2;\n"
		    "\t\tASEA_STORE64(l_sp, {QWORD0});\n"
		    "\t\tl_bc += 3;\n",
		    fmt::arg("QWORD0", ins.qword0())
		);
		break;
	}

	case asBC_PshV4: {
		emit(
		    "\t\t--l_sp;\n"
		    "\t\tASEA_STORE32(l_sp, ASEA_LOAD32(asDWORD, l_fp - {SWORD0}));\n"
		    "\t\t++l_bc;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_PshV8: {
		emit(
		    "\t\tl_sp -= 2;\n"
		    "\t\tASEA_STORE64(l_sp, ASEA_LOAD64(asQWORD, l_fp - {SWORD0}));\n"
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
		    "\t\tASEA_STORE32(l_fp - {SWORD0}, {DWORD0});\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("DWORD0", ins.dword0())
		);
		break;
	}

	case asBC_SetV8: {
		emit(
		    "\t\tASEA_STORE64(l_fp - {SWORD0}, {QWORD0});\n"
		    "\t\tl_bc += 3;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("QWORD0", ins.qword0())
		);
		break;
	}

	case asBC_CpyVtoR4: {
		emit(
		    "\t\t(*regs).valueRegister = ASEA_LOAD32(asDWORD, l_fp - {SWORD0});\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_CpyRtoV4: {
		emit(
		    "\t\tASEA_STORE32(l_fp - {SWORD0}, (*regs).valueRegister);\n"
		    "\t\tl_bc++;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		break;
	}

	case asBC_CpyVtoV4: {
		emit(
		    "\t\tASEA_STORE32(l_fp - {SWORD0}, ASEA_LOAD32(asDWORD, l_fp - {SWORD1}));\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		break;
	}

	case asBC_CpyVtoV8: {
		emit(
		    "\t\tASEA_STORE64(l_fp - {SWORD0}, ASEA_LOAD64(asQWORD, l_fp - {SWORD1}));\n"
		    "\t\tl_bc += 2;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		break;
	}

		// case asBC_CALL:
		// {
		//     // TODO: when possible, translate this to a JIT to JIT function call

		//     int fn = ins.arg_int();
		//     emit(
		//         "\t\tint i = {FN_ID};\n"
		//         "\t\tl_bc += 2;\n"
		//         "\t\tasASSERT( i>= 0 );\n"
		//         // "\t\t asASSERT( (i & FUNC_IMPORTED) == 0 );"
		//         "",
		//         fmt::arg("FN_ID", fn)
		//     );
		//     emit_save_vm_registers();
		//     emit(
		//         "\t\tint r = asea_call_script_function(regs, {FN_ID});\n",
		//         fmt::arg("FN_ID", fn)
		//     );
		//     emit_load_vm_registers();
		//     emit("\t\tif (r != asEXECUTION_ACTIVE) {{ return; }}\n");
		//     branch_bc();
		//     break;
		// }

	case asBC_CALL: {
		emit_vm_fallback(function, "instructions that branch to l_bc are not supported yet");
		break;
	}

	case asBC_CMPIi: {
		emit(
		    "\t\tint i1 = ASEA_LOAD32(asINT32, l_fp - {SWORD0});\n"
		    "\t\tint i2 = {INT0};\n"
		    "\t\tif( i1 == i2 )     (*regs).valueRegister = (asINT64)0;\n"
		    "\t\telse if( i1 < i2 ) (*regs).valueRegister = (asINT64)-1;\n"
		    "\t\telse               (*regs).valueRegister = (asINT64)1;\n"
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
		emit_cond_branch(ins, 2, "(asINT64)(*regs).valueRegister == 0");
		break;
	}
	case asBC_JNZ: {
		emit_cond_branch(ins, 2, "(asINT64)(*regs).valueRegister != 0");
		break;
	}
	case asBC_JS: {
		emit_cond_branch(ins, 2, "(asINT64)(*regs).valueRegister < 0");
		break;
	}
	case asBC_JNS: {
		emit_cond_branch(ins, 2, "(asINT64)(*regs).valueRegister >= 0");
		break;
	}
	case asBC_JP: {
		emit_cond_branch(ins, 2, "(asINT64)(*regs).valueRegister > 0");
		break;
	}
	case asBC_JNP: {
		emit_cond_branch(ins, 2, "(asINT64)(*regs).valueRegister <= 0");
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

	// float cases need reinterpreting, this is broken
	// case asBC_iTOf: {
	// 	emit_primitive_cast_stack(ins, var_types::s32, var_types::f32, true);
	// 	break;
	// }
	// case asBC_fTOi: {
	// 	emit_primitive_cast_stack(ins, var_types::f32, var_types::s32, true);
	// 	break;
	// }
	// case asBC_uTOf: {
	// 	emit_primitive_cast_stack(ins, var_types::u32, var_types::f32, true);
	// 	break;
	// }
	// case asBC_fTOu: {
	// 	emit_primitive_cast_stack(ins, var_types::f32, var_types::u32, true);
	// 	break;
	// }
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

	case asBC_PopPtr:
	case asBC_PshGPtr:
	case asBC_PSF:
	case asBC_SwapPtr:
	case asBC_NOT:
	case asBC_PshG4:
	case asBC_LdGRdR4:
	case asBC_RET:
	case asBC_TZ:
	case asBC_TNZ:
	case asBC_TS:
	case asBC_TNS:
	case asBC_TP:
	case asBC_TNP:
	case asBC_NEGi:
	case asBC_NEGf:
	case asBC_NEGd:
	case asBC_INCi16:
	case asBC_INCi8:
	case asBC_DECi16:
	case asBC_DECi8:
	case asBC_INCi:
	case asBC_DECi:
	case asBC_INCf:
	case asBC_DECf:
	case asBC_INCd:
	case asBC_DECd:
	case asBC_IncVi:
	case asBC_DecVi:
	case asBC_COPY:
	case asBC_PshVPtr:
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
	case asBC_REFCPY:
	case asBC_CHKREF:
	case asBC_GETOBJREF:
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
	case asBC_RDR1:
	case asBC_RDR2:
	case asBC_RDR4:
	case asBC_RDR8:
	case asBC_LDG:
	case asBC_LDV:
	case asBC_PGA:
	case asBC_CmpPtr:
	case asBC_VAR:
	case asBC_dTOi:
	case asBC_dTOu:
	case asBC_dTOf:
	case asBC_iTOd:
	case asBC_uTOd:
	case asBC_fTOd:
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
	case asBC_fTOi64:
	case asBC_dTOi64:
	case asBC_fTOu64:
	case asBC_dTOu64:
	case asBC_i64TOf:
	case asBC_u64TOf:
	case asBC_i64TOd:
	case asBC_u64TOd:
	case asBC_NEGi64:
	case asBC_INCi64:
	case asBC_DECi64:
	case asBC_ADDi64:
	case asBC_SUBi64:
	case asBC_MULi64:
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
	case asBC_RefCpyV:
	case asBC_JLowZ:
	case asBC_JLowNZ:
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
		emit_vm_fallback(function, "unsupported instruction");
		break;
	}

	default: {
		emit_vm_fallback(function, "unknown instruction");
		break;
	}
	}

	emit("\t}}\n");
}

void BytecodeToC::emit_vm_fallback(JitFunction& function, std::string_view reason) {
	++m_fallback_count;

	emit_save_vm_registers();

	if (is_human_readable()) {
		emit("\t\treturn; /* {} */\n", reason);
	} else {
		emit("\t\treturn;\n");
	}
}

void BytecodeToC::emit_save_vm_registers() {
	emit(
	    "\t\t(*regs).programPointer = l_bc;\n"
	    "\t\t(*regs).stackPointer = l_sp;\n"
	    "\t\t(*regs).stackFramePointer = l_fp;\n"
	);
}

void BytecodeToC::emit_load_vm_registers() {
	emit(
	    "\t\tl_bc = (*regs).programPointer;\n"
	    "\t\tl_sp = (*regs).stackPointer;\n"
	    "\t\tl_fp = (*regs).stackFramePointer;\n"
	);
}

void BytecodeToC::emit_primitive_cast_stack(BytecodeInstruction ins, VarType src, VarType dst, bool in_place) {
	angelsea_assert(src.is_trivial_cast_to(dst));
	emit(
	    "\t\t{STORE_OP}(l_fp - {DST}, ({DST_TYPE}){LOAD_OP}({SRC_TYPE}, l_fp - {SRC}));\n"
	    "\t\tl_bc += {INSTRUCTION_LENGTH};\n",
	    fmt::arg("DST_TYPE", dst.type),
	    fmt::arg("SRC_TYPE", src.type),
	    fmt::arg("DST", ins.sword0()),
	    fmt::arg("SRC", in_place ? ins.sword0() : ins.sword1()),
	    fmt::arg("LOAD_OP", src.load_op_name()),
	    fmt::arg("STORE_OP", dst.store_op_name()),
	    fmt::arg("INSTRUCTION_LENGTH", in_place ? 1 : 2)
	);
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
	    fmt::arg("BRANCH_OFFSET", ins.int0() + instruction_length),
	    fmt::arg("BRANCH_TARGET", relative_jump_target(ins.offset, ins.int0() + instruction_length))
	);
}

void BytecodeToC::emit_arithmetic_simple_stack_unary_inplace(
    BytecodeInstruction ins,
    std::string_view    op,
    VarType             var
) {
	emit(
	    "\t\tasDWORD* target = l_fp - {SWORD0};\n"
	    "\t\t{TYPE} value = {OP}{LOAD_OP}({TYPE}, l_fp - {SWORD0});\n"
	    "\t\t{STORE_OP}(target, value);\n"
	    "\t\t++l_bc;\n",
	    fmt::arg("TYPE", var.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("LOAD_OP", var.load_op_name()),
	    fmt::arg("STORE_OP", var.store_op_name())
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
	    "\t\tasDWORD* target = l_fp - {SWORD0};\n"
	    "\t\t{LHS_TYPE} lhs = {LHS_LOAD_OP}({LHS_TYPE}, l_fp - {SWORD1});\n"
	    "\t\t{RHS_TYPE} rhs = {RHS_LOAD_OP}({RHS_TYPE}, l_fp - {SWORD2});\n"
	    "\t\t{RET_TYPE} result = lhs {OP} rhs;\n"
	    "\t\t{STORE_OP}(target, result);\n"
	    "\t\tl_bc += 2;\n",
	    fmt::arg("LHS_TYPE", lhs.type),
	    fmt::arg("RHS_TYPE", rhs.type),
	    fmt::arg("LHS_LOAD_OP", lhs.load_op_name()),
	    fmt::arg("RHS_LOAD_OP", rhs.load_op_name()),
	    fmt::arg("RET_TYPE", dst.type),
	    fmt::arg("STORE_OP", dst.store_op_name()),
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
	    "\t\t{STORE_OP}(l_fp - {SWORD0}, {LHS_LOAD_OP}({LHS_TYPE}, l_fp - {SWORD1}) {OP} ({RHS_EXPR}));\n"
	    "\t\tl_bc += 3;\n",
	    fmt::arg("LHS_TYPE", lhs.type),
	    fmt::arg("LHS_LOAD_OP", lhs.load_op_name()),
	    fmt::arg("STORE_OP", dst.store_op_name()),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("RHS_EXPR", rhs_expr)
	);
}

bool BytecodeToC::is_human_readable() const { return true; }

void BytecodeToC::write_header() {
	m_buffer += R"___(/* start of angelsea static header */

/*
    This generated source file contains macro definitions and references to
    internal structures extracted from the AngelScript scripting library, which
    are licensed under the zlib license (license provided below).

    Very minor modifications may have been applied to formatting or to allow
    compilation via a C compiler.
    This should still be possible to compile as plain C++; report bugs if not!

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

typedef void asIScriptContext;
typedef void asITypeInfo;
typedef struct
{
	asDWORD          *programPointer;     /* points to current bytecode instruction */
	asDWORD          *stackFramePointer;  /* function stack frame */
	asDWORD          *stackPointer;       /* top of stack (grows downward) */
	asQWORD           valueRegister;      /* temp register for primitives */
	void             *objectRegister;     /* temp register for objects and handles */
	asITypeInfo      *objectType;         /* type of object held in object register */
	/* HACK: doProcessSuspend is normally defined as bool in C++; assume int equivalent */
	int              doProcessSuspend;    /* whether or not the JIT should break out when it encounters a suspend instruction */
	asIScriptContext *ctx;                /* the active context */
} asSVMRegisters;

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

#endif

/*
    The following definitions are part of the angelsea runtime.hpp
*/

int asea_call_script_function(void* vm_registers, int function_idx);

/*
    The following definitions are additional angelsea helpers
*/

/* FIXME: endianness - we assume LE */

#define ASEA_LOAD32(type, ptr) (type)(*(ptr))
#define ASEA_LOAD64(type, ptr) (type)((asQWORD)((ptr)[0]) | ((asQWORD)((ptr)[1]) << 32))
#define ASEA_STORE32(ptr, val) { *(ptr) = (val); }
#define ASEA_STORE64(ptr, val) { (ptr)[0] = (asDWORD)(val); (ptr)[1] = (asDWORD)((asQWORD)(val) >> 32); }

/* end of angelsea static header */

/* start of code generated by angelsea bytecode2c */
)___";
}

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset) {
	return std::size_t(std::int64_t(base_offset) + std::int64_t(relative_offset));
}

std::string_view VarType::load_op_name() const {
	return granularity == AccessGranularity::QWORD ? "ASEA_LOAD64" : "ASEA_LOAD32";
}
std::string_view VarType::store_op_name() const {
	return granularity == AccessGranularity::QWORD ? "ASEA_STORE64" : "ASEA_STORE32";
}

} // namespace angelsea::detail