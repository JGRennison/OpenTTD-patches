/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_sl.h Code handling saving and loading of vehicles. */

#ifndef SL_VEHICLE_SL_H
#define SL_VEHICLE_SL_H

#include "../base_consist.h"
#include "saveload.h"

struct DispatchRecordsStructHandlerBase : public SaveLoadStructHandler {
	using RecordPair = std::pair<const uint16_t, LastDispatchRecord>;

	NamedSaveLoadTable GetDescription() const override;
	void SaveDispatchRecords(btree::btree_map<uint16_t, LastDispatchRecord> &records) const;
	void LoadDispatchRecords(btree::btree_map<uint16_t, LastDispatchRecord> &records) const;
};

#endif /* SL_NEWGRF_SL_H */
