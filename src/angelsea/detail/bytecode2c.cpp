// SPDX-License-Identifier: BSD-2-Clause

#include "angelsea/detail/bytecodeinstruction.hpp"
#define FMT_COMPILE
#include <fmt/format.h>

#include <angelsea/detail/bytecode2c.hpp>
#include <angelsea/detail/bytecodedisasm.hpp>
#include <angelsea/detail/bytecodetools.hpp>

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
    emit("void {name}(asSVMRegisters *regs, asPWORD entryLabel) {{\n", fmt::arg("name", func_name));

    walk_bytecode(get_bytecode(function.script_function()), [&](BytecodeInstruction instruction) {
        emit("\t// bytecode: {}\n", disassemble(m_compiler->engine(), instruction));
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
    prefer to specify those as standard C
*/
#ifdef ANGELSEA_SUPPORT

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

typedef signed char    asINT8;
typedef signed short   asINT16;
typedef signed int     asINT32;
typedef unsigned char  asBYTE;
typedef unsigned short asWORD;
typedef unsigned int   asUINT;
typedef unsigned long long asPWORD; /* angelsea: asPWORD as unsigned long... can we use stdddef/stdint? FIXME: probably broken for 32-bit? */

#endif

/* end of angelsea static header */

/* start of code generated by angelsea bytecode2c */
)$";
}

}