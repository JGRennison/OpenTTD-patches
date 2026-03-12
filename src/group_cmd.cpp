/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file group_cmd.cpp Handling of the engine groups. */

#include "stdafx.h"
#include "command_func.h"
#include "train.h"
#include "vehiclelist.h"
#include "vehicle_func.h"
#include "autoreplace_base.h"
#include "autoreplace_func.h"
#include "base_station_base.h"
#include "string_func.h"
#include "company_func.h"
#include "core/pool_func.hpp"
#include "order_backup.h"
#include "tbtr_template_vehicle.h"
#include "tracerestrict.h"
#include "group_cmd.h"

#include "table/strings.h"

#include "safeguards.h"
#include "strings_func.h"
#include "town.h"
#include "townname_func.h"

GroupPool _group_pool("Group");
INSTANTIATE_POOL_METHODS(Group)

/**
 * Clear all caches.
 */
void GroupStatistics::Clear()
{
	this->num_vehicle = 0;
	this->profit_last_year = 0;
	this->num_vehicle_min_age = 0;
	this->profit_last_year_min_age = 0;

	/* This is also called when NewGRF change. So the number of engines might have changed. Reset. */
	this->num_engines.clear();
}

/**
 * Get number of vehicles of a specific engine ID.
 * @param engine Engine ID.
 * @returns number of vehicles of this engine ID.
 */
uint16_t GroupStatistics::GetNumEngines(EngineID engine) const
{
	auto found = this->num_engines.find(engine);
	if (found != std::end(this->num_engines)) return found->second;
	return 0;
}

/**
 * Returns the GroupStatistics for a specific group.
 * @param company Owner of the group.
 * @param id_g    GroupID of the group.
 * @param type    VehicleType of the vehicles in the group.
 * @return Statistics for the group.
 */
/* static */ GroupStatistics &GroupStatistics::Get(CompanyID company, GroupID id_g, VehicleType type)
{
	if (Group::IsValidID(id_g)) {
		Group *g = Group::Get(id_g);
		assert(g->owner == company);
		assert(g->vehicle_type == type);
		return g->statistics;
	}

	if (IsDefaultGroupID(id_g)) return Company::Get(company)->group_default[type];
	if (IsAllGroupID(id_g)) return Company::Get(company)->group_all[type];

	NOT_REACHED();
}

/**
 * Returns the GroupStatistic for the group of a vehicle.
 * @param v Vehicle.
 * @return GroupStatistics for the group of the vehicle.
 */
/* static */ GroupStatistics &GroupStatistics::Get(const Vehicle *v)
{
	return GroupStatistics::Get(v->owner, v->group_id, v->type);
}

/**
 * Returns the GroupStatistic for the ALL_GROUP of a vehicle type.
 * @param v Vehicle.
 * @return GroupStatistics for the ALL_GROUP of the vehicle type.
 */
/* static */ GroupStatistics &GroupStatistics::GetAllGroup(const Vehicle *v)
{
	return GroupStatistics::Get(v->owner, ALL_GROUP, v->type);
}

/**
 * Update all caches after loading a game, changing NewGRF, etc.
 */
/* static */ void GroupStatistics::UpdateAfterLoad()
{
	/* Set up the engine count for all companies */
	for (Company *c : Company::Iterate()) {
		for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
			c->group_all[type].Clear();
			c->group_default[type].Clear();
		}
	}

	/* Recalculate */
	for (Group *g : Group::Iterate()) {
		g->statistics.Clear();
	}

	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->IsEngineCountable()) continue;

		GroupStatistics::CountEngine(v, 1);
		if (v->IsPrimaryVehicle()) GroupStatistics::CountVehicle(v, 1);
	}

	for (const Company *c : Company::Iterate()) {
		GroupStatistics::UpdateAutoreplace(c->index);
	}
}

/**
 * Update num_vehicle when adding or removing a vehicle.
 * @param v Vehicle to count.
 * @param delta +1 to add, -1 to remove.
 */
/* static */ void GroupStatistics::CountVehicle(const Vehicle *v, int delta)
{
	/* make virtual trains group-neutral */
	if (HasBit(v->subtype, GVSF_VIRTUAL)) return;

	assert(delta == 1 || delta == -1);

	GroupStatistics &stats_all = GroupStatistics::GetAllGroup(v);
	GroupStatistics &stats = GroupStatistics::Get(v);

	stats_all.num_vehicle += delta;
	stats_all.profit_last_year += v->GetDisplayProfitLastYear() * delta;
	stats.num_vehicle += delta;
	stats.profit_last_year += v->GetDisplayProfitLastYear() * delta;

	if (v->economy_age > VEHICLE_PROFIT_MIN_AGE) {
		stats_all.num_vehicle_min_age += delta;
		stats_all.profit_last_year_min_age += v->GetDisplayProfitLastYear() * delta;
		stats.num_vehicle_min_age += delta;
		stats.profit_last_year_min_age += v->GetDisplayProfitLastYear() * delta;
	}
}

/**
 * Update num_engines when adding/removing an engine.
 * @param v Engine to count.
 * @param delta +1 to add, -1 to remove.
 */
