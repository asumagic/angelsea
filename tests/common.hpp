// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include <angelscript.h>
#include <angelsea/jit.hpp>
#include <catch2/catch_all.hpp>
#include <sstream>
#include <string>

#define TEST_REQUIRE(name, tag, cond)                                                                                  \
	TEST_CASE(name, tag) { REQUIRE(cond); }

#define ANGELSEA_TEST_CHECK(x)                                                                                         \
	if (!(x)) {                                                                                                        \
		throw std::runtime_error{"check failed: " #x};                                                                 \
	}                                                                                                                  \
	(void)0

extern std::stringstream out;

angelsea::JitConfig get_test_jit_config();

struct EngineContext {
	EngineContext(const angelsea::JitConfig& config = get_test_jit_config());

	~EngineContext();

	void register_interface();

	asIScriptModule& build(const char* name, const char* script_path);
	void             prepare_execution();
	void run(asIScriptModule& module, const char* entry_point, asEContextState desired_state = asEXECUTION_FINISHED);

	asIScriptEngine* engine;
	angelsea::Jit    jit;
};

std::string
run(const char* path, const char* entry = "void main()", asEContextState desired_state = asEXECUTION_FINISHED);
std::string
            run(EngineContext&  context,
                const char*     path,
                const char*     entry         = "void main()",
                asEContextState desired_state = asEXECUTION_FINISHED);
std::string run_string(const char* str, asEContextState desired_state = asEXECUTION_FINISHED);
std::string run_string(EngineContext& context, const char* str, asEContextState desired_state = asEXECUTION_FINISHED);
