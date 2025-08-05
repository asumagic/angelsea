// SPDX-License-Identifier: BSD-2-Clause

#include <string_view>

namespace angelsea::detail {

constexpr std::string_view angelsea_c_header_copyright = R"___(/*
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

)___";

constexpr std::string_view angelsea_c_header =
    // JIT-only definitions that don't depend on angelscript.h
    R"___(#ifdef ASEA_SUPPORT

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

#if __SIZEOF_POINTER__ == 4
	#define AS_PTR_SIZE 1
#else
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
)___"

    // Union to provide safe type punning with various AngelScript variables (as far as C aliasing rules allow, but
    // not C++'s) This is only _fully_ legal and could theoretically break if the compiler can  see beyond its compile
    // unit (e.g. with LTO) but it should be otherwise unproblematic (and AS itself does worse, anyway).
    R"___(
union asea_var_u;
union asea_var_u {
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
	union asea_var_u* as_var_ptr;
};
typedef union asea_var_u asea_var;

typedef struct asSVMRegisters asSVMRegisters;
typedef struct asIScriptContext asIScriptContext;
typedef struct asITypeInfo asITypeInfo;
typedef struct asCScriptFunction asCScriptFunction;
typedef struct asCObjectType asCObjectType;
typedef struct asSTypeBehaviour asSTypeBehaviour;

#endif

typedef struct {)___"
    // The layout must be compatible with asSVMRegisters.
    // We rewrite some of the asDWORD* pointers to be void* instead; this is across the compile boundary in the case of
    // JIT.
    "\tasDWORD *pc;\n"           // points to current bytecode instruction
    "\tvoid *fp;\n"              // function stack frame
    "\tvoid *sp;\n"              // top of stack (grows downward)
    "\tasea_var value;\n"        // temp register for primitives
    "\tvoid *obj;\n"             // temp register for objects and handles
    "\tasITypeInfo *obj_type;\n" // type of object held in object register
    // HACK: doProcessSuspend is normally defined as bool in C++; assume char equivalent
    "\tchar do_suspend;\n"       // whether or the JIT should break out when it encounters asBC_SUSPEND
    "\tasIScriptContext *ctx;\n" // active script context

    R"___(} asea_vm_registers;

typedef union { float f; asDWORD i; } asea_i2f;)___"

    // Angelsea runtime functions, see runtime.hpp

    R"___(
void asea_call_script_function(asSVMRegisters* vm_registers, void* function);
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);
void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text);
float asea_fmodf(float a, float b);
float asea_fmod(float a, float b);
)___"

    // Helper macros
    R"___(
#define ASEA_FDIV(lhs, rhs) lhs / rhs
#define ASEA_FMOD32(lhs, rhs) asea_fmodf(lhs, rhs)
#define ASEA_FMOD64(lhs, rhs) asea_fmod(lhs, rhs)

/* start of code generated by angelsea bytecode2c */
)___";

} // namespace angelsea::detail
