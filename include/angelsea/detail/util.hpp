// SPDX-License-Identifier: BSD-2-Clause

#pragma once

namespace angelsea::detail {

template<class... Ts> struct overloaded : Ts... {
	using Ts::operator()...;
};

// shouldn't be needed with C++20 but the clang version on ubuntu 22.04 seems to dislike not having it
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

} // namespace angelsea::detail