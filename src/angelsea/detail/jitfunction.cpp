// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/jitfunction.hpp>
#include <angelsea/detail/log.hpp>

namespace angelsea::detail {

JitFunction::JitFunction(JitCompiler& compiler, asIScriptFunction& script_function) :
    m_compiler(compiler), m_script_function(&script_function) {
	detail::log(
	    m_compiler,
	    script_function,
	    LogSeverity::VERBOSE,
	    "Registering \"{}\" for compilation",
	    script_function.GetDeclaration(true, true, true)
	);
}

} // namespace angelsea::detail
