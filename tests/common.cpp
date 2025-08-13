// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"
#include "angelscript.h"
#include "angelsea/config.hpp"

#include <iostream>
#include <scriptarray/scriptarray.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scriptstdstring/scriptstdstring.h>

std::stringstream out;

namespace bindings {
void message_callback(const asSMessageInfo* info, [[maybe_unused]] void* param) {
	const char* message_type = nullptr;

	switch (info->type) {
	case asMSGTYPE_INFORMATION: {
		message_type = "INFO";
		break;
	}

	case asMSGTYPE_WARNING: {
		message_type = "WARN";
		break;
	}

	default: {
		message_type = "ERR ";
		break;
	}
	}

	std::cerr << info->section << ':' << info->row << ':' << info->col << ": " << message_type << ": " << info->message
	          << '\n';
}

void print(const std::string& message) { out << message << '\n'; }

void print_int(long value) { out << value << '\n'; }
void print_uint(unsigned long value) { out << value << '\n'; }
void print_char(char value) { out << value; }
} // namespace bindings

bool is_env_set(const char* env) {
	const char* env_value = getenv(env);
	return env_value != nullptr && *env_value != '\0';
}

bool set_env_int_variable(const char* env, int& target) {
	const char* env_value = getenv(env);

	if (env_value == nullptr) {
		return false;
	}

	// will throw an exceptiwith bad args - this is fine for testing
	target = std::stoi(env_value);
	return true;
}

angelsea::JitConfig get_test_jit_config() {
	angelsea::JitConfig config{
	    .log_targets = {
			.verbose = is_env_set("ASEA_VERBOSE") ? asMSGTYPE_INFORMATION : (asEMsgType)-1,
		},
	    .debug = {
	        .dump_c_code   = is_env_set("ASEA_DUMP_C"),
	        .dump_mir_code = is_env_set("ASEA_DUMP_MIR"),
		},
		.c = {
			.human_readable = true
		},
		.experimental_stack_elision = true,
	};

	set_env_int_variable("ASEA_MIR_DEBUG_LEVEL", config.debug.mir_debug_level);
	set_env_int_variable("ASEA_MIR_OPT_LEVEL", config.mir_optimization_level);

	return config;
}

EngineContext::EngineContext(const angelsea::JitConfig& config) : engine{asCreateScriptEngine()}, jit{config, *engine} {
	engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, true);
	engine->SetEngineProperty(asEP_JIT_INTERFACE_VERSION, 2);
	engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);
	ANGELSEA_TEST_CHECK(engine->SetJITCompiler(&jit) >= 0);

	register_interface();
}

EngineContext::~EngineContext() { engine->ShutDownAndRelease(); }

void EngineContext::register_interface() {
	RegisterStdString(engine);
	RegisterScriptArray(engine, true);

	ANGELSEA_TEST_CHECK(
	    engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(bindings::print), asCALL_CDECL) >= 0
	);
	ANGELSEA_TEST_CHECK(
	    engine->RegisterGlobalFunction("void print(int64)", asFUNCTION(bindings::print_int), asCALL_CDECL) >= 0
	);
	ANGELSEA_TEST_CHECK(
	    engine->RegisterGlobalFunction("void print(uint64)", asFUNCTION(bindings::print_uint), asCALL_CDECL) >= 0
	);
	ANGELSEA_TEST_CHECK(
	    engine->RegisterGlobalFunction("void putchar(uint8)", asFUNCTION(bindings::print_char), asCALL_CDECL) >= 0
	);

	ANGELSEA_TEST_CHECK(engine->SetMessageCallback(asFUNCTION(bindings::message_callback), nullptr, asCALL_CDECL) >= 0);
}

asIScriptModule& EngineContext::build(const char* name, const char* script_path) {
	CScriptBuilder builder;
	ANGELSEA_TEST_CHECK(builder.StartNewModule(engine, name) >= 0);
	ANGELSEA_TEST_CHECK(builder.AddSectionFromFile(script_path) >= 0);
	ANGELSEA_TEST_CHECK(builder.BuildModule() >= 0);

	return *engine->GetModule(name);
}

void EngineContext::prepare_execution() {}

void EngineContext::run(asIScriptModule& module, const char* entry_point, asEContextState desired_state) {
	prepare_execution();

	asIScriptFunction* function = module.GetFunctionByDecl(entry_point);
	ANGELSEA_TEST_CHECK(function != nullptr);

	asIScriptContext* context = engine->CreateContext();
	ANGELSEA_TEST_CHECK(context->Prepare(function) >= 0);

	ANGELSEA_TEST_CHECK(context->Execute() == desired_state);

	context->Release();
}

std::string run(const char* path, const char* entry, asEContextState desired_state) {
	EngineContext context;
	return run(context, path, entry, desired_state);
}

std::string run(EngineContext& context, const char* path, const char* entry, asEContextState desired_state) {
	out                     = {};
	asIScriptModule& module = context.build(path, path);
	context.run(module, entry, desired_state);
	return out.str();
}

std::string run_string(const char* str, asEContextState desired_state) {
	EngineContext context;
	return run_string(context, str, desired_state);
}

std::string run_string(EngineContext& context, const char* str, asEContextState desired_state) {
	out = {};

	CScriptBuilder builder;
	builder.StartNewModule(context.engine, "build");
	builder.AddSectionFromMemory("str", (std::string("void main() {") + str + ";}").c_str());
	builder.BuildModule();

	context.run(*context.engine->GetModule("build"), "void main()", desired_state);

	return out.str();
}
