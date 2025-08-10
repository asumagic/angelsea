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

	Jit(const Jit&)            = delete;
	Jit& operator=(const Jit&) = delete;

	Jit(Jit&&)            = default;
	Jit& operator=(Jit&&) = default;

	~Jit() override;

	void NewFunction(asIScriptFunction* scriptFunc) override;
	void CleanFunction(asIScriptFunction* scriptFunc, asJITFunction jitFunc) override;

	using CompileFunc = void(void* ud);

	/// Configure a compilation callback for asynchronous/threaded compilation.
	/// Your callback will be called with a callable function pointer, which you must invoke with the provided void*
	/// user data parameter, wherever you see fit. This can be in a thread pool or a dedicated thread, for instance.
	///
	/// Compile times are typically not very long, but long enough to be an issue for realtime applications.
	/// When lazy compilation triggers a function compile, a compile task will be spawned. Once the compile is finished,
	/// and the main thread running the script is hitting a JitEntry again, the script function will be patched to allow
	/// jumping into the JIT.
	///
	/// For the time being, compile tasks can lock mutexes for heavy tasks, and thus block the thread for a fairly long
	/// amount of time. Hence, it is discouraged to make compile jobs happen in the same background pool as other tasks.
	void SetCompileCallback(std::function<void(CompileFunc*, void*)> callback);

	private:
	std::unique_ptr<detail::MirJit> m_compiler;
};

} // namespace angelsea