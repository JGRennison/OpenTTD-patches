/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string_consumer.cpp Implementation of string parsing. */

#include "../stdafx.h"
#include "string_consumer.hpp"

#include "bitmath_func.hpp"
#include "utf8.hpp"
#include "format.hpp"

#include "../string_func.h"

#if defined(STRGEN) || defined(SETTINGSGEN)
#include "../error_func.h"
#else
#include "../debug.h"
#endif

#include <charconv>

#include "../safeguards.h"

/* static */ const std::string_view StringConsumer::WHITESPACE_NO_NEWLINE = "\t\v\f\r ";
/* static */ const std::string_view StringConsumer::WHITESPACE_OR_NEWLINE = "\t\n\v\f\r ";

/* static */ void StringConsumer::LogError(std::string &&msg)
{
#if defined(STRGEN) || defined(SETTINGSGEN)
	FatalErrorI(std::move(msg));
#else
	debug_print(DebugLevelID::misc, 0, msg);
#endif
}

std::optional<uint16_t> StringConsumer::PeekUint16LE() const
{
	if (this->GetBytesLeft() < 2) return std::nullopt;
	return static_cast<uint8_t>(this->src[this->position]) |
		static_cast<uint8_t>(this->src[this->position + 1]) << 8;
}

std::optional<uint32_t> StringConsumer::PeekUint32LE() const
{
	if (this->GetBytesLeft() < 4) return std::nullopt;
	return static_cast<uint8_t>(this->src[this->position]) |
		static_cast<uint8_t>(this->src[this->position + 1]) << 8 |
		static_cast<uint8_t>(this->src[this->position + 2]) << 16 |
		static_cast<uint8_t>(this->src[this->position + 3]) << 24;
}

std::optional<uint64_t> StringConsumer::PeekUint64LE() const
{
	if (this->GetBytesLeft() < 8) return std::nullopt;
	return static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position])) |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 1])) << 8 |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 2])) << 16 |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 3])) << 24 |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 4])) << 32 |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 5])) << 40 |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 6])) << 48 |
		static_cast<uint64_t>(static_cast<uint8_t>(this->src[this->position + 7])) << 56;
}

std::pair<StringConsumer::size_type, char32_t> StringConsumer::PeekUtf8() const
{
	auto buf = this->src.substr(this->position);
	return DecodeUtf8(buf);
}

std::string_view StringConsumer::Peek(size_type len) const
{
	auto buf = this->src.substr(this->position);
	if (len == std::string_view::npos) {
		len = buf.size();
	} else if (len > buf.size()) {
		len = buf.size();
	}
	return buf.substr(0, len);
}

void StringConsumer::Skip(size_type len)
{
	if (len == std::string_view::npos) {
		this->position = this->src.size();
	} else if (size_type max_len = GetBytesLeft(); len > max_len) {
		LogError(fmt::format("Source buffer too short: {} > {}", len, max_len));
		this->position = this->src.size();
	} else {
		this->position += len;
	}
}

StringConsumer::size_type StringConsumer::Find(std::string_view str) const
{
	assert(!str.empty());
	auto buf = this->src.substr(this->position);
	return buf.find(str);
}

StringConsumer::size_type StringConsumer::FindUtf8(char32_t c) const
{
	auto [data, len] = EncodeUtf8(c);
	return this->Find({data, len});
}

StringConsumer::size_type StringConsumer::FindCharIn(std::string_view chars) const
{
	assert(!chars.empty());
	auto buf = this->src.substr(this->position);
	return buf.find_first_of(chars);
}

StringConsumer::size_type StringConsumer::FindCharNotIn(std::string_view chars) const
{
	assert(!chars.empty());
	auto buf = this->src.substr(this->position);
	return buf.find_first_not_of(chars);
}

std::string_view StringConsumer::PeekUntil(std::string_view str, SeparatorUsage sep) const
{
	assert(!str.empty());
	auto buf = this->src.substr(this->position);
	auto len = buf.find(str);
	if (len != std::string_view::npos) {
		switch (sep) {
			case READ_ONE_SEPARATOR:
				if (buf.compare(len, str.size(), str) == 0) len += str.size();
				break;
			case READ_ALL_SEPARATORS:
				while (buf.compare(len, str.size(), str) == 0) len += str.size();
				break;
			default:
				break;
		}
	}
	return buf.substr(0, len);
}

std::string_view StringConsumer::PeekUntilUtf8(char32_t c, SeparatorUsage sep) const
{
	auto [data, len] = EncodeUtf8(c);
	return PeekUntil({data, len}, sep);
}

