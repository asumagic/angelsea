// SPDX-License-Identifier: BSD-2-Clause

#pragma once

namespace angelsea::detail {

template<class... Ts> struct overloaded : Ts... {
	using Ts::operator()...;
};

} // namespace angelsea::detail