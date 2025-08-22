// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "angelsea/detail/util.hpp"
#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <as_property.h>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>
#include <fmt/format.h>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace angelsea::detail {

struct TranspiledBlocks {
	std::string forward_declarations;
	std::string function_code;
};

struct TranspiledCode {
	TranspiledCode()                                 = default;
	TranspiledCode(const TranspiledCode&)            = delete;
	TranspiledCode& operator=(const TranspiledCode&) = delete;
	TranspiledCode(TranspiledCode&&)                 = default;
	TranspiledCode& operator=(TranspiledCode&&)      = default;
	~TranspiledCode()                                = default;

	std::vector<const char*> source_bits;

	/// Holds whatever dynamic sources in \ref source_bits need to be held. Those will be less complete than \ref
	/// source_bits because \ref source_bits can also refer to some static/constant strings.
	TranspiledBlocks code_blocks;
};

class BytecodeToC {
	public:
	struct ExternBytecodeDefinition {
		asIScriptFunction* fn;
	};
	struct ExternScriptFunction {
		int id;
	};
	struct ExternSystemFunction {
		int id;
	};
	struct ExternGlobalVariable {
		void*              ptr;
		asCGlobalProperty* property;
	};
	/// An external string constant, whose type depends on the registered string factory.
	struct ExternStringConstant {
		void* ptr;
	};
	struct ExternTypeInfo {
		asITypeInfo* object_type;
	};
	using ExternMapping = std::variant<
	    ExternBytecodeDefinition,
	    ExternGlobalVariable,
	    ExternStringConstant,
	    ExternScriptFunction,
	    ExternSystemFunction,
	    ExternTypeInfo>;

	using OnMapFunctionCallback = std::function<void(asIScriptFunction&, const std::string& name)>;
	using OnMapExternCallback   = std::function<void(const char* c_name, const ExternMapping& kind, void* raw_value)>;

	BytecodeToC(const JitConfig& config, asIScriptEngine& engine, std::string c_symbol_prefix = "asea_jit");

	void           prepare_new_context();
	TranspiledCode finalize_context();

	void translate_function(std::string_view internal_module_name, asIScriptFunction& fn);

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
	std::size_t get_fallback_count() const { return m_module_state.fallback_count; }

	private:
	struct StackPushInfo {
		VarType type;
	};

	enum class ErrorHandler : std::uint8_t {
		VM_FALLBACK         = 1 << 0,
		ERR_NULL            = 1 << 1,
		ERR_DIVIDE_BY_ZERO  = 1 << 2,
		ERR_DIVIDE_OVERFLOW = 1 << 3,
	};

	struct FnState {
		asIScriptFunction* fn;
		/// Current instruction being translated (if in a callee of translate_instruction)
		InsRef ins;

		/// Any Jitentry that is not the first?
		bool has_any_late_jit_entries = true;

		/// Map from switch bytecode offset (asBC_JMPP) to all its targets
		std::unordered_map<std::size_t, std::vector<std::size_t>> switch_map;

		/// Set of targets that may be branched to (via `goto bcXX;`), whether from relative jump instructions or JIT
		/// entry points
		std::unordered_set<std::size_t> branch_targets;

		/// Information for stack pushes related to \ref fn_to_stack_push
		std::unordered_map<std::size_t, StackPushInfo> stack_push_infos;

		/// Map from call instruction offset to associated stack pushes; order by order of stack push
		/// Complementary to \ref stack_push_to_fn
		std::unordered_map<std::size_t, std::vector<std::size_t>> fn_to_stack_push;

		std::unordered_map<std::size_t, VirtualInstruction> overriden_instructions;

		/// Symbols that already have been emitted, to avoid duplicated declarations
		std::unordered_set<std::string> emitted_symbols; // (might be good to find a way to remove?)

		bool has_direct_generic_call;

		std::underlying_type_t<ErrorHandler> error_handlers_mask;
	};

	std::string create_new_entry_point_name(asIScriptFunction& fn);

	bool is_instruction_blacklisted(asEBCInstr bc) const;
	void translate_instruction(FnState& state);

	template<class... Ts> void emit(fmt::format_string<Ts...> format, Ts&&... format_args) {
		emit_to(m_module_state.code_blocks.function_code, format, std::forward<Ts>(format_args)...);
	}

	template<class... Ts> void emit_to(std::string& target, fmt::format_string<Ts...> format, Ts&&... format_args) {
		fmt::format_to(std::back_inserter(target), format, std::forward<Ts>(format_args)...);
	}

