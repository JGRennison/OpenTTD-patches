/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file serialisation.hpp Functions related to (de)serialisation of buffers */

#ifndef SERIALISATION_HPP
#define SERIALISATION_HPP

#include "bitmath_func.hpp"

#include <vector>
#include <string>

void   BufferSend_bool  (std::vector<byte> &buffer, size_t limit, bool   data);
void   BufferSend_uint8 (std::vector<byte> &buffer, size_t limit, uint8  data);
void   BufferSend_uint16(std::vector<byte> &buffer, size_t limit, uint16 data);
void   BufferSend_uint32(std::vector<byte> &buffer, size_t limit, uint32 data);
void   BufferSend_uint64(std::vector<byte> &buffer, size_t limit, uint64 data);
void   BufferSend_string(std::vector<byte> &buffer, size_t limit, const std::string_view data);
size_t BufferSend_bytes (std::vector<byte> &buffer, size_t limit, const byte *begin, const byte *end);
void   BufferSend_binary(std::vector<byte> &buffer, size_t limit, const char *data, const size_t size);

template <typename T>
struct BufferSerialisationHelper {
	void Send_bool(bool data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_bool(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint8(uint8 data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint8(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint16(uint16 data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint16(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint32(uint32 data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint32(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_uint64(uint64 data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_uint64(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	void Send_string(const std::string_view data)
	{
		T *self = static_cast<T *>(this);
		BufferSend_string(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data);
	}

	size_t Send_bytes(const byte *begin, const byte *end)
	{
		T *self = static_cast<T *>(this);
		return BufferSend_bytes(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), begin, end);
	}

	void Send_binary(const char *data, const size_t size)
	{
		T *self = static_cast<T *>(this);
		BufferSend_binary(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data, size);
	}
};

#endif /* SERIALISATION_HPP */
