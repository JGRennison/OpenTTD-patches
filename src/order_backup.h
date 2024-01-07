/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_backup.h Functions related to order backups. */

#ifndef ORDER_BACKUP_H
#define ORDER_BACKUP_H

#include "core/pool_type.hpp"
#include "group_type.h"
#include "tile_type.h"
#include "vehicle_type.h"
#include "base_consist.h"
#include "order_base.h"
#include "sl/saveload_common.h"
#include <vector>

/** Unique identifier for an order backup. */
typedef uint8_t OrderBackupID;
struct OrderBackup;

/** The pool type for order backups. */
typedef Pool<OrderBackup, OrderBackupID, 1, 256> OrderBackupPool;
/** The pool with order backups. */
extern OrderBackupPool _order_backup_pool;

/** Flag to pass to the vehicle construction command when an order should be preserved. */
static const uint32_t MAKE_ORDER_BACKUP_FLAG = 1U << 31;

namespace upstream_sl {
	SaveLoadTable GetOrderBackupDescription();
	struct BKORChunkHandler;
}

/**
 * Data for backing up an order of a vehicle so it can be
 * restored after a vehicle is rebuilt in the same depot.
 */
struct OrderBackup : OrderBackupPool::PoolItem<&_order_backup_pool>, BaseConsist {
private:
	friend SaveLoadTable GetOrderBackupDescription(); ///< Saving and loading of order backups.
	friend upstream_sl::SaveLoadTable upstream_sl::GetOrderBackupDescription(); ///< Saving and loading of order backups.
	friend void Load_BKOR();   ///< Creating empty orders upon savegame loading.
	friend void Save_BKOR();   ///< Saving orders upon savegame saving.
	friend upstream_sl::BKORChunkHandler;
	uint32_t user;             ///< The user that requested the backup.
	TileIndex tile;            ///< Tile of the depot where the order was changed.
	GroupID group;             ///< The group the vehicle was part of.

	const Vehicle *clone;      ///< Vehicle this vehicle was a clone of.
	Order *orders;             ///< The actual orders if the vehicle was not a clone.

	std::vector<DispatchSchedule> dispatch_schedules; ///< Scheduled dispatch schedules

	static uint update_counter;

	/** Creation for savegame restoration. */
	OrderBackup() {}
	OrderBackup(const Vehicle *v, uint32_t user);

	void DoRestore(Vehicle *v);

public:
	~OrderBackup();

	static void Backup(const Vehicle *v, uint32_t user);
	static void Restore(Vehicle *v, uint32_t user);

	static void ResetOfUser(TileIndex tile, uint32_t user);
	static void ResetUser(uint32_t user);
	static void Reset(TileIndex tile = INVALID_TILE, bool from_gui = true);

	static void ClearGroup(GroupID group);
	static void ClearVehicle(const Vehicle *v);
	static void RemoveOrder(OrderType type, DestinationID destination, bool hangar);

	static uint GetUpdateCounter() { return update_counter; }
};

#endif /* ORDER_BACKUP_H */
