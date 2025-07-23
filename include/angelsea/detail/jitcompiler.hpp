// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>

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

    private:
    JitConfig m_config;
    asIScriptEngine* m_engine;
};

}