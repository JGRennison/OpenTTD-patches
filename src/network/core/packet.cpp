/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file packet.cpp Basic functions to create, fill and read packets.
 */

#include "../../stdafx.h"
#include "../../string_func.h"
#include "../../string_func_extra.h"
#include "../../command_type.h"

#include "packet.h"

#include "../../safeguards.h"

/**
 * Create a packet that is used to read from a network socket.
 * @param cs                The socket handler associated with the socket we are reading from.
 * @param limit             The maximum size of packets to accept.
 * @param initial_read_size The initial amount of data to transfer from the socket into the
 *                          packet. This defaults to just the required bytes to determine the
 *                          packet's size. That default is the wanted for streams such as TCP
 *                          as you do not want to read data of the next packet yet. For UDP
 *                          you need to read the whole packet at once otherwise you might
 *                          loose some the data of the packet, so there you pass the maximum
 *                          size for the packet you expect from the network.
 */
Packet::Packet(NetworkSocketHandler *cs, size_t limit, size_t initial_read_size) : pos(0), limit(limit)
{
	assert(cs != nullptr);

	this->cs = cs;
	this->buffer.resize(initial_read_size);
}

/**
 * Creates a packet to send
 * @param type  The type of the packet to send
 * @param limit The maximum number of bytes the packet may have. Default is COMPAT_MTU.
 *              Be careful of compatibility with older clients/servers when changing
 *              the limit as it might break things if the other side is not expecting
 *              much larger packets than what they support.
 */
Packet::Packet(PacketType type, size_t limit) : pos(0), limit(limit), cs(nullptr)
{
	this->ResetState(type);
}

void Packet::ResetState(PacketType type)
{
	this->cs = nullptr;
	this->buffer.clear();

	/* Allocate space for the the size so we can write that in just before sending the packet. */
	this->Send_uint16(0);
	this->Send_uint8(type);
}

/**
 * Writes the packet size from the raw packet from packet->size
 */
void Packet::PrepareToSend()
{
	assert(this->cs == nullptr);

	this->buffer[0] = GB(this->Size(), 0, 8);
	this->buffer[1] = GB(this->Size(), 8, 8);

	this->pos  = 0; // We start reading from here
	this->buffer.shrink_to_fit();
}

/**
 * Is it safe to write to the packet, i.e. didn't we run over the buffer?
 * @param bytes_to_write The amount of bytes we want to try to write.
 * @return True iff the given amount of bytes can be written to the packet.
 */
bool Packet::CanWriteToPacket(size_t bytes_to_write)
{
	return this->Size() + bytes_to_write <= this->limit;
}



/*
 * Receiving commands
 * Again, the next couple of functions are endian-safe
 *  see the comment before Send_bool for more info.
 */


/**
 * Is it safe to read from the packet, i.e. didn't we run over the buffer?
 * In case \c close_connection is true, the connection will be closed when one would
 * overrun the buffer. When it is false, the connection remains untouched.
 * @param bytes_to_read    The amount of bytes we want to try to read.
 * @param close_connection Whether to close the connection if one cannot read that amount.
 * @return True if that is safe, otherwise false.
 */
bool Packet::CanReadFromPacket(size_t bytes_to_read, bool close_connection)
{
	/* Don't allow reading from a quit client/client who send bad data */
	if (this->cs->HasClientQuit()) return false;

	/* Check if variable is within packet-size */
	if (this->pos + bytes_to_read > this->Size()) {
		if (close_connection) this->cs->NetworkSocketHandler::MarkClosed();
		return false;
	}

	return true;
}

/**
 * Check whether the packet, given the position of the "write" pointer, has read
 * enough of the packet to contain its size.
 * @return True iff there is enough data in the packet to contain the packet's size.
 */
bool Packet::HasPacketSizeData() const
{
	return this->pos >= sizeof(PacketSize);
}

/**
 * Get the number of bytes in the packet.
 * When sending a packet this is the size of the data up to that moment.
 * When receiving a packet (before PrepareToRead) this is the allocated size for the data to be read.
 * When reading a packet (after PrepareToRead) this is the full size of the packet.
 * @return The packet's size.
 */
size_t Packet::Size() const
{
	return this->buffer.size();
}

size_t Packet::ReadRawPacketSize() const
{
	return (size_t)this->buffer[0] + ((size_t)this->buffer[1] << 8);
}

/**
 * Reads the packet size from the raw packet and stores it in the packet->size
 * @return True iff the packet size seems plausible.
 */
