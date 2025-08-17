// SPDX-License-Identifier: BSD-2-Clause

namespace angelsea::detail {

constexpr const char* angelsea_c_header_copyright = R"___(/*
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

constexpr const char* angelsea_c_header =
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

typedef struct asSVMRegisters_t asSVMRegisters;
typedef struct asIScriptContext_t asIScriptContext;
typedef struct asITypeInfo_t asITypeInfo;
typedef struct asCScriptFunction_t asCScriptFunction;
typedef struct asCObjectType_t asCObjectType;
typedef struct asITypeInfo_t asITypeInfo;
typedef struct asCScriptEngine_t asCScriptEngine;
typedef struct asCScriptObject_t asCScriptObject;
typedef struct asSTypeBehaviour_t asSTypeBehaviour;

#endif

typedef struct {)___"
    // The layout must be compatible with asSVMRegisters.
    // We rewrite some of the asDWORD* pointers to be void* instead; this is across the compile boundary in the case of
    // JIT.
    "\tasDWORD *pc;\n"           // points to current bytecode instruction
    "\tvoid *fp;\n"              // function stack frame
    "\tvoid *sp;\n"              // top of stack (grows downward)
    "\tasQWORD value;\n"         // temp register for primitives
    "\tvoid *obj;\n"             // temp register for objects and handles
    "\tasITypeInfo *obj_type;\n" // type of object held in object register
    // HACK: doProcessSuspend is normally defined as bool in C++; assume char equivalent
    "\tchar do_suspend;\n"       // whether or the JIT should break out when it encounters asBC_SUSPEND
    "\tasIScriptContext *ctx;\n" // active script context

    R"___(} asea_vm_registers;

typedef struct {)___"
    // The layout must be compatible with asCGeneric. This makes assumptions about the C++ ABI! We do not actually make
    // use of the vtable; but we assume the location and size of it.
    // Used only when experimental direct generic calls are implemented.

    R"___(
	void *_vtable;
	asCScriptEngine *engine;
	asCScriptFunction *sysFunction;
	void *currentObject;
	asDWORD *stackPointer;
	void *objectRegister;
	asQWORD returnVal;
} asea_generic;

typedef union {
	float   f;
	asDWORD i;
} asea_i2f;

typedef union {
	double f;
	asQWORD i;
} asea_i2f64;

typedef struct {
	void* ptr;
	asUINT len;
	asUINT max_len;
} asea_array;

static asea_i2f asea_i2f_inst;
static asea_i2f64 asea_i2f64_inst;
)___"

    // Angelsea runtime functions, see runtime.hpp

    R"___(
void asea_call_script_function(asSVMRegisters* vm_registers, void* function);
int asea_prepare_script_stack(asSVMRegisters* vm_registers, void* function, void* pc, void* sp, void *fp);
int asea_prepare_script_stack_and_vars(asSVMRegisters* vm_registers, void* function, void* pc, void* sp, void *fp);
void asea_debug_message(asSVMRegisters* vm_registers, const char* text);
void asea_debug_int(asSVMRegisters* vm_registers, asPWORD x);
void asea_set_internal_exception(asSVMRegisters* vm_registers, const char* text);
float fmodf(float a, float b);
double fmod(double a, double b);
void asea_clean_args(asSVMRegisters* vm_registers, void* function, asDWORD* args);
int asea_call_system_function(asSVMRegisters* vm_registers, int fn);
int asea_call_object_method(asSVMRegisters* vm_registers, void* obj, int fn);
void* asea_new_script_object(asCObjectType* obj_type);
void asea_cast(asSVMRegisters* vm_registers, asCScriptObject* obj, asDWORD type_id);
void* asea_alloc(asQWORD size);
void  asea_free(void* ptr);

typedef void (*asea_jit_fn)(asSVMRegisters*, asPWORD);

extern char asea_generic_vtable[];
)___"

    // Helper macros
    R"___(
#define ASEA_FDIV(lhs, rhs) lhs / rhs
)___";

constexpr const char* angelsea_c_header_offsets =
    R"___(extern const asPWORD asea_offset_ctx_callstack;
extern const asPWORD asea_offset_ctx_status;
extern const asPWORD asea_offset_ctx_currentfn;
extern const asPWORD asea_offset_ctx_stackindex;
)___";

} // namespace angelsea::detail
