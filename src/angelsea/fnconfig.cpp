// SPDX-License-Identifier: BSD-2-Clause

#include <angelsea/fnconfig.hpp>

namespace angelsea {

void parse_function_metadata(FnConfig& config, std::string_view metadata) {
	if (metadata == "ignore_perf_warnings") {
		config.ignore_perf_warnings = true;
	} else if (metadata == "disable_jit") {
		config.disable_jit = true;
	} else if (metadata == "dump_c") {
		config.dump_c = true;
	}
}

} // namespace angelsea