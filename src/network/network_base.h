/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_base.h Base core network types and some helper functions to access them. */

#ifndef NETWORK_BASE_H
#define NETWORK_BASE_H

#include "network_type.h"
#include "core/address.h"
#include "../core/pool_type.hpp"
#include "../company_type.h"
#include "../date_type.h"

/** Type for the pool with client information. */
using NetworkClientInfoPool = Pool<NetworkClientInfo, ClientPoolID, 8, MAX_CLIENT_SLOTS, PoolType::NetworkClient>;
extern NetworkClientInfoPool _networkclientinfo_pool;

/** Container for all information known about a client. */
struct NetworkClientInfo : NetworkClientInfoPool::PoolItem<&_networkclientinfo_pool> {
	ClientID client_id;      ///< Client identifier (same as ClientState->client_id)
	std::string client_name; ///< Name of the client
	//std::string public_key; ///< The public key of the client.
	CompanyID client_playas; ///< As which company is this client playing (CompanyID)
	EconTime::Date join_date; ///< Gamedate the client has joined
	EconTime::DateFract join_date_fract;
	uint8_t join_tick_skip_counter;
	uint32_t join_frame;

	/**
	 * Create a new client.
	 * @param client_id The unique identifier of the client.
	 */
	NetworkClientInfo(ClientID client_id = INVALID_CLIENT_ID) : client_id(client_id) {}
	~NetworkClientInfo();

	static NetworkClientInfo *GetByClientID(ClientID client_id);
};

#endif /* NETWORK_BASE_H */
