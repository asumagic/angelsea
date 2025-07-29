// SPDX-License-Identifier: BSD-2-Clause

#include "mir-gen.h"
#include "mir.h"
#include <angelscript.h>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/log.hpp>
#include <angelsea/detail/mirjit.hpp>
#include <angelsea/detail/runtime.hpp>
#include <string>
#include <unordered_map>

namespace angelsea::detail {

Mir::Mir() { m_ctx = MIR_init(); }
Mir::~Mir() { MIR_finish(m_ctx); }

C2Mir::C2Mir(Mir& mir) : m_ctx(mir) { c2mir_init(m_ctx); }
C2Mir::~C2Mir() { c2mir_finish(m_ctx); }

void jit_release_workaround(asSVMRegisters* regs, asPWORD) { regs->programPointer += 1 + AS_PTR_SIZE; }

void MirJit::register_function(asIScriptFunction& script_function) {
	m_functions.emplace(&script_function);
	script_function.SetJITFunction(static_cast<asJITFunction>(jit_release_workaround));
}

void MirJit::unregister_function(asIScriptFunction& script_function) {
	const auto it = m_functions.find(&script_function);
	if (it != m_functions.end()) {
		m_functions.erase(it);
	}
}

bool MirJit::compile_all() {
	detail::log(
	    config(),
	    engine(),
	    LogSeverity::VERBOSE,
	    "Requesting compilation for {} functions",
	    m_functions.size()
	);

	bool success = true;

	// TODO: do away with the unordered_map for this lookup; we could get
	// sufficiently deterministic names by querying the module and function IDs
	// from AngelScript

	std::unordered_map<std::string, asIScriptFunction*> c_name_to_func;

	BytecodeToC c_generator{config(), engine()};
	c_generator.set_map_function_callback([&](asIScriptFunction& fn, const std::string& name) {
		c_name_to_func.emplace(name, &fn);
	});

	MIR_gen_init(m_mir);

	success &= compile_c_to_mir(c_generator);

	MIR_gen_set_debug_file(m_mir, config().mir_diagnostic_file);
	MIR_gen_set_debug_level(m_mir, config().mir_debug_level);

	MIR_gen_set_optimize_level(m_mir, config().mir_optimization_level);

	bind_runtime();
	success &= link_compiled_functions(c_name_to_func);

	if (config().dump_mir_code) {
		angelsea_assert(config().dump_mir_code_file != nullptr);
		MIR_output(m_mir, config().dump_mir_code_file);
	}

	MIR_gen_finish(m_mir);

	return success;
}

void MirJit::bind_runtime() {}

bool MirJit::compile_c_to_mir(BytecodeToC& c_generator) {
	bool success = true;

	C2Mir c2mir{m_mir};

	// no include dir
	std::array<const char*, 1> include_dirs{nullptr};

	// TODO: what the hell is clang-format doing to this
	std::array<c2mir_macro_command, 1> macros{{// Trigger the various definitions and macros of the generated header
	                                           {.def_p = true, .name = "ANGELSEA_SUPPORT", .def = "1"}
	}};

	c2mir_options c_options{
	    .message_file       = config().c2mir_diagnostic_file,
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
	    .macro_commands     = macros.data(),
	    .include_dirs       = include_dirs.data()
	};

	auto modules = compute_module_map();

	// NOTE: I don't think there is fundamentally something that makes it
	// impossible or impractical to generate all modules within the same C
	// source file other than maybe compile time overhead.
	// bytecode2c was written explicitly not to care -- simply don't call
	// `prepare_new_context`, and you should be able to emit multiple modules at
	// once.
	// Not sure if MIR maybe does some form of link-time optimization of its own
	// (see MIR_link lazy generation), but this would be relevant only once we
	// can natively do calls across JIT script functions anyway...

	// TODO: Investigate if partial recompiles work, and make them more
	// efficient

	// TODO: Freeing functions

	for (auto& [script_module, functions] : modules) {
		if (script_module == nullptr) {
			// in the "anonymous" module?
			for (std::size_t i = 0; i < functions.size(); ++i) {
				// convert each function as their own virtual module
				// TODO: is this useful? should reconsider
				success
				    &= compile_c_module(c_generator, c_options, "<anon>", nullptr, std::span{functions}.subspan(i, 1));
			}
		} else {
			success &= compile_c_module(
			    c_generator,
			    c_options,
			    script_module->GetName(),
			    script_module,
			    std::span{functions}
			);
		}
	}

	return success;
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

	c_generator.prepare_new_context();
	c_generator.set_map_extern_callback([&](const char*                       c_name,
	                                        const BytecodeToC::ExternMapping& mapping,
	                                        void* raw_value) { MIR_load_external(m_mir, c_name, raw_value); });
	c_generator.translate_module(internal_module_name, script_module, functions);

	if (config().dump_c_code) {
		angelsea_assert(config().dump_c_code_file != nullptr);
		fputs(c_generator.source().c_str(), config().dump_c_code_file);
	}

	InputData input_data(c_generator.source());
	if (!c2mir_compile(m_mir, &c_options, c2mir_getc_callback, &input_data, internal_module_name, nullptr)) {
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

	return true;
}

bool MirJit::link_compiled_functions(const std::unordered_map<std::string, asIScriptFunction*>& c_name_to_func) {
	bool success = true;

	for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(m_mir)); module != nullptr;
	     module              = DLIST_NEXT(MIR_module_t, module)) {
		MIR_load_module(m_mir, module);

		void (*jit_interface)(MIR_context_t ctx, MIR_item_t item);
		switch (config().mir_compilation_mode) {
		case JitConfig::MirCompilationMode::Lazy: jit_interface = MIR_set_lazy_gen_interface; break;
		case JitConfig::MirCompilationMode::LazyBB: jit_interface = MIR_set_lazy_bb_gen_interface; break;
		case JitConfig::MirCompilationMode::Normal:
		default: jit_interface = MIR_set_gen_interface;
		}
		MIR_link(m_mir, jit_interface, nullptr);

		for (MIR_item_t mir_func = DLIST_HEAD(MIR_item_t, module->items); mir_func != nullptr;
		     mir_func            = DLIST_NEXT(MIR_item_t, mir_func)) {
			if (mir_func->item_type != MIR_func_item) {
				continue;
			}

			const auto symbol = std::string_view{mir_func->u.func->name};

			// TODO: pointless string allocation but heterogeneous lookup is
			// annoying in C++ unordered_map
			auto it = c_name_to_func.find(std::string{symbol});
			if (it == c_name_to_func.end()) {
				continue;
			}

			asIScriptFunction& fn = *it->second;

			auto* entry_point = reinterpret_cast<asJITFunction>(MIR_gen(m_mir, mir_func));

			log(config(),
			    engine(),
			    fn,
			    LogSeverity::VERBOSE,
			    "Hooking function `{}` as `{}`!",
			    fn.GetDeclaration(true, true, true),
			    fmt::ptr(entry_point));

			if (entry_point == nullptr) {
				log(config(),
				    engine(),
				    fn,
				    LogSeverity::ERROR,
				    "Failed to compile function `{}`",
				    fn.GetDeclaration(true, true, true));

				success = false;
			}

			const auto err = fn.SetJITFunction(entry_point);
			angelsea_assert(err == asSUCCESS);
		}
	}

	return success;
}

std::unordered_map<asIScriptModule*, std::vector<asIScriptFunction*>> MirJit::compute_module_map() {
	std::unordered_map<asIScriptModule*, std::vector<asIScriptFunction*>> ret;

	for (auto& fn : m_functions) {
		ret[fn->GetModule()].push_back(fn);
	}

	return ret;
}

} // namespace angelsea::detail
