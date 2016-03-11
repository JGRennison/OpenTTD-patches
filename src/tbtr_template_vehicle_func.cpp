/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_vehicle_func.cpp Template-based train replacement: template vehicle functions. */

#include "stdafx.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "window_func.h"
#include "command_func.h"
#include "vehicle_gui.h"
#include "train.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "core/geometry_type.hpp"
#include "debug.h"
#include "zoom_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "cargoaction.h"
#include "train.h"
#include "company_func.h"
#include "newgrf.h"
#include "spritecache.h"
#include "articulated_vehicles.h"
#include "autoreplace_func.h"

#include "depot_base.h"

#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_func.h"

#include <map>
#include <stdio.h>

#include "safeguards.h"

Vehicle *vhead, *vtmp;
static const uint MAX_ARTICULATED_PARTS = 100;

#ifdef _DEBUG
// debugging printing functions for convenience, usually called from gdb
void tbtr_debug_pat()
{
	TemplateVehicle *tv;
	FOR_ALL_TEMPLATES(tv) {
		if (tv->Prev()) continue;
		tbtr_debug_ptv(tv);
		printf("__________\n");
	}
}

void tbtr_debug_pav()
{
	Train *t;
	FOR_ALL_TRAINS(t) {
		if (t->Previous()) continue;
		tbtr_debug_pvt(t);
		printf("__________\n");
	}
}

void tbtr_debug_ptv(TemplateVehicle* tv)
{
	if (!tv) return;
	while (tv->Next() ) {
		printf("eid:%3d  st:%2d  tv:%p  next:%p  cargo: %d  cargo_sub: %d\n", tv->engine_type, tv->subtype, tv, tv->Next(), tv->cargo_type, tv->cargo_subtype);
		tv = tv->Next();
	}
	printf("eid:%3d  st:%2d  tv:%p  next:%p  cargo: %d  cargo_sub: %d\n", tv->engine_type, tv->subtype, tv, tv->Next(),  tv->cargo_type, tv->cargo_subtype);
}

void tbtr_debug_pvt (const Train *printme)
{
	for (const Train *tmp = printme; tmp; tmp = tmp->Next()) {
		if (tmp->index <= 0) {
			printf("train has weird index: %d %d %p\n", tmp->index, tmp->engine_type, tmp);
			return;
		}
		printf("eid:%3d  index:%2d  subtype:%2d  vehstat: %d  cargo_t: %d   cargo_sub: %d  ref:%p\n", tmp->engine_type, tmp->index, tmp->subtype, tmp->vehstatus, tmp->cargo_type, tmp->cargo_subtype, tmp);
	}
}
#endif

void BuildTemplateGuiList(GUITemplateList *list, Scrollbar *vscroll, Owner oid, RailType railtype)
{
	list->Clear();
	const TemplateVehicle *tv;

	FOR_ALL_TEMPLATES(tv) {
		if (tv->owner == oid && (tv->IsPrimaryVehicle() || tv->IsFreeWagonChain()) && TemplateVehicleContainsEngineOfRailtype(tv, railtype)) {
			*list->Append() = tv;
		}
	}

	list->RebuildDone();
	if (vscroll) vscroll->SetCount(list->Length());
}

Money CalculateOverallTemplateCost(const TemplateVehicle *tv)
{
	Money val = 0;

	for (; tv; tv = tv->Next()) {
		val += (Engine::Get(tv->engine_type))->GetCost();
	}
	return val;
}

void DrawTemplate(const TemplateVehicle *tv, int left, int right, int y)
{
	if (!tv) return;

	DrawPixelInfo tmp_dpi, *old_dpi;
	int max_width = right - left + 1;
	int height = ScaleGUITrad(14);
	if (!FillDrawPixelInfo(&tmp_dpi, left, y, max_width, height)) return;

	old_dpi = _cur_dpi;
	_cur_dpi = &tmp_dpi;

	const TemplateVehicle *t = tv;
	int offset = 0;

	while (t) {
		PaletteID pal = GetEnginePalette(t->engine_type, _current_company);
		DrawSprite(t->cur_image, pal, offset + t->image_width / 2, ScaleGUITrad(11));

		offset += t->image_width;
		t = t->Next();
	}

	_cur_dpi = old_dpi;
}

