// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <functional>
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

	using CompileFunc = void(void* ud);

	/// Configure a compilation callback for asynchronous/threaded compilation.
	/// Your callback will be called with a callable function pointer, which you must invoke with the provided void*
	/// user data parameter, wherever you see fit. This can be in a thread pool or a dedicated thread, for instance.
	void SetCompileCallback(std::function<void(CompileFunc*, void*)> callback);

	private:
	std::unique_ptr<detail::MirJit> m_compiler;
};

} // namespace angelsea