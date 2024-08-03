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
Packet::Packet(Packet::ReadTag tag, NetworkSocketHandler *cs, size_t limit, size_t initial_read_size) : pos(0), limit(limit)
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
Packet::Packet(NetworkSocketHandler *cs, PacketType type, size_t limit) : pos(0), limit(limit), cs(cs)
{
	this->ResetState(type);
}

void Packet::ResetState(PacketType type)
{
	this->buffer.clear();
	this->tx_packet_type = type;

	/* Allocate space for the the size so we can write that in just before sending the packet. */
	size_t size = EncodedLengthOfPacketSize();
	if (cs != nullptr && cs->send_encryption_handler != nullptr) {
		/* Allocate some space for the message authentication code of the encryption. */
		size += cs->send_encryption_handler->MACSize();
		this->encyption_pending = true;
	} else {
		this->encyption_pending = false;
	}
	assert(this->CanWriteToPacket(size));
	this->buffer.resize(size, 0);

	this->Send_uint8(type);
}

/**
 * Writes the packet size from the raw packet from packet->size
 */
void Packet::PrepareForSendQueue()
{
	/* Prevent this to be called twice and for packets that have been received. */
	assert(this->buffer[0] == 0 && this->buffer[1] == 0);

	this->buffer[0] = GB(this->Size(), 0, 8);
	this->buffer[1] = GB(this->Size(), 8, 8);

	this->pos = 0; // We start reading from here
	this->buffer.shrink_to_fit();
}

void Packet::PreSendEncryption()
{
	this->encyption_pending = false;
	size_t offset = EncodedLengthOfPacketSize();
	size_t mac_size = cs->send_encryption_handler->MACSize();
	size_t message_offset = offset + mac_size;
	cs->send_encryption_handler->Encrypt(std::span(&this->buffer[offset], mac_size), std::span(&this->buffer[message_offset], this->buffer.size() - message_offset));
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

void Packet::WriteAtOffset_uint16(size_t offset, uint16_t data)
{
	assert(offset + 1 < this->buffer.size());
	this->buffer[offset]     = GB(data, 0, 8);
	this->buffer[offset + 1] = GB(data, 8, 8);
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
	return this->pos >= EncodedLengthOfPacketSize();
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
	size_t size = static_cast<size_t>(this->buffer[0]);
	size       += static_cast<size_t>(this->buffer[1]) << 8;

	/* If the size of the packet is less than the bytes required for the size and type of
	 * the packet, or more than the allowed limit, then something is wrong with the packet.
	 * In those cases the packet can generally be regarded as containing garbage data. */
	if (size < EncodedLengthOfPacketSize() + EncodedLengthOfPacketType() || size > this->limit) return false;

	this->buffer.resize(size);
	this->pos = static_cast<PacketSize>(EncodedLengthOfPacketSize());
	return true;
}

/**
 * Prepares the packet so it can be read
 * @return True when the packet was valid, otherwise false.
 */
bool Packet::PrepareToRead()
{
	/* Put the position on the right place */
	this->pos = static_cast<PacketSize>(EncodedLengthOfPacketSize());

	if (cs == nullptr || cs->receive_encryption_handler == nullptr) return true;

	size_t mac_size = cs->receive_encryption_handler->MACSize();
	if (this->buffer.size() <= pos + mac_size) return false;

	bool valid = cs->receive_encryption_handler->Decrypt(std::span(&this->buffer[pos], mac_size), std::span(&this->buffer[pos + mac_size], this->buffer.size() - pos - mac_size));
	this->pos += static_cast<PacketSize>(mac_size);
	return valid;
}

/**
 * Get the \c PacketType from this packet.
 * @return The packet type.
 */
PacketType Packet::GetPacketType() const
{
	assert(this->Size() >= EncodedLengthOfPacketSize() + EncodedLengthOfPacketType());
	size_t offset = EncodedLengthOfPacketSize();
	if (cs != nullptr && cs->send_encryption_handler != nullptr) offset += cs->send_encryption_handler->MACSize();
	return static_cast<PacketType>(buffer[offset]);
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
 * Extract at most the length of the span bytes from the packet into the span.
 * @param span The span to write the bytes to.
 * @return The number of bytes that were actually read.
 */
size_t Packet::Recv_bytes(std::span<uint8_t> span)
{
	auto tranfer_to_span = [](std::span<uint8_t> destination, const char *source, size_t amount) {
		size_t to_copy = std::min(amount, destination.size());
		std::copy(source, source + to_copy, destination.data());
		return to_copy;
	};

	return this->TransferOut(tranfer_to_span, span);
}

bool SubPacketDeserialiser::CanDeserialiseBytes(size_t bytes_to_read, bool raise_error)
{
	if (this->pos + bytes_to_read > this->size) {
		if (raise_error) this->cs->NetworkSocketHandler::MarkClosed();
		return false;
	}

	return true;
}
