// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("brainfuck interpreter", "[megatest][bf]") { REQUIRE(run("scripts/bfint.as") == "hello world"); }

TEST_CASE("brainfuck benchmark", "[benchmark]") {
	EngineContext context{angelsea::JitConfig{.log_targets{.performance_warning = asEMsgType(-1)}}};

	out = {};

	asIScriptModule& jit_module = context.build("build", "scripts/bfint.as");
	context.prepare_execution();

	asIScriptFunction* jit_main = jit_module.GetFunctionByDecl("void bench()");
	ANGELSEA_TEST_CHECK(jit_main != nullptr);

	context.engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, false);

	asIScriptModule&   interp_module = context.build("build-nojit", "scripts/bfint.as");
	asIScriptFunction* interp_main   = interp_module.GetFunctionByDecl("void bench()");
	ANGELSEA_TEST_CHECK(interp_main != nullptr);

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_bf = [&](asIScriptFunction* fn) {
		ANGELSEA_TEST_CHECK(script_context->Prepare(fn) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
	};

	BENCHMARK("Interpreter brainfuck hello") { run_bf(interp_main); };
	BENCHMARK("JIT         brainfuck hello") { run_bf(jit_main); };
}