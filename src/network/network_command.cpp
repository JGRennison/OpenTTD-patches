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
#include "../command_aux.h"
#include "../company_func.h"
#include "../settings_type.h"

#include "../safeguards.h"

/** Table with all the callbacks we'll use for conversion*/
static CommandCallback * const _callback_table[] = {
	/* 0x00 */ nullptr,
	/* 0x01 */ CcBuildPrimaryVehicle,
	/* 0x02 */ CcBuildAirport,
	/* 0x03 */ CcBuildBridge,
	/* 0x04 */ CcPlaySound_CONSTRUCTION_WATER,
	/* 0x05 */ CcBuildDocks,
	/* 0x06 */ CcFoundTown,
	/* 0x07 */ CcBuildRoadTunnel,
	/* 0x08 */ CcBuildRailTunnel,
	/* 0x09 */ CcBuildWagon,
	/* 0x0A */ CcRoadDepot,
	/* 0x0B */ CcRailDepot,
	/* 0x0C */ CcPlaceSign,
	/* 0x0D */ CcPlaySound_EXPLOSION,
	/* 0x0E */ CcPlaySound_CONSTRUCTION_OTHER,
	/* 0x0F */ CcPlaySound_CONSTRUCTION_RAIL,
	/* 0x10 */ CcStation,
	/* 0x11 */ CcTerraform,
	/* 0x12 */ CcAI,
	/* 0x13 */ CcCloneVehicle,
	/* 0x14 */ CcGiveMoney,
	/* 0x15 */ CcCreateGroup,
	/* 0x16 */ CcFoundRandomTown,
	/* 0x17 */ CcRoadStop,
	/* 0x18 */ CcBuildIndustry,
	/* 0x19 */ CcStartStopVehicle,
	/* 0x1A */ CcGame,
	/* 0x1B */ CcAddVehicleNewGroup,
	/* 0x1C */ CcAddPlan,
	/* 0x1D */ CcSetVirtualTrain,
	/* 0x1E */ CcVirtualTrainWagonsMoved,
	/* 0x1F */ CcDeleteVirtualTrain,
	/* 0x20 */ CcAddVirtualEngine,
	/* 0x21 */ CcMoveNewVirtualEngine,
	/* 0x22 */ CcAddNewSchDispatchSchedule,
	/* 0x23 */ CcSwapSchDispatchSchedules,
};

/** Local queue of packets waiting for handling. */
static CommandQueue _local_wait_queue;
/** Local queue of packets waiting for execution. */
static CommandQueue _local_execution_queue;

/**
 * Prepare a DoCommand to be send over the network
 * @param tile The tile to perform a command on (see #CommandProc)
 * @param p1 Additional data for the command (see #CommandProc)
 * @param p2 Additional data for the command (see #CommandProc)
 * @param p3 Additional data for the command (see #CommandProc)
 * @param cmd The command to execute (a CMD_* value)
 * @param callback A callback function to call after the command is finished
 * @param text The text to pass
 * @param company The company that wants to send the command
 * @param aux_data Auxiliary command data
 */
