// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <angelsea/detail/jitcompiler.hpp>
#include <fmt/format.h>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace angelsea::detail {

using ModuleId   = std::size_t;
using FunctionId = std::size_t;

/// Access granularity of a stack frame access, required to do aliasing-safe
/// loads and stores. This means zero-extension as required.
enum class AccessGranularity { DWORD, QWORD };

/// Describes the type of a value on the stack, which is useful to abstract its
/// loading and storing.
/// This is used both for operands and the destination.
struct VarType {
	/// C type name
	std::string_view type;
	/// Access granularity for stack load/store
	AccessGranularity granularity;

	bool operator==(const VarType& other) const = default;

	std::string_view load_op_name() const {
		return granularity == AccessGranularity::QWORD ? "ASEA_LOAD64" : "ASEA_LOAD32";
	}

	std::string_view store_op_name() const {
		return granularity == AccessGranularity::QWORD ? "ASEA_STORE64" : "ASEA_STORE32";
	}
};

namespace var_types {
static constexpr VarType s8{"asINT8", AccessGranularity::DWORD}, s16{"asINT16", AccessGranularity::DWORD},
    s32{"asINT32", AccessGranularity::DWORD}, s64{"asINT64", AccessGranularity::QWORD},
    u8{"asBYTE", AccessGranularity::DWORD}, u16{"asWORD", AccessGranularity::DWORD},
    u32{"asDWORD", AccessGranularity::DWORD}, u64{"asQWORD", AccessGranularity::QWORD};
} // namespace var_types

class BytecodeToC {
	public:
	using OnMapFunctionCallback = std::function<void(JitFunction&, const std::string& name)>;

	BytecodeToC(JitCompiler& compiler);

	void prepare_new_context();

	ModuleId translate_module(
	    std::string_view        internal_module_name,
	    asIScriptModule*        script_module,
	    std::span<JitFunction*> functions
	);

	FunctionId translate_function(std::string_view internal_module_name, JitFunction& function);

	std::string entry_point_name(ModuleId module_id, FunctionId function_id) const;

	std::string& source() { return m_buffer; }

	bool is_human_readable() const;

	/// Configure the callback to be invoked when a function is mapped to a C
	/// function name. This is useful to track the generated entry points in
	/// the source code.
	void set_map_function_callback(OnMapFunctionCallback callback) { m_on_map_function_callback = callback; }

	/// Returns the number of fallbacks to the VM generated since
	/// `prepare_new_context`.
	/// If `== 0`, then all translated functions were fully translated.
	std::size_t get_fallback_count() const { return m_fallback_count; }

	private:
	void write_header();

	void translate_instruction(JitFunction& function, BytecodeInstruction instruction);

	template<class... Ts> void emit(fmt::format_string<Ts...> format, Ts&&... format_args) {
		fmt::format_to(std::back_inserter(m_buffer), format, std::forward<Ts>(format_args)...);
	}

	void emit_entry_dispatch(JitFunction& function);
	void emit_vm_fallback(JitFunction& function, std::string_view reason);

	void emit_load_vm_registers();
	void emit_save_vm_registers();

	void emit_cond_branch(BytecodeInstruction ins, std::size_t instruction_length, std::string_view test);
	void emit_primitive_cast_stack(BytecodeInstruction ins, VarType src, VarType dst);
	void emit_arithmetic_simple_stack_unary_inplace(BytecodeInstruction ins, std::string_view op, VarType var);
	void emit_arithmetic_simple_stack_stack(
	    BytecodeInstruction ins,
	    std::string_view    op,
	    VarType             lhs,
	    VarType             rhs,
	    VarType             dst
	);
	void emit_arithmetic_simple_stack_imm(
	    BytecodeInstruction ins,
	    std::string_view    op,
	    VarType             lhs,
	    std::string_view    rhs_expr,
	    VarType             dst
	);

	JitCompiler* m_compiler;
	std::string  m_buffer;
	ModuleId     m_current_module_id;
	ModuleId     m_current_function_id;

	OnMapFunctionCallback m_on_map_function_callback;

	std::size_t m_fallback_count;
};

std::size_t relative_jump_target(std::size_t base_offset, int relative_offset);

} // namespace angelsea::detail