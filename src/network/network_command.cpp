/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_command.cpp Command handling over network connections. */

#include "../stdafx.h"
#include "network_admin.h"
#include "network_client.h"
#include "network_server.h"
#include "../command_func.h"
#include "../company_func.h"
#include "../error_func.h"
#include "../settings_type.h"

#include "../safeguards.h"

/** Local queue of packets waiting for handling. */
static CommandQueue _local_wait_queue;
/** Local queue of packets waiting for execution. */
static CommandQueue _local_execution_queue;

/**
 * Prepare a DoCommand to be send over the network
 * @param cmd The command to execute (a CMD_* value)
 * @param tile The tile to perform a command on
 * @param payload The command payload (must be already validated as the correct type)
 * @param err_message Message prefix to show on error
 * @param callback A callback function to call after the command is finished
 * @param callback_param Parameter for the callback function
 * @param company The company that wants to send the command
 */
void NetworkSendCommandImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, CompanyID company)
{
	assert(IsValidCommand(cmd));

	CommandPacket c;
	c.company = company;

	c.command_container.cmd = cmd;
	c.command_container.error_msg = error_msg;
	c.command_container.tile = tile;

	c.callback = callback;
	c.callback_param = callback_param;

	if (_network_server) {
		/* If we are the server, we queue the command in our 'special' queue.
		 *   In theory, we could execute the command right away, but then the
		 *   client on the server can do everything 1 tick faster than others.
		 *   So to keep the game fair, we delay the command with 1 tick
		 *   which gives about the same speed as most clients.
		 */
		c.frame = _frame_counter_max + 1;
		c.my_cmd = true;

		c.command_container.payload = payload.Clone();

		_local_wait_queue.push_back(std::move(c));
		return;
	}

	c.frame = 0; // The client can't tell which frame, so just make it 0

	/* Clients send their command to the server and forget all about the packet */
	MyClient::SendCommand(SerialiseCommandPacketUsingPayload(c, payload));
}

/**
 * Sync our local command queue to the command queue of the given
 * socket. This is needed for the case where we receive a command
 * before saving the game for a joining client, but without the
 * execution of those commands. Not syncing those commands means
 * that the client will never get them and as such will be in a
 * desynced state from the time it started with joining.
 * @param cs The client to sync the queue to.
 */
void NetworkSyncCommandQueue(NetworkClientSocket *cs)
{
	for (const CommandPacket &p : _local_execution_queue) {
		OutgoingCommandPacket &c = cs->outgoing_queue.emplace_back();
		c = SerialiseCommandPacket(p);
		c.callback = CommandCallback::None;
	}
}

/**
 * Execute all commands on the local command queue that ought to be executed this frame.
 */
void NetworkExecuteLocalCommandQueue()
{
	extern ClientID _cmd_client_id;
	assert(IsLocalCompany());

	CommandQueue &queue = (_network_server ? _local_execution_queue : ClientNetworkGameSocketHandler::my_client->incoming_queue);

	bool record_sync_event = false;
	auto cp = queue.begin();
	for (; cp != queue.end(); cp++) {
		/* The queue is always in order, which means
		 * that the first element will be executed first. */
		if (_frame_counter < cp->frame) break;

		if (_frame_counter > cp->frame) {
			/* If we reach here, it means for whatever reason, we've already executed
			 * past the command we need to execute. */
			FatalError("[net] Trying to execute a packet in the past!");
		}

		/* We can execute this command */
		_current_company = cp->company;
		_cmd_client_id = cp->client_id;
		DoCommandPImplementation(cp->command_container.cmd, cp->command_container.tile, *cp->command_container.payload, cp->command_container.error_msg,
				cp->callback, cp->callback_param, DCIF_NETWORK_COMMAND | DCIF_TYPE_CHECKED | (cp->my_cmd ? DCIF_NONE : DCIF_NOT_MY_CMD));

		record_sync_event = true;
	}
	queue.erase(queue.begin(), cp);

	/* Local company may have changed, so we should not restore the old value */
	_current_company = _local_company;
	_cmd_client_id = INVALID_CLIENT_ID;

	if (record_sync_event) RecordSyncEvent(NSRE_CMD);
}

/**
 * Free the local command queues.
 */
void NetworkFreeLocalCommandQueue()
{
	_local_wait_queue.clear();
	_local_execution_queue.clear();
}

