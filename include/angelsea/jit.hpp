// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>

namespace angelsea
{

struct JitConfig
{
};

class JitCompiler : public asIJITCompiler
{
    public:
    JitCompiler(const JitConfig& config = {}) :
        m_config(config)
    {}

	virtual int  CompileFunction(asIScriptFunction* function, asJITFunction* output) override;
	virtual void ReleaseJITFunction(asJITFunction func) override;

    private:
    JitConfig m_config;
};

}