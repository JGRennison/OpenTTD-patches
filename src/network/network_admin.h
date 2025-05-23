/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_admin.h Server part of the admin network protocol. */

#ifndef NETWORK_ADMIN_H
#define NETWORK_ADMIN_H

#include "network_internal.h"
#include "core/tcp_listen.h"
#include "core/tcp_admin.h"

extern AdminID _redirect_console_to_admin;

class ServerNetworkAdminSocketHandler;
/** Pool with all admin connections. */
using NetworkAdminSocketPool = Pool<ServerNetworkAdminSocketHandler, AdminID, 2, 16, PoolType::NetworkAdmin>;
extern NetworkAdminSocketPool _networkadminsocket_pool;

/** Class for handling the server side of the game connection. */
class ServerNetworkAdminSocketHandler : public NetworkAdminSocketPool::PoolItem<&_networkadminsocket_pool>, public NetworkAdminSocketHandler, public TCPListenHandler<ServerNetworkAdminSocketHandler, ADMIN_PACKET_SERVER_FULL, ADMIN_PACKET_SERVER_BANNED> {
private:
	std::unique_ptr<NetworkAuthenticationServerHandler> authentication_handler; ///< The handler for the authentication.
protected:
	NetworkRecvStatus Receive_ADMIN_JOIN(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_QUIT(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_UPDATE_FREQUENCY(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_POLL(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_CHAT(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_EXTERNAL_CHAT(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_RCON(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_GAMESCRIPT(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_PING(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_JOIN_SECURE(Packet &p) override;
	NetworkRecvStatus Receive_ADMIN_AUTH_RESPONSE(Packet &p) override;

	NetworkRecvStatus SendProtocol();
	NetworkRecvStatus SendPong(uint32_t d1);
	NetworkRecvStatus SendAuthRequest();
	NetworkRecvStatus SendEnableEncryption();
public:
	AdminUpdateFrequency update_frequency[ADMIN_UPDATE_END]; ///< Admin requested update intervals.
	std::chrono::steady_clock::time_point connect_time;      ///< Time of connection.
	NetworkAddress address;                                  ///< Address of the admin.

	ServerNetworkAdminSocketHandler(SOCKET s);
	~ServerNetworkAdminSocketHandler();

	NetworkRecvStatus SendError(NetworkErrorCode error);
	NetworkRecvStatus SendWelcome();
	NetworkRecvStatus SendNewGame();
	NetworkRecvStatus SendShutdown();

	NetworkRecvStatus SendDate();
	NetworkRecvStatus SendClientJoin(ClientID client_id);
	NetworkRecvStatus SendClientInfo(const NetworkClientSocket *cs, const NetworkClientInfo *ci);
	NetworkRecvStatus SendClientUpdate(const NetworkClientInfo *ci);
	NetworkRecvStatus SendClientQuit(ClientID client_id);
	NetworkRecvStatus SendClientError(ClientID client_id, NetworkErrorCode error);
	NetworkRecvStatus SendCompanyNew(CompanyID company_id);
	NetworkRecvStatus SendCompanyInfo(const Company *c);
	NetworkRecvStatus SendCompanyUpdate(const Company *c);
	NetworkRecvStatus SendCompanyRemove(CompanyID company_id, AdminCompanyRemoveReason bcrr);
	NetworkRecvStatus SendCompanyEconomy();
	NetworkRecvStatus SendCompanyStats();

	NetworkRecvStatus SendChat(NetworkAction action, DestType desttype, ClientID client_id, const std::string &msg, NetworkTextMessageData data);
	NetworkRecvStatus SendRcon(uint16_t colour, const std::string_view command);
	NetworkRecvStatus SendConsole(const std::string_view origin, const std::string_view command);
	NetworkRecvStatus SendGameScript(const std::string_view json);
	NetworkRecvStatus SendCmdNames();
	NetworkRecvStatus SendCmdLogging(ClientID client_id, const CommandPacket &cp);
	NetworkRecvStatus SendRconEnd(const std::string_view command);

	static void Send();
	static void AcceptConnection(SOCKET s, const NetworkAddress &address);
	static bool AllowConnection();
	static void WelcomeAll();

	/**
	 * Get the name used by the listener.
	 * @return the name to show in debug logs and the like.
	 */
	static const char *GetName()
	{
		return "admin";
	}

	struct ServerNetworkAdminSocketHandlerFilter {
		bool operator() (size_t index) { return ServerNetworkAdminSocketHandler::Get(index)->GetAdminStatus() == ADMIN_STATUS_ACTIVE; }
	};

	/**
	 * Returns an iterable ensemble of all active admin sockets
	 * @param from index of the first socket to consider
	 * @return an iterable ensemble of all active admin sockets
	 */
	static Pool::IterateWrapperFiltered<ServerNetworkAdminSocketHandler, ServerNetworkAdminSocketHandlerFilter> IterateActive(size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<ServerNetworkAdminSocketHandler, ServerNetworkAdminSocketHandlerFilter>(from, ServerNetworkAdminSocketHandlerFilter{});
	}
};

void NetworkAdminClientInfo(const NetworkClientSocket *cs, bool new_client = false);
void NetworkAdminClientUpdate(const NetworkClientInfo *ci);
void NetworkAdminClientQuit(ClientID client_id);
void NetworkAdminClientError(ClientID client_id, NetworkErrorCode error_code);
void NetworkAdminCompanyNew(const Company *company);
void NetworkAdminCompanyUpdate(const Company *company);
void NetworkAdminCompanyRemove(CompanyID company_id, AdminCompanyRemoveReason bcrr);

void NetworkAdminChat(NetworkAction action, DestType desttype, ClientID client_id, const std::string &msg, NetworkTextMessageData data = NetworkTextMessageData(), bool from_admin = false);
void NetworkAdminUpdate(AdminUpdateFrequency freq);
void NetworkServerSendAdminRcon(AdminID admin_index, TextColour colour_code, const std::string_view string);
void NetworkAdminConsole(const std::string_view origin, const std::string_view string);
void NetworkAdminGameScript(const std::string_view json);
void NetworkAdminCmdLogging(const NetworkClientSocket *owner, const CommandPacket &cp);

#endif /* NETWORK_ADMIN_H */
