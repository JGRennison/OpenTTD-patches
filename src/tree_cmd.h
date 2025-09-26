/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tree_cmd.h Command definitions related to tree tiles. */

#ifndef TREE_CMD_H
#define TREE_CMD_H

#include "command_type.h"
#include <map>

struct TreePlacerData {
	uint8_t type; uint8_t count;
};

struct TreeSyncCmdData final : public CommandPayloadSerialisable<TreeSyncCmdData> {
	ClientID calling_client = INVALID_CLIENT_ID; // Client using this command.
	std::map<TileIndex, TreePlacerData> sync_data; // Map of every tile index and the tree type/count intended to be on this tile.

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const override;
};

DEF_CMD_TUPLE(CMD_PLANT_TREE, CmdPlantTree, CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, uint8_t, uint8_t, bool>)
DEF_CMD_DIRECT_NT(CMD_SYNC_TREES, CmdSyncTrees, CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, TreeSyncCmdData)

#endif /* TREE_CMD_H */
