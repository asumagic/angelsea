// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <angelsea/detail/debug.hpp>
#include <span>

namespace angelsea::detail {

class BytecodeView {
	public:
	class Iterator {
		public:
		using difference_type = std::ptrdiff_t;
		using element_type    = BytecodeInstruction;

		Iterator() { angelsea_assert(false); }
		element_type operator*() const {
			const asSBCInfo&  info             = asBCInfo[*reinterpret_cast<const asBYTE*>(bytecode_current)];
			const std::size_t instruction_size = asBCTypeSize[info.type];

			return BytecodeInstruction{
			    .pointer = bytecode_current,
			    .info    = &info,
			    .offset  = std::size_t(std::distance(bytecode_start, bytecode_current)),
			    .size    = instruction_size,
			};
		}
		Iterator& operator++() {
			bytecode_current += (**this).size;
			return *this;
		}
		Iterator operator++(int) {
			auto tmp = *this;
			++(*this);
			return tmp;
		}
		auto operator<=>(const Iterator&) const = default;

		private:
		friend class BytecodeView;
		Iterator(asDWORD* bytecode_start, asDWORD* bytecode_current) :
		    bytecode_start(bytecode_start), bytecode_current(bytecode_current) {}

		asDWORD *bytecode_start, *bytecode_current;
	};

	BytecodeView(std::span<asDWORD> bytecode) : m_bytecode(bytecode) {}
	std::span<asDWORD> span() { return m_bytecode; }

	auto begin() { return Iterator{m_bytecode.data(), m_bytecode.data()}; }
	auto end() { return Iterator{m_bytecode.data(), m_bytecode.data() + m_bytecode.size()}; }

	private:
	static_assert(std::forward_iterator<Iterator>);
	std::span<asDWORD> m_bytecode;
};

inline BytecodeView get_bytecode(asIScriptFunction& fn) {
	asUINT   length;
	asDWORD* bytecode = fn.GetByteCode(&length);
	return {std::span{bytecode, length}};
}

} // namespace angelsea::detail