// copy important stuff from the virtual vehicle to the template
inline void SetupTemplateVehicleFromVirtual(TemplateVehicle *tmp, TemplateVehicle *prev, Train *virt)
{
	if (prev) {
		prev->SetNext(tmp);
		tmp->SetPrev(prev);
		tmp->SetFirst(prev->First());
	}
	tmp->railtype = virt->railtype;
	tmp->owner = virt->owner;
	tmp->value = virt->value;

	// set the subtype but also clear the virtual flag while doing it
	tmp->subtype = virt->subtype & ~(1 << GVSF_VIRTUAL);
	// set the cargo type and capacity
	tmp->cargo_type = virt->cargo_type;
	tmp->cargo_subtype = virt->cargo_subtype;
	tmp->cargo_cap = virt->cargo_cap;

	const GroundVehicleCache *gcache = virt->GetGroundVehicleCache();
	tmp->max_speed = virt->GetDisplayMaxSpeed();
	tmp->power = gcache->cached_power;
	tmp->weight = gcache->cached_weight;
	tmp->max_te = gcache->cached_max_te / 1000;

	tmp->spritenum = virt->spritenum;
	tmp->cur_image = virt->GetImage(DIR_W, EIT_PURCHASE);
	Point *p = new Point();
	tmp->image_width = virt->GetDisplayImageWidth(p);
}

// create a full TemplateVehicle based train according to a virtual train
TemplateVehicle* TemplateVehicleFromVirtualTrain(Train *virt)
{
	if (!virt) return NULL;

	Train *init_virt = virt;

	int len = CountVehiclesInChain(virt);
	if (!TemplateVehicle::CanAllocateItem(len)) {
		return NULL;
	}

	TemplateVehicle *tmp;
	TemplateVehicle *prev = NULL;
	for (; virt; virt = virt->Next()) {
		tmp = new TemplateVehicle(virt->engine_type);
		SetupTemplateVehicleFromVirtual(tmp, prev, virt);
		prev = tmp;
	}

	tmp->First()->SetRealLength(CeilDiv(init_virt->gcache.cached_total_length * 10, TILE_SIZE));
	return tmp->First();
}

// return last in a chain (really last, so even a singular articulated part of a vehicle if the last one is artic)
inline TemplateVehicle* Last(TemplateVehicle *chain)
{
	if (!chain) return NULL;
	while (chain->Next()) {
		chain = chain->Next();
	}
	return chain;
}

inline Train* Last(Train *chain)
{
	if (!chain) return NULL;
	while (chain->GetNextUnit()) {
		chain = chain->GetNextUnit();
	}
	return chain;
}

// return: pointer to former vehicle
TemplateVehicle *DeleteTemplateVehicle(TemplateVehicle *todel)
{
	if (!todel) return NULL;
	TemplateVehicle *cur = todel;
	delete todel;
	return cur;
}

// forward declaration, defined in train_cmd.cpp
CommandCost CmdSellRailWagon(DoCommandFlag, Vehicle*, uint16, uint32);

Train* DeleteVirtualTrain(Train *chain, Train *to_del) {
	if (chain != to_del) {
		CmdSellRailWagon(DC_EXEC, to_del, 0, 0);
		return chain;
	}
	else {
		chain = chain->GetNextUnit();
		CmdSellRailWagon(DC_EXEC, to_del, 0, 0);
		return chain;
	}
}

// retrieve template vehicle from templatereplacement that belongs to the given group
TemplateVehicle* GetTemplateVehicleByGroupID(GroupID gid) {
	TemplateReplacement *tr;
	// first try to find a templatereplacement issued for the given groupid
	FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
		if (tr->Group() == gid) {
			return TemplateVehicle::GetIfValid(tr->Template()); // there can be only one
		}
	}
	// if that didn't work, try to find a templatereplacement for ALL_GROUP
	if (gid != ALL_GROUP) {
		FOR_ALL_TEMPLATE_REPLACEMENTS(tr) {
			if (tr->Group() == ALL_GROUP) {
				return TemplateVehicle::GetIfValid(tr->Template());
			}
		}
	}
	// if all failed, just return null
	return NULL;
}

/**
 * Check a template consist whether it contains any engine of the given railtype
 */
bool TemplateVehicleContainsEngineOfRailtype(const TemplateVehicle *tv, RailType type)
{
	/* For standard rail engines, allow only those */
	if (type == RAILTYPE_BEGIN || type == RAILTYPE_RAIL) {
		while (tv) {
			if (tv->railtype != type) {
				return false;
			}
			tv = tv->GetNextUnit();
		}
		return true;
	}
	/* For electrified rail engines, standard wagons or engines are allowed to be included */
	while (tv) {
		if (tv->railtype == type) {
			return true;
		}
		tv = tv->GetNextUnit();
	}
	return false;
}

//helper
bool ChainContainsVehicle(Train *chain, Train *mem)
{
	for (; chain; chain = chain->Next()) {
		if (chain == mem) {
			return true;
		}
	}
	return false;
}

// has O(n)
Train* ChainContainsEngine(EngineID eid, Train *chain) {
	for (; chain; chain=chain->GetNextUnit())
		if (chain->engine_type == eid)
			return chain;
	return NULL;
}

