// SPDX-License-Identifier: BSD-2-Clause

#include "angelsea/config.hpp"
#include "common.hpp"

#include "benchmark.hpp"
#include <angelscript.h>
#include <as_datatype.h>
#include <as_generic.h>
#include <autowrapper/aswrappedcall.h>
#include <nanobench.h>

struct SomeClass {
	int a = 0;

	void add_obj(int x) { a += x; }
};

struct NoisyClass {
	NoisyClass() { out << "Con" << c; }
	NoisyClass(const NoisyClass& other) { out << "Cpy" << c; }
	~NoisyClass() { out << "Des" << c; }
	std::string c = "";
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

void take_noisy(NoisyClass a, NoisyClass b) {}

void bind_generic_functions(asIScriptEngine& e) {
	e.RegisterGlobalFunction("int noarg()", asFUNCTION(generic_noarg_hack), asCALL_GENERIC);
	e.RegisterGlobalFunction("int sum3int(int, int, int)", asFUNCTION(generic_sum3int), asCALL_GENERIC);
	e.RegisterGlobalFunction(
	    "string return_string(int, int, const string&in)",
	    asFUNCTION(generic_return_string),
	    asCALL_GENERIC
	);

	e.RegisterObjectType("SomeClass", sizeof(SomeClass), asOBJ_VALUE | asOBJ_POD);
	e.RegisterObjectMethod("SomeClass", "void add_obj(int x)", asFUNCTION(generic_add_obj_hack), asCALL_GENERIC);
	e.RegisterObjectProperty("SomeClass", "int a", asOFFSET(SomeClass, a));

	e.RegisterObjectType("NoisyClass", sizeof(NoisyClass), asOBJ_VALUE | asGetTypeTraits<NoisyClass>());
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
	    "void take_noisy(NoisyClass a, NoisyClass b)",
	    asFUNCTION(generic_take_noisy),
	    asCALL_GENERIC
	);
}

TEST_CASE("generic calling convention", "[abi][conv_generic]") {
	EngineContext context;
	bind_generic_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+noarg())") == "123\n");

	// involves the stack pointer
	REQUIRE(run_string(context, "print(''+sum3int(500, 30, 2))") == "532\n");

	// involves the object pointer
	REQUIRE(run_string(context, "SomeClass s; s.a = 0; s.add_obj(100); print(''+s.a);") == "100\n");

	// involves a return on stack, but no cleanargs or otherwise
	REQUIRE(run_string(context, "print(return_string(123, 456, ' :3'));") == "hello world! 123, 456 :3\n");

	// involves cleanargs
	REQUIRE(run_string(context, "take_noisy(NoisyClass(), NoisyClass());") == "ConCpyDesConCpyDesDesDes");
}

TEST_CASE("generic abi benchmark", "[abi][conv_generic][benchmark]") {
	EngineContext context;
	bind_generic_functions(*context.engine);

	out = {};

	auto get_bench = [](asIScriptModule& module, const char* decl) -> asIScriptFunction& {
		asIScriptFunction* fn = module.GetFunctionByDecl(decl);
		ANGELSEA_TEST_CHECK(fn != nullptr);
		return *fn;
	};

	asIScriptModule&   jit_module               = context.build("build", "scripts/abi.as");
	asIScriptFunction& jit_million_bench        = get_bench(jit_module, "void benchmark_1M_calls_noarg()");
	asIScriptFunction& jit_million_method_bench = get_bench(jit_module, "void benchmark_1M_calls_method_arg()");
	context.prepare_execution();

	context.engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, false);
	asIScriptModule&   interp_module               = context.build("build-nojit", "scripts/abi.as");
	asIScriptFunction& interp_million_bench        = get_bench(interp_module, "void benchmark_1M_calls_noarg()");
	asIScriptFunction& interp_million_method_bench = get_bench(interp_module, "void benchmark_1M_calls_method_arg()");

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_fn = [&](asIScriptFunction& fn) -> int {
		ANGELSEA_TEST_CHECK(script_context->Prepare(&fn) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
		return script_context->GetReturnDWord();
	};

	{
		auto b = default_benchmark();
		b.title("1M generic `void(this, int)`");
		b.run("Interpreter", [&] { return run_fn(interp_million_method_bench); });
		b.run("JIT", [&] { return run_fn(jit_million_method_bench); });
	}

	{
		auto b = default_benchmark();
		b.title("1M generic `int()`");
		b.run("Interpreter", [&] { return run_fn(interp_million_bench); });
		b.run("JIT", [&] { return run_fn(jit_million_bench); });
	}
}

