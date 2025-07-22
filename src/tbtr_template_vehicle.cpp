/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_vehicle.cpp Template-based train replacement: template vehicle. */

#include "stdafx.h"
#include "articulated_vehicles.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "command_func.h"
#include "company_func.h"
#include "core/pool_func.hpp"
#include "core/pool_type.hpp"
#include "core/random_func.hpp"
#include "engine_func.h"
#include "engine_type.h"
#include "group.h"
#include "group_type.h"
#include "newgrf_cargo.h"
#include "newgrf_engine.h"
#include "newgrf.h"
#include "newgrf_spritegroup.h"
#include "table/strings.h"
#include "table/train_cmd.h"
#include "tbtr_template_vehicle_func.h"
#include "tbtr_template_vehicle.h"
#include "train.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "vehicle_type.h"

#include "3rdparty/robin_hood/robin_hood.h"
#include "3rdparty/svector/svector.h"

#include <functional>

#include "safeguards.h"

TemplatePool _template_pool("TemplatePool");
INSTANTIATE_POOL_METHODS(Template)

robin_hood::unordered_flat_map<GroupID, TemplateID> _template_replacements;
robin_hood::unordered_flat_map<GroupID, TemplateID> _template_replacement_index_recursive;
static ankerl::svector<GroupID, 16> _template_replacement_pending_group_updates;
static constexpr uint32_t INDEX_RECURSIVE_GUARD_REINDEX_PENDING = 0x80000000;
static uint32_t _template_replacement_index_recursive_guard = 0;

static void MarkTrainsInGroupAsPendingTemplateReplacement(GroupID gid, const TemplateVehicle *tv);

void TemplateVehicleImageDimensions::SetFromTrain(const Train *t)
{
	this->reference_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;
	this->vehicle_pitch = 0;
	this->cached_veh_length = t->gcache.cached_veh_length;

	const Engine *e = t->GetEngine();
	if (e->GetGRF() != nullptr && is_custom_sprite(e->u.rail.image_index)) {
		this->reference_width = e->GetGRF()->traininfo_vehicle_width;
		this->vehicle_pitch = e->GetGRF()->traininfo_vehicle_pitch;
	}
	if (t->gcache.cached_veh_length != 8 && HasBit(t->flags, VRF_REVERSE_DIRECTION) && !EngInfo(t->engine_type)->misc_flags.Test(EngineMiscFlag::RailFlips)) {
		this->vehicle_flip_length = t->gcache.cached_veh_length;
	} else {
		this->vehicle_flip_length = -1;
	}
}

TemplateVehicle::TemplateVehicle(VehicleType type, EngineID eid, Owner current_owner)
{
	this->type = type;
	this->engine_type = eid;

	this->reuse_depot_vehicles = false;
	this->keep_remaining_vehicles = false;
	this->refit_as_template = true;
	this->replace_old_only = false;

	this->first = this;
	this->next = 0x0;
	this->previous = 0x0;

	this->sprite_seq.Set(SPR_IMG_QUERY);

	this->owner = current_owner;

	this->real_consist_length = 0;
	this->ctrl_flags = 0;
}

TemplateVehicle::~TemplateVehicle()
{
	TemplateVehicle *v = this->Next();
	this->SetNext(nullptr);

	delete v;
}

/** getting */
void TemplateVehicle::SetNext(TemplateVehicle *v) { this->next = v; }
void TemplateVehicle::SetPrev(TemplateVehicle *v) { this->previous = v; }
void TemplateVehicle::SetFirst(TemplateVehicle *v) { this->first = v; }

TemplateVehicle *TemplateVehicle::GetNextUnit() const
{
		TemplateVehicle *tv = this->Next();
		while (tv != nullptr && HasBit(tv->subtype, GVSF_ARTICULATED_PART)) {
			tv = tv->Next();
		}
		if (tv != nullptr && HasBit(tv->subtype, GVSF_MULTIHEADED) && !HasBit(tv->subtype, GVSF_ENGINE)) {
			tv = tv->Next();
		}
		return tv;
}

