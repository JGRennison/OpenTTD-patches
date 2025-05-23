/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tcp.cpp Basic functions to receive and send TCP packets.
 */

#include "../../stdafx.h"
#include "../../debug.h"

#include "tcp.h"

#include "../../safeguards.h"

NetworkTCPSocketHandler::~NetworkTCPSocketHandler()
{
	this->CloseSocket();
}

/**
 * Close the actual socket of the connection.
 * Please make sure CloseConnection is called before CloseSocket, as
 * otherwise not all resources might be released.
 */
void NetworkTCPSocketHandler::CloseSocket()
{
	if (this->sock != INVALID_SOCKET) closesocket(this->sock);
	this->sock = INVALID_SOCKET;
}

/**
 * This will put this socket handler in a close state. It will not
 * actually close the OS socket; use CloseSocket for this.
 * @param error Whether we quit under an error condition or not.
 * @return new status of the connection.
 */
NetworkRecvStatus NetworkTCPSocketHandler::CloseConnection([[maybe_unused]] bool error)
{
	this->MarkClosed();
	this->writable = false;

	this->packet_queue.clear();
	this->packet_recv = nullptr;

	return NETWORK_RECV_STATUS_OKAY;
}

/**
 * This function puts the packet in the send-queue and it is send as
 * soon as possible. This is the next tick, or maybe one tick later
 * if the OS-network-buffer is full)
 * @param packet the packet to send
 */
void NetworkTCPSocketHandler::SendPacket(std::unique_ptr<Packet> packet)
{
	assert(packet != nullptr);

	packet->PrepareForSendQueue();

	this->packet_queue.push_back(std::move(packet));
}

/**
 * This function puts the packet in the send-queue and it is send as
 * soon as possible. This is the next tick, or maybe one tick later
 * if the OS-network-buffer is full)
 * @param packet the packet to send
 */
void NetworkTCPSocketHandler::SendPrependPacket(std::unique_ptr<Packet> packet, int queue_after_packet_type)
{
	assert(packet != nullptr);

	packet->PrepareForSendQueue();

	if (queue_after_packet_type >= 0) {
		for (auto iter = this->packet_queue.begin(); iter != this->packet_queue.end(); ++iter) {
			if ((*iter)->GetTransmitPacketType() == queue_after_packet_type) {
				++iter;
				this->packet_queue.insert(iter, std::move(packet));
				return;
			}
		}
	}

	/* The very first packet in the queue may be partially written out, so cannot be replaced.
	 * If the queue is non-empty, swap packet with the first packet in the queue.
	 * The insert the packet (either the incoming packet or the previous first packet) at the front. */
	if (!this->packet_queue.empty()) {
		packet.swap(this->packet_queue.front());
	}
	this->packet_queue.push_front(std::move(packet));
}

/**
 * Shrink the packet send queue to fit (e.g. after having sent the map to a network client)
 */
void NetworkTCPSocketHandler::ShrinkToFitSendQueue()
{
	this->packet_queue.shrink_to_fit();
}

/**
 * Sends all the buffered packets out for this client. It stops when:
 *   1) all packets are send (queue is empty)
 *   2) the OS reports back that it can not send any more
 *      data right now (full network-buffer, it happens ;))
 *   3) sending took too long
 * @param closing_down Whether we are closing down the connection.
 * @return \c true if a (part of a) packet could be sent and
 *         the connection is not closed yet.
 */
