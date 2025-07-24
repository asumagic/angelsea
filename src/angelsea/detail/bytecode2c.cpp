// SPDX-License-Identifier: BSD-2-Clause

#include "angelscript.h"
#include "angelsea/detail/bytecodeinstruction.hpp"
#define FMT_COMPILE
#include <fmt/format.h>

#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/bytecodedisasm.hpp>
#include <angelsea/detail/bytecodetools.hpp>
#include <angelsea/detail/log.hpp>

namespace angelsea::detail
{

BytecodeToC::BytecodeToC(JitCompiler& compiler) :
    m_compiler(&compiler)
{
    m_buffer.reserve(1024 * 64);
    m_current_module_id = 0;
    m_current_function_id = 0;
}

void BytecodeToC::prepare_new_context()
{
    m_buffer.clear();
    write_header();
}

ModuleId BytecodeToC::translate_module(
    std::string_view internal_module_name,
    asIScriptModule* script_module,
    std::span<JitFunction*> functions
) {
    ++m_current_module_id;

    // NOTE: module name and section names are separate concepts, and there may
    // be several script sections in a module
    emit(R"$(
/* MODULE: {module_name} */ 
)$", fmt::arg("module_name", internal_module_name));

    for (JitFunction* function : functions)
    {
        translate_function(internal_module_name, *function);
    }

    return m_current_module_id;
}

FunctionId BytecodeToC::translate_function(
    std::string_view internal_module_name,
    JitFunction& function
) {
    ++m_current_function_id;

    const auto func_name = entry_point_name(m_current_module_id, m_current_function_id);
    if (m_on_map_function_callback)
    {
        m_on_map_function_callback(function, func_name);
    }

    if (is_human_readable())
    {
        const char* section_name;
        int row, col;
        function.script_function().GetDeclaredAt(&section_name, &row, &col);
        emit("/* {}:{}:{}: {} */\n", section_name, row, col, function.script_function().GetDeclaration(true, true, true));
    }

    // JIT entry signature is `void(asSVMRegisters *regs, asPWORD jitArg)`
    emit(
        "void {name}(asSVMRegisters *regs, asPWORD entryLabel) {{\n",
        fmt::arg("name", func_name)
    );

    // Local VM variables; load them from the registers
    emit(
        "\tasDWORD *l_bc = m_regs.programPointer;\n"
        "\tasDWORD *l_sp = m_regs.stackPointer;\n"
        "\tasDWORD *l_fp = m_regs.stackFramePointer;\n"
        "\n"
    );

    // Transpiled functions are compiled to be JIT entry points for the
    // AngelScript VM.
    //
    // The conversion process is relatively simple: There is no deep analysis of
    // bytecode; for each bytecode instruction we emit one block of C code,
    // which is largely similar to the equivalent source code in the AngelScript
    // VM (asCContext::ExecuteNext()).
    // If we can't handle an instruction, we rebuild whatever state we need to
    // return to the VM and we `return;` out of the function. This includes
    // instructions we might not be supporting yet, or that are too complex to
    // implement.
    //
    // A script function may have one equivalent JIT function (the one we are
    // emitting here).
    // To differentiate between JIT entry points, we can assign a non-zero
    // asPWORD to each of them.
    // We handle this by simply assigning each asBC_JitEntry a unique increasing
    // number (we will call this an entry ID). We then simply `switch` on that
    // entry ID (see later) to `goto` to the C handler of a given bytecode
    // instruction.
    //
    // A transpiled function looks like this (simplified, with offsets made up,
    // etc.):
    //
    // void asea_jit_mod1_fn3(asSVMRegisters *regs, asPWORD entryLabel) {
    //     switch (entryLabel)
    //     {
    //     case 1: goto bc0;
    //     case 2: goto bc7;
    //     }
    // 
    //     /* bytecode: JitEntry 1 */
    //     bc0: { /* <-- unique C label where the value is the equivalent bytecode offset */
    //         /* <- no useful handler for jit entries */
    //     }
    //     /* fallthrough to the next instruction */
    //
    //     /* bytecode: [DISASSEMBLED INSTRUCTION] */
    //     bc3: {
    //         blah blah do stuff
    //         /* <-- code handling the instruction */
    //     }
    //     /* fallthrough to the next instruction */
    //
    //     /* bytecode: JitEntry 2 */
    //     bc3: {
    //     }
    //
    //     etc.
    // }

    emit_entry_dispatch(function);

    walk_bytecode(get_bytecode(function.script_function()), [&](BytecodeInstruction ins) {
        if (is_human_readable())
        {
            emit("\t/* bytecode: {} */\n", disassemble(m_compiler->engine(), ins));
        }

        emit("\tbc{}: {{\n", ins.offset);

        // TODO: after a fallback don't bother emitting fallback code at all
        // until the next JitEntry

        switch(ins.info->bc)
        {
        case asBC_JitEntry:
        {
            emit("\t\tl_bc += 1+AS_PTR_SIZE;\n");
            break;
        }

        case asBC_SUSPEND:
        {
            log(*m_compiler, function.script_function(), LogSeverity::PERF_WARNING, "asBC_SUSPEND found; this will fallback to the VM and be slow!");
            emit_vm_fallback(function, "SUSPEND is not implemented yet");
            break;
        }

        default:
        {
            emit_vm_fallback(function, "unsupported instruction");
            break;
        }
        }

        emit("\t}}\n");
    });

    emit("}}\n");

    return m_current_function_id;
}

std::string BytecodeToC::entry_point_name(
    ModuleId module_id,
    FunctionId function_id
) const {
    return fmt::format("asea_jit_mod{}_fn{}", module_id, function_id);
}

void BytecodeToC::emit_entry_dispatch(JitFunction& function)
{
    // 0 means the JIT entry point will not be used, start at 1
    asPWORD current_entry_id = 1;

    emit("\tswitch(entryLabel) {{\n");

    walk_bytecode(get_bytecode(function.script_function()), [&](BytecodeInstruction ins) {
        if (ins.info->bc != asBC_JitEntry)
        {
            return; // skip to the next
        }

        // patch the JIT entry with the index we use in the switch
        ins.arg_pword() = current_entry_id;

        emit("\tcase {}: goto bc{};\n", current_entry_id, ins.offset);

        ++current_entry_id;
    });

    emit("\t}}\n\n");
}

void BytecodeToC::emit_vm_fallback(JitFunction& function, std::string_view reason)
{
    // update VM registers
    emit(
        "\t\tm_regs.programPointer = l_bc;\n"
        "\t\tm_regs.stackPointer = l_sp;\n"
        "\t\tm_regs.stackFramePointer = l_fp;\n"
    );

    if (is_human_readable())
    {
        emit("\t\treturn; /* {} */\n", reason);
    }
    else
    {
        emit("\t\treturn;\n");
    }
}

bool BytecodeToC::is_human_readable() const
{
    return true;
}

void BytecodeToC::write_header()
{
    m_buffer += R"$(/* start of angelsea static header */

/*
    This generated source file contains macro definitions and references to
    internal structures extracted from the AngelScript scripting library, which
    are licensed under the zlib license (license provided below).

    Very minor modifications may have been applied to formatting or to allow
    compilation via a C compiler.
    This should still be possible to compile as plain C++; report bugs if not!

    Generated function definitions are the result of stitching of code stencils
    which are closely based on the definition and internal structure of the
    AngelScript virtual machine.
    Checks and references to variables may be elided at compile time when
    possible.
*/

/*
   AngelCode Scripting Library
   Copyright (c) 2003-2025 Andreas Jonsson

   This software is provided 'as-is', without any express or implied
   warranty. In no event will the authors be held liable for any
   damages arising from the use of this software.

   Permission is granted to anyone to use this software for any
   purpose, including commercial applications, and to alter it and
   redistribute it freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you
      must not claim that you wrote the original software. If you use
      this software in a product, an acknowledgment in the product
      documentation would be appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and
      must not be misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
      distribution.

   The original version of this library can be located at:
   http://www.angelcode.com/angelscript/

   Andreas Jonsson
   andreas@angelcode.com
*/

/*
    Definitions normally provided by <angelscript.h>, but that in case of JIT we
    override them with definitions provided by c2mir.
*/
#ifdef ANGELSEA_SUPPORT

typedef __INT8_TYPE__    asINT8;
typedef __INT16_TYPE__   asINT16;
typedef __INT32_TYPE__   asINT32;
typedef __UINT8_TYPE__   asBYTE;
typedef __UINT16_TYPE__  asWORD;
typedef __UINT32_TYPE__  asUINT;
typedef __UINT32_TYPE__  asDWORD;
typedef __UINT64_TYPE__  asQWORD;
typedef __UINTPTR_TYPE__ asPWORD;

#define asBC_DWORDARG(x)  (*(((asDWORD*)x)+1))
#define asBC_INTARG(x)    (*(int*)(((asDWORD*)x)+1))
#define asBC_QWORDARG(x)  (*(asQWORD*)(((asDWORD*)x)+1))
#define asBC_FLOATARG(x)  (*(float*)(((asDWORD*)x)+1))
#define asBC_PTRARG(x)    (*(asPWORD*)(((asDWORD*)x)+1))
#define asBC_WORDARG0(x)  (*(((asWORD*)x)+1))
#define asBC_WORDARG1(x)  (*(((asWORD*)x)+2))
#define asBC_SWORDARG0(x) (*(((short*)x)+1))
#define asBC_SWORDARG1(x) (*(((short*)x)+2))
#define asBC_SWORDARG2(x) (*(((short*)x)+3))

/* TODO: is this ever used in the VM other than AS_PTR_SIZE? */
#if __SIZEOF_POINTER__ == 4
	#define asBCTYPE_PTR_ARG    asBCTYPE_DW_ARG
	#define asBCTYPE_PTR_DW_ARG asBCTYPE_DW_DW_ARG
	#define asBCTYPE_wW_PTR_ARG asBCTYPE_wW_DW_ARG
	#define asBCTYPE_rW_PTR_ARG asBCTYPE_rW_DW_ARG
	#define AS_PTR_SIZE 1
#else
	#define asBCTYPE_PTR_ARG    asBCTYPE_QW_ARG
	#define asBCTYPE_PTR_DW_ARG asBCTYPE_QW_DW_ARG
	#define asBCTYPE_wW_PTR_ARG asBCTYPE_wW_QW_ARG
	#define asBCTYPE_rW_PTR_ARG asBCTYPE_rW_QW_ARG
	#ifndef AS_PTR_SIZE
	#define AS_PTR_SIZE 2
#endif

struct asIScriptContext;
struct asSVMRegisters
{
	asDWORD          *programPointer;     // points to current bytecode instruction
	asDWORD          *stackFramePointer;  // function stack frame
	asDWORD          *stackPointer;       // top of stack (grows downward)
	asQWORD           valueRegister;      // temp register for primitives
	void             *objectRegister;     // temp register for objects and handles
	asITypeInfo      *objectType;         // type of object held in object register
	bool              doProcessSuspend;   // whether or not the JIT should break out when it encounters a suspend instruction
	asIScriptContext *ctx;                // the active context
};
#endif

/* end of angelsea static header */

/* start of code generated by angelsea bytecode2c */
)$";
}

}