std::string_view StringConsumer::ReadUntilUtf8(char32_t c, SeparatorUsage sep)
{
	auto [data, len] = EncodeUtf8(c);
	return ReadUntil({data, len}, sep);
}

void StringConsumer::SkipUntilUtf8(char32_t c, SeparatorUsage sep)
{
	auto [data, len] = EncodeUtf8(c);
	return SkipUntil({data, len}, sep);
}

void StringConsumer::SkipIntegerBase(int base)
{
	this->SkipIf("-");
	if (base == 0) {
		if (this->ReadIf("0x") || this->ReadIf("0X")) { // boolean short-circuit ensures only one prefix is read
			base = 16;
		} else {
			base = 10;
		}
	}
	switch (base) {
		default:
			assert(false);
			break;
		case 8:
			this->Skip(this->FindCharIf([](char c) {
				return c < '0' || c > '7';
			}));
			break;
		case 10:
			this->Skip(this->FindCharIf([](char c) {
				return c < '0' || c > '9';
			}));
			break;
		case 16:
			this->Skip(this->FindCharIf([](char c) {
				if (c >= '0' && c <= '9') return false;
				c |= 0x20;
				return c < 'a' || c > 'f';
			}));
			break;
	}
}

void StringConsumer::LogErrorBufferTooShort(StringConsumer::size_type len, StringConsumer::size_type size)
{
	LogError(fmt::format("Source buffer too short: {} > {}", len, size));
}

void StringConsumer::LogErrorIntegerOutOfRange(std::string_view str)
{
	LogError(fmt::format("Integer out of range: '{}'", str));
}

void StringConsumer::LogErrorIntegerOutOfRange2(std::string_view str, std::string_view str2)
{
	LogError(fmt::format("Integer out of range: '{}'+'{}'", str, str2));
}

void StringConsumer::LogErrorCannotParseInteger(std::string_view str, std::string_view str2)
{
	LogError(fmt::format("Cannot parse integer: '{}'+'{}'", str, str2));
}

template <class T>
std::pair<StringConsumer::size_type, T> StringConsumer::ParseIntegerBase(std::string_view src, int base, bool clamp, bool log_errors)
{
	if (base == 0) {
		/* Try positive hex */
		if (src.starts_with("0x") || src.starts_with("0X")) {
			auto [len, value] = ParseIntegerBase<T>(src.substr(2), 16, clamp, log_errors);
			if (len == 0) return {};
			return {len + 2, value};
		}

		/* Try negative hex */
		if (std::is_signed_v<T> && (src.starts_with("-0x") || src.starts_with("-0X"))) {
			using Unsigned = std::make_unsigned_t<T>;
			auto [len, uvalue] = ParseIntegerBase<Unsigned>(src.substr(3), 16, clamp, log_errors);
			if (len == 0) return {};
			T value = static_cast<T>(0 - uvalue);
			if (value > 0) {
				if (!clamp) {
					if (log_errors) LogErrorIntegerOutOfRange(src.substr(0, len + 3));
					return {};
				}
				value = std::numeric_limits<T>::lowest();
			}
			return {len + 3, value};
		}

		/* Try decimal */
		return ParseIntegerBase<T>(src, 10, clamp, log_errors);
	}

	T value{};
	assert(base == 8 || base == 10 || base == 16); // we only support these bases when skipping
	auto result = std::from_chars(src.data(), src.data() + src.size(), value, base);
	auto len = result.ptr - src.data();
	if (result.ec == std::errc::result_out_of_range) {
		if (!clamp) {
			if (log_errors) LogErrorCannotParseInteger(src.substr(0, len), src.substr(len, 4));
			return {};
		}
		if (src.starts_with("-")) {
			value = std::numeric_limits<T>::lowest();
		} else {
			value = std::numeric_limits<T>::max();
		}
	} else if (result.ec != std::errc{}) {
		if (log_errors) LogErrorCannotParseInteger(src.substr(0, len), src.substr(len, 4));
		return {};
	}
	return {len, value};
}

template std::pair<StringConsumer::size_type, int64_t> StringConsumer::ParseIntegerBase<int64_t>(std::string_view src, int base, bool clamp, bool log_errors);
template std::pair<StringConsumer::size_type, uint64_t> StringConsumer::ParseIntegerBase<uint64_t>(std::string_view src, int base, bool clamp, bool log_errors);
template std::pair<StringConsumer::size_type, int32_t> StringConsumer::ParseIntegerBase<int32_t>(std::string_view src, int base, bool clamp, bool log_errors);
template std::pair<StringConsumer::size_type, uint32_t> StringConsumer::ParseIntegerBase<uint32_t>(std::string_view src, int base, bool clamp, bool log_errors);
