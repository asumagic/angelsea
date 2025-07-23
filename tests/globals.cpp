// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("globals", "[globals]")
{
	REQUIRE(run("scripts/globals.as", "void assign_read()") == "123\n123\n123\n123\n");
}
