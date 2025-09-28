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
#include "tree_type.h"
#include <vector>

struct TreePlacerData {
	TreeType tree_type;
	uint8_t count;
};

struct BulkTreeCmdData final : public CommandPayloadSerialisable<BulkTreeCmdData> {
	static constexpr size_t MAX_SERIALISED_COUNT = 512;

	std::vector<std::pair<TileIndex, TreePlacerData>> plant_tree_data; // List of every tile index and the tree type/count intended to be on this tile.

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const override;
};

DEF_CMD_TUPLE(CMD_PLANT_TREE, CmdPlantTree, CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, TreeType, uint8_t, bool>)
DEF_CMD_DIRECT_LT(CMD_BULK_TREE, CmdBulkTree, CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, BulkTreeCmdData)

#endif /* TREE_CMD_H */
