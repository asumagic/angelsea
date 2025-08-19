// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <algorithm>
#include <angelscript.h>
#include <angelsea/detail/debug.hpp>
#include <array>
#include <as_objecttype.h>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>
#include <bit>
#include <cstddef>
#include <fmt/core.h>
#include <optional>
#include <string_view>
#include <variant>

namespace angelsea::detail {
/// Lightweight view to a bytecode instruction that holds offset information (as it is regularly used) and provides
/// convenient argument fetching among other metadata.
struct InsRef {
	asDWORD*         pointer;
	std::size_t      offset;
	asEBCInstr       opcode() const { return info().bc; }
	const asSBCInfo& info() const { return asBCInfo[*std::bit_cast<asBYTE*>(pointer)]; }
	std::size_t      size() const { return asBCTypeSize[info().type]; };

	asDWORD& dword0(std::size_t offset = 0) const { return asBC_DWORDARG(pointer + offset); }
	int&     int0(std::size_t offset = 0) const { return asBC_INTARG(pointer + offset); }
	asQWORD& qword0(std::size_t offset = 0) const { return asBC_QWORDARG(pointer + offset); }
	float&   float0(std::size_t offset = 0) const { return asBC_FLOATARG(pointer + offset); }
	asPWORD& pword0(std::size_t offset = 0) const { return asBC_PTRARG(pointer + offset); }
	asWORD&  word0(std::size_t offset = 0) const { return asBC_WORDARG0(pointer + offset); }
	asWORD&  word1(std::size_t offset = 0) const { return asBC_WORDARG1(pointer + offset); }
	short&   sword0(std::size_t offset = 0) const { return asBC_SWORDARG0(pointer + offset); }
	short&   sword1(std::size_t offset = 0) const { return asBC_SWORDARG1(pointer + offset); }
	short&   sword2(std::size_t offset = 0) const { return asBC_SWORDARG2(pointer + offset); }
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

namespace var_types {
static constexpr VarType s8{"asINT8", "asINT8", 1}, s16{"asINT16", "asINT16", 2}, s32{"asINT32", "asINT32", 4},
    s64{"asINT64", "asINT64", 8}, u8{"asBYTE", "asBYTE", 1}, u16{"asWORD", "asWORD", 2}, u32{"asDWORD", "asDWORD", 4},
    u64{"asQWORD", "asQWORD", 8}, pword{"asPWORD", "asPWORD", 8 /* should never be used for this type anyway */},
    void_ptr{"void*", "ptr", 8 /* same as pword */}, f32{"float", "float", 4}, f64{"double", "double", 8};
} // namespace var_types

template<typename T> inline std::string imm_int(T v, VarType type) { return fmt::format("({}){}", type.c, v); }

namespace operands {

/// Models a reference to a specific variable slot in the current function's stack frame.
struct FrameVariable {
	short   idx;
	VarType type;

	VarType get_type() const { return type; }

	bool operator==(const FrameVariable&) const = default;
};

/// Models the address of a specific variable slot in the currente function's stack frame.
struct FrameVariablePointer {
	short idx;

	static constexpr VarType get_type() { return var_types::void_ptr; }

	bool operator==(const FrameVariablePointer&) const = default;
};

/// Models a value that is directly embedded in the code.
template<class T> struct Immediate {
	T value;

	static constexpr VarType get_type() {
		// just handle the types we may be using
		if constexpr (std::is_same_v<T, asDWORD>) {
			return var_types::u32;
		}
		if constexpr (std::is_same_v<T, asQWORD>) {
			return var_types::u64;
		}
		if constexpr (std::is_same_v<T, asPWORD>) {
			return var_types::pword;
		}
		if constexpr (std::is_same_v<T, asINT32>) {
			return var_types::s32;
		}
		if constexpr (std::is_same_v<T, float>) {
			return var_types::f32;
		}
	}

	bool operator==(const Immediate<T>&) const = default;
};

/// Models a pointer to a global variable. The pointer points directly to the actual value. Figuring out what the global
/// variable is relies inspecting the script engine context.
struct GlobalVariable {
	void*   ptr;
	VarType type;
	/// Whether the operand can refer to a global string in the string pool (or if not, if it can only refer to proper
	/// global variables). Depends on the instruction.
	bool can_refer_to_str;
	/// Whether we are dereferencing the global variable, or merely taking its address.
	bool dereference; // TODO: should this just be a different operand type?

