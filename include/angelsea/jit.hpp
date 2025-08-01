// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <memory>

namespace angelsea {

namespace detail {
class MirJit;
}

class Jit final : public asIJITCompilerV2 {
	public:
	Jit(const JitConfig& config, asIScriptEngine& engine);
	~Jit();

	virtual void NewFunction(asIScriptFunction* scriptFunc) override;
	virtual void CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc) override;

	private:
	std::unique_ptr<detail::MirJit> m_compiler;
};

} // namespace angelsea