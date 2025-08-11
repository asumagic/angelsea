// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <algorithm>
#include <angelscript.h>
#include <angelsea/detail/debug.hpp>
#include <array>
#include <as_scriptengine.h>
#include <as_scriptfunction.h>
#include <cstddef>
#include <optional>

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

struct Jump : BytecodeInstruction {
	public:
	static constexpr std::array valid_opcodes
	    = {asBC_JMP, asBC_JZ, asBC_JLowZ, asBC_JNZ, asBC_JLowNZ, asBC_JS, asBC_JNS, asBC_JP, asBC_JNP};
	Jump(BytecodeInstruction& ins) : BytecodeInstruction(ins) { // NOLINT
		angelsea_assert(is_specific_ins<Jump>(ins));
	}

	int relative_offset() { return int0() + int(size); }
	int target_offset() { return int(offset) + relative_offset(); }
};

struct CallSystemDirect : BytecodeInstruction {
	public:
	static constexpr std::array valid_opcodes = {asBC_CALLSYS, asBC_Thiscall1};
	CallSystemDirect(BytecodeInstruction& ins) : BytecodeInstruction(ins) { // NOLINT
		angelsea_assert(is_specific_ins<Jump>(ins));
	}

	int                function_index() { return int0(); }
	asCScriptFunction& function(asCScriptEngine& engine) { return *engine.scriptFunctions[function_index()]; }
};

} // namespace bcins
} // namespace angelsea::detail