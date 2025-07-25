// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/runtime.hpp>

#include <angelsea/detail/debug.hpp>

#include <angelscript.h>
#include <as_context.h>
#include <as_scriptengine.h>

extern "C"
{

int asea_call_script_function(asSVMRegisters* vm_registers, int function_idx)
{
    auto& context = static_cast<asCContext&>(*vm_registers->ctx);
    auto& engine = context.m_engine;

    angelsea_assert(function_idx < engine->scriptFunctions.GetLength());
    context.CallScriptFunction(engine->scriptFunctions[function_idx]);

    return context.m_status;
}

}