	template<class... Ts>
	void emit_forward_declaration(
	    FnState&                  state,
	    std::string               symbol_name,
	    fmt::format_string<Ts...> format,
	    Ts&&... format_args
	) {
		auto [it, not_already_emitted] = state.emitted_symbols.emplace(std::move(symbol_name));
		if (!not_already_emitted) {
			return;
		}
		emit_to(m_module_state.code_blocks.forward_declarations, format, std::forward<Ts>(format_args)...);
	}

	/// Determines which asBC_JitEntry instructions should be valid entry points for the JITted function, and sets the
	/// JIT asPWORD arguments in the bytecode accordingly (0 for unused entry points, non-zero values otherwise).
	///
	/// Populates \ref FnState::has_any_jit_entries
	void configure_jit_entries(FnState& state);

	/// Discovers all asBC_JMPP instructions in the bytecode, which directly correspond to `switch` statements in source
	/// code, and populates \ref FnState::switch_map to map all possible branch targets of a specific switch.
	void discover_switch_map(FnState& state);

	/// Discovers all possible branch targets that may ever be used within JIT code and populates \ref
	/// FnState::branch_targets.
	void discover_branch_targets(FnState& state);

	/// Discovers all function calls for basic information storing on function calls to be known early before emitting
	/// code for the function. Currently, only populates \ref has_direct_generic_call.
	void discover_function_calls(FnState& state);

	/// Best-effort discovery of all stack pushes associated with a direct system function call and populates \ref
	/// FnState::stack_push_to_fn and its equivalent \ref FnState::fn_to_stack_push. This information can be used to
	/// eliminate stack pushes used to fetch function call arguments (e.g. replacing a stack push of a variable to a
	/// direct reference to the variable).
	///
	/// These mappings may be incomplete and miss early stack operations, and these mappings might not actually all be
	/// stack pushes that can be removed. All the pushes that are there are supported for removal by \ref
	/// translate_instruction, however.
	/// Call instructions optimizing based on these mappings should push values that should still make it to the stack
	/// (e.g. in case of a function call fallback or because the stack offset was not associated with any argument or
	/// such), and should always compute the stack offset of those pushes.
	///
	/// This function does not look at the calling convention of the callee.
	///
	/// This function depends on \ref discover_branch_targets being executed prior.
	void discover_function_call_pushes(FnState& state);

	/// Discover peephole optimizations to populate the virtual instructions.
	void discover_peephole(FnState& state);

	void emit_entry_dispatch(FnState& state);
	void emit_error_handlers(FnState& state);

	void        emit_vm_fallback(FnState& state, std::string_view reason);
	std::string jump_to_error_handler_code(FnState& state, ErrorHandler handler);

	void emit_save_sp(FnState& state);
	void emit_save_pc(FnState& state, bool next_pc);

	std::string emit_global_lookup(FnState& state, void* pointer, bool global_var_only);
	std::string emit_type_info_lookup(FnState& state, asITypeInfo& type);

	[[nodiscard]] bool is_complex_passed_by_value(const asCDataType& type) const;

	/// Emits a struct that emulates the layout for ABI purposes in the current scope and returns its generated name.
	/// Returns an empty string on failure e.g. when we cannot figure safely figure out an equivalent.
	std::string emit_dummy_struct_declaration(FnState& state, const asCDataType& type);

	struct SystemCall {
		int              fn_idx;
		std::string_view object_pointer_override;
		/// Is this a direct function call from the VM (e.g. for behaviors), or is this a script call
		bool is_internal_call;
	};

	struct SystemCallEmitResult {
		bool             ok;
		std::string_view fail_reason;
	};

	struct ScriptCallByIdx {
		int fn_idx;
	};

	/// Script call where the function index is not known, e.g. during virtual or interface calls, but the signature is
	/// known and provided through an asCScriptFunction pointer.
	struct ScriptCallByExpr {
		asCScriptFunction* fn_decl;
		std::string_view   expr;
	};

	void emit_direct_script_call_ins(FnState& state, std::variant<ScriptCallByIdx, ScriptCallByExpr> call);

	/// Emit code to perform a system call, potentially directly if config allows. On failure, a direct call is emitted.
	/// This function never calls emit_vm_fallback; i.e. it may perform calls via the VM but it will never return from
	/// the JIT function to do so.
	void emit_system_call(FnState& state, SystemCall call);

	/// Emit code to perform a direct system call (i.e. with a known signature and target). On failure, no code is
	/// emitted and the returned result object sets `ok == false`.
	[[nodiscard]] SystemCallEmitResult emit_direct_system_call(FnState& state, SystemCall call);

