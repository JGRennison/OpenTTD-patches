/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file serialisation.cpp Implementation related to (de)serialisation of buffers. */

#include "../stdafx.h"
#include "serialisation.hpp"
#include "../string_func_extra.h"

/**
 * Is it safe to write to the packet, i.e. didn't we run over the buffer?
 * @param bytes_to_write The amount of bytes we want to try to write.
 * @return True iff the given amount of bytes can be written to the packet.
 */
static bool BufferCanWriteToPacket(const std::vector<byte> &buffer, size_t limit, size_t bytes_to_write)
{
	return buffer.size() + bytes_to_write <= limit;
}

/*
 * The next couple of functions make sure we can send
 *  uint8, uint16, uint32 and uint64 endian-safe
 *  over the network. The least significant bytes are
 *  sent first.
 *
 *  So 0x01234567 would be sent as 67 45 23 01.
 *
 * A bool is sent as a uint8 where zero means false
 *  and non-zero means true.
 */

/**
 * Package a boolean in the packet.
 * @param data The data to send.
 */
void BufferSend_bool(std::vector<byte> &buffer, size_t limit, bool data)
{
	BufferSend_uint8(buffer, limit, data ? 1 : 0);
}

/**
 * Package a 8 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint8(std::vector<byte> &buffer, size_t limit, uint8 data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.emplace_back(data);
}

/**
 * Package a 16 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint16(std::vector<byte> &buffer, size_t limit, uint16 data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.insert(buffer.end(), {
		(uint8)GB(data,  0, 8),
		(uint8)GB(data,  8, 8),
	});
}

/**
 * Package a 32 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint32(std::vector<byte> &buffer, size_t limit, uint32 data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.insert(buffer.end(), {
		(uint8)GB(data,  0, 8),
		(uint8)GB(data,  8, 8),
		(uint8)GB(data, 16, 8),
		(uint8)GB(data, 24, 8),
	});
}

/**
 * Package a 64 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint64(std::vector<byte> &buffer, size_t limit, uint64 data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.insert(buffer.end(), {
		(uint8)GB(data,  0, 8),
		(uint8)GB(data,  8, 8),
		(uint8)GB(data, 16, 8),
		(uint8)GB(data, 24, 8),
		(uint8)GB(data, 32, 8),
		(uint8)GB(data, 40, 8),
		(uint8)GB(data, 48, 8),
		(uint8)GB(data, 56, 8),
	});
}

/**
 * Sends a string over the network. It sends out
 * the string + '\0'. No size-byte or something.
 * @param data The string to send
 */
void BufferSend_string(std::vector<byte> &buffer, size_t limit, const std::string_view data)
{
	assert(BufferCanWriteToPacket(buffer, limit, data.size() + 1));
	buffer.insert(buffer.end(), data.begin(), data.end());
	buffer.emplace_back('\0');
}

/**
 * Send as many of the bytes as possible in the packet. This can mean
 * that it is possible that not all bytes are sent. To cope with this
 * the function returns the amount of bytes that were actually sent.
 * @param begin The begin of the buffer to send.
 * @param end   The end of the buffer to send.
 * @return The number of bytes that were added to this packet.
 */
size_t BufferSend_bytes(std::vector<byte> &buffer, size_t limit, const byte *begin, const byte *end)
{
	size_t amount = std::min<size_t>(end - begin, limit - buffer.size());
	buffer.insert(buffer.end(), begin, begin + amount);
	return amount;
}

/**
 * Sends a binary data over the network.
 * @param data The data to send
 */
void BufferSend_binary(std::vector<byte> &buffer, size_t limit, const char *data, const size_t size)
{
	assert(data != nullptr);
	assert(BufferCanWriteToPacket(buffer, limit, size));
	buffer.insert(buffer.end(), data, data + size);
}

void BufferRecvStringValidate(std::string &buffer, StringValidationSettings settings)
{
	StrMakeValidInPlace(buffer, settings);
}
