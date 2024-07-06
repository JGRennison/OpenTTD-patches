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
#include "network_sync.h"
#include "core/tcp_coordinator.h"
#include "core/tcp_game.h"

#include "../command_type.h"
#include "../date_type.h"

#include <array>
#include <vector>

static const uint32_t FIND_SERVER_EXTENDED_TOKEN = 0x2A49582A;

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
#endif /* RANDOM_DEBUG */

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

extern uint32_t _frame_counter_server; // The frame_counter of the server, if in network-mode
extern uint32_t _frame_counter_max; // To where we may go with our clients
extern uint32_t _frame_counter;

extern uint32_t _last_sync_frame; // Used in the server to store the last time a sync packet was sent to clients.

/* networking settings */
extern NetworkAddressList _broadcast_list;

extern uint32_t _sync_seed_1;
extern uint64_t _sync_state_checksum;
extern uint32_t _sync_frame;
extern EconTime::Date _last_sync_date;
extern EconTime::DateFract _last_sync_date_fract;
extern uint8_t _last_sync_tick_skip_counter;
extern uint32_t _last_sync_frame_counter;
extern bool _network_first_time;
/* Vars needed for the join-GUI */
extern NetworkJoinStatus _network_join_status;
extern uint8_t _network_join_waiting;
extern uint32_t _network_join_bytes;
extern uint32_t _network_join_bytes_total;
extern ConnectionType _network_server_connection_type;
extern std::string _network_server_invite_code;

/* Variable available for clients. */
extern std::string _network_server_name;

extern uint8_t _network_reconnect;

extern CompanyMask _network_company_passworded;

void NetworkQueryServer(const std::string &connection_string);

void GetBindAddresses(NetworkAddressList *addresses, uint16_t port);
struct NetworkGameList *NetworkAddServer(const std::string &connection_string, bool manually = true, bool never_expire = false);
void NetworkRebuildHostList();
void UpdateNetworkGameWindow();

struct NetworkGameKeys {
	std::array<uint8_t, 32> x25519_priv_key;    ///< x25519 key: private part
	std::array<uint8_t, 32> x25519_pub_key;     ///< x25519 key: public part
	bool inited = false;

	void Initialise();
};

struct NetworkSharedSecrets {
	std::array<uint8_t, 64> shared_data;

	~NetworkSharedSecrets();
};

/* From network_command.cpp */
/**
 * Everything we need to know about a command to be able to execute it.
 */
struct CommandPacket : CommandContainer {
	/** Make sure the pointer is nullptr. */
	CommandPacket() : frame(0), client_id(INVALID_CLIENT_ID), company(INVALID_COMPANY), my_cmd(false) {}
	uint32_t frame;      ///< the frame in which this packet is executed
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
std::string GenerateCompanyPasswordHash(const std::string &password, const std::string &password_server_id, uint32_t password_game_seed);
std::vector<uint8_t> GenerateGeneralPasswordHash(const std::string &password, const std::string &password_server_id, uint64_t password_game_seed);
std::string NetworkGenerateRandomKeyString(uint bytes);

std::string_view ParseCompanyFromConnectionString(const std::string &connection_string, CompanyID *company_id);
NetworkAddress ParseConnectionString(const std::string &connection_string, uint16_t default_port);
std::string NormalizeConnectionString(const std::string &connection_string, uint16_t default_port);

void ClientNetworkEmergencySave();

#endif /* NETWORK_INTERNAL_H */
