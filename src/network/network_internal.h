/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_internal.h Variables and function used internally. */

#ifndef NETWORK_INTERNAL_H
#define NETWORK_INTERNAL_H

#include "network_func.h"
#include "core/tcp_coordinator.h"
#include "core/tcp_game.h"

#include "../command_type.h"
#include "../date_type.h"

#include <vector>
#include <array>
#include <memory>

static const uint32 FIND_SERVER_EXTENDED_TOKEN = 0x2A49582A;

#ifdef RANDOM_DEBUG
/**
 * If this line is enable, every frame will have a sync test
 *  this is not needed in normal games. Normal is like 1 sync in 100
 *  frames. You can enable this if you have a lot of desyncs on a certain
 *  game.
 * Remember: both client and server have to be compiled with this
 *  option enabled to make it to work. If one of the two has it disabled
 *  nothing will happen.
 */
#define ENABLE_NETWORK_SYNC_EVERY_FRAME

/**
 * In theory sending 1 of the 2 seeds is enough to check for desyncs
 *   so in theory, this next define can be left off.
 */
#define NETWORK_SEND_DOUBLE_SEED
#endif /* RANDOM_DEBUG */

/**
 * Helper variable to make the dedicated server go fast until the (first) join.
 * Used to load the desync debug logs, i.e. for reproducing a desync.
 * There's basically no need to ever enable this, unless you really know what
 * you are doing, i.e. debugging a desync.
 * See docs/desync.txt for details.
 */
#ifdef DEBUG_DUMP_COMMANDS
extern bool _ddc_fastforward;
#else
#define _ddc_fastforward (false)
#endif /* DEBUG_DUMP_COMMANDS */

typedef class ServerNetworkGameSocketHandler NetworkClientSocket;

/** Status of the clients during joining. */
enum NetworkJoinStatus {
	NETWORK_JOIN_STATUS_CONNECTING,
	NETWORK_JOIN_STATUS_AUTHORIZING,
	NETWORK_JOIN_STATUS_WAITING,
	NETWORK_JOIN_STATUS_DOWNLOADING,
	NETWORK_JOIN_STATUS_PROCESSING,
	NETWORK_JOIN_STATUS_REGISTERING,

	NETWORK_JOIN_STATUS_GETTING_COMPANY_INFO,
	NETWORK_JOIN_STATUS_END,
};

extern uint32 _frame_counter_server; // The frame_counter of the server, if in network-mode
extern uint32 _frame_counter_max; // To where we may go with our clients
extern uint32 _frame_counter;

extern uint32 _last_sync_frame; // Used in the server to store the last time a sync packet was sent to clients.

/* networking settings */
extern NetworkAddressList _broadcast_list;

extern uint32 _sync_seed_1;
#ifdef NETWORK_SEND_DOUBLE_SEED
extern uint32 _sync_seed_2;
#endif
extern uint64 _sync_state_checksum;
extern uint32 _sync_frame;
extern Date   _last_sync_date;
extern DateFract _last_sync_date_fract;
extern uint8  _last_sync_tick_skip_counter;
extern uint32  _last_sync_frame_counter;
extern bool _network_first_time;
/* Vars needed for the join-GUI */
extern NetworkJoinStatus _network_join_status;
extern uint8 _network_join_waiting;
extern uint32 _network_join_bytes;
extern uint32 _network_join_bytes_total;
extern ConnectionType _network_server_connection_type;
extern std::string _network_server_invite_code;

/* Variable available for clients. */
extern std::string _network_server_name;

extern uint8 _network_reconnect;

extern CompanyMask _network_company_passworded;

/* Sync debugging */
struct NetworkSyncRecord {
	uint32 frame;
	uint32 seed_1;
	uint64 state_checksum;
};
extern std::vector<NetworkSyncRecord> _network_client_sync_records;
extern std::unique_ptr<std::array<NetworkSyncRecord, 1024>> _network_server_sync_records;
extern uint32 _network_server_sync_records_next;

void NetworkQueryServer(const std::string &connection_string);

void GetBindAddresses(NetworkAddressList *addresses, uint16 port);
struct NetworkGameList *NetworkAddServer(const std::string &connection_string, bool manually = true, bool never_expire = false);
void NetworkRebuildHostList();
void UpdateNetworkGameWindow();

struct NetworkGameKeys {
	byte x25519_priv_key[32];    ///< x25519 key: private part
	byte x25519_pub_key[32];     ///< x25519 key: public part
	bool inited = false;

	void Initialise();
};

struct NetworkSharedSecrets {
	byte shared_data[64];

	~NetworkSharedSecrets();
};

/* From network_command.cpp */
/**
 * Everything we need to know about a command to be able to execute it.
 */
struct CommandPacket : CommandContainer {
	/** Make sure the pointer is nullptr. */
	CommandPacket() : next(nullptr), frame(0), client_id(INVALID_CLIENT_ID), company(INVALID_COMPANY), my_cmd(false) {}
	CommandPacket *next; ///< the next command packet (if in queue)
	uint32 frame;        ///< the frame in which this packet is executed
	ClientID client_id;  ///< originating client ID (or INVALID_CLIENT_ID if not specified)
	CompanyID company;   ///< company that is executing the command
	bool my_cmd;         ///< did the command originate from "me"
};

void NetworkDistributeCommands();
void NetworkExecuteLocalCommandQueue();
void NetworkFreeLocalCommandQueue();
void NetworkSyncCommandQueue(NetworkClientSocket *cs);

void ShowNetworkError(StringID error_string);
void NetworkTextMessage(NetworkAction action, TextColour colour, bool self_send, const std::string &name, const std::string &str = "", NetworkTextMessageData data = NetworkTextMessageData(), const char *data_str = "");
uint NetworkCalculateLag(const NetworkClientSocket *cs);
StringID GetNetworkErrorMsg(NetworkErrorCode err);
bool NetworkMakeClientNameUnique(std::string &new_name);
std::string GenerateCompanyPasswordHash(const std::string &password, const std::string &password_server_id, uint32 password_game_seed);
std::vector<uint8> GenerateGeneralPasswordHash(const std::string &password, const std::string &password_server_id, uint64 password_game_seed);
std::string BytesToHexString(const byte *data, size_t length);
std::string NetworkGenerateRandomKeyString(uint bytes);

std::string_view ParseCompanyFromConnectionString(const std::string &connection_string, CompanyID *company_id);
NetworkAddress ParseConnectionString(const std::string &connection_string, uint16 default_port);
std::string NormalizeConnectionString(const std::string &connection_string, uint16 default_port);

void ClientNetworkEmergencySave();

void NetworkRandomBytesWithFallback(void *buf, size_t n);

#endif /* NETWORK_INTERNAL_H */
