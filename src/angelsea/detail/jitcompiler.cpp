// SPDX-License-Identifier: BSD-2-Clause

#include "angelscript.h"
extern "C"
{
#include <c2mir/c2mir.h>
#include <mir.h>
#include <mir-gen.h>
}

#include <angelsea/detail/jitcompiler.hpp>
#include <angelsea/detail/log.hpp>
#include <angelsea/detail/debug.hpp>
#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/runtime.hpp>
#include <string>
#include <unordered_map>

namespace angelsea::detail
{

void JitCompiler::register_function(asIScriptFunction& script_function)
{
    JitFunction& jit_function = get_or_create_jit_function(script_function);
}

void JitCompiler::unregister_function(asIScriptFunction& script_function)
{
    const auto it = m_functions.find(&script_function);
    angelsea_assert(it != m_functions.end());
    m_functions.erase(it);
}

struct InputData
{
    std::string* c_source;
    std::size_t current_offset;

    InputData(std::string& source) : c_source{&source}, current_offset{0} {}
};

void JitCompiler::compile_all()
{
    detail::log(*this, LogSeverity::VERBOSE, "Requesting compilation for {} functions", m_functions.size());

    MIR_context_t mir = MIR_init();
    c2mir_init(mir);

    // no include dir
    std::array<const char*, 2> include_dirs {nullptr};

    std::array<c2mir_macro_command, 1> macros {{
        {.def_p = true, .name = "ANGELSEA_SUPPORT", .def = "1"}
    }};

    c2mir_options c_options {
        .message_file = stderr, // TODO: optional
        .debug_p = false,
        .verbose_p = false,
        .ignore_warnings_p = false,
        .no_prepro_p = false,
        .prepro_only_p = false,
        .syntax_only_p = false,
        .pedantic_p = false, // seems to break compile..?
        .asm_p = false,
        .object_p = false,
        .module_num = 0, // ?
        .prepro_output_file = nullptr,
        .output_file_name = nullptr,
        .macro_commands_num = macros.size(),
        .macro_commands = macros.data(),
        .include_dirs = include_dirs.data()
    };

    std::unordered_map<std::string, JitFunction*> c_name_to_func;

    BytecodeToC c_generator{*this};
    c_generator.set_map_function_callback([&](JitFunction& function, const std::string& name) {
        c_name_to_func.emplace(name, &function);
    });

    auto modules = compute_module_map();

    const auto getc_callback = +[](void* user_data) -> int {
        InputData& info = *static_cast<InputData*>(user_data);
        if (info.current_offset >= info.c_source->size())
        {
            return EOF;
        }
        char c = (*info.c_source)[info.current_offset];
        ++info.current_offset;
        return c;
    };

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

    const auto compile_module = [&](
        const char* internal_module_name,
        asIScriptModule* script_module,
        std::span<JitFunction*> functions
    ) {
        c_generator.prepare_new_context();
        c_generator.translate_module(
            internal_module_name,
            script_module,
            functions
        );

        // fmt::print(stderr, "Translated function:\n{}", c_generator.source());

        InputData input_data(c_generator.source());
        if (!c2mir_compile(mir, &c_options, getc_callback, &input_data, internal_module_name, nullptr))
        {
            log(*this, LogSeverity::ERROR, "Failed to compile translated C module \"{}\"", script_module->GetName());
        }

        if (c_generator.get_fallback_count() > 0)
        {
            log(*this, LogSeverity::PERF_WARNING, "Number of fallbacks for module \"{}\": {}", script_module->GetName(), c_generator.get_fallback_count());
        }
    };

    for (auto& [script_module, functions] : modules)
    {
        if (script_module == nullptr)
        {
            // in the "anonymous" module?
            for (std::size_t i = 0; i < functions.size(); ++i)
            {
                // convert each function as their own virtual module
                // TODO: is this useful? should reconsider
                compile_module("<anon>", nullptr, std::span{functions}.subspan(i, 1));
            }
        }
        else
        {
            compile_module(script_module->GetName(), script_module, std::span{functions});
        }
    }

    MIR_gen_init(mir);
    // MIR_gen_set_debug_file(mir, stdout);
    // MIR_gen_set_debug_level(mir, 10);

    // TODO: expose as an option
    MIR_gen_set_optimize_level(mir, 3);

    c2mir_finish(mir);

    // TODO: move to its own bind_runtime function, or use the link interface
    MIR_load_external(mir, "asea_call_script_function", reinterpret_cast<void*>(asea_call_script_function));

    // lookup functions
    for (
        MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(mir));
        module != nullptr;
        module = DLIST_NEXT(MIR_module_t, module)
    ) {
        MIR_load_module(mir, module);

        // TODO: investigate lazy gen options, and exposing interpretation instead
        MIR_link(mir, MIR_set_lazy_gen_interface, nullptr);

        for (
            MIR_item_t mir_func = DLIST_HEAD (MIR_item_t, module->items);
            mir_func != nullptr;
            mir_func = DLIST_NEXT (MIR_item_t, mir_func)
        ) {
            if (mir_func->item_type != MIR_func_item)
            {
                continue;
            }

            const auto symbol = std::string_view{mir_func->u.func->name};

            // TODO: pointless string allocation but heterogeneous lookup is
            // annoying in C++ unordered_map
            auto it = c_name_to_func.find(mir_func->u.func->name);
            if (it != c_name_to_func.end())
            {
                JitFunction& jit_function = *it->second;

                auto* entry_point = reinterpret_cast<asJITFunction>(MIR_gen(mir, mir_func));

                log(*this, jit_function.script_function(), LogSeverity::VERBOSE, "Hooking function `{}` as `{}`!", jit_function.script_function().GetDeclaration(true, true, true), fmt::ptr(entry_point));

                if (entry_point == nullptr)
                {
                    log(*this, jit_function.script_function(), LogSeverity::ERROR, "Failed to compile function `{}`", jit_function.script_function().GetDeclaration(true, true, true));
                }

                const auto err = jit_function.script_function().SetJITFunction(entry_point);

                if (err == asNOT_SUPPORTED)
                {
                    log(*this, jit_function.script_function(), LogSeverity::ERROR, "Failed to set JIT function (asNOT_SUPPORTED), did you forget to set asEP_JIT_INTERFACE_VERSION to 2?");
                }
            }
        }
    }

    // MIR_output(mir, stderr);

    MIR_gen_finish(mir);

    // FIXME: free MIR context at destruction time
}

JitFunction* JitCompiler::get_jit_function(asIScriptFunction& function)
{
    const auto it = m_functions.find(&function);
    return (it != m_functions.end()) ? &it->second : nullptr;
}

JitFunction& JitCompiler::get_or_create_jit_function(asIScriptFunction& function)
{
    const auto it = m_functions.find(&function);

    if (it != m_functions.end())
    {
        return it->second;
    }

    // insert key &func, value JITFunction(*this, function))
    const auto [ins_it, success] = m_functions.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(&function),
        std::forward_as_tuple(*this, function)
    );
    return ins_it->second;
}

std::unordered_map<asIScriptModule*, std::vector<JitFunction*>> JitCompiler::compute_module_map()
{
    std::unordered_map<asIScriptModule*, std::vector<JitFunction*>> ret;
    
    for (auto& [script_function, function] : m_functions)
    {
        ret[script_function->GetModule()].push_back(&function);
    }

    return ret;
}

}
