// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/jit.hpp>

namespace angelsea {

void Jit::NewFunction(asIScriptFunction* scriptFunc) { m_compiler.register_function(*scriptFunc); }

void Jit::CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc) {
	m_compiler.unregister_function(*scriptFunc);
	std::ignore = jitFunc;
}

bool Jit::CompileModules() { return m_compiler.compile_all(); }

Jit::~Jit() {}

} // namespace angelsea