SendPacketsState NetworkTCPSocketHandler::SendPackets(bool closing_down)
{
	/* We can not write to this socket!! */
	if (!this->writable) return SPS_NONE_SENT;
	if (!this->IsConnected()) return SPS_CLOSED;

	while (!this->packet_queue.empty()) {
		Packet &p = *this->packet_queue.front();
		p.CheckPendingPreSendEncryption();
		ssize_t res = p.TransferOut<int>(send, this->sock, 0);
		if (res == -1) {
			NetworkError err = NetworkError::GetLast();
			if (!err.WouldBlock()) {
				/* Something went wrong.. close client! */
				if (!closing_down) {
					Debug(net, 0, "Send failed: {}", err.AsString());
					this->CloseConnection();
				}
				return SPS_CLOSED;
			}
			return SPS_PARTLY_SENT;
		}
		if (res == 0) {
			/* Client/server has left us :( */
			if (!closing_down) this->CloseConnection();
			return SPS_CLOSED;
		}

		/* Is this packet sent? */
		if (p.RemainingBytesToTransfer() == 0) {
			/* Go to the next packet */
			if (GetDebugLevel(DebugLevelID::net) >= 5) this->LogSentPacket(p);
			this->packet_queue.pop_front();
		} else {
			return SPS_PARTLY_SENT;
		}
	}

	return SPS_ALL_SENT;
}

/**
 * Receives a packet for the given client
 * @return The received packet (or nullptr when it didn't receive one)
 */
std::unique_ptr<Packet> NetworkTCPSocketHandler::ReceivePacket()
{
	ssize_t res;

	if (!this->IsConnected()) return nullptr;

	if (this->packet_recv == nullptr) {
		this->packet_recv = std::make_unique<Packet>(Packet::ReadTag{}, this, TCP_MTU);
	}

	Packet &p = *this->packet_recv.get();

	/* Read packet size */
	if (!p.HasPacketSizeData()) {
		while (p.RemainingBytesToTransfer() != 0) {
			res = p.TransferIn<int>(recv, this->sock, 0);
			if (res == -1) {
				NetworkError err = NetworkError::GetLast();
				if (!err.WouldBlock()) {
					/* Something went wrong... */
					if (!err.IsConnectionReset()) Debug(net, 0, "Recv failed: {}", err.AsString());
					this->CloseConnection();
					return nullptr;
				}
				/* Connection would block, so stop for now */
				return nullptr;
			}
			if (res == 0) {
				/* Client/server has left */
				this->CloseConnection();
				return nullptr;
			}
		}

		/* Parse the size in the received packet and if not valid, close the connection. */
		if (!p.ParsePacketSize()) {
			Debug(net, 0, "ParsePacketSize failed, possible packet stream corruption");
			this->CloseConnection();
			return nullptr;
		}
	}

	/* Read rest of packet */
	while (p.RemainingBytesToTransfer() != 0) {
		res = p.TransferIn<int>(recv, this->sock, 0);
		if (res == -1) {
			NetworkError err = NetworkError::GetLast();
			if (!err.WouldBlock()) {
				/* Something went wrong... */
				if (!err.IsConnectionReset()) Debug(net, 0, "Recv failed: {}", err.AsString());
				this->CloseConnection();
				return nullptr;
			}
			/* Connection would block */
			return nullptr;
		}
		if (res == 0) {
			/* Client/server has left */
			this->CloseConnection();
			return nullptr;
		}
	}

	if (!p.PrepareToRead()) {
		Debug(net, 0, "Invalid packet received (too small / decryption error)");
		this->CloseConnection();
		return nullptr;
	}
	return std::move(this->packet_recv);
}

void NetworkTCPSocketHandler::LogSentPacket(const Packet &pkt) {}

/**
 * Check whether this socket can send or receive something.
 * @return \c true when there is something to receive.
 * @note Sets #writable if more data can be sent.
 */
bool NetworkTCPSocketHandler::CanSendReceive()
{
	assert(this->sock != INVALID_SOCKET);

	fd_set read_fd, write_fd;
	struct timeval tv;

	FD_ZERO(&read_fd);
	FD_ZERO(&write_fd);

	FD_SET(this->sock, &read_fd);
	FD_SET(this->sock, &write_fd);

	tv.tv_sec = tv.tv_usec = 0; // don't block at all.
	if (select(FD_SETSIZE, &read_fd, &write_fd, nullptr, &tv) < 0) return false;

	this->writable = !!FD_ISSET(this->sock, &write_fd);
	return FD_ISSET(this->sock, &read_fd) != 0;
}
