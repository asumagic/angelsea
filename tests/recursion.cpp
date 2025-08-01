// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("recursive fibonacci", "[fib]") {
	EngineContext context;

	out = {};

	asIScriptModule& module = context.build("build", "scripts/fib.as");
	context.prepare_execution();

	asIScriptFunction* fib = module.GetFunctionByDecl("int fib(int)");
	ANGELSEA_TEST_CHECK(fib != nullptr);

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_fib = [&](int i) -> int {
		ANGELSEA_TEST_CHECK(script_context->Prepare(fib) >= 0);
		ANGELSEA_TEST_CHECK(script_context->SetArgDWord(0, i) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
		return script_context->GetReturnDWord();
	};

	REQUIRE(run_fib(10) == 55);
	REQUIRE(run_fib(20) == 6765);
	REQUIRE(run_fib(25) == 75025);
	REQUIRE(run_fib(35) == 9227465);
}
