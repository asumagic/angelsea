// SPDX-License-Identifier: BSD-2-Clause

#include <angelscript.h>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/bytecodeinstruction.hpp>
#include <angelsea/detail/bytecodetools.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/log.hpp>
#include <angelsea/detail/mirjit.hpp>
#include <angelsea/detail/runtime.hpp>
#include <mir-gen.h>
#include <mir.h>
#include <string>
#include <unordered_map>

namespace angelsea::detail {

Mir::Mir(MIR_alloc_t alloc, MIR_code_alloc_t code_alloc) { m_ctx = MIR_init2(alloc, code_alloc); }
Mir::~Mir() { MIR_finish(m_ctx); }

C2Mir::C2Mir(Mir& mir) : m_ctx(mir) { c2mir_init(m_ctx); }
C2Mir::~C2Mir() { c2mir_finish(m_ctx); }

static void bind_runtime(Mir& mir) {
#define ASEA_BIND_MIR(name) MIR_load_external(mir, #name, reinterpret_cast<void*>(name))
	ASEA_BIND_MIR(asea_call_script_function);
	ASEA_BIND_MIR(asea_debug_message);
	ASEA_BIND_MIR(asea_set_internal_exception);
	ASEA_BIND_MIR(asea_fmodf);
	ASEA_BIND_MIR(asea_fmod);
#undef ASEA_BIND_MIR
}

void jit_entry_function_counter(asSVMRegisters* regs, asPWORD lazy_fn_raw) {
	auto& lazy_fn = *reinterpret_cast<LazyMirFunction*>(lazy_fn_raw);

	if (lazy_fn.hits_before_compile == 0) {
		lazy_fn.jit_engine->translate_lazy_function(lazy_fn);
		return; // let jitentry rerun in case compilation updated the jit function
	}
	--lazy_fn.hits_before_compile;

	regs->programPointer += 1 + AS_PTR_SIZE; // skip the jitentry
}

void jit_entry_await_async(asSVMRegisters* regs, asPWORD pending_fn_raw) {
	auto& lazy_fn = *reinterpret_cast<AsyncMirFunction*>(pending_fn_raw);

	if (lazy_fn.ready.load()) {
		lazy_fn.jit_engine->transfer_and_destroy(lazy_fn);
		return; // let jitentry rerun
	}

	regs->programPointer += 1 + AS_PTR_SIZE; // skip the jitentry
}

MirJit::MirJit(const JitConfig& config, asIScriptEngine& engine) :
    m_config(config), m_engine(&engine), m_mir{}, m_c_generator{m_config, *m_engine}, m_ignore_unregister{nullptr} {
	bind_runtime(m_mir);
}

MirJit::~MirJit() {
	std::unique_lock lk{m_termination_mutex};
	while (m_terminating_threads > 0) {
		m_termination_cv.wait(lk);
	}
}

void MirJit::register_function(asIScriptFunction& script_function) {
	auto [lazy_mir_it, not_already_registered] = m_lazy_functions.emplace(
	    &script_function,
	    LazyMirFunction{
	        .jit_engine          = this,
	        .script_function     = &script_function,
	        .hits_before_compile = config().triggers.hits_before_func_compile
	    }
	);

	if (!not_already_registered) {
		return;
	}

	LazyMirFunction* lazy_fn = &lazy_mir_it->second;
	setup_jit_callback(script_function, jit_entry_function_counter, lazy_fn, false);
}

void MirJit::unregister_function(asIScriptFunction& script_function) {
	if (&script_function == m_ignore_unregister) {
		return;
	}

	m_lazy_functions.erase(&script_function);
	// can't unload modules from MIR AFAIK
}

struct InputData {
	std::string* c_source;
	std::size_t  current_offset;

