/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file core/udp.cpp Basic functions to receive and send UDP packets.
 */

#include "../../stdafx.h"
#include "../../date_func.h"
#include "../../debug.h"
#include "../../core/random_func.hpp"
#include "udp.h"

#include "../../safeguards.h"

/**
 * Create an UDP socket but don't listen yet.
 * @param bind the addresses to bind to.
 */
NetworkUDPSocketHandler::NetworkUDPSocketHandler(NetworkAddressList *bind)
{
	if (bind != nullptr) {
		for (NetworkAddress &addr : *bind) {
			this->bind.push_back(addr);
		}
	} else {
		/* As an empty hostname and port 0 don't go well when
		 * resolving it we need to add an address for each of
		 * the address families we support. */
		this->bind.emplace_back("", 0, AF_INET);
		this->bind.emplace_back("", 0, AF_INET6);
	}

	this->fragment_token = ((uint64_t) InteractiveRandom()) | (((uint64_t) InteractiveRandom()) << 32);
}


/**
 * Start listening on the given host and port.
 * @return true if at least one port is listening
 */
bool NetworkUDPSocketHandler::Listen()
{
	/* Make sure socket is closed */
	this->CloseSocket();

	for (NetworkAddress &addr : this->bind) {
		addr.Listen(SOCK_DGRAM, &this->sockets);
	}

	return !this->sockets.empty();
}

/**
 * Close the actual UDP socket.
 */
void NetworkUDPSocketHandler::CloseSocket()
{
	for (auto &s : this->sockets) {
		closesocket(s.first);
	}
	this->sockets.clear();
}

/**
 * Send a packet over UDP
 * @param p    the packet to send
 * @param recv the receiver (target) of the packet
 * @param all  send the packet using all sockets that can send it
 * @param broadcast whether to send a broadcast message
 */
void NetworkUDPSocketHandler::SendPacket(Packet &p, NetworkAddress &recv, bool all, bool broadcast, bool short_mtu)
{
	if (this->sockets.empty()) this->Listen();

	const uint MTU = short_mtu ? UDP_MTU_SHORT : UDP_MTU;

	if (p.Size() > MTU) {
		p.PrepareToSend();

		uint64_t token = this->fragment_token++;
		const uint PAYLOAD_MTU = MTU - (1 + 2 + 8 + 1 + 1 + 2);

		const size_t packet_size = p.Size();
		const uint8_t frag_count = (uint8_t)((packet_size + PAYLOAD_MTU - 1) / PAYLOAD_MTU);

		Packet frag(this, PACKET_UDP_EX_MULTI);
		uint8_t current_frag = 0;
		size_t offset = 0;
		while (offset < packet_size) {
			uint16_t payload_size = (uint16_t)std::min<size_t>(PAYLOAD_MTU, packet_size - offset);
			frag.Send_uint64(token);
			frag.Send_uint8(current_frag);
			frag.Send_uint8(frag_count);
			frag.Send_uint16(payload_size);
			frag.Send_binary(p.GetBufferData() + offset, payload_size);
			current_frag++;
			offset += payload_size;
			this->SendPacket(frag, recv, all, broadcast, short_mtu);
			frag.ResetState(PACKET_UDP_EX_MULTI);
		}
		assert_msg(current_frag == frag_count, "{}, {}", current_frag, frag_count);
		return;
	}

	for (auto &s : this->sockets) {
		/* Make a local copy because if we resolve it we cannot
		 * easily unresolve it so we can resolve it later again. */
		NetworkAddress send(recv);

		/* Not the same type */
		if (!send.IsFamily(s.second.GetAddress()->ss_family)) continue;

		p.PrepareToSend();

		if (broadcast) {
			/* Enable broadcast */
			unsigned long val = 1;
			if (setsockopt(s.first, SOL_SOCKET, SO_BROADCAST, (char *) &val, sizeof(val)) < 0) {
				Debug(net, 1, "Setting broadcast mode failed: {}", NetworkError::GetLast().AsString());
			}
		}

		/* Send the buffer */
		ssize_t res = p.TransferOut<int>(sendto, s.first, 0, (const struct sockaddr *)send.GetAddress(), send.GetAddressLength());
		Debug(net, 7, "sendto({})",  NetworkAddressDumper().GetAddressAsString(&send));

		/* Check for any errors, but ignore it otherwise */
		if (res == -1) Debug(net, 1, "sendto({}) failed with: {}", NetworkAddressDumper().GetAddressAsString(&send), NetworkError::GetLast().AsString());

		if (!all) break;
	}
}