TemplateVehicle *TemplateVehicle::GetPrevUnit()
{
	TemplateVehicle *tv = this->Prev();
	while (tv && HasBit(tv->subtype, GVSF_ARTICULATED_PART|GVSF_ENGINE)) {
		tv = tv->Prev();
	}
	if (tv && HasBit(tv->subtype, GVSF_MULTIHEADED|GVSF_ENGINE)) tv = tv->Prev();
	return tv;
}

void ClearTemplateReplacements()
{
	_template_replacements.clear();
	_template_replacement_index_recursive.clear();
}

bool ShouldServiceTrainForTemplateReplacement(const Train *t, const TemplateVehicle *tv)
{
	const Company *c = Company::Get(t->owner);
	if (tv->IsReplaceOldOnly() && !t->NeedsAutorenewing(c, false)) return false;
	Money needed_money = c->settings.engine_renew_money;
	if (needed_money > GetAvailableMoney(c->index)) return false;
	TBTRDiffFlags diff = TrainTemplateDifference(t, tv);
	if (diff & TBTRDF_CONSIST) {
		if (_settings_game.difficulty.infinite_money) return true;
		/* Check money.
		 * We want 2*(the price of the whole template) without looking at the value of the vehicle(s) we are going to sell, or not need to buy. */
		for (const TemplateVehicle *tv_unit = tv; tv_unit != nullptr; tv_unit = tv_unit->GetNextUnit()) {
			if (!Engine::Get(tv->engine_type)->company_avail.Test(t->owner)) return false;
			needed_money += 2 * Engine::Get(tv->engine_type)->GetCost();
		}
		return needed_money <= c->money;
	} else {
		return diff != TBTRDF_NONE;
	}
}

static void MarkTrainsInGroupAsPendingTemplateReplacement(GroupID gid, const TemplateVehicle *tv)
{
	if (_template_replacement_index_recursive_guard != 0) return;

	std::vector<GroupID> groups;
	groups.push_back(gid);

	Owner owner = Group::Get(gid)->owner;

	for (const Group *group : Group::Iterate()) {
		if (group->vehicle_type != VEH_TRAIN || group->owner != owner || group->index == gid) continue;

		auto is_descendant = [gid](const Group *g) -> bool {
			while (true) {
				if (g->parent == GroupID::Invalid()) return false;
				if (g->parent == gid) {
					/* If this group has its own template defined, it's not a descendant for template inheriting purposes */
					if (_template_replacements.find(g->index) != _template_replacements.end()) return false;
					return true;
				}
				g = Group::Get(g->parent);
			}

			NOT_REACHED();
		};
		if (is_descendant(group)) {
			groups.push_back(group->index);
		}
	}

	std::sort(groups.begin(), groups.end());

	for (Train *t : Train::IterateFrontOnly()) {
		if (!t->IsFrontEngine() || t->owner != owner || t->group_id >= NEW_GROUP) continue;

		if (std::binary_search(groups.begin(), groups.end(), t->group_id)) {
			AssignBit(t->vehicle_flags, VF_REPLACEMENT_PENDING, tv != nullptr && ShouldServiceTrainForTemplateReplacement(t, tv));
		}
	}
}

void MarkTrainsUsingTemplateAsPendingTemplateReplacement(const TemplateVehicle *tv)
{
	Owner owner = tv->owner;

	for (Train *t : Train::IterateFrontOnly()) {
		if (!t->IsFrontEngine() || t->owner != owner || t->group_id >= NEW_GROUP) continue;

		if (GetTemplateIDByGroupIDRecursive(t->group_id) == tv->index) {
			AssignBit(t->vehicle_flags, VF_REPLACEMENT_PENDING, ShouldServiceTrainForTemplateReplacement(t, tv));
		}
	}
}

TemplateID GetTemplateIDByGroupID(GroupID gid)
{
	auto iter = _template_replacements.find(gid);
	if (iter == _template_replacements.end()) return INVALID_TEMPLATE;
	return iter->second;
}

TemplateID GetTemplateIDByGroupIDRecursive(GroupID gid)
{
	auto iter = _template_replacement_index_recursive.find(gid);
	if (iter == _template_replacement_index_recursive.end()) return INVALID_TEMPLATE;
	return iter->second;
}

