// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/detail/bytecodedisasm.hpp>
#include <angelsea/detail/jitcompiler.hpp>
#include <angelsea/detail/debug.hpp>
#include <fmt/format.h>

namespace angelsea::detail
{

std::string disassemble(
    asIScriptEngine& engine,
    BytecodeInstruction instruction
) {
	// Handle certain instructions specifically
	switch (instruction.info->bc)
	{
	// case asBC_JitEntry:
	// case asBC_SUSPEND:
	// {
	// 	return {};
	// }

	case asBC_CALL:
	case asBC_CALLSYS:
	case asBC_Thiscall1:
	{
        auto* func = static_cast<asIScriptFunction*>(engine.GetFunctionById(instruction.arg_int()));

        angelsea_assert(func != nullptr);

        return fmt::format(
            "{} {} # {}", instruction.info->name, func->GetName(), func->GetDeclaration(true, true, true));
	}

	default: break;
	}

	// Disassemble based on the generic instruction type
	// TODO: figure out variable references
	switch (instruction.info->type)
	{
	case asBCTYPE_NO_ARG:
	{
		return fmt::format("{}", instruction.info->name);
	}

	case asBCTYPE_W_ARG:
	case asBCTYPE_wW_ARG:
	case asBCTYPE_rW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, instruction.arg_sword0());
	}

	case asBCTYPE_DW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, instruction.arg_int());
	}

	case asBCTYPE_rW_DW_ARG:
	case asBCTYPE_wW_DW_ARG:
	case asBCTYPE_W_DW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_int());
	}

	case asBCTYPE_QW_ARG:
	{
		return fmt::format("{} {}", instruction.info->name, instruction.arg_pword());
	}

	case asBCTYPE_DW_DW_ARG:
	{
		// TODO: double check this
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_int(), instruction.arg_int(1));
	}

	case asBCTYPE_wW_rW_rW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			instruction.arg_sword0(),
			instruction.arg_sword1(),
			instruction.arg_sword2());
	}

	case asBCTYPE_wW_QW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_pword(1));
	}

	case asBCTYPE_wW_rW_ARG:
	case asBCTYPE_rW_rW_ARG:
	case asBCTYPE_wW_W_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_sword1());
	}

	case asBCTYPE_wW_rW_DW_ARG:
	case asBCTYPE_rW_W_DW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			instruction.arg_sword0(),
			instruction.arg_sword1(),
			instruction.arg_int(1));
	}

	case asBCTYPE_QW_DW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_pword(), instruction.arg_int(2));
	}

	case asBCTYPE_rW_QW_ARG:
	{
		return fmt::format("{} {} {}", instruction.info->name, instruction.arg_sword0(), instruction.arg_pword(1));
	}

	case asBCTYPE_rW_DW_DW_ARG:
	{
		return fmt::format(
			"{} {} {} {}",
			instruction.info->name,
			instruction.arg_sword0(),
			instruction.arg_int(1),
			instruction.arg_int(2));
	}

	default:
	{
		return fmt::format("(unimplemented)");
	}
	}
}

}
