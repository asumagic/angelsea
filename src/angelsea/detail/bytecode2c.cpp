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

static constexpr std::string_view save_registers_sequence
    = "\t\tregs->programPointer = l_bc;\n"
      "\t\tregs->stackPointer = l_sp;\n"
      "\t\tregs->stackFramePointer = l_fp;\n";

static constexpr std::string_view load_registers_sequence
    = "\t\tl_bc = regs->programPointer;\n"
      "\t\tl_sp = regs->stackPointer;\n"
      "\t\tl_fp = regs->stackFramePointer;\n";

BytecodeToC::BytecodeToC(const JitConfig& config, asIScriptEngine& engine, std::string jit_fn_prefix) :
    m_config(config), m_script_engine(engine), m_jit_fn_prefix(std::move(jit_fn_prefix)) {
	m_module_state.buffer.reserve(1024 * 64);
}

void BytecodeToC::prepare_new_context() {
	m_module_state.fallback_count      = 0;
	m_module_state.string_constant_idx = 0;

	m_module_state.buffer.clear();
	m_module_state.buffer += angelsea_c_header;
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

	emit_entry_dispatch(state);

	for (BytecodeInstruction ins : get_bytecode(fn)) {
		state.ins = ins;
		translate_instruction(state);
	}

	emit_error_handlers(state);

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

void BytecodeToC::emit_entry_dispatch(FnState& state) {
	emit(
	    "\tswitch(entryLabel) {{\n"
	    "\tdefault:\n"
	);

	bool    last_was_jit_entry = false;
	asPWORD jit_entry_id       = 1;

	for (BytecodeInstruction ins : get_bytecode(state.fn)) {
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

void BytecodeToC::emit_error_handlers(FnState& state) {
	if (state.error_handlers.null) {
		emit(
		    "\terr_null:\n"
		    "{SAVE_REGS}"
		    "\tasea_set_internal_exception(_regs, \"" TXT_NULL_POINTER_ACCESS
		    "\");\n"
		    "\t\t\treturn;\n"
		    "\t\n",
		    fmt::arg("SAVE_REGS", save_registers_sequence)
		);
	}

	if (state.error_handlers.divide_by_zero) {
		emit(
		    "\terr_divide_by_zero:\n"
		    "{SAVE_REGS}"
		    "\tasea_set_internal_exception(_regs, \"" TXT_DIVIDE_BY_ZERO
		    "\");\n"
		    "\t\t\treturn;\n"
		    "\t\n",
		    fmt::arg("SAVE_REGS", save_registers_sequence)
		);
	}

	if (state.error_handlers.divide_overflow) {
		emit(
		    "\terr_divide_overflow:\n"
		    "{SAVE_REGS}"
		    "\tasea_set_internal_exception(_regs, \"" TXT_DIVIDE_OVERFLOW
		    "\");\n"
		    "\t\t\treturn;\n"
		    "\t\n",
		    fmt::arg("SAVE_REGS", save_registers_sequence)
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
	case asBC_JitEntry: {
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_STR: {
		emit_vm_fallback(state, "deprecated instruction");
		break;
	}

	case asBC_SUSPEND: {
		log(m_config,
		    m_script_engine,
		    fn,
		    LogSeverity::PERF_WARNING,
		    "asBC_SUSPEND found; this will fallback to the VM and be slow!");
		emit_vm_fallback(state, "SUSPEND is not implemented yet");
		break;
	}

	case asBC_TYPEID:
	case asBC_PshC4:  {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -1);\n"
		    "\t\tASEA_STACK_TOP.as_asDWORD = {DWORD0};\n",
		    fmt::arg("DWORD0", ins.dword0())
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_PshC8: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -2);\n"
		    "\t\tASEA_STACK_TOP.as_asQWORD = {QWORD0};\n",
		    fmt::arg("QWORD0", ins.qword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PshV4: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -1);\n"
		    "\t\tASEA_STACK_TOP.as_asDWORD = ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_PshV8: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -2);\n"
		    "\t\tASEA_STACK_TOP.as_asQWORD = ASEA_FRAME_VAR({SWORD0}).as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_PshVPtr: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = ASEA_FRAME_VAR({SWORD0}).as_asPWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PshRPtr: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = regs->valueRegister.as_asPWORD;\n"
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_PopRPtr: {
		emit(
		    "\t\tregs->valueRegister.as_asPWORD = ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, AS_PTR_SIZE);\n"
		);
		emit_auto_bc_inc(state);
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
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_SetV8: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asQWORD = (asQWORD){QWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("QWORD0", ins.qword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_CpyVtoR4: {
		emit(
		    "\t\tregs->valueRegister.as_asDWORD = ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_CpyRtoV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = regs->valueRegister.as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_CpyVtoV4: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asDWORD = ASEA_FRAME_VAR({SWORD1}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_CpyVtoV8: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_asQWORD = "
		    "ASEA_FRAME_VAR({SWORD1}).as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_LDV: {
		emit(
		    "\t\tregs->valueRegister.as_asPWORD = (asPWORD)&ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PSF: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD)&ASEA_FRAME_VAR({SWORD0});\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_LDG: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), true);
		emit("\t\tregs->valueRegister.as_asPWORD = &{};\n", symbol);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PGA: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), false);
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD)&{OBJ};\n",
		    fmt::arg("OBJ", symbol)
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PshGPtr: {
		std::string symbol = emit_global_lookup(state, reinterpret_cast<void**>(ins.pword0()), false);
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD){OBJ};\n",
		    fmt::arg("OBJ", symbol)
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PshNull: {
		// TODO: simple but not tested! how can we get AS to emit it?
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = 0;\n"
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_PopPtr: {
		emit("\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, AS_PTR_SIZE);\n");
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_VAR: {
		emit(
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, -AS_PTR_SIZE);\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD = (asPWORD){SWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_CHKREF: {
		emit("\t\tif (ASEA_STACK_TOP.as_asPWORD == 0) {{ goto err_null; }}\n");
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
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

		emit(
		    "\t\tasPWORD *dst = &ASEA_FRAME_VAR({SWORD0}).as_asPWORD;\n"
		    "\t\tasPWORD src = ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\t*dst = src;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
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
		    "\t\tasPWORD *dst = (asPWORD*)ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\tl_sp = ASEA_STACK_DWORD_OFFSET(l_sp, AS_PTR_SIZE);\n"
		    "\t\tasPWORD src = ASEA_STACK_TOP.as_asPWORD;\n"
		    "\t\t*dst = src;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);

		break;
	}

	case asBC_LOADOBJ: {
		emit(
		    "\t\tvoid **a = &ASEA_FRAME_VAR({SWORD0}).as_ptr;\n"
		    "\t\tregs->objectType = 0;\n"
		    "\t\tregs->objectRegister = *a;\n"
		    "\t\t*a = 0;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_STOREOBJ: {
		emit(
		    "\t\tASEA_FRAME_VAR({SWORD0}).as_ptr = regs->objectRegister;\n"
		    "\t\tregs->objectRegister = 0;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);

		break;
	}

	case asBC_GETOBJ: {
		emit(
		    "\t\tasPWORD *a = &ASEA_STACK_VAR({WORD0}).as_asPWORD;\n"
		    "\t\tasPWORD offset = *a;\n"
		    "\t\tasPWORD *v = &ASEA_FRAME_VAR(offset).as_asPWORD;\n"
		    "\t\t*a = *v;\n"
		    "\t\t*v = 0;\n",
		    fmt::arg("WORD0", ins.word0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_LoadRObjR: {
		emit(
		    "\t\tasPWORD base = ASEA_FRAME_VAR({SWORD0}).as_asPWORD;\n"
		    "\t\tif (base == 0) {{ goto err_null; }}\n"
		    "\t\tregs->valueRegister.as_asPWORD = base + {SWORD1};\n",
		    fmt::arg("SWORD0", ins.sword0()),
		    fmt::arg("SWORD1", ins.sword1())
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_LoadThisR: {
		emit(
		    "\t\tasPWORD base = ASEA_FRAME_VAR(0).as_asPWORD;\n"
		    "\t\tif (base == 0) {{ goto err_null; }}\n"
		    "\t\tregs->valueRegister.as_asPWORD = base + {SWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_WRTV1: {
		emit(
		    "\t\tASEA_VALUEREG_DEREF().as_asBYTE = ASEA_FRAME_VAR({SWORD0}).as_asBYTE;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_WRTV2: {
		emit(
		    "\t\tASEA_VALUEREG_DEREF().as_asWORD = ASEA_FRAME_VAR({SWORD0}).as_asWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_WRTV4: {
		emit(
		    "\t\tASEA_VALUEREG_DEREF().as_asDWORD = ASEA_FRAME_VAR({SWORD0}).as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_WRTV8: {
		emit(
		    "\t\tASEA_VALUEREG_DEREF().as_asQWORD = ASEA_FRAME_VAR({SWORD0}).as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_RDR1: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asBYTE = ASEA_VALUEREG_DEREF().as_asBYTE;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_RDR2: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = 0;\n"
		    "\t\tvar->as_asWORD = ASEA_VALUEREG_DEREF().as_asWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_RDR4: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asDWORD = ASEA_VALUEREG_DEREF().as_asDWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
		break;
	}
	case asBC_RDR8: {
		emit(
		    "\t\tasea_var* var = &ASEA_FRAME_VAR({SWORD0});\n"
		    "\t\tvar->as_asQWORD = ASEA_VALUEREG_DEREF().as_asQWORD;\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		emit_auto_bc_inc(state);
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
		    "\t\tl_bc += 2;\n"
		    "{SAVE_REGS}"
		    "\t\tasea_call_script_function(_regs, (asCScriptFunction*)&{FN});\n"
		    "\t\treturn;\n",
		    fmt::arg("FN", fn_symbol),
		    fmt::arg("FN_ID", fn_idx),
		    fmt::arg("SAVE_REGS", save_registers_sequence)
		);
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
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_ADDSi: {
		// FIXME: concerning wtf: if we store &ASEA_STACK_TOP.as_asPWORD to a temporary and use it, then we get
		// corruption with -O2 (not if disabling load GVN), again.
		emit(
		    "\t\tif (ASEA_STACK_TOP.as_asPWORD == 0) {{ goto err_null; }}\n"
		    "\t\tASEA_STACK_TOP.as_asPWORD += {SWORD0};\n",
		    fmt::arg("SWORD0", ins.sword0())
		);
		state.error_handlers.null = true;
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_IncVi: {
		emit("\t\t++ASEA_FRAME_VAR({SWORD0}).as_asINT32;\n", fmt::arg("SWORD0", ins.sword0()));
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_DecVi: {
		emit("\t\t--ASEA_FRAME_VAR({SWORD0}).as_asINT32;\n", fmt::arg("SWORD0", ins.sword0()));
		emit_auto_bc_inc(state);
		break;
	}

	case asBC_JZ:     emit_cond_branch_ins(state, "regs->valueRegister.as_asINT32 == 0"); break;
	case asBC_JLowZ:  emit_cond_branch_ins(state, "regs->valueRegister.as_asBYTE == 0"); break;
	case asBC_JNZ:    emit_cond_branch_ins(state, "regs->valueRegister.as_asINT32 != 0"); break;
	case asBC_JLowNZ: emit_cond_branch_ins(state, "regs->valueRegister.as_asBYTE != 0"); break;
	case asBC_JS:     emit_cond_branch_ins(state, "regs->valueRegister.as_asINT32 < 0"); break;
	case asBC_JNS:    emit_cond_branch_ins(state, "regs->valueRegister.as_asINT32 >= 0"); break;
	case asBC_JP:     emit_cond_branch_ins(state, "regs->valueRegister.as_asINT32 > 0"); break;
	case asBC_JNP:    emit_cond_branch_ins(state, "regs->valueRegister.as_asINT32 <= 0"); break;

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

	case asBC_CMPIi:  emit_compare_var_imm_ins(state, s32, fmt::to_string(ins.int0())); break;
	case asBC_CMPIu:  emit_compare_var_imm_ins(state, u32, fmt::to_string(ins.dword0())); break;
	case asBC_CMPIf:
		emit("\t\tasea_i2f rhs_i2f = {{.i={}}};\n", ins.dword0());
		emit_compare_var_imm_ins(state, f32, "rhs_i2f.f");
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

	case asBC_SwapPtr:
	case asBC_PshG4:
	case asBC_LdGRdR4:
	case asBC_RET:
	case asBC_COPY:
	case asBC_RDSPtr:
	case asBC_JMPP:
	case asBC_CALLSYS:
	case asBC_CALLBND:
	case asBC_ALLOC:
	case asBC_FREE:
	case asBC_GETREF:
	case asBC_ClrVPtr:
	case asBC_OBJTYPE:
	case asBC_CpyVtoR8:
	case asBC_CpyVtoG4:
	case asBC_CpyRtoV8:
	case asBC_CpyGtoV4:
	case asBC_SetG4:
	case asBC_ChkRefS:
	case asBC_ChkNullV:
	case asBC_CALLINTF:
	case asBC_Cast:
	case asBC_ChkNullS:
	case asBC_ClrHi:
	case asBC_CallPtr:
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
	case asBC_POWu64:
	case asBC_Thiscall1:    {
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

void BytecodeToC::emit_auto_bc_inc(FnState& state) { emit("\t\tl_bc += {};\n", state.ins.size); }

void BytecodeToC::emit_vm_fallback(FnState& state, std::string_view reason) {
	++m_module_state.fallback_count;

	emit("{}", save_registers_sequence);

	if (m_config.c.human_readable) {
		emit("\t\treturn; /* {} */\n", reason);
	} else {
		emit("\t\treturn;\n");
	}
}

void BytecodeToC::emit_primitive_cast_var_ins(FnState& state, VarType src, VarType dst) {
	BytecodeInstruction& ins      = state.ins;
	const bool           in_place = ins.size == 1;

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
		emit_auto_bc_inc(state);
		return;
	}
	emit(
	    "\t\tASEA_FRAME_VAR({DST}).as_{DST_TYPE} = ASEA_FRAME_VAR({SRC}).as_{SRC_TYPE};\n",
	    fmt::arg("DST_TYPE", dst.type),
	    fmt::arg("SRC_TYPE", src.type),
	    fmt::arg("DST", ins.sword0()),
	    fmt::arg("SRC", in_place ? ins.sword0() : ins.sword1())
	);
	emit_auto_bc_inc(state);
}

std::string BytecodeToC::emit_global_lookup(FnState& state, void** pointer, bool global_var_only) {
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

		fn_symbol = fmt::format("asea_strobj{}_{}", m_module_state.string_constant_idx, entry_point_name(state.fn));

		if (m_on_map_extern_callback) {
			m_on_map_extern_callback(fn_symbol.c_str(), ExternStringConstant{*pointer}, pointer);
		}

		++m_module_state.string_constant_idx;
	}

	emit("\t\textern void* {};\n", fn_symbol);
	return fn_symbol;
}

void BytecodeToC::emit_cond_branch_ins(FnState& state, std::string_view test) {
	BytecodeInstruction& ins = state.ins;
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

void BytecodeToC::emit_compare_var_var_ins(FnState& state, VarType type) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{TYPE} lhs = ASEA_FRAME_VAR({SWORD0}).as_{TYPE};\n"
	    "\t\t{TYPE} rhs = ASEA_FRAME_VAR({SWORD1}).as_{TYPE};\n"
	    "\t\tif      (lhs == rhs) regs->valueRegister.as_asINT32 = 0;\n"
	    "\t\telse if (lhs < rhs)  regs->valueRegister.as_asINT32 = -1;\n"
	    "\t\telse                 regs->valueRegister.as_asINT32 = 1;\n",
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("TYPE", type.type)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_compare_var_imm_ins(FnState& state, VarType type, std::string_view rhs_expr) {
	emit(
	    "\t\t{TYPE} lhs = ASEA_FRAME_VAR({SWORD0}).as_{TYPE};\n"
	    "\t\t{TYPE} rhs = {RHS};\n"
	    "\t\tif      (lhs == rhs) regs->valueRegister.as_asINT32 = 0;\n"
	    "\t\telse if (lhs < rhs)  regs->valueRegister.as_asINT32 = -1;\n"
	    "\t\telse                 regs->valueRegister.as_asINT32 = 1;\n",
	    fmt::arg("SWORD0", state.ins.sword0()),
	    fmt::arg("RHS", rhs_expr),
	    fmt::arg("TYPE", type.type)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_test_ins(FnState& state, std::string_view op_with_rhs_0) {
	emit(
	    "\t\tasINT32 value = regs->valueRegister.as_asINT32;\n"
	    "\t\tregs->valueRegister.as_asQWORD = 0;\n"
	    "\t\tregs->valueRegister.as_asBYTE = (value {OP} 0) ? "
	    "VALUE_OF_BOOLEAN_TRUE : 0;\n",
	    fmt::arg("OP", op_with_rhs_0)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_prefixop_valuereg_ins(FnState& state, std::string_view op, VarType var) {
	emit("\t\t{OP}ASEA_VALUEREG_DEREF().as_{TYPE};\n", fmt::arg("OP", op), fmt::arg("TYPE", var.type));
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_unop_var_inplace_ins(FnState& state, std::string_view op, VarType var) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{TYPE} = {OP} ASEA_FRAME_VAR({SWORD0}).as_{TYPE};\n",
	    fmt::arg("TYPE", var.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0())
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_binop_var_var_ins(FnState& state, std::string_view op, VarType lhs, VarType rhs, VarType dst) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{LHS_TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{LHS_TYPE};\n"
	    "\t\t{RHS_TYPE} rhs = ASEA_FRAME_VAR({SWORD2}).as_{RHS_TYPE};\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{RET_TYPE} = lhs {OP} rhs;\n",
	    fmt::arg("LHS_TYPE", lhs.type),
	    fmt::arg("RHS_TYPE", rhs.type),
	    fmt::arg("RET_TYPE", dst.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("SWORD2", ins.sword2())
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
	    "\t\t{LHS_TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{LHS_TYPE};\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{DST_TYPE} = lhs {OP} ({RHS_EXPR});\n",
	    fmt::arg("LHS_TYPE", lhs.type),
	    fmt::arg("DST_TYPE", dst.type),
	    fmt::arg("OP", op),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("RHS_EXPR", rhs_expr)
	);
	emit_auto_bc_inc(state);
}

void BytecodeToC::emit_divmod_var_float_ins(FnState& state, std::string_view op, VarType type) {
	BytecodeInstruction& ins = state.ins;
	emit(
	    "\t\t{TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{TYPE};\n"
	    "\t\t{TYPE} divider = ASEA_FRAME_VAR({SWORD2}).as_{TYPE};\n"
	    "\t\tif (divider == 0) {{ goto err_divide_by_zero; }}\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{TYPE} = {OP}(lhs, divider);\n",
	    fmt::arg("TYPE", type.type),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("SWORD2", ins.sword2()),
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
	    "\t\t{TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{TYPE};\n"
	    "\t\t{TYPE} divider = ASEA_FRAME_VAR({SWORD2}).as_{TYPE};\n"
	    "\t\tif (divider == 0) {{ goto err_divide_by_zero; }}\n"
	    "\t\tif (divider == -1 && lhs == ({TYPE}){LHS_OVERFLOW}) {{ goto err_divide_overflow; }}\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{TYPE} = lhs {OP} divider;\n",
	    fmt::arg("TYPE", type.type),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
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
	    "\t\t{TYPE} lhs = ASEA_FRAME_VAR({SWORD1}).as_{TYPE};\n"
	    "\t\t{TYPE} divider = ASEA_FRAME_VAR({SWORD2}).as_{TYPE};\n"
	    "\t\tif (divider == 0) {{ goto err_divide_by_zero; }}\n"
	    "\t\tASEA_FRAME_VAR({SWORD0}).as_{TYPE} = lhs {OP} divider;\n",
	    fmt::arg("TYPE", type.type),
	    fmt::arg("SWORD0", ins.sword0()),
	    fmt::arg("SWORD1", ins.sword1()),
	    fmt::arg("SWORD2", ins.sword2()),
	    fmt::arg("OP", op),
	    fmt::arg("SAVE_REGS", save_registers_sequence)
	);
	state.error_handlers.divide_by_zero = true;
	emit_auto_bc_inc(state);
}

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset) {
	return std::size_t(std::int64_t(base_offset) + std::int64_t(relative_offset));
}

} // namespace angelsea::detail