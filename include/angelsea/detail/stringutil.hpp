// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <fmt/format.h>
#include <string>
#include <string_view>

namespace angelsea::detail {

inline bool is_alpha_numerical(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

inline std::string escape_c_literal(std::string_view str) {
	std::string ret;
	ret.reserve(str.size()); // always underestimated but that's ok

	std::string_view legal_chars = "!#%&'()*+,-./:;<=>?[]^_{|}~ ";

	for (char c : str) {
		// handle the most common cases (either paste the characters as-is or
		// escape them) for the string to be readable
		if (is_alpha_numerical(c) || legal_chars.find(c) != legal_chars.npos) {
			ret += c;
		} else if (c == '\r') {
			ret += "\\r";
		} else if (c == '\n') {
			ret += "\\n";
		} else if (c == '\t') {
			ret += "\\t";
		} else if (c == '"') {
			ret += "\\\"";
		} else if (c == '\\') {
			ret += "\\\\";
		} else {
			// hex encode the rest
			ret += fmt::format("\\x{:02x}", c);
		}
	}

	return ret;
}

} // namespace angelsea::detail