/**
 * "Send" a particular CommandPacket to all clients.
 * @param cp    The command that has to be distributed.
 * @param owner The client that owns the command,
 */
static void DistributeCommandPacket(CommandPacket cp, const NetworkClientSocket *owner)
{
	CommandCallback callback = cp.callback;
	cp.frame = _frame_counter_max + 1;

	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->status >= NetworkClientSocket::STATUS_MAP) {
			/* Callbacks are only send back to the client who sent them in the
			 *  first place. This filters that out. */
			cp.callback = (cs != owner) ? CommandCallback::None : callback;
			cp.my_cmd = (cs == owner);
			cs->outgoing_queue.push_back(SerialiseCommandPacket(cp));
		}
	}

	cp.callback = (nullptr != owner) ? CommandCallback::None : callback;
	cp.my_cmd = (nullptr == owner);
	_local_execution_queue.push_back(std::move(cp));
}

/**
 * "Send" a particular CommandQueue to all clients.
 * @param queue The queue of commands that has to be distributed.
 * @param owner The client that owns the commands,
 */
static void DistributeQueue(CommandQueue &queue, const NetworkClientSocket *owner)
{
#ifdef DEBUG_DUMP_COMMANDS
	/* When replaying we do not want this limitation. */
	int to_go = UINT16_MAX;
#else
	int to_go = _settings_client.network.commands_per_frame;
	if (owner == nullptr) {
		/* This is the server, use the commands_per_frame_server setting if higher */
		to_go = std::max<int>(to_go, _settings_client.network.commands_per_frame_server);
	}
#endif

	/* Not technically the most performant way, but consider clients rarely click more than once per tick. */
	for (auto cp = queue.begin(); cp != queue.end(); /* removing some items */) {
		/* Do not distribute commands when paused and the command is not allowed while paused. */
		if (_pause_mode.Any() && !IsCommandAllowedWhilePaused(cp->command_container.cmd)) {
			++cp;
			continue;
		}

		/* Limit the number of commands per client per tick. */
		if (--to_go < 0) break;

		NetworkAdminCmdLogging(owner, *cp);
		DistributeCommandPacket(std::move(*cp), owner);
		cp = queue.erase(cp);
	}
}

/** Distribute the commands of ourself and the clients. */
void NetworkDistributeCommands()
{
	/* First send the server's commands. */
	DistributeQueue(_local_wait_queue, nullptr);

	/* Then send the queues of the others. */
	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		DistributeQueue(cs->incoming_queue, cs);
	}
}

/**
 * Receives a command from the network.
 * @param p the packet to read from.
 * @param cp the struct to write the data to.
 * @return An error message, or nullptr when there has been no error.
 */
const char *NetworkGameSocketHandler::ReceiveCommand(Packet &p, CommandPacket &cp)
{
	cp.company = (CompanyID)p.Recv_uint8();
	DeserialisationBuffer buf = p.BorrowAsDeserialisationBuffer();
	const char *err = cp.command_container.Deserialise(buf);
	p.ReturnDeserialisationBuffer(std::move(buf));
	if (err != nullptr) return err;

	uint8_t callback = p.Recv_uint8();
	if (callback >= static_cast<uint8_t>(CommandCallback::End)) return "invalid callback";

	cp.callback = static_cast<CommandCallback>(callback);
	if (callback != 0) {
		cp.callback_param = p.Recv_uint32();
	} else {
		cp.callback_param = 0;
	}

	return nullptr;
}

/**
 * Sends a command over the network.
 * @param p the packet to send it in.
 * @param cp the packet to actually send.
 */
void NetworkGameSocketHandler::SendCommand(Packet &p, const OutgoingCommandPacket &cp)
{
	p.Send_uint8(cp.company);

	cp.command_container.Serialise(p.AsBufferSerialisationRef());

	uint8_t callback = static_cast<uint8_t>(cp.callback);
	if (callback >= static_cast<uint8_t>(CommandCallback::End)) {
		Debug(net, 0, "Unknown callback for command; no callback sent (command: {})", cp.command_container.cmd);
		callback = 0; // CommandCallback::None
	}
	p.Send_uint8(callback);
	if (callback != 0) {
		p.Send_uint32(cp.callback_param);
	}
}
