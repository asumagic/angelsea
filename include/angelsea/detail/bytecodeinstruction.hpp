// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <algorithm>
#include <angelscript.h>
#include <angelsea/detail/debug.hpp>
#include <array>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>
#include <cstddef>
#include <fmt/core.h>
#include <optional>
#include <string_view>
#include <variant>

namespace angelsea::detail {
struct BytecodeInstruction {
	asDWORD*         pointer;
	const asSBCInfo* info;
	std::size_t      offset;
	std::size_t      size;

	asDWORD& dword0(std::size_t offset = 0) { return asBC_DWORDARG(pointer + offset); }
	int&     int0(std::size_t offset = 0) { return asBC_INTARG(pointer + offset); }
	asQWORD& qword0(std::size_t offset = 0) { return asBC_QWORDARG(pointer + offset); }
	float&   float0(std::size_t offset = 0) { return asBC_FLOATARG(pointer + offset); }
	asPWORD& pword0(std::size_t offset = 0) { return asBC_PTRARG(pointer + offset); }
	asWORD&  word0(std::size_t offset = 0) { return asBC_WORDARG0(pointer + offset); }
	asWORD&  word1(std::size_t offset = 0) { return asBC_WORDARG1(pointer + offset); }
	short&   sword0(std::size_t offset = 0) { return asBC_SWORDARG0(pointer + offset); }
	short&   sword1(std::size_t offset = 0) { return asBC_SWORDARG1(pointer + offset); }
	short&   sword2(std::size_t offset = 0) { return asBC_SWORDARG2(pointer + offset); }
};

/// Describes the type of a value on the stack, which is useful to abstract its
/// loading and storing.
/// This is used both for operands and the destination.
struct VarType {
	/// C type name
	std::string_view c;
	/// Accessor name for asea_var
	/// FIXME: change uses of c where relevant
	std::string_view var_accessor;
	std::size_t      size = 0;

	bool operator==(const VarType& other) const { return c == other.c; };
};

template<class... Ts> struct overloaded : Ts... {
	using Ts::operator()...;
};

template<typename T> inline std::string imm_int(T v, VarType type) { return fmt::format("({}){}", type.c, v); }

namespace operands {
struct Var {
	short   idx;
	VarType type;
};

template<class T> struct Immediate {
	T value;
};
} // namespace operands

namespace var_types {
static constexpr VarType s8{"asINT8", "asINT8", 1}, s16{"asINT16", "asINT16", 2}, s32{"asINT32", "asINT32", 4},
    s64{"asINT64", "asINT64", 8}, u8{"asBYTE", "asBYTE", 1}, u16{"asWORD", "asWORD", 2}, u32{"asDWORD", "asDWORD", 4},
    u64{"asQWORD", "asQWORD", 8}, pword{"asPWORD", "asPWORD", 8 /* should never be used for this type anyway */},
    void_ptr{"void*", "ptr", 8 /* same as pword */}, f32{"float", "float", 4}, f64{"double", "double", 8};
} // namespace var_types

namespace bcins {
template<typename T> bool is_specific_ins(const BytecodeInstruction& bc) {
	return std::find(T::valid_opcodes.begin(), T::valid_opcodes.end(), bc.info->bc) != T::valid_opcodes.end();
}

template<typename T> std::optional<T> try_as(BytecodeInstruction& ins) {
	if (is_specific_ins<T>(ins)) {
		return T{ins};
	}
	return {};
}

/// Conditional or unconditional jump instruction, excluding switches.
struct Jump : BytecodeInstruction {
	public:
	static constexpr std::array valid_opcodes
	    = {asBC_JMP, asBC_JZ, asBC_JLowZ, asBC_JNZ, asBC_JLowNZ, asBC_JS, asBC_JNS, asBC_JP, asBC_JNP};
	explicit Jump(BytecodeInstruction& ins) : BytecodeInstruction(ins) {
		using namespace var_types;

		angelsea_assert(is_specific_ins<Jump>(ins));

		switch (ins.info->bc) {
		case asBC_JZ:     cond_expr = {s32, "=="}; break;
		case asBC_JLowZ:  cond_expr = {u8, "=="}; break;
		case asBC_JNZ:    cond_expr = {s32, "!="}; break;
		case asBC_JLowNZ: cond_expr = {u8, "!="}; break;
		case asBC_JS:     cond_expr = {s32, "<"}; break;
		case asBC_JNS:    cond_expr = {s32, ">="}; break;
		case asBC_JP:     cond_expr = {s32, ">"}; break;
		case asBC_JNP:    cond_expr = {s32, "<="}; break;
		default:          break;
		}
	}