	InputData(std::string& source) : c_source{&source}, current_offset{0} {}
};

static int c2mir_getc_callback(void* user_data) {
	InputData& info = *static_cast<InputData*>(user_data);
	if (info.current_offset >= info.c_source->size()) {
		return EOF;
	}
	char c = (*info.c_source)[info.current_offset];
	++info.current_offset;
	return c;
}

void MirJit::translate_lazy_function(LazyMirFunction& fn) {
	std::string c_name;
	m_c_generator.set_map_function_callback([&](asIScriptFunction& received_fn, const std::string& name) {
		angelsea_assert(&received_fn == fn.script_function);
		c_name = name;
	});

	asIScriptModule* script_module = fn.script_function->GetModule();

	const char* name = script_module != nullptr ? script_module->GetName() : "<anon>";

	// TODO: b2c in thread as well
	m_c_generator.prepare_new_context();
	m_c_generator.set_map_extern_callback([&](const char*                                        c_name,
	                                          [[maybe_unused]] const BytecodeToC::ExternMapping& mapping,
	                                          void* raw_value) { MIR_load_external(m_mir, c_name, raw_value); });
	m_c_generator.translate_function(name, *fn.script_function);

	if (m_c_generator.get_fallback_count() > 0) {
		log(config(),
		    engine(),
		    LogSeverity::PERF_WARNING,
		    "Number of fallbacks for module \"{}\": {}",
		    name,
		    m_c_generator.get_fallback_count());
	}

	if (config().debug.dump_c_code) {
		angelsea_assert(config().debug.dump_c_code_file != nullptr);
		fputs(m_c_generator.source().c_str(), config().debug.dump_c_code_file);
	}

	auto [async_fn_it, success] = m_async_codegen_functions.try_emplace(fn.script_function);
	angelsea_assert(success);
	AsyncMirFunction& async_fn = async_fn_it->second;
	async_fn.jit_engine        = this;
	async_fn.script_function   = fn.script_function;
	async_fn.c_name            = c_name;
	async_fn.c_source          = m_c_generator.source();
	async_fn.pretty_name       = name;

	for (BytecodeInstruction ins : get_bytecode(*fn.script_function)) {
		if (ins.info->bc == asBC_JitEntry) {
			async_fn.jit_entry_args.emplace_back(&ins.pword0(), ins.pword0());
		}
	}

	setup_jit_callback(*fn.script_function, jit_entry_await_async, &async_fn, true);

	m_lazy_functions.erase(fn.script_function);

	{
		std::unique_lock lk{m_termination_mutex};
		++m_terminating_threads;
	}
	if (m_compile_callback) {
		m_compile_callback(
		    +[](void* ud) {
			    auto& async_fn = *reinterpret_cast<AsyncMirFunction*>(ud);
			    async_fn.jit_engine->codegen_async_function(async_fn);
		    },
		    &async_fn
		);
	} else {
		codegen_async_function(async_fn);
	}
}

void MirJit::codegen_async_function(AsyncMirFunction& fn) {
	{
		Mir&  compile_mir = fn.compiled.mir;
		C2Mir c2mir{compile_mir};

		// TODO: what the hell is clang-format doing to this
		std::array<c2mir_macro_command, 1> macros{{// Trigger the various definitions and macros of the generated header
		                                           {.def_p = true, .name = "ASEA_SUPPORT", .def = "1"}
		}};

		c2mir_options c_options{
		    .message_file       = config().debug.c2mir_diagnostic_file,
		    .debug_p            = false,
		    .verbose_p          = false,
		    .ignore_warnings_p  = false,
		    .no_prepro_p        = false,
		    .prepro_only_p      = false,
		    .syntax_only_p      = false,
		    .pedantic_p         = false, // seems to break compile..?
		    .asm_p              = false,
		    .object_p           = false,
		    .module_num         = 0, // ?
		    .prepro_output_file = nullptr,
		    .output_file_name   = nullptr,
		    .macro_commands_num = macros.size(),
		    .include_dirs_num   = 0,
		    .macro_commands     = macros.data(),
		    .include_dirs       = nullptr,
		};

		InputData input_data(fn.c_source);
		if (!c2mir_compile(
		        compile_mir,
		        &c_options,
		        c2mir_getc_callback,
		        &input_data,
		        fn.pretty_name.c_str(),
		        nullptr
		    )) {
			log(config(), engine(), LogSeverity::ERROR, "Failed to compile C for \"{}\"", fn.pretty_name.c_str());
			angelsea_assert(false); // FIXME: error handling
		}

		fn.compiled.module = DLIST_TAIL(MIR_module_t, *MIR_get_module_list(compile_mir));
		fn.ready.store(true);
	} // must destroy C2Mir before MirJit potentially gets destroyed

	std::unique_lock lk{m_termination_mutex};
	--m_terminating_threads;
	m_termination_cv.notify_one();
}

void MirJit::transfer_and_destroy(AsyncMirFunction& fn) {
	MIR_change_module_ctx(fn.compiled.mir, fn.compiled.module, m_mir);

	for (auto [ptr, arg] : fn.jit_entry_args) {
		*ptr = arg;
	}

	MIR_load_module(m_mir, fn.compiled.module);
	MIR_link(m_mir, MIR_set_lazy_bb_gen_interface, nullptr);

	MIR_item_t mir_entry_fn;
	bool       found = false;
	for (MIR_item_t mir_fn = DLIST_HEAD(MIR_item_t, fn.compiled.module->items); mir_fn != nullptr;
	     mir_fn            = DLIST_NEXT(MIR_item_t, mir_fn)) {
		if (mir_fn->item_type == MIR_func_item && std::string_view{mir_fn->u.func->name} == fn.c_name) {
			found        = true;
			mir_entry_fn = mir_fn;
			break;
		}
	}

	if (!found) {
		log(config(), engine(), LogSeverity::ERROR, "Function compile failed!");
		m_lazy_functions.erase(fn.script_function);
		return;
	}

	MIR_gen_init(m_mir);

	MIR_gen_set_debug_file(m_mir, config().debug.mir_diagnostic_file);
	MIR_gen_set_debug_level(m_mir, config().debug.mir_debug_level);

	MIR_gen_set_optimize_level(m_mir, config().mir_optimization_level);

	auto* entry_point = reinterpret_cast<asJITFunction>(MIR_gen(m_mir, mir_entry_fn));

	m_ignore_unregister = fn.script_function;
	const auto err      = fn.script_function->SetJITFunction(entry_point);
	angelsea_assert(err == asSUCCESS);
	m_ignore_unregister = nullptr;

	if (config().debug.dump_mir_code) {
		angelsea_assert(config().debug.dump_mir_code_file != nullptr);
		MIR_output(m_mir, config().debug.dump_mir_code_file);
	}

	MIR_gen_finish(m_mir);
}

void MirJit::setup_jit_callback(asIScriptFunction& function, asJITFunction callback, void* ud, bool ignore_unregister) {
	for (BytecodeInstruction ins : get_bytecode(function)) {
		if (ins.info->bc == asBC_JitEntry) {
			ins.pword0() = std::bit_cast<asPWORD>(ud);
		}
	}

	if (ignore_unregister) {
		m_ignore_unregister = &function;
	}
	function.SetJITFunction(callback);
	if (ignore_unregister) {
		m_ignore_unregister = nullptr;
	}
}

} // namespace angelsea::detail
