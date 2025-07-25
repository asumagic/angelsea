// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("basic integral comparisons and 'if'", "[ifcmp]") {
	REQUIRE(run_string("int a = 0; if (a >= 0) { print(1); }") == "1\n");
	REQUIRE(run_string("int a = 0; if (a < 0) { print(0); } else { print(1); }") == "1\n");

	// Emits JLowZ
	REQUIRE(run_string("bool a = true; if (a) { print('good'); } else { print('bad'); }") == "good\n");

	// Emits JLowNZ
	REQUIRE(run_string("bool a = true, b = false; if (a || b) { print('good'); } else { print('bad'); }") == "good\n");
}

TEST_CASE("simple 'for' looping", "[forloop]") {
	REQUIRE(run_string("for (int i = 0; i < 5; ++i) { print(i); }") == "0\n1\n2\n3\n4\n");
}

TEST_CASE("simple 'while' looping", "[whileloop]") {
	REQUIRE(run_string("int i = 5; while (i != 7) { print(i); ++i; }") == "5\n6\n");
}

TEST_CASE("switch statement", "[switch]") {
	REQUIRE(run("scripts/switch.as") ==
		"I am Siegward of Catarina.\n"
		"To be honest, I'm in a bit of a pickle.\n"
		"Whoever it is, I'm sure I can talk some sense into them.\n"
	);
}
