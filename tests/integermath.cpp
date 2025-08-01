// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

// Note that _some_ of the 8-bit and 16-bit arithmetic checks are somewhat redundant: operations over these types
// usually get promoted to 32-bit. Checking for this potentially helps detecting bugs related to sign extension and
// such, though.

TEST_CASE("8-bit signed math", "[signedmath8]") {
	REQUIRE(run_string("int8 a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int8 a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int8 a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int8 a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int8 a = 7, b = 4; print(a % b)") == "3\n");
	REQUIRE(run_string("int8 a = 10; print(++a)") == "11\n");
	REQUIRE(run_string("int8 a = 10; print(--a)") == "9\n");
	REQUIRE(run_string("int32 a = 128, b = -129; print(int8(a) + int8(b))") == "-1\n");
}

TEST_CASE("16-bit signed math", "[signedmath16]") {
	REQUIRE(run_string("int16 a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int16 a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int16 a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int16 a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int16 a = 7, b = 4; print(a % b)") == "3\n");
	REQUIRE(run_string("int16 a = 10; print(++a)") == "11\n");
	REQUIRE(run_string("int16 a = 10; print(--a)") == "9\n");
	REQUIRE(run_string("int32 a = 128, b = -129; print(int16(a) + int16(b))") == "-1\n");
}

TEST_CASE("32-bit signed math", "[signedmath32]") {
	REQUIRE(run_string("int a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int a = 7, b = 4; print(a % b)") == "3\n");

	REQUIRE(run_string("int a = 10; print(-a)") == "-10\n");

	REQUIRE(run_string("int a = 10; print(++a)") == "11\n");
	REQUIRE(run_string("int a = 10; print(--a)") == "9\n");

	REQUIRE(run_string("int a = 10, b = 0; print(''+ a/b);\n", asEXECUTION_EXCEPTION) == "");
	REQUIRE(run_string("int a = 10, b = 0; print(''+ a%b);\n", asEXECUTION_EXCEPTION) == "");
}

TEST_CASE("64-bit signed math", "[signedmath64]") {
	REQUIRE(run_string("int64 a = 1, b = -2; print(a + b)") == "-1\n");
	REQUIRE(run_string("int64 a = 10, b = 20; print(a - b)") == "-10\n");
	REQUIRE(run_string("int64 a = 10, b = -5; print(a * b)") == "-50\n");
	REQUIRE(run_string("int64 a = 10, b = -2; print(a / b)") == "-5\n");
	REQUIRE(run_string("int64 a = 7, b = 4; print(a % b)") == "3\n");

	REQUIRE(run_string("int64 a = 10; print(-a)") == "-10\n");

	REQUIRE(run_string("int64 a = 10; print(++a)") == "11\n");
	REQUIRE(run_string("int64 a = 10; print(--a)") == "9\n");

	REQUIRE(run_string("int64 a = 10, b = 0; print(''+ a/b);\n", asEXECUTION_EXCEPTION) == "");
	REQUIRE(run_string("int64 a = int64(1) << 63, b = -1; print(''+ a/b);\n", asEXECUTION_EXCEPTION) == "");
	REQUIRE(run_string("int64 a = 10, b = 0; print(''+ a%b);\n", asEXECUTION_EXCEPTION) == "");
}

TEST_CASE("unsigned overflow logic", "[unsignedmathoverflow]") {
	REQUIRE(run_string("uint8 a = 1, b = uint8(-2); print(a + b)") == "255\n");
	REQUIRE(run_string("uint16 a = 1, b = uint16(-2); print(a + b)") == "65535\n");
	REQUIRE(run_string("uint32 a = 1, b = uint32(-2); print(a + b)") == "4294967295\n");
	REQUIRE(run_string("uint64 a = 1, b = uint64(-2); print(a + b)") == "18446744073709551615\n");
}

TEST_CASE("32-bit unsigned division math", "[unsignedmathdiv32]") {
	REQUIRE(run_string("uint32 a = 10, b = 4; print(a / b)") == "2\n");
	REQUIRE(run_string("uint32 a = 10, b = 4; print(a % b)") == "2\n");

	REQUIRE(run_string("uint32 a = 10, b = 0; print(''+ a/b);\n", asEXECUTION_EXCEPTION) == "");
	REQUIRE(run_string("uint32 a = 10, b = 0; print(''+ a%b);\n", asEXECUTION_EXCEPTION) == "");
}

TEST_CASE("64-bit unsigned division math", "[unsignedmathdiv64]") {
	REQUIRE(run_string("uint64 a = 10, b = 4; print(a / b)") == "2\n");
	REQUIRE(run_string("uint64 a = 10, b = 4; print(a % b)") == "2\n");

	REQUIRE(run_string("uint64 a = 10, b = 0; print(''+ a/b);\n", asEXECUTION_EXCEPTION) == "");
	REQUIRE(run_string("uint64 a = 10, b = 0; print(''+ a%b);\n", asEXECUTION_EXCEPTION) == "");
}

TEST_CASE("32-bit bitwise logic", "[bitwise32]") {
	REQUIRE(run_string("int32 a = 4354352, b = 1213516; print(a & b)") == "131072\n");
	REQUIRE(run_string("int32 a = 4354352, b = 1213516; print(a | b)") == "5436796\n");
	REQUIRE(run_string("int32 a = 4354352, b = 1213516; print(a ^ b)") == "5305724\n");
	REQUIRE(run_string("int32 a = 4354352, b = 2; print(a << b)") == "17417408\n");
	REQUIRE(run_string("int32 a = 4354352, b = 2; print(a >> b)") == "1088588\n");
	REQUIRE(run_string("int32 a = -4354352, b = 2; print(a >> b)") == "1072653236\n");
	REQUIRE(run_string("int32 a = 4354352, b = 2; print(a >>> b)") == "1088588\n");
	REQUIRE(run_string("int32 a = -4354352, b = 2; print(a >>> b)") == "-1088588\n");
	REQUIRE(run_string("int32 a = 0xF0F0F0F0; print(~a)") == "252645135\n");
}

TEST_CASE("64-bit bitwise logic", "[bitwise64]") {
	REQUIRE(run_string("int64 a = 4354352, b = 1213516; print(a & b)") == "131072\n");
	REQUIRE(run_string("int64 a = 4354352, b = 1213516; print(a | b)") == "5436796\n");
	REQUIRE(run_string("int64 a = 4354352, b = 1213516; print(a ^ b)") == "5305724\n");
	REQUIRE(run_string("int64 a = 4354352, b = 2; print(a << b)") == "17417408\n");
	REQUIRE(run_string("int64 a = 4354352, b = 2; print(a >> b)") == "1088588\n");
	REQUIRE(run_string("int64 a = -4354352, b = 2; print(a >> b)") == "4611686018426299316\n");
	REQUIRE(run_string("int64 a = 4354352, b = 2; print(a >>> b)") == "1088588\n");
	REQUIRE(run_string("int64 a = -4354352, b = 2; print(a >>> b)") == "-1088588\n");
	// 0x00F0F0F0F0F0F0F0
	REQUIRE(run_string("int64 a = 0xF0F0F0F0F0F0F0F0, b = 8; print(uint64(a >> b))") == "67818912035696880\n");
	// 0xF0F0F0F0F0F0F0F0
	REQUIRE(run_string("int64 a = 0xF0F0F0F0F0F0F0F0, b = 8; print(uint64(a >>> b))") == "18442505391707320560\n");
	REQUIRE(run_string("int64 a = 0xF0F0F0F0F0F0F0F0; print(~a)") == "1085102592571150095\n");
}
