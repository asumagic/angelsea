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
} // namespace angelsea::detail