// has O(n^2)
Train* DepotContainsEngine(TileIndex tile, EngineID eid, Train *not_in = NULL)
{
	Train *t;
	FOR_ALL_TRAINS(t) {
		// conditions: v is stopped in the given depot, has the right engine and if 'not_in' is given v must not be contained within 'not_in'
		// if 'not_in' is NULL, no check is needed
		if (t->tile == tile
				// If the veh belongs to a chain, wagons will not return true on IsStoppedInDepot(), only primary vehicles will
				// in case of t not a primary veh, we demand it to be a free wagon to consider it for replacement
				&& ((t->IsPrimaryVehicle() && t->IsStoppedInDepot()) || t->IsFreeWagon())
				&& t->engine_type == eid
				&& (not_in == NULL || ChainContainsVehicle(not_in, t) == false)) {
			return t;
		}
	}
	return NULL;
}

void CopyStatus(Train *from, Train *to)
{
	DoCommand(to->tile, from->group_id, to->index, DC_EXEC, CMD_ADD_VEHICLE_GROUP);
	to->cargo_type = from->cargo_type;
	to->cargo_subtype = from->cargo_subtype;

	// swap names
	char *tmp = to->name;
	to->name = from->name;
	from->name = tmp;
}

void NeutralizeStatus(Train *t)
{
	DoCommand(t->tile, DEFAULT_GROUP, t->index, DC_EXEC, CMD_ADD_VEHICLE_GROUP);
	DoCommand(0, t->index | CO_UNSHARE << 30, 0, DC_EXEC, CMD_CLONE_ORDER);
	DoCommand(0, t->index, FreeUnitIDGenerator(VEH_TRAIN, t->owner).NextID(), DC_EXEC, CMD_SET_VEHICLE_UNIT_NUMBER);
	DoCommand(0, t->index, 0, DC_EXEC, CMD_RENAME_VEHICLE, NULL);
}

bool TrainMatchesTemplate(const Train *t, TemplateVehicle *tv) {
	while (t && tv) {
		if (t->engine_type != tv->engine_type) {
			return false;
		}
		t = t->GetNextUnit();
		tv = tv->GetNextUnit();
	}
	if ((t && !tv) || (!t && tv)) {
		return false;
	}
	return true;
}


bool TrainMatchesTemplateRefit(const Train *t, TemplateVehicle *tv)
{
	if (!tv->refit_as_template) {
		return true;
	}

	while (t && tv) {
		if (t->cargo_type != tv->cargo_type || t->cargo_subtype != tv->cargo_subtype) {
			return false;
		}
		t = t->GetNextUnit();
		tv = tv->GetNextUnit();
	}
	return true;
}

void BreakUpRemainders(Train *t)
{
	while (t) {
		Train *move;
		if (HasBit(t->subtype, GVSF_ENGINE)) {
			move = t;
			t = t->Next();
			DoCommand(move->tile, move->index, INVALID_VEHICLE, DC_EXEC, CMD_MOVE_RAIL_VEHICLE);
			NeutralizeStatus(move);
		} else {
			t = t->Next();
		}
	}
}

short CountEnginesInChain(Train *t)
{
	short count = 0;
	for (; t != NULL; t = t->GetNextUnit()) {
		if (HasBit(t->subtype, GVSF_ENGINE)) {
			count++;
		}
	}
	return count;
}

int countOccurrencesInTrain(Train *t, EngineID eid)
{
	int count = 0;
	Train *tmp = t;
	for (; tmp != NULL; tmp = tmp->GetNextUnit()) {
		if (tmp->engine_type == eid) {
			count++;
		}
	}
	return count;
}

int countOccurrencesInTemplateVehicle(TemplateVehicle *contain, EngineID eid)
{
	int count = 0;
	for (; contain; contain=contain->GetNextUnit()) {
		if (contain->engine_type == eid) {
			count++;
		}
	}
	return count;
}

int countOccurrencesInDepot(TileIndex tile, EngineID eid, Train *not_in = NULL)
{
	int count = 0;
	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		// conditions: v is stopped in the given depot, has the right engine and if 'not_in' is given v must not be contained within 'not_in'
		// if 'not_in' is NULL, no check is needed
		if (v->tile == tile && v->IsStoppedInDepot() && v->engine_type == eid &&
				(not_in == 0 || ChainContainsVehicle(not_in, (Train*)v) == false)) {
			count++;
		}
	}
	return count;
}