/* static */ void GroupStatistics::CountEngine(const Vehicle *v, int delta)
{
	/* make virtual trains group-neutral */
	if (HasBit(v->subtype, GVSF_VIRTUAL)) return;

	assert(delta == 1 || delta == -1);
	GroupStatistics::GetAllGroup(v).num_engines[v->engine_type] += delta;
	GroupStatistics::Get(v).num_engines[v->engine_type] += delta;
}

/**
 * Add a vehicle's last year profit to the profit sum of its group.
 */
/* static */ void GroupStatistics::AddProfitLastYear(const Vehicle *v)
{
	GroupStatistics &stats_all = GroupStatistics::GetAllGroup(v);
	GroupStatistics &stats = GroupStatistics::Get(v);

	stats_all.profit_last_year += v->GetDisplayProfitLastYear();
	stats.profit_last_year += v->GetDisplayProfitLastYear();
}

/**
 * Add a vehicle to the profit sum of its group.
 */
/* static */ void GroupStatistics::VehicleReachedMinAge(const Vehicle *v)
{
	GroupStatistics &stats_all = GroupStatistics::GetAllGroup(v);
	GroupStatistics &stats = GroupStatistics::Get(v);

	stats_all.num_vehicle_min_age++;
	stats_all.profit_last_year_min_age += v->GetDisplayProfitLastYear();
	stats.num_vehicle_min_age++;
	stats.profit_last_year_min_age += v->GetDisplayProfitLastYear();
}

/**
 * Recompute the profits for all groups.
 */
/* static */ void GroupStatistics::UpdateProfits()
{
	/* Set up the engine count for all companies */
	for (Company *c : Company::Iterate()) {
		for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
			c->group_all[type].ClearProfits();
			c->group_default[type].ClearProfits();
		}
	}

	/* Recalculate */
	for (Group *g : Group::Iterate()) {
		g->statistics.ClearProfits();
	}

	for (const Vehicle *v : Vehicle::IterateFrontOnly()) {
		if (v->IsPrimaryVehicle() && !HasBit(v->subtype, GVSF_VIRTUAL)) {
			GroupStatistics::AddProfitLastYear(v);
			if (v->economy_age > VEHICLE_PROFIT_MIN_AGE) GroupStatistics::VehicleReachedMinAge(v);
		}
	}
}

/**
 * Update autoreplace_defined and autoreplace_finished of all statistics of a company.
 * @param company Company to update statistics for.
 */
/* static */ void GroupStatistics::UpdateAutoreplace(CompanyID company)
{
	/* Set up the engine count for all companies */
	Company *c = Company::Get(company);
	for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
		c->group_all[type].ClearAutoreplace();
		c->group_default[type].ClearAutoreplace();
	}

	/* Recalculate */
	for (Group *g : Group::Iterate()) {
		if (g->owner != company) continue;
		g->statistics.ClearAutoreplace();
	}

	for (EngineRenewList erl = c->engine_renew_list; erl != nullptr; erl = erl->next) {
		const Engine *e = Engine::Get(erl->from);
		GroupStatistics &stats = GroupStatistics::Get(company, erl->group_id, e->type);
		if (!stats.autoreplace_defined) {
			stats.autoreplace_defined = true;
			stats.autoreplace_finished = true;
		}
		if (GetGroupNumEngines(company, erl->group_id, erl->from) > 0) stats.autoreplace_finished = false;
	}
}

/**
 * Update the num engines of a groupID. Decrease the old one and increase the new one
 * @note called in SetTrainGroupID and UpdateTrainGroupID
 * @param v     Vehicle we have to update
 * @param old_g index of the old group
 * @param new_g index of the new group
 */
static inline void UpdateNumEngineGroup(const Vehicle *v, GroupID old_g, GroupID new_g)
{
	if (old_g != new_g) {
		/* Decrease the num engines in the old group */
		GroupStatistics::Get(v->owner, old_g, v->type).num_engines[v->engine_type]--;

		/* Increase the num engines in the new group */
		GroupStatistics::Get(v->owner, new_g, v->type).num_engines[v->engine_type]++;
	}
}


const Livery *GetParentLivery(const Group *g)
{
	if (g->parent == GroupID::Invalid()) {
		const Company *c = Company::Get(g->owner);
		return &c->livery[LS_DEFAULT];
	}

	const Group *pg = Group::Get(g->parent);
	return &pg->livery;
}

static inline bool IsGroupDescendantOfGroupID(const Group *g, const GroupID top_gid, const Owner owner)
{
	if (g->owner != owner) return false;

	while (true) {
		if (g->parent == top_gid) return true;
		if (g->parent == GroupID::Invalid()) return false;
		g = Group::Get(g->parent);
	}

	NOT_REACHED();
}

static inline bool IsGroupDescendantOfGroup(const Group *g, const Group *top)
{
	return IsGroupDescendantOfGroupID(g, top->index, top->owner);
}

static inline bool IsGroupIDDescendantOfGroupID(const GroupID gid, const GroupID top_gid, const Owner owner)
{
	if (IsTopLevelGroupID(gid) || gid == GroupID::Invalid()) return false;

	return IsGroupDescendantOfGroupID(Group::Get(gid), top_gid, owner);
}

