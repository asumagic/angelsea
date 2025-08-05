// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

#include <thread>

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

TEST_CASE("fib benchmark", "[fib][benchmark]") {
	EngineContext context{angelsea::JitConfig{.log_targets{.performance_warning = asEMsgType(-1)}}};

	out = {};

	asIScriptModule& jit_module = context.build("build", "scripts/fib.as");
	context.prepare_execution();

	asIScriptFunction* jit_fib = jit_module.GetFunctionByDecl("int fib(int)");
	ANGELSEA_TEST_CHECK(jit_fib != nullptr);
	asIScriptFunction* jit_fib_iterative = jit_module.GetFunctionByDecl("uint64 fib_iterative(uint64)");

	context.engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, false);

	asIScriptModule&   interp_module = context.build("build-nojit", "scripts/fib.as");
	asIScriptFunction* interp_fib    = interp_module.GetFunctionByDecl("int fib(int)");
	ANGELSEA_TEST_CHECK(interp_fib != nullptr);
	asIScriptFunction* interp_fib_iterative = interp_module.GetFunctionByDecl("uint64 fib_iterative(uint64)");

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_fib = [&](int i, asIScriptFunction* fib_fn, bool is_iterative) -> int {
		ANGELSEA_TEST_CHECK(script_context->Prepare(fib_fn) >= 0);
		if (is_iterative) {
			ANGELSEA_TEST_CHECK(script_context->SetArgQWord(0, i) >= 0);
		} else {
			ANGELSEA_TEST_CHECK(script_context->SetArgDWord(0, i) >= 0);
		}
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
		return script_context->GetReturnDWord();
	};

	ANGELSEA_TEST_CHECK(run_fib(10000, interp_fib_iterative, true) == run_fib(10000, jit_fib_iterative, true));

	BENCHMARK("Interpreter fib(10000) (iterative)") { return run_fib(10000, interp_fib_iterative, true); };
	BENCHMARK("JIT         fib(10000) (iterative)") { return run_fib(10000, jit_fib_iterative, true); };

	BENCHMARK("Interpreter fib(25) (recursive)") { return run_fib(25, interp_fib, false); };
	BENCHMARK("JIT         fib(25) (recursive)") { return run_fib(25, jit_fib, false); };
}

TEST_CASE("fib in a thread", "[fibasync]") {
	EngineContext context;
	context.jit.SetCompileCallback([](auto* func, void* ud) { std::thread([=] { func(ud); }).detach(); });

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