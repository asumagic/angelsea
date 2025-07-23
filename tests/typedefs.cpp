// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

// Typedefs don't seem to require any bytecode support, but keep this just in case.

TEST_CASE("primitive typedefs", "[typedefs]") { REQUIRE(run("scripts/typedefs.as") == "3.141\n"); }
