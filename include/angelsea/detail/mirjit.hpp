// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/fnconfig.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <utility>

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

	Mir(Mir&& other) : m_ctx(std::exchange(other.m_ctx, nullptr)) {}
	Mir& operator=(Mir&& other) {
		std::swap(m_ctx, other.m_ctx);
		return *this;
	}

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
	MirJit*                 jit_engine;
	std::optional<FnConfig> fn_config;
	asIScriptFunction*      script_function;
	std::size_t             hits_before_compile;
};

struct AsyncMirFunction {
	MirJit*                                    jit_engine;
	asIScriptFunction*                         script_function;
	std::vector<std::pair<asPWORD*, asPWORD>>  jit_entry_args;
	std::vector<std::pair<std::string, void*>> deferred_bindings;
	std::string                                c_name;
	TranspiledCode                             c_source;
	std::string                                pretty_name;
	struct {
		std::atomic<bool> ready;
		asJITFunction     jit_function;
		MIR_module_t      module;
	} compiled;
};

class MirJit {
	public:
	MirJit(const JitConfig& config, asIScriptEngine& engine);
	~MirJit();
	// TODO: unregister all methods on shutdown, or provide a method to do so at least

	MirJit(const MirJit&)            = delete;
	MirJit& operator=(const MirJit&) = delete;

	const JitConfig& config() const { return m_config; }
	asIScriptEngine& engine() { return *m_engine; }

	void register_function(asIScriptFunction& script_function);
	void unregister_function(asIScriptFunction& script_function);

	void bind_engine_globals(asIScriptEngine& engine);

	/// Triggers C translation of a lazily-compiled function. Returns `true` if the translation was actually triggered
	/// (as it can be skipped in certain circumstances), `false` if compilation was permanently cancelled or temporarily
	/// postponed.
	[[nodiscard]] bool translate_lazy_function(LazyMirFunction& fn);
	void               codegen_async_function(AsyncMirFunction& fn);
	void               link_ready_functions();
	void               link_function(AsyncMirFunction& fn);

	/// Configure a JIT entry callback to a function, where the asPWORD arg will be equal to `ud`
	void setup_jit_callback(asIScriptFunction& function, asJITFunction callback, void* ud, bool ignore_unregister);

	using CompileFunc = void(void* ud);
	void set_compile_callback(std::function<void(CompileFunc*, void*)> callback) {
		m_compile_callback = std::move(callback);
	}

	void set_fn_config_request_callback(std::function<FnConfig(asIScriptFunction&)> callback, bool manual_discovery) {
		m_request_fn_config_callback = std::move(callback);
		m_fn_config_manual_discovery = manual_discovery;
	}

	void discover_fn_config();

	private:
	JitConfig        m_config;
	asIScriptEngine* m_engine;

	Mir        m_mir;
	std::mutex m_mir_lock;

	BytecodeToC m_c_generator;

	std::unordered_map<asIScriptFunction*, LazyMirFunction> m_lazy_functions;

	// because the AS engine may unregister a function at any time, during the time the compile thread is working, it is
	// possible that the asIScriptFunction* will be dangling and reallocating, causing a host of issues. since the
	// compile thread is not manipulating any of those structures directly, when a function being compiled is being
	// unregistered, we migrate it to the pending destructions list.
	std::unordered_map<asIScriptFunction*, std::unique_ptr<AsyncMirFunction>> m_async_codegen_functions;
	std::unordered_map<asIScriptFunction*, std::unique_ptr<AsyncMirFunction>> m_async_finished_functions;
	std::vector<std::unique_ptr<AsyncMirFunction>>                            m_async_cancelled_functions;
	std::mutex                                                                m_async_finalize_mutex;

	std::mutex              m_termination_mutex;
	std::condition_variable m_termination_cv;
	std::atomic<int>        m_terminating_threads;

	std::function<void(CompileFunc*, void*)>    m_compile_callback;
	std::function<FnConfig(asIScriptFunction&)> m_request_fn_config_callback;
	bool                                        m_fn_config_manual_discovery = false;

	/// slight hack: when we SetJITFunction, AS calls our CleanFunction; but we do *not* want this to happen, because we
	/// use several temporary JIT functions, and we don't want to destroy any of our references to it during that time!
	asIScriptFunction* m_ignore_unregister;

	bool m_registered_engine_globals;

	// std::unordered_map<asIScriptFunction*, MirFunction> m_jit_functions;
};

} // namespace angelsea::detail