template <typename F>
void IterateDescendantsOfGroup(const Group *top, F func)
{
	for (Group *cg : Group::Iterate()) {
		if (IsGroupDescendantOfGroup(cg, top)) {
			func(cg);
		}
	}
}

template <typename F>
void IterateDescendantsOfGroup(GroupID id_top, F func)
{
	const Group *top = Group::GetIfValid(id_top);
	if (top != nullptr) IterateDescendantsOfGroup<F>(top, func);
}

static void PropagateChildLiveryResetVehicleCache(const Group *g)
{
	/* Company colour data is indirectly cached. */
	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		if (v->IsPrimaryVehicle() && (v->group_id == g->index || IsGroupIDDescendantOfGroupID(v->group_id, g->index, g->owner))) {
			for (Vehicle *u = v; u != nullptr; u = u->Next()) {
				u->colourmap = PAL_NONE;
				u->InvalidateNewGRFCache();
				u->InvalidateImageCache();
			}
		}
	}
}

static void PropagateChildLivery(const GroupID top_gid, const Owner owner, const Livery &top_livery)
{
	for (Group *g : Group::Iterate()) {
		if (g->owner != owner) continue;

		Livery livery = g->livery;

		const Group *pg = g;
		bool is_descendant = (g->index == top_gid);
		while (!is_descendant) {
			if (pg->parent == top_gid) {
				is_descendant = true;
				break;
			}
			if (pg->parent == GroupID::Invalid()) break;
			pg = Group::Get(pg->parent);
			if (!livery.in_use.Test(Livery::Flag::Primary)) livery.colour1 = pg->livery.colour1;
			if (!livery.in_use.Test(Livery::Flag::Secondary)) livery.colour2 = pg->livery.colour2;
			livery.in_use |= pg->livery.in_use;
		}
		if (is_descendant) {
			if (!livery.in_use.Test(Livery::Flag::Primary)) livery.colour1 = top_livery.colour1;
			if (!livery.in_use.Test(Livery::Flag::Secondary)) livery.colour2 = top_livery.colour2;
			g->livery.colour1 = livery.colour1;
			g->livery.colour2 = livery.colour2;
		}
	}
}

/**
 * Propagate a livery change to a group's children, and optionally update cached vehicle colourmaps.
 * @param g Group to propagate colours to children.
 * @param reset_cache Reset colourmap of vehicles in this group.
 */
static void PropagateChildLivery(const Group *g, bool reset_cache)
{
	PropagateChildLivery(g->index, g->owner, g->livery);
	if (reset_cache) PropagateChildLiveryResetVehicleCache(g);
}

/**
 * Update group liveries for a company. This is called when the LS_DEFAULT scheme is changed, to update groups with
 * colours set to default.
 * @param c Company to update.
 */
void UpdateCompanyGroupLiveries(const Company *c)
{
	PropagateChildLivery(GroupID::Invalid(), c->index, c->livery[LS_DEFAULT]);
}

struct GroupChangeDeferredUpdates {
	uint32_t refcount = 0;
	VehicleType vt{};

private:
	static constexpr const uint32_t MAX_VEHS = 8; ///< Maximum number of vehicles to record, beyond this just do a bulk update

	bool new_group = false;
	bool changed_groups = false;
	bool update_autoreplace = false;
	uint32_t veh_id_count = 0;
	std::array<Vehicle *, MAX_VEHS> veh_ids;

public:
	void RegisterNewGroup() { this->new_group = true; }
	void RegisterChangedGroup() { this->changed_groups = true; }
	void UpdateAutoreplace() { this->update_autoreplace = true; }

	void AddVehicle(Vehicle *v)
	{
		if (this->veh_id_count < MAX_VEHS) {
			this->veh_ids[this->veh_id_count] = v;
			this->veh_id_count++;
		} else {
			this->veh_id_count = UINT32_MAX;
		}
	}

	void Flush();
};
static GroupChangeDeferredUpdates _group_change_deferred_updates;

static void InvalidateCurrentCompanyGroupWindows(VehicleType vt, bool new_group)
{
	WindowClass wc = GetWindowClassForVehicleType(vt);
	if (HaveWindowByClass(wc)) {
		WindowNumber list_window_num = VehicleListIdentifier(VL_GROUP_LIST, vt, _current_company).ToWindowNumber();
		for (Window *w : Window::Iterate()) {
			if (w->window_class != wc) continue;

			if (w->window_number == list_window_num) {
				/* Invalidate group list window */
				w->InvalidateData(0, false);
			} else if (!new_group && VehicleListIdentifier::UnPackVehicleType(w->window_number) == vt) {
				/* Mark dirty other windows which might be affected */
				w->SetDirty();
			}
		}
	}
	if (vt == VEH_TRAIN && _current_company == _local_company) InvalidateWindowData(WC_TEMPLATEGUI_MAIN, 0, 0);

	if (!new_group && (_settings_client.gui.departure_show_vehicle || _settings_client.gui.departure_show_group)) {
		SetWindowClassesDirty(WC_DEPARTURES_BOARD);
	}
}

