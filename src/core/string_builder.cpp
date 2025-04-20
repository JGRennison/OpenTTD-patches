/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string_builder.cpp Implementation of string composing. */

#include "../stdafx.h"
#include "string_builder.hpp"
#include "utf8.hpp"
#include "../safeguards.h"

/**
 * Append binary uint16 using little endian.
 */
void StringBuilder::PutUint16LE(uint16_t value)
{
	std::array<char, 2> buf{
		static_cast<char>(static_cast<uint8_t>(value)),
		static_cast<char>(static_cast<uint8_t>(value >> 8))
	};
	this->PutBuffer(buf);
}

/**
 * Append binary int16 using little endian.
 */
void StringBuilder::PutSint16LE(int16_t value)
{
	this->PutUint16LE(static_cast<uint16_t>(value));
}

/**
 * Append binary uint32 using little endian.
 */
void StringBuilder::PutUint32LE(uint32_t value)
{
	std::array<char, 4> buf{
		static_cast<char>(static_cast<uint8_t>(value)),
		static_cast<char>(static_cast<uint8_t>(value >> 8)),
		static_cast<char>(static_cast<uint8_t>(value >> 16)),
		static_cast<char>(static_cast<uint8_t>(value >> 24))
	};
	this->PutBuffer(buf);
}

/**
 * Append binary int32 using little endian.
 */
void StringBuilder::PutSint32LE(int32_t value)
{
	this->PutUint32LE(static_cast<uint32_t>(value));
}

/**
 * Append binary uint64 using little endian.
 */
void StringBuilder::PutUint64LE(uint64_t value)
{
	std::array<char, 8> buf{
		static_cast<char>(static_cast<uint8_t>(value)),
		static_cast<char>(static_cast<uint8_t>(value >> 8)),
		static_cast<char>(static_cast<uint8_t>(value >> 16)),
		static_cast<char>(static_cast<uint8_t>(value >> 24)),
		static_cast<char>(static_cast<uint8_t>(value >> 32)),
		static_cast<char>(static_cast<uint8_t>(value >> 40)),
		static_cast<char>(static_cast<uint8_t>(value >> 48)),
		static_cast<char>(static_cast<uint8_t>(value >> 56))
	};
	this->PutBuffer(buf);
}

/**
 * Append binary int64 using little endian.
 */
void StringBuilder::PutSint64LE(int64_t value)
{
	this->PutUint64LE(static_cast<uint64_t>(value));
}

void StringBuilder::PutUtf8Impl(char32_t c)
{
	auto [buf, len] = EncodeUtf8(c);
	this->PutBuffer({buf, len});
}