	int relative_offset() { return int0() + int(size); }
	int target_offset() { return int(offset) + relative_offset(); }

	struct CondExpr {
		VarType          lhs_type;
		std::string_view c_comparison_op;
	};
	std::optional<CondExpr> cond_expr;
};

/// Comparison of an integral or floating-point type, where the result is -1, 0 or 1 for `lhs < rhs`, `lhs == rhs`, or
/// `lhs > rhs` respectively.
struct Compare : BytecodeInstruction {
	static constexpr std::array valid_opcodes
	    = {asBC_CMPi,
	       asBC_CMPu,
	       asBC_CMPi64,
	       asBC_CMPu64,
	       asBC_CmpPtr,
	       asBC_CMPf,
	       asBC_CMPd,
	       asBC_CMPIi,
	       asBC_CMPIu,
	       asBC_CMPIf};

	explicit Compare(BytecodeInstruction& ins) : BytecodeInstruction(ins) {
		using namespace var_types;

		angelsea_assert(is_specific_ins<Compare>(ins));

		switch (ins.info->bc) {
		case asBC_CMPi:
			lhs = operands::Var{sword0(), s32};
			rhs = operands::Var{sword1(), s32};
			break;
		case asBC_CMPu:
			lhs = operands::Var{sword0(), u32};
			rhs = operands::Var{sword1(), u32};
			break;
		case asBC_CMPi64:
			lhs = operands::Var{sword0(), s64};
			rhs = operands::Var{sword1(), s64};
			break;
		case asBC_CMPu64:
			lhs = operands::Var{sword0(), u64};
			rhs = operands::Var{sword1(), u64};
			break;
		case asBC_CmpPtr:
			lhs = operands::Var{sword0(), pword};
			rhs = operands::Var{sword1(), pword};
			break;
		case asBC_CMPf:
			lhs = operands::Var{sword0(), f32};
			rhs = operands::Var{sword1(), f32};
			break;
		case asBC_CMPd:
			lhs = operands::Var{sword0(), f64};
			rhs = operands::Var{sword1(), f64};
			break;
		case asBC_CMPIi:
			lhs = operands::Var{sword0(), s32};
			rhs = operands::Immediate<asINT32>{int0()};
			break;
		case asBC_CMPIu:
			lhs = operands::Var{sword0(), u32};
			rhs = operands::Immediate<asDWORD>{dword0()};
			break;
		case asBC_CMPIf:
			lhs = operands::Var{sword0(), f32};
			rhs = operands::Immediate<float>{std::bit_cast<float>(dword0())};
			break;
		default: break;
		}
	}

	operands::Var lhs;
	std::variant<operands::Var, operands::Immediate<asDWORD>, operands::Immediate<asINT32>, operands::Immediate<float>>
	    rhs;
};

/// System call (aka app function) to a known function or to a virtual method.
struct CallSystemDirect : BytecodeInstruction {
	public:
	static constexpr std::array valid_opcodes = {asBC_CALLSYS, asBC_Thiscall1};
	explicit CallSystemDirect(BytecodeInstruction& ins) : BytecodeInstruction(ins) {
		angelsea_assert(is_specific_ins<CallSystemDirect>(ins));
	}

	int                function_index() { return int0(); }
	asCScriptFunction& function(asCScriptEngine& engine) { return *engine.scriptFunctions[function_index()]; }
};

} // namespace bcins
} // namespace angelsea::detail