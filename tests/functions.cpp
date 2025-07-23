// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("simple parameterized function", "[params]") { REQUIRE(run("scripts/functions.as") == "10000\n"); }

TEST_CASE("references to primitives in parameters", "[refparams]")
{
	REQUIRE(run("scripts/refprimitives.as") == "10\n");
}

TEST_CASE("shared functions", "[shared][sharedfuncs]")
{
	EngineContext context;

	out = {};

	asIScriptModule& module_a = context.build("a", "scripts/sharedfuncs.as");
	asIScriptModule& module_b = context.build("b", "scripts/sharedfuncs.as");

	context.prepare_execution();

	asIScriptFunction* entry_a = module_a.GetFunctionByDecl("void main()");
	asIScriptFunction* entry_b = module_b.GetFunctionByDecl("void main()");

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run = [&](asIScriptFunction* func) {
		ANGELSEA_TEST_CHECK(script_context->Prepare(func) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
	};

	run(entry_a);
	run(entry_b);

	REQUIRE(out.str() == "10\n10\n");
}
