/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file network_client.h Client part of the network protocol. */

#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include "network_internal.h"

/** Class for handling the client side of the game connection. */
class ClientNetworkGameSocketHandler : public NetworkGameSocketHandler {
private:
	std::unique_ptr<class NetworkAuthenticationClientHandler> authentication_handler; ///< The handler for the authentication.
	std::string connection_string;                   ///< Address we are connected to.
	std::shared_ptr<struct PacketReader> savegame;   ///< Packet reader for reading the savegame.
	uint8_t token = 0;                               ///< The token we need to send back to the server to prove we're the right client.
	NetworkSharedSecrets last_rcon_shared_secrets{}; ///< Keys for last rcon (and incoming replies)

	/** Status of the connection with the server. */
	enum ServerStatus : uint8_t {
		STATUS_INACTIVE,      ///< The client is not connected nor active.
		STATUS_JOIN,          ///< We are trying to join a server.
		STATUS_AUTH_GAME,     ///< Last action was requesting game (server) password.
		STATUS_ENCRYPTED,     ///< The game authentication has completed and from here on the connection to the server is encrypted.
		STATUS_NEWGRFS_CHECK, ///< Last action was checking NewGRFs.
		STATUS_AUTH_COMPANY,  ///< Last action was requesting company password.
		STATUS_AUTHORIZED,    ///< The client is authorized at the server.
		STATUS_MAP_WAIT,      ///< The client is waiting as someone else is downloading the map.
		STATUS_MAP,           ///< The client is downloading the map.
		STATUS_ACTIVE,        ///< The client is active within in the game.
		STATUS_CLOSING,       ///< The client connection is in the process of being closed.
		STATUS_END,           ///< Must ALWAYS be on the end of this list!! (period)
	};

	ServerStatus status = STATUS_INACTIVE; ///< Status of the connection with the server.

	std::optional<FileHandle> desync_log_file;
	std::string server_desync_log;
	bool emergency_save_done = false;

	NetworkGameKeys intl_keys;

	static const char *GetServerStatusName(ServerStatus status);

protected:
	friend void NetworkExecuteLocalCommandQueue();
	friend void NetworkClose(bool close_admins);
	static ClientNetworkGameSocketHandler *my_client; ///< This is us!

	NetworkRecvStatus Receive_SERVER_FULL(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_BANNED(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_ERROR(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_CLIENT_INFO(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_AUTH_REQUEST(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_ENABLE_ENCRYPTION(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_NEED_COMPANY_PASSWORD(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_SETTINGS_ACCESS(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_WELCOME(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_WAIT(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_MAP_BEGIN(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_MAP_SIZE(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_MAP_DATA(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_MAP_DONE(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_JOIN(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_FRAME(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_SYNC(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_COMMAND(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_CHAT(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_EXTERNAL_CHAT(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_QUIT(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_ERROR_QUIT(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_DESYNC_LOG(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_SHUTDOWN(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_NEWGAME(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_RCON(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_CHECK_NEWGRFS(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_MOVE(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_COMPANY_UPDATE(Packet &p) override;
	NetworkRecvStatus Receive_SERVER_CONFIG_UPDATE(Packet &p) override;

	static NetworkRecvStatus SendNewGRFsOk();
	static NetworkRecvStatus SendGetMap();
	static NetworkRecvStatus SendMapOk();
	static NetworkRecvStatus SendIdentify();
	void CheckConnection();

	NetworkRecvStatus SendKeyPasswordPacket(PacketType packet_type, NetworkSharedSecrets &ss, const std::string &password, const std::string *payload);

public:
	ClientNetworkGameSocketHandler(SOCKET s, std::string connection_string);
	~ClientNetworkGameSocketHandler();

	NetworkRecvStatus CloseConnection(NetworkRecvStatus status) override;
	void ClientError(NetworkRecvStatus res);

	std::string GetDebugInfo() const override;

	const NetworkGameKeys &GetKeys()
	{
		if (!this->intl_keys.inited) this->intl_keys.Initialise();
		return this->intl_keys;
	}

	static NetworkRecvStatus SendJoin();
	static NetworkRecvStatus SendCommand(const OutgoingCommandPacket &cp);
	static NetworkRecvStatus SendError(NetworkErrorCode errorno, NetworkRecvStatus recvstatus = NETWORK_RECV_STATUS_OKAY);
	static NetworkRecvStatus SendDesyncLog(const std::string &log);
	static NetworkRecvStatus SendDesyncMessage(const char *msg);
	static NetworkRecvStatus SendDesyncSyncData();
	static NetworkRecvStatus SendQuit();
	static NetworkRecvStatus SendAck();

	static NetworkRecvStatus SendAuthResponse();
	static NetworkRecvStatus SendCompanyPassword(const std::string &password);
	static NetworkRecvStatus SendSettingsPassword(const std::string &password);

	static NetworkRecvStatus SendChat(NetworkAction action, DestType type, int dest, const std::string &msg, NetworkTextMessageData data);
	static NetworkRecvStatus SendSetPassword(const std::string &password);
	static NetworkRecvStatus SendSetName(const std::string &name);
	static NetworkRecvStatus SendRCon(const std::string &password, const std::string &command);
	static NetworkRecvStatus SendMove(CompanyID company, const std::string &password);

	static bool IsConnected();

	static void Send();
	static bool Receive();
	static bool GameLoop();

	static bool EmergencySavePossible();
};

/** Helper to make the code look somewhat nicer. */
typedef ClientNetworkGameSocketHandler MyClient;

void NetworkClient_Connected();
void NetworkClientSetCompanyPassword(const std::string &password);

/** Information required to join a server. */
struct NetworkJoinInfo {
	NetworkJoinInfo() : company(COMPANY_SPECTATOR) {}
	std::string connection_string; ///< The address of the server to join.
	CompanyID company;             ///< The company to join.
	std::string server_password;   ///< The password of the server to join.
	std::string company_password;  ///< The password of the company to join.
};

extern NetworkJoinInfo _network_join;

#endif /* NETWORK_CLIENT_H */