// basically does the same steps as CmdTemplateReplaceVehicle but without actually moving things around
CommandCost CalculateTemplateReplacementCost(Train *incoming)
{
	TileIndex tile = incoming->tile;
	TemplateVehicle *tv = GetTemplateVehicleByGroupID(incoming->group_id);
	CommandCost estimate(EXPENSES_NEW_VEHICLES);

	// count for each different eid in the incoming train
	std::map<EngineID, short> unique_eids;
	for (TemplateVehicle *tmp = tv; tmp != NULL; tmp = tmp->GetNextUnit()) {
		unique_eids[tmp->engine_type]++;
	}
	std::map<EngineID, short>::iterator it = unique_eids.begin();
	for (; it != unique_eids.end(); it++) {
		it->second -= countOccurrencesInTrain(incoming, it->first);
		it->second -= countOccurrencesInDepot(incoming->tile, it->first, incoming);
		if (it->second < 0) it->second = 0;
	}

	// get overall buying cost
	for (it = unique_eids.begin(); it != unique_eids.end(); it++) {
		for (int j = 0; j < it->second; j++) {
			estimate.AddCost(DoCommand(tile, it->first, 0, DC_NONE, CMD_BUILD_VEHICLE));
		}
	}

	return estimate;
}

// make sure the real train wagon has the right cargo
void CopyWagonStatus(TemplateVehicle *from, Train *to)
{
	to->cargo_type = from->cargo_type;
	to->cargo_subtype = from->cargo_subtype;
}

int NumTrainsNeedTemplateReplacement(GroupID g_id, TemplateVehicle *tv)
{
	int count = 0;
	if (!tv) return count;

	const Train *t;
	FOR_ALL_TRAINS(t) {
		if (t->IsPrimaryVehicle() && t->group_id == g_id && (!TrainMatchesTemplate(t, tv) || !TrainMatchesTemplateRefit(t, tv))) {
			count++;
		}
	}
	return count;
}
// refit each vehicle in t as is in tv, assume t and tv contain the same types of vehicles
void CmdRefitTrainFromTemplate(Train *t, TemplateVehicle *tv, DoCommandFlag flags)
{
	while (t && tv) {
		// refit t as tv
		uint32 cb = GetCmdRefitVeh(t);

		DoCommand(t->tile, t->index, tv->cargo_type | tv->cargo_subtype << 8 | 1 << 16 | (1 << 5), flags, cb);

		// next
		t = t->GetNextUnit();
		tv = tv->GetNextUnit();
	}
}

/** using cmdtemplatereplacevehicle as test-function (i.e. with flag DC_NONE) is not a good idea as that function relies on
 *  actually moving vehicles around to work properly.
 *  We do this worst-cast test instead.
 */
CommandCost TestBuyAllTemplateVehiclesInChain(TemplateVehicle *tv, TileIndex tile)
{
	CommandCost cost(EXPENSES_NEW_VEHICLES);

	for (; tv; tv = tv->GetNextUnit()) {
		cost.AddCost(DoCommand(tile, tv->engine_type, 0, DC_NONE, CMD_BUILD_VEHICLE));
	}

	return cost;
}


/** Transfer as much cargo from a given (single train) vehicle onto a chain of vehicles.
 *  I.e., iterate over the chain from head to tail and use all available cargo capacity (w.r.t. cargo type of course)
 *  to store the cargo from the given single vehicle.
 *  @param old_veh:     ptr to the single vehicle, which's cargo shall be moved
 *  @param new_head:    ptr to the head of the chain, which shall obtain old_veh's cargo
 *  @return:            amount of moved cargo, TODO
 */
void TransferCargoForTrain(Train *old_veh, Train *new_head)
{
	assert(new_head->IsPrimaryVehicle());

	CargoID _cargo_type = old_veh->cargo_type;
	byte _cargo_subtype = old_veh->cargo_subtype;

	// how much cargo has to be moved (if possible)
	uint remainingAmount = old_veh->cargo.TotalCount();
	// each vehicle in the new chain shall be given as much of the old cargo as possible, until none is left
	for (Train *tmp = new_head; tmp != NULL && remainingAmount > 0; tmp = tmp->GetNextUnit()) {
		if (tmp->cargo_type == _cargo_type && tmp->cargo_subtype == _cargo_subtype) {
			// calculate the free space for new cargo on the current vehicle
			uint curCap = tmp->cargo_cap - tmp->cargo.TotalCount();
			uint moveAmount = min(remainingAmount, curCap);
			// move (parts of) the old vehicle's cargo onto the current vehicle of the new chain
			if (moveAmount > 0) {
				old_veh->cargo.Shift(moveAmount, &tmp->cargo);
				remainingAmount -= moveAmount;
			}
		}
	}

	// TODO: needs to be implemented, too
	// // from autoreplace_cmd.cpp : 121
	/* Any left-overs will be thrown away, but not their feeder share. */
	//if (src->cargo_cap < src->cargo.TotalCount()) src->cargo.Truncate(src->cargo.TotalCount() - src->cargo_cap);

	/* Update train weight etc., the old vehicle will be sold anyway */
	new_head->ConsistChanged(CCF_LOADUNLOAD);
}
