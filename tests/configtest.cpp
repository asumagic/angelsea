// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"
#include <angelscript.h>
#include <angelsea/config.hpp>
#include <angelsea/fnconfig.hpp>
#include <scriptbuilder/scriptbuilder.h>

TEST_CASE("per-function script config", "[config]") {
	angelsea::JitConfig config                 = get_test_jit_config();
	config.debug.allow_function_metadata_debug = true; // if you want to test dump_c
	config.triggers.eager                      = false;
	config.triggers.hits_before_func_compile   = 0;

	EngineContext  context(config);
	CScriptBuilder builder;

	context.jit.SetFnConfigRequestCallback(
	    [&](asIScriptFunction& fn) {
		    angelsea::FnConfig fn_config;

		    for (const auto& meta : builder.GetMetadataForFunc(&fn)) {
			    constexpr std::string_view prefix = "jit::";
			    if (!meta.starts_with(prefix)) {
				    continue;
			    }
			    const auto attribute = std::string_view{meta}.substr(prefix.size());
			    parse_function_metadata(fn_config, attribute);
		    }
		    return fn_config;
	    },
	    true
	);
	out = {};

	builder.StartNewModule(context.engine, "build");
	builder.AddSectionFromMemory("str", "[jit::disable_jit] void main() { print(':3'); }");
	builder.BuildModule();
	context.jit.DiscoverFnConfig();
	context.jit.SetFnConfigRequestCallback({}, false);

	context.run(*context.engine->GetModule("build"), "void main()", asEXECUTION_FINISHED);
}