	/// Emit code to perform a direct system call (i.e. with a known signature and target), assuming it is of any of the
	/// native calling conventions. On failure, no code is emitted and the returned result object sets `ok == false`.
	[[nodiscard]] SystemCallEmitResult emit_direct_system_call_native(
	    FnState&           state,
	    SystemCall         call,
	    asCScriptFunction& fn,
	    std::string_view   fn_desc_symbol,
	    std::string_view   fn_callable_symbol
	);

	/// Emit code to perform a direct system call (i.e. with a known signature and target), assuming it is of the
	/// generic calling convention. On failure, no code is emitted and the returned result object sets `ok ==
	/// false`.
	[[nodiscard]] SystemCallEmitResult emit_direct_system_call_generic(
	    FnState&           state,
	    SystemCall         call,
	    asCScriptFunction& fn,
	    std::string_view   fn_desc_symbol,
	    std::string_view   fn_callable_symbol
	);

	/// Emits the complete handler for a stack push instruction. In case of stack pushes that are relevant to a function
	/// call, the actual stack push may be omitted and instead redirect to a temporary variable, see \ref
	/// discover_function_call_pushes.
	void emit_stack_push_ins(FnState& state, const bcins::StackPush& push);

	/// In context of the \ref discover_function_call_pushes optimization, if we failed to emit a direct call, whatever
	/// fallback option we use will be manipulating arguments via the stack. Thus, this pushes all the push elimination
	/// candidates back to stack for the current call instruction.
	void flush_stack_push_optimization(FnState& state);

	void emit_stack_push(FnState& state, std::string_view expr, VarType type);

	void emit_assign_ins(FnState& state, std::string_view dst, std::string_view src);

	/// Emits a conditional branch: If `expr` is true then jump to the specified bytecode offset, otherwise continue.
	void emit_cond_branch(FnState& state, std::string_view expr, std::size_t target_offset);

	/// Emits the handler for a compare instruction between a variable on the stack and the result of an expression
	/// (whether integral or floating-point).
	/// - If lhs == rhs => *valueRegister =  0
	/// - If lhs < rhs  => *valueRegister = -1
	/// - If lhs > rhs  => *valueRegister =  1
	void emit_compare(FnState& state, const bcins::Compare& compare);

	/// Emits the complete handler for a test instruction.
	/// Writes the boolean result of `valueRegister {op} 0` to `valueRegister`.
	void emit_test_ins(FnState& state, std::string_view op_with_rhs_0);

	/// Emits the complete handler for a primitive cast of a variable on the stack to another.
	/// Automatically determines whether the instruction takes two arguments (source and destination), or whether the
	/// operation occurs in place in the same variable location.
	void emit_primitive_cast_var_ins(FnState& state, VarType src, VarType dst);

	/// Emits the complete handler for an in-place prefix operation on the valueRegister, that is,
	/// `{op}valueRegister` (`op` normally being either `++` or `--`).
	void emit_prefixop_valuereg_ins(FnState& state, std::string_view op, VarType type);

	/// Emits the complete handler for an in-place unary operation on a variable on the stack, that is,
	/// `var = {op} var`.
	void emit_unop_var_inplace_ins(FnState& state, std::string_view op, VarType type);

	/// Emits the complete handler for a binary operation between two variables on the stack, outputting to a third
	/// one, that is, `result = lhs {op} rhs`.
	void emit_binop_var_var_ins(FnState& state, std::string_view op, VarType lhs, VarType rhs, VarType dst);

	/// Emits the complete handler for a binary operation between a variable on the stack and an immediate value,
	/// outputting to another variable, that is, `result = lhs {op} (rhs_expr)`.
	void
	emit_binop_var_imm_ins(FnState& state, std::string_view op, VarType lhs, std::string_view rhs_expr, VarType dst);

	/// Emits the complete handler for a division or modulus operation (where `op` is one of the `ASEA_FDIV`/`FMOD`
	/// macros) between two float variables on the stack, outputting to a third one. This is handled separately from
	/// regular binop because these instructions can raise exceptions.
	void emit_divmod_var_float_ins(FnState& state, std::string_view op, VarType type);

	/// Emits the complete handler for a division or modulus operation (where `op` is `/` or `%`) between two integral
	/// variables on the stack, outputting to a third one. This is handled separately from regular binop because these
	/// instructions can raise exceptions.
	/// The `lhs_overflow_value` represents the value that should be checked for to match AS exception behaviour: If
	/// divider == -1 && lhs == lhs_overflow_value, then a division overflow exception will be raised.
	void emit_divmod_var_int_ins(FnState& state, std::string_view op, std::uint64_t lhs_overflow_value, VarType type);

