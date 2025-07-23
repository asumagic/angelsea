// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <cstddef>

namespace angelsea::detail
{
struct BytecodeInstruction
{
	asDWORD*         pointer;
	const asSBCInfo* info;
	std::size_t      offset;

	asDWORD& arg_dword(std::size_t offset = 0) { return asBC_DWORDARG(pointer + offset); }
	int&     arg_int(std::size_t offset = 0) { return asBC_INTARG(pointer + offset); }
	asQWORD& arg_qword(std::size_t offset = 0) { return asBC_QWORDARG(pointer + offset); }
	float&   arg_float(std::size_t offset = 0) { return asBC_FLOATARG(pointer + offset); }
	asPWORD& arg_pword(std::size_t offset = 0) { return asBC_PTRARG(pointer + offset); }
	asWORD&  arg_word0(std::size_t offset = 0) { return asBC_WORDARG0(pointer + offset); }
	asWORD&  arg_word1(std::size_t offset = 0) { return asBC_WORDARG1(pointer + offset); }
	short&   arg_sword0(std::size_t offset = 0) { return asBC_SWORDARG0(pointer + offset); }
	short&   arg_sword1(std::size_t offset = 0) { return asBC_SWORDARG1(pointer + offset); }
	short&   arg_sword2(std::size_t offset = 0) { return asBC_SWORDARG2(pointer + offset); }
};
} // namespace angelsea::detail