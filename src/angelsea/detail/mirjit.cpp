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

void jit_entry_function_counter(asSVMRegisters* regs, asPWORD function_candidate_raw) {
	reinterpret_cast<LazyMirFunction*>(function_candidate_raw)->hit();
	regs->programPointer += 1 + AS_PTR_SIZE;
}

void LazyMirFunction::hit() {
	if (hits_before_compile == 0) {
		jit_engine->compile_lazy_function(*this); // `this` destroyed after this!
		return;
	}

	--hits_before_compile;
}

MirJit::MirJit(const JitConfig& config, asIScriptEngine& engine) :
    m_config(config), m_engine(&engine), m_mir{}, m_c_generator{m_config, *m_engine}, m_ignore_unregister{nullptr} {}

void MirJit::register_function(asIScriptFunction& script_function) {
	auto [lazy_mir_it, not_already_registered] = m_lazy_codegen_functions.emplace(
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

	// configure bytecode callbacks so that candidate->hit() will be called back on JitEntry
	for (BytecodeInstruction ins : get_bytecode(script_function)) {
		if (ins.info->bc == asBC_JitEntry) {
			ins.pword0() = std::bit_cast<asPWORD>(lazy_fn);
		}
	}

	script_function.SetJITFunction(static_cast<asJITFunction>(jit_entry_function_counter));
}

void MirJit::unregister_function(asIScriptFunction& script_function) {
	if (&script_function == m_ignore_unregister) {
		return;
	}

	m_lazy_codegen_functions.erase(&script_function);

	// TODO: deallocate module from MIR if already compiled
}

void MirJit::compile_lazy_function(LazyMirFunction& fn) {
	std::string c_name;
	m_c_generator.set_map_function_callback([&](asIScriptFunction& received_fn, const std::string& name) {
		angelsea_assert(&received_fn == fn.script_function);
		c_name = name;
	});

	// no include dir
	std::array<const char*, 1> include_dirs{nullptr};

	// TODO: what the hell is clang-format doing to this
	std::array<c2mir_macro_command, 1> macros{{// Trigger the various definitions and macros of the generated header
	                                           {.def_p = true, .name = "ASEA_SUPPORT", .def = "1"}
	}};

	asIScriptModule* script_module = fn.script_function->GetModule();

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
	    .include_dirs       = include_dirs.data(),
	};

	// TODO: error handling
	bool success = compile_c_module(
	    m_c_generator,
	    c_options,
	    script_module != nullptr ? script_module->GetName() : "<anon>",
	    script_module,
	    std::span{&fn.script_function, 1}
	);

	// TODO: break up this function
	MIR_module_t module = DLIST_TAIL(MIR_module_t, *MIR_get_module_list(m_mir));

	// load module and configure linking
	MIR_load_module(m_mir, module);

	// TODO: lazy runtime lookup
	bind_runtime();

	MIR_link(m_mir, MIR_set_lazy_gen_interface, nullptr);

	MIR_item_t mir_entry_fn;
	bool       found = false;
	for (MIR_item_t mir_fn = DLIST_HEAD(MIR_item_t, module->items); mir_fn != nullptr;
	     mir_fn            = DLIST_NEXT(MIR_item_t, mir_fn)) {
		if (mir_fn->item_type != MIR_func_item) {
			continue;
		}

		if (std::string_view{mir_fn->u.func->name} != c_name) {
			continue;
		}

		found        = true;
		mir_entry_fn = mir_fn;
	}

	if (!found) {
		log(config(), engine(), LogSeverity::ERROR, "Function compile failed!");
		m_lazy_codegen_functions.erase(fn.script_function);
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

	log(config(), engine(), LogSeverity::VERBOSE, "lazy step 3: {}: compiled!", fn.script_function->GetId());
	m_lazy_codegen_functions.erase(fn.script_function);
}

void MirJit::bind_runtime() {
#define ASEA_BIND_MIR(name) MIR_load_external(m_mir, #name, reinterpret_cast<void*>(name))
	ASEA_BIND_MIR(asea_call_script_function);
	ASEA_BIND_MIR(asea_debug_message);
	ASEA_BIND_MIR(asea_set_internal_exception);
	ASEA_BIND_MIR(asea_fmodf);
	ASEA_BIND_MIR(asea_fmod);
#undef ASEA_BIND_MIR
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

bool MirJit::compile_c_module(
    BytecodeToC&                  c_generator,
    c2mir_options&                c_options,
    const char*                   internal_module_name,
    asIScriptModule*              script_module,
    std::span<asIScriptFunction*> functions
) {
	Mir   compile_mir;
	C2Mir c2mir{compile_mir};

	c_generator.prepare_new_context();
	c_generator.set_map_extern_callback([&](const char*                       c_name,
	                                        const BytecodeToC::ExternMapping& mapping,
	                                        void* raw_value) { MIR_load_external(m_mir, c_name, raw_value); });
	for (asIScriptFunction* function : functions) {
		c_generator.translate_function(internal_module_name, *function);
	}

	if (config().debug.dump_c_code) {
		angelsea_assert(config().debug.dump_c_code_file != nullptr);
		fputs(c_generator.source().c_str(), config().debug.dump_c_code_file);
	}

	InputData input_data(c_generator.source());
	if (!c2mir_compile(compile_mir, &c_options, c2mir_getc_callback, &input_data, internal_module_name, nullptr)) {
		log(config(),
		    engine(),
		    LogSeverity::ERROR,
		    "Failed to compile translated C module \"{}\"",
		    internal_module_name);
		return false;
	}

	if (c_generator.get_fallback_count() > 0) {
		log(config(),
		    engine(),
		    LogSeverity::PERF_WARNING,
		    "Number of fallbacks for module \"{}\": {}",
		    internal_module_name,
		    c_generator.get_fallback_count());
	}

	MIR_change_module_ctx(compile_mir, DLIST_TAIL(MIR_module_t, *MIR_get_module_list(compile_mir)), m_mir);

	return true;
}

} // namespace angelsea::detail