	/// Emits the complete handler for a division or modulus operation (where `op` is `/` or `%`) between two unsigned
	/// integral variables on the stack, outputting to a third one. Equivalent to `emit_divmod_var_int_ins`, except
	/// there is no `lhs_overflow_value` logic.
	void emit_divmod_var_unsigned_ins(FnState& state, std::string_view op, VarType type);

	std::string frame_ptr(std::string_view expr);
	std::string frame_ptr(int offset);

	std::string frame_var(std::string_view expr, VarType type);
	std::string frame_var(int offset, VarType type);

	std::string stack_var(int offset, VarType type);

	const JitConfig* m_config;
	asCScriptEngine* m_script_engine;
	std::string      m_c_symbol_prefix;

	OnMapFunctionCallback m_on_map_function_callback;
	OnMapExternCallback   m_on_map_extern_callback;

	/// State for the current `prepare_new_context` context.
	struct ModuleState {
		TranspiledBlocks code_blocks         = {};
		std::size_t      fallback_count      = 0;
		std::size_t      string_constant_idx = 0;
		std::size_t      type_info_idx       = 0;
		std::size_t      fn_idx              = 0;
		std::string      fn_name;
		std::string      fn_bytecode_ptr;
	};
	ModuleState m_module_state;

	std::size_t m_module_idx;

	/// Make a local variable using some operand, where `value` is either a known operand type or a variant of known
	/// operand types. Returns the type of the created variable.
	template<class T> VarType make_local_from_operand(FnState& state, std::string_view name, const T& operand);
};

template<class T>
inline VarType BytecodeToC::make_local_from_operand(FnState& state, std::string_view name, const T& operand) {
	const auto visitor = overloaded{
	    [&]<typename U>(const operands::Immediate<U>& v)
	        requires std::is_integral_v<U>
	    {
		    const auto type = v.get_type();
		    emit(
		        "\t\t{TYPE} {NAME} = {VALUE};\n",
		        fmt::arg("TYPE", type.c),
		        fmt::arg("NAME", name),
		        fmt::arg("VALUE", imm_int(v.value, type))
		    );
		    return type;
	    },
	    [&](const operands::Immediate<float>& v) {
		    emit(
		        "\t\tasea_i2f_inst.i = {VALUE};\n"
		        "\t\tfloat {NAME} = asea_i2f_inst.f;\n",
		        fmt::arg("NAME", name),
		        fmt::arg("VALUE", imm_int(std::bit_cast<asDWORD>(v.value), var_types::u32))
		    );
		    return var_types::f32;
	    },
	    [&](const operands::FrameVariable& v) {
		    // FIXME: can't handle fp yet
		    emit(
		        "\t\t{TYPE} {NAME} = {VAR};\n",
		        fmt::arg("TYPE", v.type.c),
		        fmt::arg("NAME", name),
		        fmt::arg("VAR", frame_var(v.idx, v.type))
		    );
		    return v.type;
	    },
	    [&](const operands::FrameVariablePointer& v) {
		    emit("\t\tvoid* {NAME} = {VAR};\n", fmt::arg("NAME", name), fmt::arg("VAR", frame_ptr(v.idx)));
		    return var_types::void_ptr;
	    },
	    [&](const operands::GlobalVariable& v) {
		    std::string symbol = emit_global_lookup(state, v.ptr, !v.can_refer_to_str);
		    if (v.dereference) {
			    emit(
			        "\t\t{TYPE} {NAME} = *({TYPE}*)&{GLOBAL};\n",
			        fmt::arg("TYPE", v.type.c),
			        fmt::arg("NAME", name),
			        fmt::arg("GLOBAL", symbol)
			    );
			    return v.type;
		    }
		    emit("\t\tvoid* {NAME} = &{GLOBAL};\n", fmt::arg("NAME", name), fmt::arg("GLOBAL", symbol));
		    return var_types::void_ptr;
	    },
	    [&](const operands::ObjectType& v) {
		    const auto objtype_symbol = emit_type_info_lookup(state, *v.ptr);
		    emit(
		        "\t\tasCObjectType* {NAME} = (asCObjectType*)&{OBJ_TYPE};\n",
		        fmt::arg("NAME", name),
		        fmt::arg("OBJ_TYPE", objtype_symbol)
		    );
		    return var_types::void_ptr;
	    },
	    [&](const operands::ValueRegister& v) {
		    // FIXME: can't handle fp yet
		    emit("\t\t{TYPE} {NAME} = value_reg;\n", fmt::arg("TYPE", v.type.c), fmt::arg("NAME", name));
		    return v.type;
	    },
	};

	// dispatch over variant or call directly
	if constexpr (requires { std::visit(visitor, operand); }) {
		return std::visit(visitor, operand);
	} else {
		return visitor(operand);
	}
}

} // namespace angelsea::detail