static void BulkUpdateVehicleWindowsOnGroupChange(VehicleID veh_id = VehicleID::Invalid())
{
	/* Iterate all applicable vehicle windows in one pass. */
	if (HaveWindowByClass(WC_VEHICLE_VIEW) || HaveWindowByClass(WC_VEHICLE_DETAILS) || HaveWindowByClass(WC_VEHICLE_ORDERS) ||
			HaveWindowByClass(WC_VEHICLE_TIMETABLE) || HaveWindowByClass(WC_SCHDISPATCH_SLOTS)) {
		for (Window *w : Window::Iterate()) {
			if (veh_id != VehicleID::Invalid()) {
				if (w->window_number != veh_id) continue;
			} else {
				if (w->owner != _current_company) continue;
			}
			switch (w->window_class) {
				case WC_VEHICLE_DETAILS:
					w->InvalidateData(0, false);
					break;

				case WC_VEHICLE_VIEW:
				case WC_VEHICLE_ORDERS:
				case WC_VEHICLE_TIMETABLE:
				case WC_SCHDISPATCH_SLOTS:
					/* Set widget 0 (caption) dirty. */
					w->SetWidgetDirty(0);
					break;

				default:
					break;
			}
		}
	}
}

void GroupChangeDeferredUpdates::Flush()
{
	if (this->veh_id_count > MAX_VEHS) {
		this->changed_groups = true; // Handle many vehicle changes as a bulk group update
		this->veh_id_count = 0;
	}
	if (this->changed_groups) {
		BulkUpdateVehicleWindowsOnGroupChange(); // Individual vehicle windows
		SetWindowWidgetDirty(WC_TRACE_RESTRICT_SLOTS, VehicleListIdentifier(VL_SLOT_LIST, vt, _current_company).ToWindowNumber(), 0); // Slots vehicle panel
	} else if (this->veh_id_count > 0) {
		for (uint i = 0; i < this->veh_id_count; i++) {
			BulkUpdateVehicleWindowsOnGroupChange(this->veh_ids[i]->index); // Individual vehicle windows
			DirtyVehicleListWindowForVehicle(this->veh_ids[i]); // Vehicle list and slot windows
		}
	}

	if (this->veh_id_count != 0 || this->new_group || this->changed_groups) {
		SetWindowDirty(WC_REPLACE_VEHICLE, this->vt);
		GroupStatistics::UpdateAutoreplace(_current_company);

		/* Group/vehicle lists, template window.
		 * If veh_id_count > 0 or changed_groups, also update departure boards if needed. */
		InvalidateCurrentCompanyGroupWindows(this->vt, this->veh_id_count == 0 && !this->changed_groups);
	}

	this->new_group = false;
	this->changed_groups = false;
	this->update_autoreplace = false;
	this->veh_id_count = 0;
}

struct GroupChangeDeferredUpdateScope {
	GroupChangeDeferredUpdateScope(VehicleType vt)
	{
		_group_change_deferred_updates.vt = vt;
		_group_change_deferred_updates.refcount++;
	}

	~GroupChangeDeferredUpdateScope()
	{
		_group_change_deferred_updates.refcount--;
		if (_group_change_deferred_updates.refcount == 0) _group_change_deferred_updates.Flush();
	}
};

/**
 * Create a new vehicle group.
 * @param flags type of operation
 * @param vt vehicle type
 * @param parent_group parent groupid
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateGroup(DoCommandFlags flags, VehicleType vt, GroupID parent_group)
{
	if (!IsCompanyBuildableVehicleType(vt)) return CMD_ERROR;

	if (!Group::CanAllocateItem()) return CMD_ERROR;

	const Group *pg = Group::GetIfValid(parent_group);
	if (pg != nullptr) {
		if (pg->owner != _current_company) return CMD_ERROR;
		if (pg->vehicle_type != vt) return CMD_ERROR;
	}

	CommandCost cost;

	if (flags.Test(DoCommandFlag::Execute)) {
		GroupChangeDeferredUpdateScope updater(vt);

		Group *g = Group::Create(_current_company, vt);

		Company *c = Company::Get(g->owner);
		g->number = c->freegroups.UseID(c->freegroups.NextID());
		if (pg == nullptr) {
			g->livery.colour1 = c->livery[LS_DEFAULT].colour1;
			g->livery.colour2 = c->livery[LS_DEFAULT].colour2;
			if (c->settings.renew_keep_length) g->flags.Set(GroupFlag::ReplaceWagonRemoval);
		} else {
			g->parent = pg->index;
			g->livery.colour1 = pg->livery.colour1;
			g->livery.colour2 = pg->livery.colour2;
			g->flags = pg->flags;
			if (vt == VEH_TRAIN) ReindexTemplateReplacementsForGroup(g->index);
		}

		cost.SetResultData(g->index);

		_group_change_deferred_updates.RegisterNewGroup();
		InvalidateWindowData(WC_COMPANY_COLOUR, g->owner, g->vehicle_type);
	}

	return cost;
}


/**
 * Add all vehicles in the given group to the default group and then deletes the group.
 * @param flags type of operation
 * @param group_id index of group
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteGroup(DoCommandFlags flags, GroupID group_id)
{
	Group *g = Group::GetIfValid(group_id);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	GroupChangeDeferredUpdateScope updater(g->vehicle_type);

	/* Remove all vehicles from the group */
	Command<CMD_REMOVE_ALL_VEHICLES_GROUP>::Do(flags, group_id);

	/* Delete sub-groups */
	for (const Group *gp : Group::Iterate()) {
		if (gp->parent == g->index) {
			Command<CMD_DELETE_GROUP>::Do(flags, gp->index);
		}
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Update backupped orders if needed */
		OrderBackup::ClearGroup(g->index);

		if (g->owner < MAX_COMPANIES) {
			Company *c = Company::Get(g->owner);

			/* If we set an autoreplace for the group we delete, remove it. */
			for (const EngineRenew *er : EngineRenew::Iterate()) {
				if (er->group_id == g->index) RemoveEngineReplacementForCompany(c, er->from, g->index, flags);
			}

			c->freegroups.ReleaseID(g->number);
		}

		VehicleType vt = g->vehicle_type;

		/* Delete all template replacements using the just deleted group */
		RemoveTemplateReplacementsFromGroupToBeDeleted(g);

		/* notify tracerestrict that group is about to be deleted */
		TraceRestrictRemoveGroupID(g->index);

		/* Delete the Replace Vehicle Windows */
		CloseWindowById(WC_REPLACE_VEHICLE, g->vehicle_type);
		delete g;

		_group_change_deferred_updates.RegisterChangedGroup();
		InvalidateWindowData(WC_COMPANY_COLOUR, _current_company, vt);
	}

	return CommandCost();
}


