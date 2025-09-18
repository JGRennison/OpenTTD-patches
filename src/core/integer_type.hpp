/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file integer_type.hpp Integer type helper. */

#ifndef INTEGER_TYPE_HPP
#define INTEGER_TYPE_HPP

template<int N>
struct sized_uint {};
template<> struct sized_uint<8>  { using type = uint8_t; };
template<> struct sized_uint<16> { using type = uint16_t; };
template<> struct sized_uint<32> { using type = uint32_t; };
template<> struct sized_uint<64> { using type = uint64_t; };

template<int N>
struct sized_int {};
template<> struct sized_int<8>  { using type = int8_t; };
template<> struct sized_int<16> { using type = int16_t; };
template<> struct sized_int<32> { using type = int32_t; };
template<> struct sized_int<64> { using type = int64_t; };

template<bool SIGNED, int N>
using sized_integer_conditional_sign = std::conditional_t<SIGNED, sized_int<N>, sized_uint<N>>;

template<typename T> requires std::is_arithmetic_v<T>
using sized_integer_as = sized_integer_conditional_sign<std::is_signed_v<T>, sizeof(T) * 8>;

#endif /* INTEGER_TYPE_HPP */
