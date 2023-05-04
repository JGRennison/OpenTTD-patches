/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file y_combinator.hpp Y-combinator template implementation to support recursive lambdas and similar. */

#ifndef Y_COMBINATOR_HPP
#define Y_COMBINATOR_HPP

#include <functional>
#include <utility>

/* Based on C++ std library proposal: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0200r0.html */

template<class F>
class y_combinator_result {
	F func_;
public:
	template<class T>
	explicit y_combinator_result(T &&func): func_(std::forward<T>(func)) {}

	template<class ...Args>
	decltype(auto) operator()(Args &&...args) {
		return func_(std::ref(*this), std::forward<Args>(args)...);
	}
};

template<class F>
decltype(auto) y_combinator(F &&func) {
	return y_combinator_result<std::decay_t<F>>(std::forward<F>(func));
}

#endif /* Y_COMBINATOR_HPP */