/**
 * Alter a group
 * @param flags type of operation
 * @param mode Operation to perform.
 * @param group_id GroupID
 * @param parent_id parent group index
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdAlterGroup(DoCommandFlags flags, AlterGroupMode mode, GroupID group_id, GroupID parent_id, const std::string &text)
{
	Group *g = Group::GetIfValid(group_id);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (mode == AlterGroupMode::Rename) {
		/* Rename group */
		bool reset = text.empty();

		if (!reset) {
			if (Utf8StringLength(text) >= MAX_LENGTH_GROUP_NAME_CHARS) return CMD_ERROR;
		}

		if (flags.Test(DoCommandFlag::Execute)) {
			/* Assign the new one */
			if (reset) {
				g->name.clear();
			} else {
				g->name = text;
			}
		}
	} else if (mode == AlterGroupMode::SetParent) {
		/* Set group parent */
		const Group *pg = Group::GetIfValid(parent_id);

		if (pg != nullptr) {
			if (pg->owner != _current_company) return CMD_ERROR;
			if (pg->vehicle_type != g->vehicle_type) return CMD_ERROR;

			/* Ensure request parent isn't child of group.
			 * This is the only place that infinite loops are prevented. */
			if (GroupIsInGroup(pg->index, g->index)) return CommandCost(STR_ERROR_GROUP_CAN_T_SET_PARENT_RECURSION);
		}

		if (flags.Test(DoCommandFlag::Execute)) {
			g->parent = (pg == nullptr) ? GroupID::Invalid() : pg->index;
			GroupStatistics::UpdateAutoreplace(g->owner);
			if (g->vehicle_type == VEH_TRAIN) ReindexTemplateReplacementsForGroup(g->index);

			if (!g->livery.in_use.All({Livery::Flag::Primary, Livery::Flag::Secondary})) {
				/* Update livery with new parent's colours if either colour is default. */
				const Livery *livery = GetParentLivery(g);

				bool colour_changed = false;
				auto update_colour = [&colour_changed](Colours &group_colour, Colours new_parent_colour) {
					if (group_colour != new_parent_colour) {
						group_colour = new_parent_colour;
						colour_changed = true;
					}
				};
				if (!g->livery.in_use.Test(Livery::Flag::Primary)) update_colour(g->livery.colour1, livery->colour1);
				if (!g->livery.in_use.Test(Livery::Flag::Secondary)) update_colour(g->livery.colour2, livery->colour2);

				if (colour_changed) {
					PropagateChildLivery(g, true);
					MarkWholeScreenDirty();
				}
			}
		}
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		InvalidateWindowData(WC_REPLACE_VEHICLE, g->vehicle_type, 1);
		InvalidateCurrentCompanyGroupWindows(g->vehicle_type, false);
		SetWindowWidgetDirty(WC_TRACE_RESTRICT_SLOTS, VehicleListIdentifier(VL_SLOT_LIST, g->vehicle_type, _current_company).ToWindowNumber(), 0); // Vehicle panel
		InvalidateWindowData(WC_COMPANY_COLOUR, g->owner, g->vehicle_type);
		BulkUpdateVehicleWindowsOnGroupChange();
	}

	return CommandCost();
}

static void AddVehicleToGroup(Vehicle *v, GroupID new_g);

