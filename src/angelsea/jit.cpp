// SPDX-License-Identifier: BSD-2-Clause

#include "angelsea/detail/mirjit.hpp"
#include <angelsea/jit.hpp>

namespace angelsea {

Jit::Jit(const JitConfig& config, asIScriptEngine& engine) :
    m_compiler(std::make_unique<detail::MirJit>(config, engine)) {}

Jit::~Jit() {} // NOLINT: we'd rather not force the user to see the detail/ TUs, so for unique_ptr purposes we need the
               // destructor in this TU

void Jit::NewFunction(asIScriptFunction* scriptFunc) { m_compiler->register_function(*scriptFunc); }

void Jit::CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc) {
	m_compiler->unregister_function(*scriptFunc);
	std::ignore = jitFunc;
}

void Jit::SetCompileCallback(std::function<void(CompileFunc*, void*)> callback) {
	m_compiler->set_compile_callback(std::move(callback));
}

} // namespace angelsea