// SPDX-License-Identifier: BSD-2-Clause

#include "benchmark.hpp"
#include "common.hpp"

TEST_CASE("brainfuck interpreter", "[megatest][bf]") { REQUIRE(run("scripts/bfint.as") == "hello world"); }

TEST_CASE("brainfuck benchmark", "[benchmark]") {
	EngineContext context;

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

	{
		auto b = default_benchmark();
		b.title("Basic brainfuck benchmark");
		b.run("Interpreter", [&] { run_bf(interp_main); });
		b.run("JIT", [&] { run_bf(jit_main); });
	}
}