/**
 * Create a new vehicle group.
 * @param flags type of operation
 * @param vli packed VehicleListIdentifier
 * @param cargo Cargo filter
 * @param name the new name or an empty string when setting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateGroupFromList(DoCommandFlags flags, VehicleListIdentifier vli, CargoType cargo, const std::string &name)
{
	VehicleList list;
	if (!IsCompanyBuildableVehicleType(vli.vtype)) return CMD_ERROR;
	if (!GenerateVehicleSortList(&list, vli, cargo)) return CMD_ERROR;

	GroupChangeDeferredUpdateScope updater(vli.vtype);

	CommandCost ret = Command<CMD_CREATE_GROUP>::Do(flags, vli.vtype, GroupID::Invalid());
	if (ret.Failed()) return ret;

	if (!name.empty()) {
		if (Utf8StringLength(name) >= MAX_LENGTH_GROUP_NAME_CHARS) return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		auto group_id = ret.GetResultData<GroupID>();
		if (!group_id.has_value()) return CMD_ERROR;
		Group *g = Group::GetIfValid(*group_id);
		if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

		if (!name.empty()) {
			g->name = name;
		}

		for (const Vehicle *v : list) {
			/* Just try and don't care if some vehicles can't be added. */
			if (v->owner == _current_company && v->IsPrimaryVehicle()) {
				AddVehicleToGroup(const_cast<Vehicle *>(v), g->index);
			}
		}
	}

	return CommandCost();
}

/**
 * Do add a vehicle to a group.
 * @param v Vehicle to add.
 * @param new_g Group to add to.
 */
static void AddVehicleToGroup(Vehicle *v, GroupID new_g)
{
	GroupStatistics::CountVehicle(v, -1);

	switch (v->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:
			SetTrainGroupID(Train::From(v), new_g);
			break;

		case VEH_ROAD:
		case VEH_SHIP:
		case VEH_AIRCRAFT:
			if (v->IsEngineCountable()) UpdateNumEngineGroup(v, v->group_id, new_g);
			v->group_id = new_g;
			for (Vehicle *u = v; u != nullptr; u = u->Next()) {
				u->colourmap = PAL_NONE;
				u->InvalidateNewGRFCache();
				u->InvalidateImageCache();
				u->UpdateViewport(true);
			}
			break;
	}

	SetWindowDirty(WC_VEHICLE_DEPOT, v->tile.base());
	_group_change_deferred_updates.AddVehicle(v);

	GroupStatistics::CountVehicle(v, 1);
}

/**
 * Add a vehicle to a group
 * @param flags type of operation
 * @param group_id index of group
 * @param veh_id vehicle to add to a group
 * @param add_shared Add shared vehicles as well.
 * @return the cost of this operation or an error
 */
CommandCost CmdAddVehicleGroup(DoCommandFlags flags, GroupID group_id, VehicleID veh_id, bool add_shared)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	GroupID new_g = group_id;

	if (v == nullptr || (!Group::IsValidID(new_g) && !IsDefaultGroupID(new_g) && new_g != NEW_GROUP)) return CMD_ERROR;

	if (Group::IsValidID(new_g)) {
		const Group *g = Group::Get(new_g);
		if (g->owner != _current_company || g->vehicle_type != v->type) return CMD_ERROR;
	}

	if (v->owner != _current_company || !v->IsPrimaryVehicle()) return CMD_ERROR;

	if (new_g == v->group_id) {
		return CommandCost();
	}

	CommandCost ret;
	if (new_g == NEW_GROUP) {
		/* Create new group. */
		ret = CmdCreateGroup(flags, v->type, GroupID::Invalid());
		if (ret.Failed()) return ret;
		auto group_id = ret.GetResultData<GroupID>();
		if (group_id.has_value()) {
			new_g = *group_id;
		} else if (flags.Test(DoCommandFlag::Execute)) {
			return CMD_ERROR;
		}
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		GroupChangeDeferredUpdateScope updater(v->type);

		AddVehicleToGroup(v, new_g);

		if (add_shared) {
			/* Add vehicles in the shared order list as well. */
			for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
				if (v2->group_id != new_g) AddVehicleToGroup(v2, new_g);
			}
		}
	}

	return ret;
}

static TownID GetTownFromDestination(const DestinationID destination)
{
	const BaseStation *st = BaseStation::GetIfValid(destination.ToStationID());
	if (st != nullptr) {
		return st->town->index;
	}

	return TownID::Invalid();
}

static std::pair<TownID, TownID> GetAutoGroupMostRelevantTowns(const Vehicle *vehicle)
{
	TownID first = TownID::Invalid();
	TownID last = TownID::Invalid();
	robin_hood::unordered_flat_set<TownID> seen_towns;

	for (const Order *order : vehicle->Orders()) {
		if (order->GetType() != OT_GOTO_STATION) continue;

		const DestinationID dest = order->GetDestination();
		TownID town = GetTownFromDestination(dest);

		if (town != TownID::Invalid() && seen_towns.insert(town).second) {
			/* Town not seen before and now inserted into seen_towns. */
			if (first == TownID::Invalid()) first = town;
			last = town;
		}
	}

	return std::make_pair(first, last);
}

static CargoTypes GetVehicleCargoList(const Vehicle *vehicle)
{
	CargoTypes cargoes = 0;

	for (const Vehicle *u = vehicle; u != nullptr; u = u->Next()) {
		if (u->cargo_cap == 0) continue;

		SetBit(cargoes, u->cargo_type);
	}
	return cargoes;
}

