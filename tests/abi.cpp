// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

#include <angelscript.h>
#include <as_datatype.h>
#include <as_generic.h>
#include <autowrapper/aswrappedcall.h>

struct SomeClass {
	int a = 0;
};

struct NoisyClass {
	NoisyClass() { out << "Con"; }
	NoisyClass(const NoisyClass& other) { out << "Cpy"; }
	~NoisyClass() { out << "Des"; }
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

void generic_return_string(asIScriptGeneric* gen) {
	std::string* ret = static_cast<std::string*>(gen->GetAddressOfReturnLocation());
	new (ret) std::string("hello world! ");
	*ret += std::to_string(gen->GetArgDWord(0));
	*ret += ", ";
	*ret += std::to_string(gen->GetArgDWord(1));
	*ret += *static_cast<std::string*>(gen->GetArgObject(2));
}

void generic_take_noisy(asIScriptGeneric* gen) {}

void bind_generic_functions(asIScriptEngine& e) {
	e.RegisterGlobalFunction("int generic_noarg()", asFUNCTION(generic_noarg_hack), asCALL_GENERIC);
	e.RegisterGlobalFunction("int generic_sum3int(int, int, int)", asFUNCTION(generic_sum3int), asCALL_GENERIC);
	e.RegisterGlobalFunction(
	    "string generic_return_string(int, int, const string&in)",
	    asFUNCTION(generic_return_string),
	    asCALL_GENERIC
	);

	e.RegisterObjectType("SomeClass", sizeof(SomeClass), asOBJ_VALUE | asOBJ_POD);
	e.RegisterObjectMethod(
	    "SomeClass",
	    "void generic_add_obj(int x)",
	    asFUNCTION(generic_add_obj_hack),
	    asCALL_GENERIC
	);
	e.RegisterObjectProperty("SomeClass", "int a", asOFFSET(SomeClass, a));

	e.RegisterObjectType("NoisyClass", sizeof(SomeClass), asOBJ_VALUE | asGetTypeTraits<NoisyClass>());
	e.RegisterObjectBehaviour("NoisyClass", asBEHAVE_CONSTRUCT, "void f()", WRAP_CON(NoisyClass, ()), asCALL_GENERIC);
	e.RegisterObjectBehaviour(
	    "NoisyClass",
	    asBEHAVE_CONSTRUCT,
	    "void f(const NoisyClass &in)",
	    WRAP_CON(NoisyClass, (const NoisyClass&)),
	    asCALL_GENERIC
	);
	e.RegisterObjectBehaviour("NoisyClass", asBEHAVE_DESTRUCT, "void f()", WRAP_DES(NoisyClass), asCALL_GENERIC);

	e.RegisterGlobalFunction(
	    "void generic_take_noisy(NoisyClass a, NoisyClass b)",
	    asFUNCTION(generic_take_noisy),
	    asCALL_GENERIC
	);
}

TEST_CASE("generic calling convention", "[abi][conv_generic]") {
	EngineContext context;
	bind_generic_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+generic_noarg())") == "123\n");

	// involves the stack pointer
	REQUIRE(run_string(context, "print(''+generic_sum3int(500, 30, 2))") == "532\n");

	// involves the object pointer
	REQUIRE(run_string(context, "SomeClass s; s.a = 0; s.generic_add_obj(100); print(''+s.a);") == "100\n");

	// involves a return on stack, but no cleanargs or otherwise
	REQUIRE(run_string(context, "print(generic_return_string(123, 456, ' :3'));") == "hello world! 123, 456 :3\n");

	// involves cleanargs
	REQUIRE(run_string(context, "generic_take_noisy(NoisyClass(), NoisyClass());") == "ConCpyDesConCpyDesDesDes");
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

int native_noarg() { return 123; }

int native_sum3int(int a, int b, int c) { return a + b + c; }

void bind_native_functions(asIScriptEngine& e) {
	e.RegisterGlobalFunction("int native_noarg()", asFUNCTION(native_noarg), asCALL_CDECL);
	e.RegisterGlobalFunction("int native_sum3int(int, int, int)", asFUNCTION(native_sum3int), asCALL_CDECL);
}

TEST_CASE("native calling convention", "[abi][conv_native]") {
	EngineContext context;
	bind_native_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+native_noarg())") == "123\n");

	// involves the stack pointer
	REQUIRE(run_string(context, "print(''+native_sum3int(500, 30, 2))") == "532\n");
}