// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

#include <as_datatype.h>
#include <as_generic.h>

struct SomeClass {
	int a = 0;
};

void generic_noarg(asIScriptGeneric* gen) {
	// asCGeneric* g = static_cast<asCGeneric*>(gen);
	gen->SetReturnDWord(123);
}

void generic_noarg_hack(asIScriptGeneric* gen) {
	asCGeneric* g = static_cast<asCGeneric*>(gen);
	g->returnVal  = 123;
}

void generic_sum3int(asIScriptGeneric* gen) {
	int a = gen->GetArgDWord(0);
	int b = gen->GetArgDWord(1);
	int c = gen->GetArgDWord(2);
	gen->SetReturnDWord(a + b + c);
}

void generic_add_obj_hack(asIScriptGeneric* gen) {
	asCGeneric* g = static_cast<asCGeneric*>(gen);
	static_cast<SomeClass*>(g->currentObject)->a += g->stackPointer[0];
}

void bind_generic_functions(asIScriptEngine& e) {
	e.RegisterGlobalFunction("int generic_noarg()", asFUNCTION(generic_noarg_hack), asCALL_GENERIC);
	e.RegisterGlobalFunction("int generic_sum3int(int, int, int)", asFUNCTION(generic_sum3int), asCALL_GENERIC);

	e.RegisterObjectType("SomeClass", sizeof(SomeClass), asOBJ_VALUE | asOBJ_POD);
	e.RegisterObjectMethod(
	    "SomeClass",
	    "void generic_add_obj(int x)",
	    asFUNCTION(generic_add_obj_hack),
	    asCALL_GENERIC
	);
	e.RegisterObjectProperty("SomeClass", "int a", asOFFSET(SomeClass, a));
}

TEST_CASE("generic calling convention", "[abi][conv_generic]") {
	EngineContext context;
	bind_generic_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+generic_noarg())") == "123\n");
	REQUIRE(run_string(context, "print(''+generic_sum3int(500, 30, 2))") == "532\n");
	REQUIRE(run_string(context, "SomeClass s; s.a = 0; s.generic_add_obj(100); print(''+s.a);") == "100\n");
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

	asIScriptModule&   jit_module               = context.build("build", "scripts/abi.as");
	asIScriptFunction& jit_million_bench        = get_bench(jit_module, "void benchmark_1M_calls_generic_noarg()");
	asIScriptFunction& jit_million_method_bench = get_bench(jit_module, "void benchmark_1M_calls_generic_method_arg()");
	context.prepare_execution();

	context.engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, false);
	asIScriptModule&   interp_module        = context.build("build-nojit", "scripts/abi.as");
	asIScriptFunction& interp_million_bench = get_bench(interp_module, "void benchmark_1M_calls_generic_noarg()");
	asIScriptFunction& interp_million_method_bench
	    = get_bench(interp_module, "void benchmark_1M_calls_generic_method_arg()");

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_fn = [&](asIScriptFunction& fn) -> int {
		ANGELSEA_TEST_CHECK(script_context->Prepare(&fn) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
		return script_context->GetReturnDWord();
	};

	BENCHMARK("JIT    1M generic `void(this, int)`") { return run_fn(jit_million_method_bench); };
	BENCHMARK("Interp 1M generic `void(this, int)`") { return run_fn(interp_million_method_bench); };

	BENCHMARK("JIT    1M generic `int()`") { return run_fn(jit_million_bench); };
	BENCHMARK("Interp 1M generic `int()`") { return run_fn(interp_million_bench); };
}