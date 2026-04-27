/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_network_debug.cpp Tests for network debug related functions. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../network/core/tcp_game.h"


TEST_CASE("GetPacketGameTypeName")
{
	uint number_of_tests = 0;
#define CHECK_NAME(pkttype) number_of_tests++; CHECK(GetPacketGameTypeName(PacketGameType:: pkttype) == std::string_view{#pkttype})
	CHECK_NAME(ServerFull);
	CHECK_NAME(ServerBanned);
	CHECK_NAME(ClientJoin);
	CHECK_NAME(ServerError);
	CHECK_NAME(ClientUnused);
	CHECK_NAME(ServerUnused);
	CHECK_NAME(ServerGameInfo);
	CHECK_NAME(ClientGameInfo);
	CHECK_NAME(ServerNewGame);
	CHECK_NAME(ServerShutdown);
	CHECK_NAME(ServerGameInfoExtended);
	CHECK_NAME(ServerAuthenticationRequest);
	CHECK_NAME(ClientAuthenticationResponse);
	CHECK_NAME(ServerEnableEncryption);
	CHECK_NAME(ClientIdentify);
	CHECK_NAME(ServerCheckNewGRFs);
	CHECK_NAME(ClientNewGRFsChecked);
	CHECK_NAME(ServerNeedCompanyPassword);
	CHECK_NAME(ClientCompanyPassword);
	CHECK_NAME(ClientSettingsPassword);
	CHECK_NAME(ServerSettingsAccess);
	CHECK_NAME(ServerWelcome);
	CHECK_NAME(ServerClientInfo);
	CHECK_NAME(ClientGetMap);
	CHECK_NAME(ServerWaitForMap);
	CHECK_NAME(ServerMapBegin);
	CHECK_NAME(ServerMapSize);
	CHECK_NAME(ServerMapData);
	CHECK_NAME(ServerMapDone);
	CHECK_NAME(ClientMapOk);
	CHECK_NAME(ServerClientJoined);
	CHECK_NAME(ServerFrame);
	CHECK_NAME(ClientAck);
	CHECK_NAME(ServerSync);
	CHECK_NAME(ClientCommand);
	CHECK_NAME(ServerCommand);
	CHECK_NAME(ClientChat);
	CHECK_NAME(ServerChat);
	CHECK_NAME(ServerExternalChat);
	CHECK_NAME(ClientRemoteConsoleCommand);
	CHECK_NAME(ServerRemoteConsoleCommand);
	CHECK_NAME(ClientMove);
	CHECK_NAME(ServerMove);
	CHECK_NAME(ClientSetPassword);
	CHECK_NAME(ClientSetName);
	CHECK_NAME(ServerCompanyUpdate);
	CHECK_NAME(ServerConfigurationUpdate);
	CHECK_NAME(ClientQuit);
	CHECK_NAME(ServerQuit);
	CHECK_NAME(ClientError);
	CHECK_NAME(ServerErrorQuit);
	CHECK_NAME(ClientDesyncLog);
	CHECK_NAME(ServerDesyncLog);
	CHECK_NAME(ClientDesyncMessage);
	CHECK_NAME(ClientDesyncSyncData);
#undef CHECK_NAME
	CHECK(number_of_tests == static_cast<uint>(PacketGameType::End));
}
