/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file math_func.cpp Math functions. */

#include "../stdafx.h"
#include "math_func.hpp"
#include "bitmath_func.hpp"

#include "../safeguards.h"

/**
 * Compute least common multiple (lcm) of arguments \a a and \a b, the smallest
 * integer value that is a multiple of both \a a and \a b.
 * @param a First number.
 * @param b second number.
 * @return Least common multiple of values \a a and \a b.
 *
 * @note This function only works for non-negative values of \a a and \a b.
 */
int LeastCommonMultiple(int a, int b)
{
	if (a == 0 || b == 0) return 0; // By definition.
	if (a == 1 || a == b) return b;
	if (b == 1) return a;

	return a * b / GreatestCommonDivisor(a, b);
}

/**
 * Compute greatest common divisor (gcd) of \a a and \a b.
 * @param a First number.
 * @param b second number.
 * @return Greatest common divisor of \a a and \a b.
 */
int GreatestCommonDivisor(int a, int b)
{
	while (b != 0) {
		int t = b;
		b = a % b;
		a = t;
	}
	return a;

}

/**
 * Deterministic approximate division.
 * Cancels out division errors stemming from the integer nature of the division over multiple runs.
 * @param a Dividend.
 * @param b Divisor.
 * @return a/b or (a/b)+1.
 */
int DivideApprox(int a, int b)
{
	int random_like = (((int64) (a + b)) * ((int64) (a - b))) % b;

	int remainder = a % b;

	int ret = a / b;
	if (abs(random_like) < abs(remainder)) {
		ret += ((a < 0) ^ (b < 0)) ? -1 : 1;
	}

	return ret;
}

/**
 * Compute the integer square root.
 * @param num Radicand.
 * @return Rounded integer square root.
 * @note Algorithm taken from http://en.wikipedia.org/wiki/Methods_of_computing_square_roots
 */
uint32 IntSqrt(uint32 num)
{
	uint32 res = 0;
	uint32 bit = 1UL << 30; // Second to top bit number.

	/* 'bit' starts at the highest power of four <= the argument. */
	while (bit > num) bit >>= 2;

	while (bit != 0) {
		if (num >= res + bit) {
			num -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}

	/* Arithmetic rounding to nearest integer. */
	if (num > res) res++;

	return res;
}

/**
 * Compute the integer square root.
 * @param num Radicand.
 * @return Rounded integer square root.
 * @note Algorithm taken from http://en.wikipedia.org/wiki/Methods_of_computing_square_roots
 */
uint32 IntSqrt64(uint64 num)
{
	uint64 res = 0;
	uint64 bit = 1ULL << 62; // Second to top bit number.

	/* 'bit' starts at the highest power of four <= the argument. */
	while (bit > num) bit >>= 2;

	while (bit != 0) {
		if (num >= res + bit) {
			num -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}

	/* Arithmetic rounding to nearest integer. */
	if (num > res) res++;

	return (uint32)res;
}

/**
 * Compute the integer cube root.
 * @param num Radicand.
 * @return Rounded integer square root.
 * @note Algorithm taken from https://stackoverflow.com/a/56738014
 */
uint32 IntCbrt(uint64 num)
{
	uint64 r0 = 1;
	uint64 r1 = 0;

	if (num == 0) return 0;

#ifdef WITH_BITMATH_BUILTINS
	int b = 64 - __builtin_clzll(num);
#ifdef _DEBUG
	assert(b == FindLastBit(num) + 1);
#endif
#else
	int b = FindLastBit(num) + 1;
#endif

	r0 <<= (b + 2) / 3; /* ceil(b / 3) */

	do /* quadratic convergence: */
	{
		r1 = r0;
		r0 = (2 * r1 + num / (r1 * r1)) / 3;
	}
	while (r0 < r1);

	return ((uint32) r1); /* floor(cbrt(x)); */
}

/**
 * Compress unsigned integer into 16 bits, in a way that increases dynamic range, at the expense of precision for large values
 */
uint16 RXCompressUint(uint32 num)
{
	if (num <= 0x100) return num;
	if (num <= 0x7900) return 0x100 + ((num - 0x100) >> 3);
	return std::min<uint32>(UINT16_MAX, 0x1000 + ((num - 0x7900) >> 6));
}

/**
 * Inverse of RXCompressUint
 */
uint32 RXDecompressUint(uint16 num)
{
	if (num > 0x1000) return ((num - 0x1000) << 6) + 0x7900;
	if (num > 0x100) return ((num - 0x100) << 3) + 0x100;
	return num;
}