/**
 * Receive a packet at UDP level
 */
void NetworkUDPSocketHandler::ReceivePackets()
{
	for (auto &s : this->sockets) {
		for (int i = 0; i < 1000; i++) { // Do not infinitely loop when DoSing with UDP
			struct sockaddr_storage client_addr;
			memset(&client_addr, 0, sizeof(client_addr));

			/* The limit is UDP_MTU, but also allocate that much as we need to read the whole packet in one go. */
			Packet p(Packet::ReadTag{}, this, UDP_MTU, UDP_MTU);
			socklen_t client_len = sizeof(client_addr);

			/* Try to receive anything */
			SetNonBlocking(s.first); // Some OSes seem to lose the non-blocking status of the socket
			ssize_t nbytes = p.TransferIn<int>(recvfrom, s.first, 0, (struct sockaddr *)&client_addr, &client_len);

			/* Did we get the bytes for the base header of the packet? */
			if (nbytes <= 0) break;    // No data, i.e. no packet
			if (nbytes <= 2) continue; // Invalid data; try next packet
#ifdef __EMSCRIPTEN__
			client_len = FixAddrLenForEmscripten(client_addr);
#endif

			NetworkAddress address(client_addr, client_len);

			/* If the size does not match the packet must be corrupted.
			 * Otherwise it will be marked as corrupted later on. */
			if (!p.ParsePacketSize() || static_cast<size_t>(nbytes) != p.Size()) {
				Debug(net, 1, "received a packet with mismatching size from {}, ({}, {})", NetworkAddressDumper().GetAddressAsString(&address), (uint)nbytes, (uint)p.Size());
				continue;
			}
			if (!p.PrepareToRead()) {
				Debug(net, 1, "Invalid packet received (too small / decryption error)");
				continue;
			}

			/* Handle the packet */
			this->HandleUDPPacket(p, address);
		}
	}
}

/**
 * Handle an incoming packets by sending it to the correct function.
 * @param p the received packet
 * @param client_addr the sender of the packet
 */
void NetworkUDPSocketHandler::HandleUDPPacket(Packet &p, NetworkAddress &client_addr)
{
	PacketUDPType type;

	/* New packet == new client, which has not quit yet */
	this->Reopen();

	type = (PacketUDPType)p.Recv_uint8();

	switch (this->HasClientQuit() ? PACKET_UDP_END : type) {
		case PACKET_UDP_CLIENT_FIND_SERVER:   this->Receive_CLIENT_FIND_SERVER(p, client_addr);   break;
		case PACKET_UDP_SERVER_RESPONSE:      this->Receive_SERVER_RESPONSE(p, client_addr);      break;

		case PACKET_UDP_EX_MULTI:             this->Receive_EX_MULTI(p, client_addr);             break;
		case PACKET_UDP_EX_SERVER_RESPONSE:   this->Receive_EX_SERVER_RESPONSE(p, client_addr);   break;

		default:
			if (this->HasClientQuit()) {
				Debug(net, 0, "[udp] received invalid packet type {} from {}", type, NetworkAddressDumper().GetAddressAsString(client_addr));
			} else {
				Debug(net, 0, "[udp] received illegal packet from {}", NetworkAddressDumper().GetAddressAsString(client_addr));
			}
			break;
	}
}

