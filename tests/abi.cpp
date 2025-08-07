// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

#include <as_datatype.h>
#include <as_generic.h>

void generic_noarg(asIScriptGeneric* gen) {
	// asCGeneric* g = static_cast<asCGeneric*>(gen);
	gen->SetReturnDWord(123);
}

void generic_noarg_hack(asIScriptGeneric* gen) {
	asCGeneric* g = static_cast<asCGeneric*>(gen);
	g->returnVal  = 123;
}

void bind_generic_functions(asIScriptEngine& e) {
	e.RegisterGlobalFunction("int generic_noarg()", asFUNCTION(generic_noarg_hack), asCALL_GENERIC);
}

TEST_CASE("generic calling convention", "[abi][conv_generic]") {
	EngineContext context;
	bind_generic_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+generic_noarg())") == "123\n");
}

TEST_CASE("generic abi benchmark", "[abi][conv_generic][benchmark]") {
	EngineContext context{angelsea::JitConfig{.log_targets{.performance_warning = asEMsgType(-1)}}};
	bind_generic_functions(*context.engine);

	out = {};

	auto get_bench = [](asIScriptModule& module, const char* decl) -> asIScriptFunction& {
		asIScriptFunction* fn = module.GetFunctionByDecl(decl);
		ANGELSEA_TEST_CHECK(fn != nullptr);
		return *fn;
	};

	asIScriptModule&   jit_module        = context.build("build", "scripts/abi.as");
	asIScriptFunction& jit_million_bench = get_bench(jit_module, "void benchmark_1M_calls_generic_noarg()");
	context.prepare_execution();

	context.engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, false);
	asIScriptModule&   interp_module        = context.build("build-nojit", "scripts/abi.as");
	asIScriptFunction& interp_million_bench = get_bench(interp_module, "void benchmark_1M_calls_generic_noarg()");

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_fn = [&](asIScriptFunction& fn) -> int {
		ANGELSEA_TEST_CHECK(script_context->Prepare(&fn) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
		return script_context->GetReturnDWord();
	};

	BENCHMARK("JIT    1M generic `int()`") { return run_fn(jit_million_bench); };
	BENCHMARK("Interp 1M generic `int()`") { return run_fn(interp_million_bench); };
}