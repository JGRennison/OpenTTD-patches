/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group.h Base class for groups and group functions. */

#ifndef GROUP_H
#define GROUP_H

#include "group_type.h"
#include "core/pool_type.hpp"
#include "company_type.h"
#include "vehicle_type.h"
#include "engine_type.h"
#include "livery.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include <string>

typedef Pool<Group, GroupID, 16, 64000> GroupPool;
extern GroupPool _group_pool; ///< Pool of groups.

/** Statistics and caches on the vehicles in a group. */
struct GroupStatistics {
	Money profit_last_year;                 ///< Sum of profits for all vehicles.
	Money profit_last_year_min_age;         ///< Sum of profits for vehicles considered for profit statistics.
	btree::btree_map<EngineID, uint16_t> num_engines; ///< Caches the number of engines of each type the company owns.
	uint16_t num_vehicle;                   ///< Number of vehicles.
	uint16_t num_vehicle_min_age;           ///< Number of vehicles considered for profit statistics;
	bool autoreplace_defined;               ///< Are any autoreplace rules set?
	bool autoreplace_finished;              ///< Have all autoreplacement finished?

	void Clear();

	void ClearProfits()
	{
		this->profit_last_year = 0;

		this->num_vehicle_min_age = 0;
		this->profit_last_year_min_age = 0;
	}

	void ClearAutoreplace()
	{
		this->autoreplace_defined = false;
		this->autoreplace_finished = false;
	}

	uint16_t GetNumEngines(EngineID engine) const;

	static GroupStatistics &Get(CompanyID company, GroupID id_g, VehicleType type);
	static GroupStatistics &Get(const Vehicle *v);
	static GroupStatistics &GetAllGroup(const Vehicle *v);

	static void CountVehicle(const Vehicle *v, int delta);
	static void CountEngine(const Vehicle *v, int delta);
	static void AddProfitLastYear(const Vehicle *v);
	static void VehicleReachedMinAge(const Vehicle *v);

	static void UpdateProfits();
	static void UpdateAfterLoad();
	static void UpdateAutoreplace(CompanyID company);
};

enum class GroupFlag : uint8_t {
	ReplaceProtection = 0, ///< If set, the global autoreplace has no effect on the group
	ReplaceWagonRemoval = 1, ///< If set, autoreplace will perform wagon removal on vehicles in this group.
};
using GroupFlags = EnumBitSet<GroupFlag, uint8_t>;

enum class GroupFoldBits : uint8_t {
	None                = 0,
	GroupView           = 1U << 0, ///< If set, this group is folded in the group view.
	TemplateReplaceView = 1U << 1, ///< If set, this group is folded in the template replacement view.
};
DECLARE_ENUM_AS_BIT_SET(GroupFoldBits)

/** Group data. */
struct Group : GroupPool::PoolItem<&_group_pool> {
	std::string name;           ///< Group Name
	Owner owner;                ///< Group Owner
	VehicleType vehicle_type;   ///< Vehicle type of the group

	GroupFlags flags{}; ///< Group flags
	Livery livery;              ///< Custom colour scheme for vehicles in this group
	GroupStatistics statistics; ///< NOSAVE: Statistics and caches on the vehicles in the group.

	GroupFoldBits folded_mask = GroupFoldBits::None; ///< NOSAVE: Which views this group is folded in?

	GroupID parent;             ///< Parent group
	uint16_t number;            ///< Per-company group number.

	Group(CompanyID owner = INVALID_COMPANY);

	bool IsFolded(GroupFoldBits fold_bit) const { return (this->folded_mask & fold_bit) != GroupFoldBits::None; }
};


inline bool IsDefaultGroupID(GroupID index)
{
	return index == DEFAULT_GROUP;
}

/**
 * Checks if a GroupID stands for all vehicles of a company
 * @param id_g The GroupID to check
 * @return true is id_g is identical to ALL_GROUP
 */
inline bool IsAllGroupID(GroupID id_g)
{
	return id_g == ALL_GROUP;
}

inline bool IsTopLevelGroupID(GroupID index)
{
	return index == DEFAULT_GROUP || index == ALL_GROUP;
}

uint GetGroupNumEngines(CompanyID company, GroupID id_g, EngineID id_e);
uint GetGroupNumVehicle(CompanyID company, GroupID id_g, VehicleType type);
uint GetGroupNumVehicleMinAge(CompanyID company, GroupID id_g, VehicleType type);
Money GetGroupProfitLastYearMinAge(CompanyID company, GroupID id_g, VehicleType type);

void SetTrainGroupID(Train *v, GroupID grp);
void UpdateTrainGroupID(Train *v);
void RemoveAllGroupsForCompany(const CompanyID company);
bool GroupIsInGroup(GroupID search, GroupID group);
void UpdateCompanyGroupLiveries(const Company *c);

std::string GenerateAutoNameForVehicleGroup(const Vehicle *v);

#endif /* GROUP_H */