void NetworkUDPSocketHandler::Receive_EX_MULTI(Packet &p, NetworkAddress &client_addr)
{
	uint64_t token        = p.Recv_uint64();
	uint8_t index         = p.Recv_uint8 ();
	uint8_t total         = p.Recv_uint8 ();
	uint16_t payload_size = p.Recv_uint16();

	Debug(net, 6, "[udp] received multi-part packet from {}: {}, {}/{}, {} bytes",
			NetworkAddressDumper().GetAddressAsString(client_addr), token, index, total, payload_size);

	if (total == 0 || index >= total) return;
	if (!p.CanReadFromPacket(payload_size)) return;

	time_t cur_time = time(nullptr);

	auto add_to_fragment = [&](FragmentSet &fs) {
		fs.fragments[index].assign((const char *) p.GetBufferData() + p.GetRawPos(), payload_size);

		uint total_payload = 0;
		for (auto &frag : fs.fragments) {
			if (!frag.size()) return;

			total_payload += (uint)frag.size();
		}

		Debug(net, 6, "[udp] merged multi-part packet from {}: {}, {} bytes",
				NetworkAddressDumper().GetAddressAsString(client_addr), token, total_payload);

		Packet merged(Packet::ReadTag{}, this, TCP_MTU, 0);
		merged.ReserveBuffer(total_payload);
		for (auto &frag : fs.fragments) {
			merged.Send_binary((const uint8_t *)frag.data(), frag.size());
		}
		merged.ParsePacketSize();
		if (!merged.PrepareToRead()) return;

		/* If the size does not match the packet must be corrupted.
		 * Otherwise it will be marked as corrupted later on. */
		if (total_payload != merged.ReadRawPacketSize()) {
			Debug(net, 1, "received an extended packet with mismatching size from {}, ({}, {})",
					NetworkAddressDumper().GetAddressAsString(client_addr), (uint)total_payload, (uint)merged.ReadRawPacketSize());
		} else {
			this->HandleUDPPacket(merged, client_addr);
		}

		fs = this->fragments.back();
		this->fragments.pop_back();
	};

	uint i = 0;
	while (i < this->fragments.size()) {
		FragmentSet &fs = this->fragments[i];
		if (fs.create_time < cur_time - 10) {
			fs = this->fragments.back();
			this->fragments.pop_back();
			continue;
		}

		if (fs.token == token && fs.address == client_addr && fs.fragments.size() == total) {
			add_to_fragment(fs);
			return;
		}
		i++;
	}

	this->fragments.push_back({ token, client_addr, cur_time, {} });
	this->fragments.back().fragments.resize(total);
	add_to_fragment(this->fragments.back());
}

/**
 * Helper for logging receiving invalid packets.
 * @param type The received packet type.
 * @param client_addr The address we received the packet from.
 */
void NetworkUDPSocketHandler::ReceiveInvalidPacket(PacketUDPType type, NetworkAddress &client_addr)
{
	Debug(net, 0, "[udp] received packet type {} on wrong port from {}", type, NetworkAddressDumper().GetAddressAsString(client_addr));
}

void NetworkUDPSocketHandler::Receive_CLIENT_FIND_SERVER(Packet &p, NetworkAddress &client_addr) { this->ReceiveInvalidPacket(PACKET_UDP_CLIENT_FIND_SERVER, client_addr); }
void NetworkUDPSocketHandler::Receive_SERVER_RESPONSE(Packet &p, NetworkAddress &client_addr) { this->ReceiveInvalidPacket(PACKET_UDP_SERVER_RESPONSE, client_addr); }
void NetworkUDPSocketHandler::Receive_EX_SERVER_RESPONSE(Packet &p, NetworkAddress &client_addr) { this->ReceiveInvalidPacket(PACKET_UDP_EX_SERVER_RESPONSE, client_addr); }