bool Packet::ParsePacketSize()
{
	assert(this->cs != nullptr);
	size_t size = (size_t)this->buffer[0];
	size       += (size_t)this->buffer[1] << 8;

	/* If the size of the packet is less than the bytes required for the size and type of
	 * the packet, or more than the allowed limit, then something is wrong with the packet.
	 * In those cases the packet can generally be regarded as containing garbage data. */
	if (size < sizeof(PacketSize) + sizeof(PacketType) || size > this->limit) return false;

	this->buffer.resize(size);
	this->pos = sizeof(PacketSize);
	return true;
}

/**
 * Prepares the packet so it can be read
 */
void Packet::PrepareToRead()
{
	/* Put the position on the right place */
	this->pos = sizeof(PacketSize);
}

/**
 * Get the \c PacketType from this packet.
 * @return The packet type.
 */
PacketType Packet::GetPacketType() const
{
	assert(this->Size() >= sizeof(PacketSize) + sizeof(PacketType));
	return static_cast<PacketType>(buffer[sizeof(PacketSize)]);
}

/**
 * Read a boolean from the packet.
 * @return The read data.
 */
bool Packet::Recv_bool()
{
	return this->Recv_uint8() != 0;
}

/**
 * Read a 8 bits integer from the packet.
 * @return The read data.
 */
uint8 Packet::Recv_uint8()
{
	uint8 n;

	if (!this->CanReadFromPacket(sizeof(n), true)) return 0;

	n = this->buffer[this->pos++];
	return n;
}

/**
 * Read a 16 bits integer from the packet.
 * @return The read data.
 */
uint16 Packet::Recv_uint16()
{
	uint16 n;

	if (!this->CanReadFromPacket(sizeof(n), true)) return 0;

	n  = (uint16)this->buffer[this->pos++];
	n += (uint16)this->buffer[this->pos++] << 8;
	return n;
}

/**
 * Read a 32 bits integer from the packet.
 * @return The read data.
 */
uint32 Packet::Recv_uint32()
{
	uint32 n;

	if (!this->CanReadFromPacket(sizeof(n), true)) return 0;

	n  = (uint32)this->buffer[this->pos++];
	n += (uint32)this->buffer[this->pos++] << 8;
	n += (uint32)this->buffer[this->pos++] << 16;
	n += (uint32)this->buffer[this->pos++] << 24;
	return n;
}

/**
 * Read a 64 bits integer from the packet.
 * @return The read data.
 */
uint64 Packet::Recv_uint64()
{
	uint64 n;

	if (!this->CanReadFromPacket(sizeof(n), true)) return 0;

	n  = (uint64)this->buffer[this->pos++];
	n += (uint64)this->buffer[this->pos++] << 8;
	n += (uint64)this->buffer[this->pos++] << 16;
	n += (uint64)this->buffer[this->pos++] << 24;
	n += (uint64)this->buffer[this->pos++] << 32;
	n += (uint64)this->buffer[this->pos++] << 40;
	n += (uint64)this->buffer[this->pos++] << 48;
	n += (uint64)this->buffer[this->pos++] << 56;
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
std::string Packet::Recv_string(size_t length, StringValidationSettings settings)
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

	return StrMakeValid(str, settings);
}

/**
 * Get the amount of bytes that are still available for the Transfer functions.
 * @return The number of bytes that still have to be transfered.
 */
size_t Packet::RemainingBytesToTransfer() const
{
	return this->Size() - this->pos;
}

/**
 * Reads a string till it finds a '\0' in the stream.
 * @param buffer The buffer to put the data into.
 * @param settings The string validation settings.
 */
void Packet::Recv_string(std::string &buffer, StringValidationSettings settings)
{
	/* Don't allow reading from a closed socket */
	if (cs->HasClientQuit()) return;

	size_t length = ttd_strnlen((const char *)(this->buffer.data() + this->pos), this->Size() - this->pos - 1);
	buffer.assign((const char *)(this->buffer.data() + this->pos), length);
	this->pos += (PacketSize)length + 1;
	StrMakeValidInPlace(buffer, settings);
}

/**
 * Reads binary data.
 * @param buffer The buffer to put the data into.
 * @param size   The size of the data.
 */
void Packet::Recv_binary(char *buffer, size_t size)
{
	if (!this->CanReadFromPacket(size, true)) return;

	memcpy(buffer, &this->buffer[this->pos], size);
	this->pos += (PacketSize) size;
}

/**
 * Reads binary data.
 * @param buffer The buffer to put the data into.
 * @param size   The size of the data.
 */
void Packet::Recv_binary(std::string &buffer, size_t size)
{
	if (!this->CanReadFromPacket(size, true)) return;

	buffer.assign((const char *) &this->buffer[this->pos], size);
	this->pos += (PacketSize) size;
}