int native_noarg() { return 123; }

int native_sum3int(int a, int b, int c) { return a + b + c; }
int native_sum3float(float a, float b, float c) { return int(a + b + c); }

// clang-format off
int native_manyargs(int x0, int x1, int x2, int x3, int x4, int x5, int x6, int x7, int x8, float y1, float y2, float y3, float y4, int x9, int x10, int x11, int x12, int x13, int x14, int x15, int x16) { // clang-format on
	return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 + x8 + (y1 + y2 + y3 + y4) + x9 + x10 + x11 + x12 + x13 + x14 + x15
	    + x16;
}

SomeClass ret_trivial_by_value() { return {.a = 69420}; }

int pass_trivial_by_value(SomeClass a, SomeClass b) { return a.a + b.a; }

std::string return_string(int a, int b, const std::string& c) {
	std::string ret("hello world! ");
	ret += std::to_string(a);
	ret += ", ";
	ret += std::to_string(b);
	ret += c;
	return ret;
}

void native_destruct_noisy(NoisyClass* c) { c->~NoisyClass(); }

void bind_native_functions(asIScriptEngine& e) {
	e.RegisterGlobalFunction("int noarg()", asFUNCTION(native_noarg), asCALL_CDECL);
	e.RegisterGlobalFunction("int sum3int(int, int, int)", asFUNCTION(native_sum3int), asCALL_CDECL);
	e.RegisterGlobalFunction("float sum3float(float, float, float)", asFUNCTION(native_sum3float), asCALL_CDECL);
	e.RegisterGlobalFunction(
	    "string return_string(int, int, const string&in)",
	    asFUNCTION(generic_return_string),
	    asCALL_GENERIC
	);

	e.RegisterObjectType(
	    "SomeClass",
	    sizeof(SomeClass),
	    asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS | asGetTypeTraits<SomeClass>()
	);
	e.RegisterObjectMethod("SomeClass", "void add_obj(int x)", asMETHOD(SomeClass, add_obj), asCALL_THISCALL);
	e.RegisterObjectProperty("SomeClass", "int a", asOFFSET(SomeClass, a));

	e.RegisterGlobalFunction("SomeClass ret_trivial_by_value()", asFUNCTION(ret_trivial_by_value), asCALL_CDECL);
	e.RegisterGlobalFunction(
	    "int pass_trivial_by_value(SomeClass a, SomeClass b)",
	    asFUNCTION(pass_trivial_by_value),
	    asCALL_CDECL
	);

	// TODO: cdecl behs?
	e.RegisterObjectType("NoisyClass", sizeof(NoisyClass), asOBJ_VALUE | asGetTypeTraits<NoisyClass>());
	e.RegisterObjectBehaviour("NoisyClass", asBEHAVE_CONSTRUCT, "void f()", WRAP_CON(NoisyClass, ()), asCALL_GENERIC);
	e.RegisterObjectBehaviour(
	    "NoisyClass",
	    asBEHAVE_CONSTRUCT,
	    "void f(const NoisyClass &in)",
	    WRAP_CON(NoisyClass, (const NoisyClass&)),
	    asCALL_GENERIC
	);
	e.RegisterObjectBehaviour(
	    "NoisyClass",
	    asBEHAVE_DESTRUCT,
	    "void f()",
	    asFUNCTION(native_destruct_noisy),
	    asCALL_CDECL_OBJLAST
	);

	e.RegisterGlobalFunction("void take_noisy(NoisyClass a, NoisyClass b)", asFUNCTION(take_noisy), asCALL_CDECL);

	e.RegisterGlobalFunction(
	    "int native_manyargs(int x0, int x1, int x2, int x3, int x4, int x5, int x6, int x7, int x8, float y1, "
	    "float "
	    "y2, float y3, float y4, int x9, int x10, int x11, int x12, int x13, int x14, int x15, int x16)",
	    asFUNCTION(native_manyargs),
	    asCALL_CDECL
	);
}