std::string GenerateAutoNameForVehicleGroup(const Vehicle *v)
{
	auto [town_from, town_to] = GetAutoGroupMostRelevantTowns(v);
	if (town_from == TownID::Invalid()) return "";

	CargoTypes cargoes = GetVehicleCargoList(v);

	if (town_from == town_to) {
		return GetString(STR_VEHICLE_AUTO_GROUP_LOCAL_ROUTE, town_from, (cargoes != 0) ? STR_VEHICLE_AUTO_GROUP_CARGO_LIST : STR_EMPTY, cargoes);
	} else {
		return GetString(STR_VEHICLE_AUTO_GROUP_ROUTE, town_from, town_to, (cargoes != 0) ? STR_VEHICLE_AUTO_GROUP_CARGO_LIST : STR_EMPTY, cargoes);
	}
}

/**
 * Add all shared vehicles of all vehicles from a group
 * @param flags type of operation
 * @param id_g index of group
 * @param type type of vehicles
 * @return the cost of this operation or an error
 */
CommandCost CmdAddSharedVehicleGroup(DoCommandFlags flags, GroupID id_g, VehicleType type)
{
	if (!Group::IsValidID(id_g) || !IsCompanyBuildableVehicleType(type)) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		GroupChangeDeferredUpdateScope updater(type);
		/* Find the first front engine which belong to the group id_g
		 * then add all shared vehicles of this front engine to the group id_g */
		for (const Vehicle *v : Vehicle::IterateTypeFrontOnly(type)) {
			if (v->IsPrimaryVehicle()) {
				if (v->group_id != id_g) continue;

				/* For each shared vehicles add it to the group */
				for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
					if (v2->group_id != id_g) Command<CMD_ADD_VEHICLE_GROUP>::Do(flags, id_g, v2->index, false);
				}
			}
		}
	}

	return CommandCost();
}


/**
 * Remove all vehicles from a group
 * @param flags type of operation
 * @param group_id index of group
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveAllVehiclesGroup(DoCommandFlags flags, GroupID group_id)
{
	const Group *g = Group::GetIfValid(group_id);

	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		GroupChangeDeferredUpdateScope updater(g->vehicle_type);

		/* Find each Vehicle that belongs to the group old_g and add it to the default group */
		for (Vehicle *v : Vehicle::IterateTypeFrontOnly(g->vehicle_type)) {
			if (v->IsPrimaryVehicle()) {
				if (v->group_id != group_id) continue;

				/* Add The Vehicle to the default group */
				AddVehicleToGroup(v, DEFAULT_GROUP);
			}
		}
	}

	return CommandCost();
}

/**
 * Set the livery for a vehicle group.
 * @param flags     Command flags.
 * @param group_id Group ID.
 * @param primary Set primary instead of secondary colour
 * @param colour Colour.
 */
CommandCost CmdSetGroupLivery(DoCommandFlags flags, GroupID group_id, bool primary, Colours colour)
{
	Group *g = Group::GetIfValid(group_id);

	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (colour >= COLOUR_END && colour != INVALID_COLOUR) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		if (primary) {
			g->livery.in_use.Set(Livery::Flag::Primary, colour != INVALID_COLOUR);
			if (colour == INVALID_COLOUR) colour = GetParentLivery(g)->colour1;
			g->livery.colour1 = colour;
		} else {
			g->livery.in_use.Set(Livery::Flag::Secondary, colour != INVALID_COLOUR);
			if (colour == INVALID_COLOUR) colour = GetParentLivery(g)->colour2;
			g->livery.colour2 = colour;
		}

		PropagateChildLivery(g, true);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}

/**
 * Set group flag for a group and its sub-groups.
 * @param g initial group.
 * @param set 1 to set or 0 to clear protection.
 */
static void SetGroupFlag(Group *g, GroupFlag flag, bool set, bool children)
{
	if (set) {
		g->flags.Set(flag);
	} else {
		g->flags.Reset(flag);
	}

	if (!children) return;

	IterateDescendantsOfGroup(g, [&](Group *pg) {
		SetGroupFlag(pg, flag, set, false);
	});
}

/**
 * (Un)set group flag from a group
 * @param flags type of operation
 * @param group_id index of group array
 * @param flag flag to set, by value not bit.
 * @param value value to set the flag to.
 * @param recursive to apply to sub-groups.
 * @return the cost of this operation or an error
 */
CommandCost CmdSetGroupFlag(DoCommandFlags flags, GroupID group_id, GroupFlag flag, bool value, bool recursive)
{
	Group *g = Group::GetIfValid(group_id);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (flag != GroupFlag::ReplaceProtection && flag != GroupFlag::ReplaceWagonRemoval) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		SetGroupFlag(g, flag, value, recursive);

		SetWindowDirty(GetWindowClassForVehicleType(g->vehicle_type), VehicleListIdentifier(VL_GROUP_LIST, g->vehicle_type, _current_company).ToWindowNumber());
		InvalidateWindowData(WC_REPLACE_VEHICLE, g->vehicle_type);
	}

	return CommandCost();
}