	VarType get_type() const { return dereference ? type : var_types::void_ptr; }

	bool operator==(const GlobalVariable&) const = default;
};

/// Models a reference to an object type.
struct ObjectType {
	asCObjectType* ptr;

	static constexpr VarType get_type() { return var_types::void_ptr; }

	bool operator==(const ObjectType&) const = default;
};

/// Models that the operand should be the value register interpreted as a specific type.
struct ValueRegister {
	VarType type;

	VarType get_type() const { return type; }

	bool operator==(const ValueRegister&) const = default;
};
} // namespace operands

VarType visit_operand_type(const auto& variant) {
	return std::visit([](const auto& operand) { return operand.get_type(); }, variant);
}

/// Groups semantically similar bytecode instructions under common structs with metadata
namespace bcins {
template<typename T> bool is_specific_ins(const InsRef& bc) {
	return std::find(T::valid_opcodes.begin(), T::valid_opcodes.end(), bc.opcode()) != T::valid_opcodes.end();
}

template<typename T> std::optional<T> try_as(const InsRef& ins) {
	if (is_specific_ins<T>(ins)) {
		return T{ins};
	}
	return {};
}

/// Conditional or unconditional jump instruction, excluding switches.
struct Jump : InsRef {
	static constexpr std::array valid_opcodes
	    = {asBC_JMP, asBC_JZ, asBC_JLowZ, asBC_JNZ, asBC_JLowNZ, asBC_JS, asBC_JNS, asBC_JP, asBC_JNP};
	explicit Jump(const InsRef& ins) : InsRef(ins) {
		using namespace var_types;

		angelsea_assert(is_specific_ins<Jump>(ins));

		switch (ins.info().bc) {
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

	int relative_offset() { return int0() + int(size()); }
	int target_offset() { return int(offset) + relative_offset(); }

	struct CondExpr {
		VarType          lhs_type;
		std::string_view c_comparison_op;
	};
	std::optional<CondExpr> cond_expr;
};

struct StackPush : InsRef {
	static constexpr std::array valid_opcodes
	    = {asBC_TYPEID,
	       asBC_PshC4,
	       asBC_PshV4,
	       asBC_PshG4,
	       asBC_PshC8,
	       asBC_PshV8,
	       asBC_VAR,
	       asBC_PshNull,
	       asBC_PshVPtr,
	       asBC_PshGPtr,
	       asBC_PshRPtr,
	       asBC_PSF,
	       asBC_PGA,
	       asBC_OBJTYPE};

	explicit StackPush(const InsRef& ins) : InsRef(ins) {
		using namespace var_types;
		using namespace operands;

		angelsea_assert(is_specific_ins<StackPush>(ins));

		switch (ins.opcode()) {
			// TODO: when supported
			// case asBC_FuncPtr:
			// case asBC_PshListElmnt:
		case asBC_TYPEID:
		case asBC_PshC4:   value = Immediate<asDWORD>{ins.dword0()}; break;
		case asBC_PshV4:   value = FrameVariable{ins.sword0(), u32}; break;
		case asBC_PshG4:   value = GlobalVariable{std::bit_cast<void*>(ins.pword0()), u32, true, true}; break;
		case asBC_PshC8:   value = Immediate<asQWORD>{ins.qword0()}; break;
		case asBC_PshV8:   value = FrameVariable{ins.sword0(), u64}; break;
		case asBC_VAR:     value = Immediate<asPWORD>{asPWORD(ins.sword0())}; break;
		case asBC_PshNull: value = Immediate<asPWORD>{0}; break;
		case asBC_PshVPtr: value = FrameVariable{ins.sword0(), pword}; break;
		case asBC_PshGPtr: value = GlobalVariable{std::bit_cast<void*>(ins.pword0()), pword, true, true}; break;
		case asBC_PshRPtr: value = ValueRegister{pword}; break;
		case asBC_PSF:     value = FrameVariablePointer{ins.sword0()}; break;
		case asBC_PGA:     value = GlobalVariable{std::bit_cast<void*>(ins.pword0()), {}, true, false}; break;
		case asBC_OBJTYPE: value = ObjectType{std::bit_cast<asCObjectType*>(ins.pword0())}; break;
		default:           angelsea_assert(false);
		}
	}
	std::variant<
	    operands::FrameVariable,
	    operands::FrameVariablePointer,
	    operands::GlobalVariable,
	    operands::ObjectType,
	    operands::ValueRegister,
	    operands::Immediate<asDWORD>,
	    operands::Immediate<asQWORD>>
	    value;
};

/// Comparison of an integral or floating-point type, where the result is -1, 0 or 1 for `lhs < rhs`, `lhs == rhs`,
/// or `lhs > rhs` respectively.
struct Compare : InsRef {
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

	explicit Compare(const InsRef& ins) : InsRef(ins) {
		using namespace var_types;
		using namespace operands;

		angelsea_assert(is_specific_ins<Compare>(ins));

		switch (ins.opcode()) {
		case asBC_CMPi:
			lhs = FrameVariable{sword0(), s32};
			rhs = FrameVariable{sword1(), s32};
			break;
		case asBC_CMPu:
			lhs = FrameVariable{sword0(), u32};
			rhs = FrameVariable{sword1(), u32};
			break;
		case asBC_CMPi64:
			lhs = FrameVariable{sword0(), s64};
			rhs = FrameVariable{sword1(), s64};
			break;
		case asBC_CMPu64:
			lhs = FrameVariable{sword0(), u64};
			rhs = FrameVariable{sword1(), u64};
			break;
		case asBC_CmpPtr:
			lhs = FrameVariable{sword0(), pword};
			rhs = FrameVariable{sword1(), pword};
			break;
		case asBC_CMPf:
			lhs = FrameVariable{sword0(), f32};
			rhs = FrameVariable{sword1(), f32};
			break;
		case asBC_CMPd:
			lhs = FrameVariable{sword0(), f64};
			rhs = FrameVariable{sword1(), f64};
			break;
		case asBC_CMPIi:
			lhs = FrameVariable{sword0(), s32};
			rhs = Immediate<asINT32>{int0()};
			break;
		case asBC_CMPIu:
			lhs = FrameVariable{sword0(), u32};
			rhs = Immediate<asDWORD>{dword0()};
			break;
		case asBC_CMPIf:
			lhs = FrameVariable{sword0(), f32};
			rhs = Immediate<float>{std::bit_cast<float>(dword0())};
			break;
		default: break;
		}
	}

	operands::FrameVariable lhs;
	std::variant<
	    operands::FrameVariable,
	    operands::Immediate<asDWORD>,
	    operands::Immediate<asINT32>,
	    operands::Immediate<float>>
	    rhs;
};

/// System call (aka app function) to a known function or to a virtual method.
struct CallSystemDirect : InsRef {
	public:
	static constexpr std::array valid_opcodes = {asBC_CALLSYS, asBC_Thiscall1};
	explicit CallSystemDirect(const InsRef& ins) : InsRef(ins) {
		angelsea_assert(is_specific_ins<CallSystemDirect>(ins));
	}

	int                function_index() { return int0(); }
	asCScriptFunction& function(asCScriptEngine& engine) { return *engine.scriptFunctions[function_index()]; }
};

} // namespace bcins

/// Virtual instructions in angelsea
namespace virtins {

/// Conditional jump that bypasses the compare's write to the value register.
struct FusedCompareJump {
	bcins::Compare compare;
	bcins::Jump    jump;
};

/// No-op that does not do anything. Used to mask over instructions that have been fused.
struct Nop {};

} // namespace virtins

// TODO: pretty inefficient, at some point we probably want to just translate the whole bytecode to a vector of
// our own that is more compact than a vector of std::variant. Would also get rid of the virtins::Nop
using VirtualInstruction = std::variant<virtins::FusedCompareJump, virtins::Nop>;

} // namespace angelsea::detail