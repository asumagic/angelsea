// SPDX-License-Identifier: BSD-2-Clause

#include "angelsea/detail/mirjit.hpp"
#include <angelsea/jit.hpp>

namespace angelsea {

Jit::Jit(const JitConfig& config, asIScriptEngine& engine) :
    m_compiler(std::make_unique<detail::MirJit>(config, engine)) {}

Jit::~Jit() {}

void Jit::NewFunction(asIScriptFunction* scriptFunc) { m_compiler->register_function(*scriptFunc); }

void Jit::CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc) {
	m_compiler->unregister_function(*scriptFunc);
	std::ignore = jitFunc;
}

} // namespace angelsea