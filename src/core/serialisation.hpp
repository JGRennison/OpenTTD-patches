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
#include "../string_type.h"
#include "../string_func.h"

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
void   BufferSend_buffer(std::vector<byte> &buffer, size_t limit, const byte *data, const size_t size);

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

	void Send_buffer(const byte *data, const size_t size)
	{
		T *self = static_cast<T *>(this);
		BufferSend_buffer(self->GetSerialisationBuffer(), self->GetSerialisationLimit(), data, size);
	}

	void Send_buffer(const std::vector<byte> &data)
	{
		this->Send_buffer(data.data(), data.size());
	}
};

void BufferRecvStringValidate(std::string &buffer, StringValidationSettings settings);

template <typename T>
struct BufferDeserialisationHelper {
private:
	const byte *GetBuffer()
	{
		return static_cast<T *>(this)->GetDeserialisationBuffer();
	}

	size_t GetBufferSize()
	{
		return static_cast<T *>(this)->GetDeserialisationBufferSize();
	}

public:
	bool CanRecvBytes(size_t bytes_to_read, bool raise_error = true)
	{
		return static_cast<T *>(this)->CanDeserialiseBytes(bytes_to_read, raise_error);
	}

	/**
	 * Read a boolean from the packet.
	 * @return The read data.
	 */
	bool Recv_bool()
	{
		return this->Recv_uint8() != 0;
	}

	/**
	 * Read a 8 bits integer from the packet.
	 * @return The read data.
	 */
	uint8 Recv_uint8()
	{
		uint8 n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n = this->GetBuffer()[pos++];
		return n;
	}

	/**
	 * Read a 16 bits integer from the packet.
	 * @return The read data.
	 */
	uint16 Recv_uint16()
	{
		uint16 n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n  = (uint16)this->GetBuffer()[pos++];
		n += (uint16)this->GetBuffer()[pos++] << 8;
		return n;
	}

	/**
	 * Read a 32 bits integer from the packet.
	 * @return The read data.
	 */
	uint32 Recv_uint32()
	{
		uint32 n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n  = (uint32)this->GetBuffer()[pos++];
		n += (uint32)this->GetBuffer()[pos++] << 8;
		n += (uint32)this->GetBuffer()[pos++] << 16;
		n += (uint32)this->GetBuffer()[pos++] << 24;
		return n;
	}

	/**
	 * Read a 64 bits integer from the packet.
	 * @return The read data.
	 */
	uint64 Recv_uint64()
	{
		uint64 n;

		if (!this->CanRecvBytes(sizeof(n), true)) return 0;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		n  = (uint64)this->GetBuffer()[pos++];
		n += (uint64)this->GetBuffer()[pos++] << 8;
		n += (uint64)this->GetBuffer()[pos++] << 16;
		n += (uint64)this->GetBuffer()[pos++] << 24;
		n += (uint64)this->GetBuffer()[pos++] << 32;
		n += (uint64)this->GetBuffer()[pos++] << 40;
		n += (uint64)this->GetBuffer()[pos++] << 48;
		n += (uint64)this->GetBuffer()[pos++] << 56;
		return n;
	}

	/**
	 * Reads characters (bytes) from the packet until it finds a '\0', or reaches a
	 * maximum of \c length characters.
	 * When the '\0' has not been reached in the first \c length read characters,
	 * more characters are read from the packet until '\0' has been reached. However,
	 * these characters will not end up in the returned string.
	 * The length of the returned string will be at most \c length - 1 characters.
	 * @param length   The maximum length of the string including '\0'.
	 * @param settings The string validation settings.
	 * @return The validated string.
	 */
	std::string Recv_string(size_t length, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK)
	{
		assert(length > 1);

		/* Both loops with Recv_uint8 terminate when reading past the end of the
		 * packet as Recv_uint8 then closes the connection and returns 0. */
		std::string str;
		char character;
		while (--length > 0 && (character = this->Recv_uint8()) != '\0') str.push_back(character);

		if (length == 0) {
			/* The string in the packet was longer. Read until the termination. */
			while (this->Recv_uint8() != '\0') {}
		}

		BufferRecvStringValidate(str, settings);
		return str;
	}

	/**
	 * Reads a string till it finds a '\0' in the stream.
	 * @param buffer The buffer to put the data into.
	 * @param settings The string validation settings.
	 */
	void Recv_string(std::string &buffer, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK)
	{
		/* Don't allow reading from a closed socket */
		if (!this->CanRecvBytes(0, false)) return;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		size_t length = ttd_strnlen((const char *)(this->GetBuffer() + pos), this->GetBufferSize() - pos - 1);
		buffer.assign((const char *)(this->GetBuffer() + pos), length);
		pos += (decltype(pos))length + 1;
		BufferRecvStringValidate(buffer, settings);
	}

	/**
	 * Reads binary data.
	 * @param buffer The buffer to put the data into.
	 * @param size   The size of the data.
	 */
	void Recv_binary(char *buffer, size_t size)
	{
		if (!this->CanRecvBytes(size, true)) return;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		memcpy(buffer, &this->GetBuffer()[pos], size);
		pos += (decltype(pos)) size;
	}

	/**
	 * Reads binary data.
	 * @param buffer The buffer to put the data into.
	 * @param size   The size of the data.
	 */
	void Recv_binary(std::string &buffer, size_t size)
	{
		if (!this->CanRecvBytes(size, true)) return;

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();

		buffer.assign((const char *) &this->GetBuffer()[pos], size);
		pos += (decltype(pos)) size;
	}

	/**
	 * Reads a length-prefixed binary buffer from the packet.
	 * @return The binary buffer.
	 */
	std::vector<uint8> Recv_buffer()
	{
		uint16 length = this->Recv_uint16();

		if (!this->CanRecvBytes(length, true)) return {};

		auto &pos = static_cast<T *>(this)->GetDeserialisationPosition();
		std::vector<uint8> buffer { &this->GetBuffer()[pos], &this->GetBuffer()[pos + length] };
		pos += length;

		return buffer;
	}
};

#endif /* SERIALISATION_HPP */
