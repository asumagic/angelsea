// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/jitcompiler.hpp>

namespace angelsea
{

class Jit final : public asIJITCompilerV2
{
    public:
    Jit(const JitConfig& config, asIScriptEngine& engine) :
        m_compiler(config, engine)
    {}

    ~Jit();

	virtual void NewFunction(asIScriptFunction* scriptFunc) override;
	virtual void CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc) override;

    void CompileModules();

    private:
    detail::JitCompiler m_compiler;
};

}