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
[[maybe_unused]] static bool BufferCanWriteToPacket(const std::vector<uint8_t> &buffer, size_t limit, size_t bytes_to_write)
{
	return buffer.size() + bytes_to_write <= limit;
}

/*
 * The next couple of functions make sure we can send
 *  uint8_t, uint16_t, uint32_t and uint64_t endian-safe
 *  over the network. The least significant bytes are
 *  sent first.
 *
 *  So 0x01234567 would be sent as 67 45 23 01.
 *
 * A bool is sent as a uint8_t where zero means false
 *  and non-zero means true.
 */

/**
 * Package a boolean in the packet.
 * @param data The data to send.
 */
void BufferSend_bool(std::vector<uint8_t> &buffer, size_t limit, bool data)
{
	BufferSend_uint8(buffer, limit, data ? 1 : 0);
}

/**
 * Package a 8 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint8(std::vector<uint8_t> &buffer, size_t limit, uint8_t data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.emplace_back(data);
}

/**
 * Package a 16 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint16(std::vector<uint8_t> &buffer, size_t limit, uint16_t data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.insert(buffer.end(), {
		(uint8_t)GB(data,  0, 8),
		(uint8_t)GB(data,  8, 8),
	});
}

/**
 * Package a 32 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint32(std::vector<uint8_t> &buffer, size_t limit, uint32_t data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.insert(buffer.end(), {
		(uint8_t)GB(data,  0, 8),
		(uint8_t)GB(data,  8, 8),
		(uint8_t)GB(data, 16, 8),
		(uint8_t)GB(data, 24, 8),
	});
}

/**
 * Package a 64 bits integer in the packet.
 * @param data The data to send.
 */
void BufferSend_uint64(std::vector<uint8_t> &buffer, size_t limit, uint64_t data)
{
	assert(BufferCanWriteToPacket(buffer, limit, sizeof(data)));
	buffer.insert(buffer.end(), {
		(uint8_t)GB(data,  0, 8),
		(uint8_t)GB(data,  8, 8),
		(uint8_t)GB(data, 16, 8),
		(uint8_t)GB(data, 24, 8),
		(uint8_t)GB(data, 32, 8),
		(uint8_t)GB(data, 40, 8),
		(uint8_t)GB(data, 48, 8),
		(uint8_t)GB(data, 56, 8),
	});
}

/**
 * Sends a string over the network. It sends out
 * the string + '\0'. No size-byte or something.
 * @param data The string to send
 */
void BufferSend_string(std::vector<uint8_t> &buffer, size_t limit, const std::string_view data)
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
size_t BufferSend_binary_until_full(std::vector<uint8_t> &buffer, size_t limit, const uint8_t *begin, const uint8_t *end)
{
	size_t amount = std::min<size_t>(end - begin, limit - buffer.size());
	buffer.insert(buffer.end(), begin, begin + amount);
	return amount;
}

/**
 * Sends a binary data over the network.
 * @param data The data to send
 */
void BufferSend_binary(std::vector<uint8_t> &buffer, size_t limit, const uint8_t *data, const size_t size)
{
	assert(data != nullptr);
	assert(BufferCanWriteToPacket(buffer, limit, size));
	buffer.insert(buffer.end(), data, data + size);
}

/**
 * Sends a binary buffer over the network.
 * The data is length prefixed with a uint16.
 * @param data The string to send
 */
void BufferSend_buffer(std::vector<uint8_t> &buffer, size_t limit, const uint8_t *data, const size_t size)
{
	assert(size <= UINT16_MAX);
	assert(BufferCanWriteToPacket(buffer, limit, size + 2));
	buffer.insert(buffer.end(), {
		(uint8_t)GB(size,  0, 8),
		(uint8_t)GB(size,  8, 8),
	});
	buffer.insert(buffer.end(), data, data + size);
}

/**
 * Send a uint16_t at the given offset in the written buffer.
 * @param data The data to send.
 */
void BufferSendAtOffset_uint16(std::vector<uint8_t> &buffer, size_t offset, uint16_t data)
{
	assert(offset + 1 < buffer.size());
	buffer[offset]     = GB(data, 0, 8);
	buffer[offset + 1] = GB(data, 8, 8);
}

void BufferRecvStringValidate(std::string &buffer, StringValidationSettings settings)
{
	StrMakeValidInPlace(buffer, settings);
}
