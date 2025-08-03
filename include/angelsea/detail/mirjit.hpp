// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/debug.hpp>
#include <span>
#include <unordered_map>

extern "C" {
#include <c2mir/c2mir.h>
#include <mir-alloc.h>
#include <mir-code-alloc.h>
#include <mir-gen.h>
#include <mir.h>
}

namespace angelsea::detail {

class Mir {
	public:
	explicit Mir(MIR_context_t ctx) : m_ctx{ctx} {}
	Mir(MIR_alloc_t alloc = nullptr, MIR_code_alloc_t code_alloc = nullptr);
	~Mir();

	Mir(const Mir&)            = delete;
	Mir& operator=(const Mir&) = delete;

	operator MIR_context_t() { return m_ctx; }

	private:
	MIR_context_t m_ctx;
};

class C2Mir {
	public:
	C2Mir(Mir& mir);
	~C2Mir();

	C2Mir(const C2Mir&)            = delete;
	C2Mir& operator=(const C2Mir&) = delete;

	private:
	MIR_context_t m_ctx;
};

class MirJit;

struct LazyMirFunction {
	MirJit*            jit_engine;
	asIScriptFunction* script_function;
	std::size_t        hits_before_compile;

	void hit();
};

class MirJit {
	public:
	MirJit(const JitConfig& config, asIScriptEngine& engine);
	// TODO: unregister all methods on shutdown, or provide a method to do so at least

	MirJit(const MirJit&)            = delete;
	MirJit& operator=(const MirJit&) = delete;

	const JitConfig& config() const { return m_config; }
	asIScriptEngine& engine() { return *m_engine; }

	void register_function(asIScriptFunction& script_function);
	void unregister_function(asIScriptFunction& script_function);

	void compile_lazy_function(LazyMirFunction& fn);

	private:
	void               bind_runtime();
	[[nodiscard]] bool compile_c_to_mir(BytecodeToC& c_generator);
	[[nodiscard]] bool compile_c_module(
	    BytecodeToC&                  c_generator,
	    c2mir_options&                c_options,
	    const char*                   internal_module_name,
	    asIScriptModule*              script_module,
	    std::span<asIScriptFunction*> functions
	);

	JitConfig        m_config;
	asIScriptEngine* m_engine;

	Mir m_mir;

	BytecodeToC m_c_generator;

	std::unordered_map<asIScriptFunction*, LazyMirFunction> m_lazy_codegen_functions;

	/// slight hack: when we SetJITFunction, AS calls our CleanFunction; but we do *not* want this to happen, because we
	/// use several temporary JIT functions, and we don't want to destroy any of our references to it during that time!
	asIScriptFunction* m_ignore_unregister;

	// std::unordered_map<asIScriptFunction*, MirFunction> m_jit_functions;
};

} // namespace angelsea::detail