void NetworkSendCommand(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback *callback, const char *text, CompanyID company, const CommandAuxiliaryBase *aux_data)
{
	assert((cmd & CMD_FLAGS_MASK) == 0);

	CommandPacket c;
	c.company  = company;
	c.tile     = tile;
	c.p1       = p1;
	c.p2       = p2;
	c.p3       = p3;
	c.cmd      = cmd;
	c.callback = callback;
	if (aux_data != nullptr) c.aux_data.reset(aux_data->Clone());

	if (text != nullptr) {
		c.text.assign(text);
	} else {
		c.text.clear();
	}

	if (_network_server) {
		/* If we are the server, we queue the command in our 'special' queue.
		 *   In theory, we could execute the command right away, but then the
		 *   client on the server can do everything 1 tick faster than others.
		 *   So to keep the game fair, we delay the command with 1 tick
		 *   which gives about the same speed as most clients.
		 */
		c.frame = _frame_counter_max + 1;
		c.my_cmd = true;

		_local_wait_queue.push_back(std::move(c));
		return;
	}

	c.frame = 0; // The client can't tell which frame, so just make it 0

	/* Clients send their command to the server and forget all about the packet */
	MyClient::SendCommand(c);
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
	for (CommandPacket &p : _local_execution_queue) {
		CommandPacket &c = cs->outgoing_queue.emplace_back(p);
		c.callback = nullptr;
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
			error("[net] Trying to execute a packet in the past!");
		}

		/* We can execute this command */
		_current_company = cp->company;
		_cmd_client_id = cp->client_id;
		cp->cmd |= CMD_NETWORK_COMMAND;
		DoCommandP(&(*cp), cp->my_cmd);

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
static void DistributeCommandPacket(CommandPacket &cp, const NetworkClientSocket *owner)
{
	CommandCallback *callback = cp.callback;
	cp.frame = _frame_counter_max + 1;

	for (NetworkClientSocket *cs : NetworkClientSocket::Iterate()) {
		if (cs->status >= NetworkClientSocket::STATUS_MAP) {
			/* Callbacks are only send back to the client who sent them in the
			 *  first place. This filters that out. */
			cp.callback = (cs != owner) ? nullptr : callback;
			cp.my_cmd = (cs == owner);
			cs->outgoing_queue.push_back(cp);
		}
	}

	cp.callback = (nullptr != owner) ? nullptr : callback;
	cp.my_cmd = (nullptr == owner);
	_local_execution_queue.push_back(cp);
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
		if (_pause_mode != PM_UNPAUSED && !IsCommandAllowedWhilePaused(cp->cmd)) {
			++cp;
			continue;
		}

		/* Limit the number of commands per client per tick. */
		if (--to_go < 0) break;

		DistributeCommandPacket(*cp, owner);
		NetworkAdminCmdLogging(owner, *cp);
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
 * @return an error message. When nullptr there has been no error.
 */
const char *NetworkGameSocketHandler::ReceiveCommand(Packet &p, CommandPacket &cp)
{
	cp.company = (CompanyID)p.Recv_uint8();
	cp.cmd     = p.Recv_uint32();
	if (!IsValidCommand(cp.cmd))               return "invalid command";
	if (GetCommandFlags(cp.cmd) & CMD_OFFLINE) return "single-player only command";
	if ((cp.cmd & CMD_FLAGS_MASK) != 0)        return "invalid command flag";

	cp.p1      = p.Recv_uint32();
	cp.p2      = p.Recv_uint32();
	cp.p3      = p.Recv_uint64();
	cp.tile    = p.Recv_uint32();

	StringValidationSettings settings = (!_network_server && GetCommandFlags(cp.cmd) & CMD_STR_CTRL) != 0 ? SVS_ALLOW_CONTROL_CODE | SVS_REPLACE_WITH_QUESTION_MARK : SVS_REPLACE_WITH_QUESTION_MARK;
	p.Recv_string(cp.text, settings);

	byte callback = p.Recv_uint8();
	if (callback >= lengthof(_callback_table))  return "invalid callback";

	cp.callback = _callback_table[callback];

	uint16_t aux_data_size = p.Recv_uint16();
	if (aux_data_size > 0 && p.CanReadFromPacket(aux_data_size, true)) {
		CommandAuxiliarySerialised *aux_data = new CommandAuxiliarySerialised();
		cp.aux_data.reset(aux_data);
		aux_data->serialised_data.resize(aux_data_size);
		p.Recv_binary((aux_data->serialised_data.data()), aux_data_size);
	}

	return nullptr;
}

/**
 * Sends a command over the network.
 * @param p the packet to send it in.
 * @param cp the packet to actually send.
 */
void NetworkGameSocketHandler::SendCommand(Packet &p, const CommandPacket &cp)
{
	p.Send_uint8 (cp.company);
	p.Send_uint32(cp.cmd);
	p.Send_uint32(cp.p1);
	p.Send_uint32(cp.p2);
	p.Send_uint64(cp.p3);
	p.Send_uint32(cp.tile);
	p.Send_string(cp.text.c_str());

	byte callback = 0;
	while (callback < lengthof(_callback_table) && _callback_table[callback] != cp.callback) {
		callback++;
	}

	if (callback == lengthof(_callback_table)) {
		DEBUG(net, 0, "Unknown callback for command; no callback sent (command: %d)", cp.cmd);
		callback = 0; // _callback_table[0] == nullptr
	}
	p.Send_uint8 (callback);

	size_t aux_data_size_pos = p.Size();
	p.Send_uint16(0);
	if (cp.aux_data != nullptr) {
		CommandSerialisationBuffer serialiser(p.GetSerialisationBuffer(), p.GetSerialisationLimit());
		cp.aux_data->Serialise(serialiser);
		p.WriteAtOffset_uint16(aux_data_size_pos, (uint16_t)(p.Size() - aux_data_size_pos - 2));
	}
}