/**
 * Affect the groupID of a train to new_g.
 * @note called in CmdAddVehicleGroup and CmdMoveRailVehicle
 * @param v     First vehicle of the chain.
 * @param new_g index of array group
 */
void SetTrainGroupID(Train *v, GroupID new_g)
{
	if (!Group::IsValidID(new_g) && !IsDefaultGroupID(new_g)) return;

	assert(v->IsFrontEngine() || IsDefaultGroupID(new_g));

	for (Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->IsEngineCountable()) UpdateNumEngineGroup(u, u->group_id, new_g);

		u->group_id = new_g;
		u->colourmap = PAL_NONE;
		u->InvalidateNewGRFCache();
		u->InvalidateImageCache();
		u->UpdateViewport(true);
	}

	if (_group_change_deferred_updates.refcount != 0) return;

	/* Update the Replace Vehicle Windows */
	GroupStatistics::UpdateAutoreplace(v->owner);
	SetWindowDirty(WC_REPLACE_VEHICLE, VEH_TRAIN);
}


/**
 * Recalculates the groupID of a train. Should be called each time a vehicle is added
 * to/removed from the chain,.
 * @note this needs to be called too for 'wagon chains' (in the depot, without an engine)
 * @note Called in CmdBuildRailVehicle, CmdBuildRailWagon, CmdMoveRailVehicle, CmdSellRailWagon
 * @param v First vehicle of the chain.
 */
void UpdateTrainGroupID(Train *v)
{
	assert(v->IsFrontEngine() || v->IsFreeWagon());

	GroupID new_g = v->IsFrontEngine() ? v->group_id : (GroupID)DEFAULT_GROUP;
	for (Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->IsEngineCountable()) UpdateNumEngineGroup(u, u->group_id, new_g);

		u->group_id = new_g;
		u->colourmap = PAL_NONE;
		u->InvalidateNewGRFCache();
		u->InvalidateImageCache();
	}

	/* Update the Replace Vehicle Windows */
	GroupStatistics::UpdateAutoreplace(v->owner);
	SetWindowDirty(WC_REPLACE_VEHICLE, VEH_TRAIN);
}

/**
 * Get the number of engines with EngineID id_e in the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param id_e The EngineID of the engine to count
 * @return The number of engines with EngineID id_e in the group
 */
uint GetGroupNumEngines(CompanyID company, GroupID id_g, EngineID id_e)
{
	uint count = 0;
	const Engine *e = Engine::Get(id_e);
	IterateDescendantsOfGroup(id_g, [&](const Group *g) {
		count += GroupStatistics::Get(company, g->index, e->type).GetNumEngines(id_e);
	});
	return count + GroupStatistics::Get(company, id_g, e->type).GetNumEngines(id_e);
}

/**
 * Get the number of vehicles in the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param type The vehicle type of the group
 * @return The number of vehicles in the group
 */
uint GetGroupNumVehicle(CompanyID company, GroupID id_g, VehicleType type)
{
	uint count = 0;
	IterateDescendantsOfGroup(id_g, [&](const Group *g) {
		count += GroupStatistics::Get(company, g->index, type).num_vehicle;
	});
	return count + GroupStatistics::Get(company, id_g, type).num_vehicle;
}

/**
 * Get the number of vehicles above profit minimum age in the group with GroupID
 * id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param type The vehicle type of the group
 * @return The number of vehicles above profit minimum age in the group
 */
uint GetGroupNumVehicleMinAge(CompanyID company, GroupID id_g, VehicleType type)
{
	uint count = 0;
	IterateDescendantsOfGroup(id_g, [&](const Group *g) {
		count += GroupStatistics::Get(company, g->index, type).num_vehicle_min_age;
	});
	return count + GroupStatistics::Get(company, id_g, type).num_vehicle_min_age;
}

/**
 * Get last year's profit of vehicles above minimum age
 * for the group with GroupID id_g and its sub-groups.
 * @param company The company the group belongs to
 * @param id_g The GroupID of the group used
 * @param type The vehicle type of the group
 * @return Last year's profit of vehicles above minimum age for the group
 */
Money GetGroupProfitLastYearMinAge(CompanyID company, GroupID id_g, VehicleType type)
{
	Money sum = 0;
	IterateDescendantsOfGroup(id_g, [&](const Group *g) {
		sum += GroupStatistics::Get(company, g->index, type).profit_last_year_min_age;
	});
	return sum + GroupStatistics::Get(company, id_g, type).profit_last_year_min_age;
}

void RemoveAllGroupsForCompany(const CompanyID company)
{
	ReindexTemplateReplacementsRecursiveGuard guard;

	for (Group *g : Group::Iterate()) {
		if (company == g->owner) {
			RemoveTemplateReplacementsFromGroupToBeDeleted(g);
			delete g;
		}
	}
}


/**
 * Test if GroupID group is a descendant of (or is) GroupID search
 * @param search The GroupID to search in
 * @param group The GroupID to search for
 * @return True iff group is search or a descendant of search
 */
bool GroupIsInGroup(GroupID search, GroupID group)
{
	if (!Group::IsValidID(search)) return search == group;

	do {
		if (search == group) return true;
		search = Group::Get(search)->parent;
	} while (search != GroupID::Invalid());

	return false;
}
