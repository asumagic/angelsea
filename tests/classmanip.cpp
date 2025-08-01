// SPDX-License-Identifier: BSD-2-Clause

#include "angelscript.h"
#include "common.hpp"

TEST_CASE("string handling", "[str]") {
	REQUIRE(run("scripts/stringmanip.as", "void string_ref()") == "hello\n");
	REQUIRE(run("scripts/stringmanip.as", "void string_concat()") == "hello world\n");
	REQUIRE(run("scripts/stringmanip.as", "void string_concat2()") == "hello world\n");
	REQUIRE(run("scripts/stringmanip.as", "void string_manylocals_concat()") == "hello world\n");
	REQUIRE(run("scripts/stringmanip.as", "void string_function_reference()") == "hello world!\n");
	REQUIRE(run("scripts/stringmanip.as", "void string_function_value()") == "hello world!\n");
	REQUIRE(run_string(R"(print(parseInt("ABCD", 16)))") == "43981\n");
}

TEST_REQUIRE("simple array logic", "[array][factory]", run("scripts/arrays/simple.as") == "123\nhi\n");

TEST_REQUIRE(
    "arrays with user class types",
    "[array][factory][arrayuser]",
    run("scripts/arrays/userclass.as") == "123\n456\n789\n"
);

TEST_REQUIRE(
    "arrays and initialization lists",
    "[array][factory]",
    run("scripts/arrays/initializationlists.as") == "123\n456\n789\nhello\nhi\n123\n"
);

TEST_CASE("user classes", "[userclass][simpleuserclass]") {
	REQUIRE(run("scripts/userclasses.as", "void test()") == "hello\n");
	REQUIRE(run("scripts/userclasses.as", "void method_test()") == "hello\n123\n456\n789\n");
	REQUIRE(
	    run("scripts/userclasses.as", "void method_field_test()") == "hello\n10\n20\n30\n40\n50\n60\n70\n80\n90\n100\n"
	);
	REQUIRE(run("scripts/userclasses.as", "void handle_test()") == "hello\n123\n456\n789\n");
	REQUIRE(run("scripts/userclasses.as", "void return_field_test()") == "hello\nworld\n");

	REQUIRE(
	    run("scripts/userclasses.as", "void pass_by_value_test()") == "hello\n10\n20\n30\n40\n50\n60\n70\n80\n90\n100\n"
	);

	REQUIRE(run("scripts/userclasses.as", "void is_test()") == "hello\nhello\nok\nok\nok\n");
}

TEST_CASE("user class chkref", "[chkref][userclass]") {
	REQUIRE(run("scripts/userclasses.as", "void null_test()", asEXECUTION_EXCEPTION) == "");
}

TEST_CASE("globals with user classes", "[userclass][globals][globalswithclasses]") {
	REQUIRE(run("scripts/globalswithclasses.as", "void global_test()") == "123\n456\n789\n");
}

TEST_CASE("user class Vec3f", "[userclass][vec3f]") {
	REQUIRE(run("scripts/vec3f.as") == "150\nx: -50; y: 100; z: -50\nx: 10; y: 7.5; z: 5\n");
}

class PgaTestBase {
	public:
	virtual void foo() { out << bar << '\n'; }
	int          bar;
};

void pga_test_construct(PgaTestBase* s) {}
void pga_test_destruct(PgaTestBase* s) {}

TEST_CASE("push global address variable", "[asBC_PGA]") {

	EngineContext ctx;
	ctx.engine->RegisterObjectType("Base", sizeof(PgaTestBase), asOBJ_REF | asOBJ_NOCOUNT);
	ctx.engine->RegisterObjectMethod("Base", "void foo()", asMETHOD(PgaTestBase, foo), asCALL_THISCALL);
	ctx.engine->RegisterObjectProperty("Base", "int bar", asOFFSET(PgaTestBase, bar));

	PgaTestBase b;
	ctx.engine->RegisterGlobalProperty("Base b", &b);

	out = {};

	asIScriptModule& module = ctx.build("asbc_pga_test", "scripts/bc_pga_var_test.as");
	ctx.run(module, "void foo()");

	REQUIRE(out.str() == "123\n");
}

TEST_CASE("devirtualization", "[devirt]") { REQUIRE(run("scripts/devirt.as") == "hello\n"); }

TEST_CASE("virtual system functions", "[sysvirt]") {
	class Base {
		public:
		virtual void foo() { out << "Base::foo()\n"; }
	};

	class Derived final : public Base {
		public:
		void foo() override { out << "Derived::foo()\n"; }
	};

	EngineContext ctx;
	ctx.engine->RegisterObjectType("Base", sizeof(Base), asOBJ_REF | asOBJ_NOCOUNT);
	ctx.engine->RegisterObjectMethod("Base", "void foo()", asMETHOD(Base, foo), asCALL_THISCALL);

	Derived b;
	ctx.engine->RegisterGlobalProperty("Base b", &b);

	REQUIRE(run_string(ctx, "b.foo()") == "Derived::foo()\n");
}
