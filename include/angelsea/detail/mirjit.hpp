// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace angelsea::detail {

class MirJit {
	public:
	MirJit(const JitConfig& config, asIScriptEngine& engine) : m_config(config), m_engine(&engine) {}

	const JitConfig& config() const { return m_config; }
	asIScriptEngine& engine() { return *m_engine; }

	void register_function(asIScriptFunction& script_function);
	void unregister_function(asIScriptFunction& script_function);

	bool compile_all();

	std::unordered_map<asIScriptModule*, std::vector<asIScriptFunction*>> compute_module_map();

	private:
	JitConfig        m_config;
	asIScriptEngine* m_engine;

	std::unordered_set<asIScriptFunction*> m_functions;
};

} // namespace angelsea::detail