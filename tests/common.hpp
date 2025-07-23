// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include <angelsea/jit.hpp>
#include <angelscript.h>
#include <catch2/catch_all.hpp>
#include <sstream>
#include <string>

#define TEST_REQUIRE(name, tag, cond)                                                                                  \
	TEST_CASE(name, tag) { REQUIRE(cond); }

#define asllvm_test_check(x)                                                                                           \
	if (!(x))                                                                                                          \
	{                                                                                                                  \
		throw std::runtime_error{"check failed: " #x};                                                                 \
	}                                                                                                                  \
	(void)0

extern std::stringstream out;

struct EngineContext
{
	EngineContext(angelsea::JitConfig config = {});

	~EngineContext();

	void register_interface();

	asIScriptModule& build(const char* name, const char* script_path);

	void run(asIScriptModule& module, const char* entry_point);

	asIScriptEngine*     engine;
	angelsea::JitCompiler jit;
};

std::string run(const char* path, const char* entry = "void main()");
std::string run(EngineContext& context, const char* path, const char* entry = "void main()");
std::string run_string(const char* str);
std::string run_string(EngineContext& context, const char* str);