void RemoveTemplateReplacement(GroupID gid)
{
	if (_template_replacements.erase(gid) > 0) {
		ReindexTemplateReplacementsForGroup(gid);
	}
}

void IssueTemplateReplacement(GroupID gid, TemplateID tid)
{
	_template_replacements[gid] = tid;
	ReindexTemplateReplacements();
	MarkTrainsInGroupAsPendingTemplateReplacement(gid, TemplateVehicle::Get(tid));
}

void RemoveTemplateReplacementsReferencingTemplate(TemplateID tid)
{
	bool update = false;
	for (auto it = _template_replacements.begin(); it != _template_replacements.end();) {
		if (it->second == tid) {
			_template_replacement_pending_group_updates.push_back(it->first);
			it = _template_replacements.erase(it);
			update = true;
		} else {
			++it;
		}
	}
	if (update) ReindexTemplateReplacements();
}

uint TemplateVehicle::NumGroupsUsingTemplate() const
{
	uint amount = 0;
	for (const auto &it : _template_replacements) {
		if (it.second == this->index) {
			amount++;
		}
	}
	return amount;
}

/*
 * A group which is about to be deleted is empty and has no children,
 * so no additional work to update child groups or vehicles is required.
 */
void RemoveTemplateReplacementsFromGroupToBeDeleted(const Group *g)
{
	if (g->vehicle_type != VEH_TRAIN) return;

	_template_replacements.erase(g->index);
	_template_replacement_index_recursive.erase(g->index);
}

void ReindexTemplateReplacementsForGroup(GroupID gid)
{
	_template_replacement_pending_group_updates.push_back(gid);
	ReindexTemplateReplacements();
}

void ReindexTemplateReplacements()
{
	if (_template_replacement_index_recursive_guard != 0) {
		/* Perform the reindex later when the refcount falls to zero */
		_template_replacement_index_recursive_guard |= INDEX_RECURSIVE_GUARD_REINDEX_PENDING;
		return;
	}

	_template_replacement_index_recursive.clear();
	for (const Group *group : Group::Iterate()) {
		if (group->vehicle_type != VEH_TRAIN) continue;

		const Group *g = group;
		while (true) {
			auto iter = _template_replacements.find(g->index);
			if (iter != _template_replacements.end()) {
				_template_replacement_index_recursive[group->index] = iter->second;
				break;
			}
			if (g->parent == GroupID::Invalid()) break;
			g = Group::Get(g->parent);
		}
	}

	for (GroupID gid : _template_replacement_pending_group_updates) {
		MarkTrainsInGroupAsPendingTemplateReplacement(gid, TemplateVehicle::GetIfValid(GetTemplateIDByGroupIDRecursive(gid)));
	}
	_template_replacement_pending_group_updates.clear();
	_template_replacement_pending_group_updates.shrink_to_fit();
}

ReindexTemplateReplacementsRecursiveGuard::ReindexTemplateReplacementsRecursiveGuard()
{
	_template_replacement_index_recursive_guard++;
}

ReindexTemplateReplacementsRecursiveGuard::~ReindexTemplateReplacementsRecursiveGuard()
{
	_template_replacement_index_recursive_guard--;
	if (_template_replacement_index_recursive_guard == INDEX_RECURSIVE_GUARD_REINDEX_PENDING) {
		/* The refcount is now 0 | the reindex pending bit, clear the bit and do the reindex. */
		_template_replacement_index_recursive_guard = 0;
		ReindexTemplateReplacements();
	}
}

std::string ValidateTemplateReplacementCaches()
{
	assert(_template_replacement_index_recursive_guard == 0);

	robin_hood::unordered_flat_map<GroupID, TemplateID> saved_template_replacement_index_recursive = std::move(_template_replacement_index_recursive);

	ReindexTemplateReplacements();

	bool match_recursive = (saved_template_replacement_index_recursive == _template_replacement_index_recursive);
	_template_replacement_index_recursive = std::move(saved_template_replacement_index_recursive);

	if (!match_recursive) return "Recursive index cache does not match";

	return "";
}