TEST_CASE("native calling convention", "[abi][conv_native]") {
	EngineContext context;
	bind_native_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+noarg())") == "123\n");

	// involves the stack pointer
	REQUIRE(run_string(context, "print(''+sum3int(500, 30, 2))") == "532\n");

	REQUIRE(run_string(context, "print(''+sum3float(500.5, 29.5, 2))") == "532\n");

	REQUIRE(
	    run_string(
	        context,
	        "print(''+native_manyargs(0, 1, 2, 3, 4, 5, 6, 7, 8, 0.5, 1.5, 3.0, 6.0, 9, 10, 11, 12, 13, 14, 15, "
	        "16))"
	    )
	    == "147\n"
	);
}

TEST_CASE("native calling convention return by value", "[abi][conv_native]") {
	EngineContext context;
	bind_native_functions(*context.engine);

	REQUIRE(run_string(context, "print(''+ret_trivial_by_value().a)") == "69420\n");
}

TEST_CASE("native calling convention pass by value", "[abi][conv_native]") {
	EngineContext context;
	bind_native_functions(*context.engine);

	REQUIRE(
	    run_string(context, "SomeClass a; a.a = 123; SomeClass b; b.a = 456; print(''+pass_trivial_by_value(a, b));")
	    == "579\n"
	);

	// involves cleanargs
	REQUIRE(run_string(context, "take_noisy(NoisyClass(), NoisyClass());") == "ConCpyDesConCpyDesDesDes");
}

TEST_CASE("native abi benchmark", "[abi][conv_native][benchmark]") {
	EngineContext context;
	bind_native_functions(*context.engine);

	out = {};

	auto get_bench = [](asIScriptModule& module, const char* decl) -> asIScriptFunction& {
		asIScriptFunction* fn = module.GetFunctionByDecl(decl);
		ANGELSEA_TEST_CHECK(fn != nullptr);
		return *fn;
	};

	asIScriptModule&   jit_module               = context.build("build", "scripts/abi.as");
	asIScriptFunction& jit_million_bench        = get_bench(jit_module, "void benchmark_1M_calls_noarg()");
	asIScriptFunction& jit_million_method_bench = get_bench(jit_module, "void benchmark_1M_calls_method_arg()");
	context.prepare_execution();

	context.engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, false);
	asIScriptModule&   interp_module               = context.build("build-nojit", "scripts/abi.as");
	asIScriptFunction& interp_million_bench        = get_bench(interp_module, "void benchmark_1M_calls_noarg()");
	asIScriptFunction& interp_million_method_bench = get_bench(interp_module, "void benchmark_1M_calls_method_arg()");

	asIScriptContext* script_context = context.engine->CreateContext();

	const auto run_fn = [&](asIScriptFunction& fn) -> int {
		ANGELSEA_TEST_CHECK(script_context->Prepare(&fn) >= 0);
		ANGELSEA_TEST_CHECK(script_context->Execute() == asEXECUTION_FINISHED);
		return script_context->GetReturnDWord();
	};

	{
		auto b = default_benchmark();
		b.title("1M native `void(this, int)`");
		b.run("Interpeter", [&] { return run_fn(interp_million_method_bench); });
		b.run("JIT", [&] { return run_fn(jit_million_method_bench); });
	}

	{
		auto b = default_benchmark();
		b.title("1M native `int()`");
		b.run("Interpeter", [&] { return run_fn(interp_million_bench); });
		b.run("JIT", [&] { return run_fn(jit_million_bench); });
	}
}