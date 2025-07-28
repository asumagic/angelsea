// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <span>

namespace angelsea::detail {

void walk_bytecode(std::span<asDWORD> bytecode, std::invocable<BytecodeInstruction> auto&& walker) {
	asDWORD* bytecode_current = bytecode.data();
	asDWORD* bytecode_end     = bytecode.data() + bytecode.size();

	while (bytecode_current < bytecode_end) {
		const asSBCInfo&  info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
		const std::size_t instruction_size = asBCTypeSize[info.type];

		BytecodeInstruction context{};
		context.pointer = bytecode_current;
		context.info    = &info;
		context.offset  = std::distance(bytecode.data(), bytecode_current);

		walker(context);

		bytecode_current += instruction_size;
	}
}

inline std::span<asDWORD> get_bytecode(asIScriptFunction& fn) {
	asUINT   length;
	asDWORD* bytecode = fn.GetByteCode(&length);
	return {bytecode, length};
}

} // namespace angelsea::detail
