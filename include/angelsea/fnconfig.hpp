// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <string_view>

namespace angelsea {

/// Describes JIT tunables for a specific function
struct FnConfig {
	bool ignore_perf_warnings : 1 = false;
	bool disable_jit : 1          = false;
	bool dump_c : 1               = false;
};

/// Parses one function metadata entry, e.g. as obtained from
/// https://www.angelcode.com/angelscript/sdk/docs/manual/doc_addon_build.html#doc_addon_build_metadata
/// If using this method, you would call \ref parse_function_metadata once per metadata entry.
/// Script-side you could e.g. have [attrib_a][attrib_b]
/// Note that this function does not do prefixing at all. If for instance you want to require a `jit::` prefix, you need
/// to do it yourself.
void parse_function_metadata(FnConfig& config, std::string_view metadata);

} // namespace angelsea