// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/debug.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <c2mir/c2mir.h>
#include <mir-gen.h>
#include <mir.h>
}

namespace angelsea::detail {

class Mir {
	public:
	explicit Mir(MIR_context_t ctx) : m_ctx{ctx} {}
	Mir();
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

class MirJit {
	public:
	MirJit(const JitConfig& config, asIScriptEngine& engine) : m_config(config), m_engine(&engine) {}

	const JitConfig& config() const { return m_config; }
	asIScriptEngine& engine() { return *m_engine; }

	void register_function(asIScriptFunction& script_function);
	void unregister_function(asIScriptFunction& script_function);

	void bind_runtime();

	bool compile_all();

	std::unordered_map<asIScriptModule*, std::vector<asIScriptFunction*>> compute_module_map();

	private:
	JitConfig        m_config;
	asIScriptEngine* m_engine;

	std::optional<Mir> m_mir;

	std::unordered_set<asIScriptFunction*> m_functions;
};

} // namespace angelsea::detail