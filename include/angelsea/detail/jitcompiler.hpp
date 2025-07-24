// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/jitfunction.hpp>
#include <unordered_map>
#include <vector>

namespace angelsea::detail
{

class JitCompiler
{
    public:
    JitCompiler(const JitConfig& config, asIScriptEngine& engine) :
        m_config(config),
        m_engine(&engine)
    {}

    const JitConfig& config() const { return m_config; }
    asIScriptEngine& engine() { return *m_engine; }

    void register_function(asIScriptFunction& script_function);
    void unregister_function(asIScriptFunction& script_function);

    void compile_all();

    JitFunction* get_jit_function(asIScriptFunction& module);
    JitFunction& get_or_create_jit_function(asIScriptFunction& module);

    std::unordered_map<asIScriptModule*, std::vector<JitFunction*>> compute_module_map();

    private:
    JitConfig m_config;
    asIScriptEngine* m_engine;

    std::unordered_map<asIScriptFunction*, JitFunction> m_functions;
};

}