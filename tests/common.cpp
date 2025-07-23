// SPDX-License-Identifier: BSD-2-Clause

#include "common.hpp"

#include <iostream>
#include <scriptarray/scriptarray.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scriptstdstring/scriptstdstring.h>

std::stringstream out;

namespace bindings
{
void message_callback(const asSMessageInfo* info, [[maybe_unused]] void* param)
{
	const char* message_type = nullptr;

	switch (info->type)
	{
	case asMSGTYPE_INFORMATION:
	{
		message_type = "INFO";
		break;
	}

	case asMSGTYPE_WARNING:
	{
		message_type = "WARN";
		break;
	}

	default:
	{
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

EngineContext::EngineContext(angelsea::JitConfig config) : engine{asCreateScriptEngine()}, jit{config}
{
	engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, true);
	asllvm_test_check(engine->SetJITCompiler(&jit) >= 0);

	register_interface();
}

EngineContext::~EngineContext() { engine->ShutDownAndRelease(); }

void EngineContext::register_interface()
{
	RegisterStdString(engine);
	RegisterScriptArray(engine, true);

	asllvm_test_check(
		engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(bindings::print), asCALL_CDECL) >= 0);
	asllvm_test_check(
		engine->RegisterGlobalFunction("void print(int64)", asFUNCTION(bindings::print_int), asCALL_CDECL) >= 0);
	asllvm_test_check(
		engine->RegisterGlobalFunction("void print(uint64)", asFUNCTION(bindings::print_uint), asCALL_CDECL) >= 0);
	asllvm_test_check(
		engine->RegisterGlobalFunction("void putchar(uint8)", asFUNCTION(bindings::print_char), asCALL_CDECL) >= 0);

	asllvm_test_check(engine->SetMessageCallback(asFUNCTION(bindings::message_callback), nullptr, asCALL_CDECL) >= 0);
}

asIScriptModule& EngineContext::build(const char* name, const char* script_path)
{
	CScriptBuilder builder;
	asllvm_test_check(builder.StartNewModule(engine, name) >= 0);
	asllvm_test_check(builder.AddSectionFromFile(script_path) >= 0);
	asllvm_test_check(builder.BuildModule() >= 0);

	return *engine->GetModule(name);
}

void EngineContext::run(asIScriptModule& module, const char* entry_point)
{
	asIScriptFunction* function = module.GetFunctionByDecl(entry_point);
	asllvm_test_check(function != nullptr);

	asIScriptContext* context = engine->CreateContext();
	asllvm_test_check(context->Prepare(function) >= 0);

	asllvm_test_check(context->Execute() == asEXECUTION_FINISHED);

	context->Release();
}

std::string run(const char* path, const char* entry)
{
	EngineContext context;
	return run(context, path, entry);
}

std::string run(EngineContext& context, const char* path, const char* entry)
{
	out                     = {};
	asIScriptModule& module = context.build("build", path);
	context.run(module, entry);
	return out.str();
}

std::string run_string(const char* str)
{
	EngineContext context;
	return run_string(context, str);
}

std::string run_string(EngineContext& context, const char* str)
{
	out = {};

	CScriptBuilder builder;
	builder.StartNewModule(context.engine, "build");
	builder.AddSectionFromMemory("str", (std::string("void main() {") + str + ";}").c_str());
	builder.BuildModule();

	context.run(*context.engine->GetModule("build"), "void main()");

	return out.str();
}
