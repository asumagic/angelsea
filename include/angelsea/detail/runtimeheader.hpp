// SPDX-License-Identifier: BSD-2-Clause

#include <string_view>

namespace angelsea::detail {

constexpr std::string_view angelsea_c_header = R"___(/* start of angelsea static header */

/*
    This generated source file contains macro definitions and references to
    internal structures extracted from the AngelScript scripting library, which
    are licensed under the zlib license (license provided below).

    Very minor modifications may have been applied to formatting or to allow
    compilation via a C compiler.

    This file should NOT be compiled by a C++ compiler, as it relies on type
    punning thru unions in a way that is not legal in C++.

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
typedef __INT64_TYPE__   asINT64;
typedef __UINT8_TYPE__   asBYTE;
typedef __UINT16_TYPE__  asWORD;
typedef __UINT32_TYPE__  asUINT;
typedef __UINT32_TYPE__  asDWORD;
typedef __UINT64_TYPE__  asQWORD;
typedef __UINTPTR_TYPE__ asPWORD;

#define asASSERT

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
	#define AS_PTR_SIZE 2
#endif

#define VALUE_OF_BOOLEAN_TRUE 1

typedef enum
{
	asEXECUTION_FINISHED        = 0,
	asEXECUTION_SUSPENDED       = 1,
	asEXECUTION_ABORTED         = 2,
	asEXECUTION_EXCEPTION       = 3,
	asEXECUTION_PREPARED        = 4,
	asEXECUTION_UNINITIALIZED   = 5,
	asEXECUTION_ACTIVE          = 6,
	asEXECUTION_ERROR           = 7,
	asEXECUTION_DESERIALIZATION = 8
} asEContextState;

/*
	Union to provide safe type punning with various AngelScript variables (as
	far as C aliasing rules allow, but not C++'s)
	This is only _fully_ legal and could theoretically break if the compiler can
	see beyond its compile unit (e.g. with LTO) but it should be otherwise
	unproblematic (and AS itself does worse, anyway).
*/
typedef union {
	asINT8 as_asINT8;
	asINT16 as_asINT16;
	asINT32 as_asINT32;
	asINT64 as_asINT64;
	asBYTE as_asBYTE;
	asWORD as_asWORD;
	asDWORD as_asDWORD;
	asQWORD as_asQWORD;
	asPWORD as_asPWORD;
	float as_float;
	double as_double;
	void* as_ptr;
} asea_var;

typedef struct asSVMRegisters asSVMRegisters;
typedef struct asIScriptContext asIScriptContext;
typedef struct asITypeInfo asITypeInfo;
typedef struct asCScriptFunction asCScriptFunction;
typedef struct asCObjectType asCObjectType;
typedef struct asSTypeBehaviour asSTypeBehaviour;

/* Layout exactly mimics asSVMRegisters */
typedef struct
{
	/*
		We rewrite some of the asDWORD* pointers to be void* instead; this is
		across the compile boundary in the case of JIT.
	*/
	asDWORD          *programPointer;     /* points to current bytecode instruction */
	void             *stackFramePointer;  /* function stack frame */
	void             *stackPointer;       /* top of stack (grows downward) */
	asea_var          valueRegister;      /* temp register for primitives */
	void             *objectRegister;     /* temp register for objects and handles */
	asITypeInfo      *objectType;         /* type of object held in object register */
	/* HACK: doProcessSuspend is normally defined as bool in C++; assume int equivalent */
	int              doProcessSuspend;    /* whether or not the JIT should break out when it encounters a suspend instruction */
	asIScriptContext *ctx;                /* the active context */
} asea_vm_registers;

#endif

/*
    The following definitions are part of the angelsea runtime.hpp
*/

void asea_call_script_function(asSVMRegisters* vm_registers, void* function);
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);
void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text);

/*
    The following definitions are additional angelsea helpers
*/

#define ASEA_STACK_DWORD_OFFSET(base, dword_offset) (void*)((char*)(base) + ((dword_offset) * 4))
#define ASEA_FRAME_VAR(dword_offset) (*(asea_var*)(ASEA_STACK_DWORD_OFFSET(l_fp, -(dword_offset))))
#define ASEA_STACK_VAR(dword_offset) (*(asea_var*)(ASEA_STACK_DWORD_OFFSET(l_sp, (dword_offset))))
#define ASEA_STACK_TOP (*(asea_var*)(l_sp))
#define ASEA_VALUEREG_DEREF() (*(asea_var*)(regs->valueRegister.as_ptr))

/* end of angelsea static header */

/* start of code generated by angelsea bytecode2c */
)___";

}
