// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

TEST_CASE("brainf**k interpreter", "[megatest][bf]") { REQUIRE(run("scripts/bfint.as") == "hello world"); }
