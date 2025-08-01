// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <as_property.h>
#include <fmt/format.h>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace angelsea::detail {

/// Access granularity of a stack frame access, required to do aliasing-safe
/// loads and stores. This means zero-extension as required.
enum class AccessGranularity { DWORD, QWORD };

/// Describes the type of a value on the stack, which is useful to abstract its
/// loading and storing.
/// This is used both for operands and the destination.
struct VarType {
	/// C type name
	std::string_view type;
	std::size_t      byte_count;

	bool operator==(const VarType& other) const { return type == other.type; };
};

namespace var_types {
static constexpr VarType s8{"asINT8", 1}, s16{"asINT16", 2}, s32{"asINT32", 4}, s64{"asINT64", 8}, u8{"asBYTE", 1},
    u16{"asWORD", 2}, u32{"asDWORD", 4}, u64{"asQWORD", 8}, f32{"float", 4}, f64{"double", 8};
} // namespace var_types

class BytecodeToC {
	public:
	using OnMapFunctionCallback = std::function<void(asIScriptFunction&, const std::string& name)>;

	struct ExternScriptFunction {
		int id;
	};
	struct ExternGlobalVariable {
		void*              ptr;
		asCGlobalProperty* property;
	};
	/// An external string constant, whose type depends on the registered string
	/// factory.
	struct ExternStringConstant {
		void* ptr;
	};
	using ExternMapping       = std::variant<ExternGlobalVariable, ExternStringConstant, ExternScriptFunction>;
	using OnMapExternCallback = std::function<void(const char* c_name, const ExternMapping& kind, void* raw_value)>;

	BytecodeToC(const JitConfig& config, asIScriptEngine& engine, std::string jit_fn_prefix = "asea_jit");

	void prepare_new_context();

	void translate_module(
	    std::string_view              internal_module_name,
	    asIScriptModule*              script_module,
	    std::span<asIScriptFunction*> functions
	);

	void translate_function(std::string_view internal_module_name, asIScriptFunction& function);

	std::string entry_point_name(asIScriptFunction& fn) const;

	std::string& source() { return m_state.buffer; }

	/// Configure the callback to be invoked when a function is mapped to a C
	/// function name. This is useful to track the generated entry points in
	/// the source code.
	void set_map_function_callback(OnMapFunctionCallback callback) { m_on_map_function_callback = std::move(callback); }

	/// Configure the callback to be invoked when the C code is declaring an
	/// `extern` asPWORD variable that it knows the value of (through the
	/// engine); typically to allow making the C code not hardcode references to
	/// address in memory.
	/// The value is not kept around/defined in the C code: You *must* provide
	/// this information to the linker you are using (e.g. `MIR_load_external`,
	/// or figuring some way out if you are doing AOT).
	/// It is also possible for redundant calls to happen. In this case, the
	/// caller should at best assert that the value has not unexpectedly
	/// changed.
	void set_map_extern_callback(OnMapExternCallback callback) { m_on_map_extern_callback = std::move(callback); }

	/// Returns the number of fallbacks to the VM generated since
	/// `prepare_new_context`.
	/// If `== 0`, then all translated functions were fully translated.
	std::size_t get_fallback_count() const { return m_state.fallback_count; }

	private:
	struct FunctionTranslationState {};

	bool is_instruction_blacklisted(asEBCInstr bc) const;
	void translate_instruction(asIScriptFunction& fn, BytecodeInstruction instruction, FunctionTranslationState& state);

	template<class... Ts> void emit(fmt::format_string<Ts...> format, Ts&&... format_args) {
		fmt::format_to(std::back_inserter(m_state.buffer), format, std::forward<Ts>(format_args)...);
	}

	void emit_entry_dispatch(asIScriptFunction& fn);
	void emit_vm_fallback(asIScriptFunction& fn, std::string_view reason);

	void emit_auto_bc_inc(BytecodeInstruction ins);

	std::string emit_global_lookup(asIScriptFunction& fn, void** pointer, bool global_var_only);

	/// Emits the complete handler for a conditional relative branching instruction.
	/// If the condition provided by the expression in `test` is true, then perform a relative jump by the specified
	/// amount.
	void emit_cond_branch_ins(BytecodeInstruction ins, std::string_view test);

	/// Emits the complete handler for a test instruction.
	/// Writes the boolean result of `valueRegister {op} 0` to `valueRegister`.
	void emit_test_ins(BytecodeInstruction ins, std::string_view op_with_rhs_0);

	/// Emits the complete handler for a primitive cast of a variable on the stack to another.
	/// Automatically determines whether the instruction takes two arguments (source and destination), or whether the
	/// operation occurs in place in the same variable location.
	void emit_primitive_cast_var_ins(BytecodeInstruction ins, VarType src, VarType dst);

	/// Emits the complete handler for an in-place prefix operation on the valueRegister, that is,
	/// `{op}valueRegister` (`op` normally being either `++` or `--`).
	void emit_prefixop_valuereg_ins(BytecodeInstruction ins, std::string_view op, VarType var);

	/// Emits the complete handler for an in-place unary operation on a variable on the stack, that is,
	/// `var = {op} var`.
	void emit_unop_var_inplace_ins(BytecodeInstruction ins, std::string_view op, VarType var);

	/// Emits the complete handler for a binary operation between two variables on the stack, outputting to a third
	/// one, that is, `result = lhs {op} rhs`.
	void emit_binop_var_var_ins(BytecodeInstruction ins, std::string_view op, VarType lhs, VarType rhs, VarType dst);

	/// Emits the complete handler for a binary operation between a variable on the stack and an immediate value,
	/// outputting to another variable, that is, `result = lhs {op} (rhs_expr)`.
	void emit_binop_var_imm_ins(
	    BytecodeInstruction ins,
	    std::string_view    op,
	    VarType             lhs,
	    std::string_view    rhs_expr,
	    VarType             dst
	);

	/// Emits the complete handler for a division or modulus operation (where `op` is one of the `ASEA_FDIV`/`FMOD`
	/// macros) between two float variables on the stack, outputting to a third one. This is handled separately from
	/// regular binop because these instructions can raise exceptions.
	void emit_divmod_var_float_ins(BytecodeInstruction ins, std::string_view op, VarType type);

	const JitConfig& m_config;
	asIScriptEngine& m_script_engine;
	std::string      m_jit_fn_prefix;

	OnMapFunctionCallback m_on_map_function_callback;
	OnMapExternCallback   m_on_map_extern_callback;

	/// State for the current `prepare_new_context` context.
	struct ContextState {
		std::string buffer;
		std::size_t fallback_count;
		std::size_t string_constant_idx;
	};
	ContextState m_state;
};

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset);

} // namespace angelsea::detail