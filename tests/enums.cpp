// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

// Enums don't really seem to require any bytecode support, but keep this in here just in case.

TEST_CASE("enums", "[enums]") { REQUIRE(run("scripts/enums.as") == "11\n"); }
