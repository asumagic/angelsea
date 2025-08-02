#include <angelscript.h>
#include <angelsea.hpp>
#include <cassert>
#include <fmt/core.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scriptstdstring/scriptstdstring.h>

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

	fmt::print(stderr, "{}:{}:{}: {}: {}\n", info->section, info->row, info->col, message_type, info->message);
}

void print(const std::string& message) { fmt::print("{}\n", message); }

int main() {
	asIScriptEngine* engine = asCreateScriptEngine();

	// Configure interface
	int r;
	r = engine->SetMessageCallback(asFUNCTION(message_callback), nullptr, asCALL_CDECL);
	assert(r >= 0);
	RegisterStdString(engine);
	r = engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(print), asCALL_CDECL);
	assert(r >= 0);

	// Configure engine properties for JIT
	engine->SetEngineProperty(asEP_INCLUDE_JIT_INSTRUCTIONS, true);
	engine->SetEngineProperty(asEP_JIT_INTERFACE_VERSION, 2);
	engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, true);

	// Set up JIT
	angelsea::JitConfig config;
	config.dump_mir_code = true; // dump the code to see it's working
	angelsea::Jit jit{config, *engine};

	r = engine->SetJITCompiler(&jit);
	assert(r >= 0);

	// Add script
	CScriptBuilder builder;
	builder.StartNewModule(engine, "build");
	builder.AddSectionFromMemory("str", (std::string("void main() { print(\"hello, world!\"); }").c_str()));
	builder.BuildModule();

	asIScriptModule* module = builder.GetModule();
	assert(module != nullptr);

	asIScriptFunction* fn = module->GetFunctionByDecl("void main()");
	assert(fn != nullptr);
	asIScriptContext* context = engine->CreateContext();
	assert(context != nullptr);
	r = context->Prepare(fn);
	assert(r >= 0);
	r = context->Execute();
	assert(r >= 0);

	context->Release();

	engine->ShutDownAndRelease();
	// NOTE: `jit` outlives the engine, but it doesn't care anyway
}