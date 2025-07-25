// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("int32 comparisons", "[intcmp]") {
	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("int a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("int a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("int a = 15, b = 14; print(''+bool(a != b))") == "true\n");
}

TEST_CASE("uint32 comparisons", "[intcmp]") {
	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("uint a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("uint a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("uint a = 15, b = 14; print(''+bool(a != b))") == "true\n");
}

TEST_CASE("int64 comparisons", "[intcmp]") {
	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("int64 a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("int64 a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("int64 a = 15, b = 14; print(''+bool(a != b))") == "true\n");
}

TEST_CASE("uint64 comparisons", "[intcmp]") {
	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a == b))") == "false\n");

	REQUIRE(run_string("uint64 a = 15, b = 16; print(''+bool(a != b))") == "true\n");
	REQUIRE(run_string("uint64 a = 15, b = 15; print(''+bool(a != b))") == "false\n");
	REQUIRE(run_string("uint64 a = 15, b = 14; print(''+bool(a != b))") == "true\n");
}

TEST_CASE("f32 comparisons", "[fpcmp]") {
	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("float a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("float a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("float a = 15, b = 14; print(''+bool(a == b))") == "false\n");
}

TEST_CASE("f64 comparisons", "[fpcmp]") {
	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a > b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a > b))") == "true\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a >= b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a >= b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a >= b))") == "true\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a < b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a < b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a < b))") == "false\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a <= b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a <= b))") == "false\n");

	REQUIRE(run_string("double a = 15, b = 16; print(''+bool(a == b))") == "false\n");
	REQUIRE(run_string("double a = 15, b = 15; print(''+bool(a == b))") == "true\n");
	REQUIRE(run_string("double a = 15, b = 14; print(''+bool(a == b))") == "false\n");
}

TEST_CASE("misc. boolean logic", "[boolmisc]") {
	run_string("string()+false");
	REQUIRE(run_string("bool a = false; print('' + !a);") == "true\n");
	REQUIRE(run_string("bool a = true; print('' + !a);") == "false\n");
}
