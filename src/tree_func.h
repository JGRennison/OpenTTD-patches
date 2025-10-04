/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tree_func.h Tree functions and related types. */

#ifndef TREE_FUNC_H
#define TREE_FUNC_H

#include "economy_type.h"
#include "tree_type.h"
#include "3rdparty/robin_hood/robin_hood.h"

extern bool _tree_placer_preview_active;
extern robin_hood::unordered_flat_map<TileIndex, TreePlacerData> _tree_placer_memory;
extern TreeTypeRange _current_tree_type_range;
extern TreeTypes _current_tree_type_mask;

void PlaceTreesRandomly();
void RemoveAllTrees();
void PlaceTreeGroupAroundTile(TileIndex tile, TreeTypes tree_types, uint radius, uint count);
void SendSyncTrees(TileIndex cmd_tile);
void UpdateTreeTypeRange();

#endif /* TREE_FUNC_H */
