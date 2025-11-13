/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_cmd.cpp Commands for vehicles. */

#include "stdafx.h"
#include "roadveh.h"
#include "news_func.h"
#include "airport.h"
#include "command_func.h"
#include "company_func.h"
#include "train.h"
#include "aircraft.h"
#include "newgrf_text.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "depot_map.h"
#include "vehiclelist.h"
#include "engine_func.h"
#include "articulated_vehicles.h"
#include "autoreplace_cmd.h"
#include "autoreplace_gui.h"
#include "group.h"
#include "group_cmd.h"
#include "order_backup.h"
#include "order_cmd.h"
#include "infrastructure_func.h"
#include "ship.h"
#include "newgrf.h"
#include "company_base.h"
#include "core/random_func.hpp"
#include "vehicle_cmd.h"
#include "train_cmd.h"
#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_cmd.h"
#include "tbtr_template_vehicle_func.h"
#include "scope.h"

#include "table/strings.h"

#include "safeguards.h"

/* Tables used in vehicle_func.h to find the right error message for a certain vehicle type */
const StringID _veh_build_msg_table[] = {
	STR_ERROR_CAN_T_BUY_TRAIN,
	STR_ERROR_CAN_T_BUY_ROAD_VEHICLE,
	STR_ERROR_CAN_T_BUY_SHIP,
	STR_ERROR_CAN_T_BUY_AIRCRAFT,
};

const StringID _veh_sell_msg_table[] = {
	STR_ERROR_CAN_T_SELL_TRAIN,
	STR_ERROR_CAN_T_SELL_ROAD_VEHICLE,
	STR_ERROR_CAN_T_SELL_SHIP,
	STR_ERROR_CAN_T_SELL_AIRCRAFT,
};

const StringID _veh_sell_all_msg_table[] = {
	STR_ERROR_CAN_T_SELL_ALL_TRAIN,
	STR_ERROR_CAN_T_SELL_ALL_ROAD_VEHICLE,
	STR_ERROR_CAN_T_SELL_ALL_SHIP,
	STR_ERROR_CAN_T_SELL_ALL_AIRCRAFT,
};

const StringID _veh_autoreplace_msg_table[] = {
	STR_ERROR_CAN_T_AUTOREPLACE_TRAIN,
	STR_ERROR_CAN_T_AUTOREPLACE_ROAD_VEHICLE,
	STR_ERROR_CAN_T_AUTOREPLACE_SHIP,
	STR_ERROR_CAN_T_AUTOREPLACE_AIRCRAFT,
};

const StringID _veh_refit_msg_table[] = {
	STR_ERROR_CAN_T_REFIT_TRAIN,
	STR_ERROR_CAN_T_REFIT_ROAD_VEHICLE,
	STR_ERROR_CAN_T_REFIT_SHIP,
	STR_ERROR_CAN_T_REFIT_AIRCRAFT,
};

const StringID _send_to_depot_msg_table[] = {
	STR_ERROR_CAN_T_SEND_TRAIN_TO_DEPOT,
	STR_ERROR_CAN_T_SEND_ROAD_VEHICLE_TO_DEPOT,
	STR_ERROR_CAN_T_SEND_SHIP_TO_DEPOT,
	STR_ERROR_CAN_T_SEND_AIRCRAFT_TO_HANGAR,
};


CommandCost CmdBuildRailVehicle(TileIndex tile, DoCommandFlags flags, const Engine *e, Vehicle **v);
CommandCost CmdBuildRoadVehicle(TileIndex tile, DoCommandFlags flags, const Engine *e, Vehicle **v);
CommandCost CmdBuildShip       (TileIndex tile, DoCommandFlags flags, const Engine *e, Vehicle **v);
CommandCost CmdBuildAircraft   (TileIndex tile, DoCommandFlags flags, const Engine *e, Vehicle **v);
static CommandCost GetRefitCost(const Vehicle *v, EngineID engine_type, CargoType new_cid, uint8_t new_subtype, bool *auto_refit_allowed);

/**
 * Build a vehicle.
 * @param flags for command
 * @param tile tile of depot where the vehicle is built
 * @param eid vehicle type being built.
 * @param use_free_vehicles use free vehicles when building the vehicle.
 * @param cargo refit cargo type.
 * @param client_id User
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildVehicle(DoCommandFlags flags, TileIndex tile, EngineID eid, bool use_free_vehicles, CargoType cargo, ClientID client_id)
{
	/* Elementary check for valid location. */
	if (!IsDepotTile(tile)) return CMD_ERROR;

	VehicleType type = GetDepotVehicleType(tile);
	if (!IsTileOwner(tile, _current_company)) {
		if (!_settings_game.economy.infrastructure_sharing[type]) return CommandCost(STR_ERROR_CANT_PURCHASE_OTHER_COMPANY_DEPOT);

		const Company *c = Company::GetIfValid(GetTileOwner(tile));
		if (c == nullptr || !c->settings.infra_others_buy_in_depot[type]) return CommandCost(STR_ERROR_CANT_PURCHASE_OTHER_COMPANY_DEPOT);
	}

	/* Validate the engine type. */
	if (!IsEngineBuildable(eid, type, _current_company)) return CommandCost(STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + type);

	/* Validate the cargo type. */
	if (cargo >= NUM_CARGO && cargo != INVALID_CARGO) return CMD_ERROR;

	const Engine *e = Engine::Get(eid);
	CommandCost value(EXPENSES_NEW_VEHICLES, e->GetCost());

	/* Engines without valid cargo should not be available */
	CargoType default_cargo = e->GetDefaultCargoType();
	if (default_cargo == INVALID_CARGO) return CMD_ERROR;

	bool refitting = cargo != INVALID_CARGO && cargo != default_cargo;

	/* Check whether the number of vehicles we need to build can be built according to pool space. */
	uint num_vehicles;
	switch (type) {
		case VEH_TRAIN:    num_vehicles = (e->VehInfo<RailVehicleInfo>().railveh_type == RAILVEH_MULTIHEAD ? 2 : 1) + CountArticulatedParts(eid, false); break;
		case VEH_ROAD:     num_vehicles = 1 + CountArticulatedParts(eid, false); break;
		case VEH_SHIP:     num_vehicles = 1 + CountArticulatedParts(eid, false); break;
		case VEH_AIRCRAFT: num_vehicles = e->VehInfo<AircraftVehicleInfo>().subtype & AIR_CTOL ? 2 : 3; break;
		default: NOT_REACHED(); // Safe due to IsDepotTile()
	}
	if (!Vehicle::CanAllocateItem(num_vehicles)) return CommandCost(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);

	/* Check whether we can allocate a unit number. Autoreplace does not allocate
	 * an unit number as it will (always) reuse the one of the replaced vehicle
	 * and (train) wagons don't have an unit number in any scenario. */
	UnitID unit_num = (flags.Test(DoCommandFlag::QueryCost) || flags.Test(DoCommandFlag::AutoReplace) || (type == VEH_TRAIN && e->VehInfo<RailVehicleInfo>().railveh_type == RAILVEH_WAGON)) ? 0 : GetFreeUnitNumber(type);
	if (unit_num == UINT16_MAX) return CommandCost(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);

	/* If we are refitting we need to temporarily purchase the vehicle to be able to
	 * test it. */
	DoCommandFlags subflags = flags;
	if (refitting && !flags.Test(DoCommandFlag::Execute)) subflags.Set({DoCommandFlag::Execute, DoCommandFlag::AutoReplace});

	/* Vehicle construction needs random bits, so we have to save the random
	 * seeds to prevent desyncs. */
	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);

	Vehicle *v = nullptr;
	switch (type) {
		case VEH_TRAIN:    value.AddCost(CmdBuildRailVehicle(tile, subflags, e, &v)); break;
		case VEH_ROAD:     value.AddCost(CmdBuildRoadVehicle(tile, subflags, e, &v)); break;
		case VEH_SHIP:     value.AddCost(CmdBuildShip       (tile, subflags, e, &v)); break;
		case VEH_AIRCRAFT: value.AddCost(CmdBuildAircraft   (tile, subflags, e, &v)); break;
		default: NOT_REACHED(); // Safe due to IsDepotTile()
	}

	if (value.Succeeded()) {
		if (subflags.Test(DoCommandFlag::Execute)) {
			v->unitnumber = unit_num;
			v->value      = value.GetCost();
			value.SetResultData(v->index);
		}

		if (refitting) {
			/* Refit only one vehicle. If we purchased an engine, it may have gained free wagons.
			 * For ships try to refit all parts. */
			value.AddCost(CmdRefitVehicle(flags, v->index, cargo, 0, false, false, (v->type == VEH_SHIP) ? 0 : 1));
		} else {
			/* Fill in non-refitted capacities */
			if (e->type == VEH_TRAIN || e->type == VEH_ROAD || e->type == VEH_SHIP) {
				_returned_vehicle_capacities = GetCapacityOfArticulatedParts(eid);
				_returned_refit_capacity = _returned_vehicle_capacities[default_cargo];
				_returned_mail_refit_capacity = 0;
			} else {
				_returned_refit_capacity = e->GetDisplayDefaultCapacity(&_returned_mail_refit_capacity);
				_returned_vehicle_capacities.Clear();
				_returned_vehicle_capacities[default_cargo] = _returned_refit_capacity;
				CargoType mail = GetCargoTypeByLabel(CT_MAIL);
				if (IsValidCargoType(mail)) _returned_vehicle_capacities[mail] = _returned_mail_refit_capacity;
			}
		}

		if (flags.Test(DoCommandFlag::Execute)) {
			if (type == VEH_TRAIN && use_free_vehicles && !flags.Test(DoCommandFlag::AutoReplace) && Train::From(v)->IsEngine()) {
				/* Move any free wagons to the new vehicle. */
				NormalizeTrainVehInDepot(Train::From(v));
			}

			InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile.base());
			InvalidateVehicleListWindows(type);
			SetWindowDirty(WC_COMPANY, _current_company);
			if (IsLocalCompany()) {
				InvalidateAutoreplaceWindow(v->engine_type, v->group_id); // updates the auto replace window (must be called before incrementing num_engines)
			}
		}

		if (subflags.Test(DoCommandFlag::Execute)) {
			GroupStatistics::CountEngine(v, 1);
			GroupStatistics::UpdateAutoreplace(_current_company);

			if (v->IsPrimaryVehicle()) {
				GroupStatistics::CountVehicle(v, 1);
				if (!subflags.Test(DoCommandFlag::AutoReplace)) OrderBackup::Restore(v, client_id);
			}

			Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber);
		}


		/* If we are not in DoCommandFlag::Execute undo everything */
		if (flags != subflags) {
			Command<CMD_SELL_VEHICLE>::Do(DoCommandFlag::Execute, v->index, SellVehicleFlags::None, INVALID_CLIENT_ID);
		}
	}

	/* Only restore if we actually did some refitting */
	if (flags != subflags) RestoreRandomSeeds(saved_seeds);

	return value;
}

CommandCost CmdSellRailWagon(DoCommandFlags flags, Vehicle *t, bool sell_chain, bool backup_order, ClientID user);

/**
 * Sell a vehicle.
 * @param flags for command.
 * @param v_id vehicle ID being sold.
 * @param sell_chain sell the vehicle and all vehicles following it in the chain.
 * @param backup_order make a backup of the vehicle's order (if an engine).
 * @param client_id User.
 * @return the cost of this operation or an error.
 */
CommandCost CmdSellVehicle(DoCommandFlags flags, VehicleID v_id, SellVehicleFlags sell_flags, ClientID client_id)
{
	Vehicle *v = Vehicle::GetIfValid(v_id);
	if (v == nullptr) return CMD_ERROR;

	Vehicle *front = v->First();

	CommandCost ret = CheckOwnership(front->owner);
	if (ret.Failed()) return ret;

	if (HasFlag(sell_flags, SellVehicleFlags::VirtualOnly) != HasBit(front->subtype, GVSF_VIRTUAL)) return CMD_ERROR;

	if (front->vehstatus.Test(VehState::Crashed)) return CommandCost(STR_ERROR_VEHICLE_IS_DESTROYED);

	/* Do this check only if the vehicle to be moved is non-virtual */
	if (!HasFlag(sell_flags, SellVehicleFlags::VirtualOnly) && !front->IsStoppedInDepot()) return CommandCost(STR_ERROR_TRAIN_MUST_BE_STOPPED_INSIDE_DEPOT + front->type);

	if (v->type == VEH_TRAIN) {
		ret = CmdSellRailWagon(flags, v, HasFlag(sell_flags, SellVehicleFlags::SellChain), HasFlag(sell_flags, SellVehicleFlags::BackupOrder), client_id);
	} else {
		ret = CommandCost(EXPENSES_NEW_VEHICLES, -front->value);

		if (flags.Test(DoCommandFlag::Execute)) {
			if (front->IsPrimaryVehicle() && HasFlag(sell_flags, SellVehicleFlags::BackupOrder)) OrderBackup::Backup(front, client_id);
			delete front;
		}
	}

	return ret;
}

CommandCost CmdSellVirtualVehicle(DoCommandFlags flags, VehicleID v_id, SellVehicleFlags sell_flags, ClientID client_id)
{
	Train *v = Train::GetIfValid(v_id);
	if (v == nullptr || !v->IsVirtual()) return CMD_ERROR;

	return CmdSellVehicle(flags, v_id, sell_flags | SellVehicleFlags::VirtualOnly, client_id);
}

/**
 * Helper to run the refit cost callback.
 * @param v The vehicle we are refitting, can be nullptr.
 * @param engine_type Which engine to refit
 * @param new_cargo_type Cargo type we are refitting to.
 * @param new_subtype New cargo subtype.
 * @param[out] auto_refit_allowed The refit is allowed as an auto-refit.
 * @return Price for refitting
 */
static int GetRefitCostFactor(const Vehicle *v, EngineID engine_type, CargoType new_cargo_type, uint8_t new_subtype, bool *auto_refit_allowed)
{
	/* Prepare callback param with info about the new cargo type. */
	const Engine *e = Engine::Get(engine_type);

	/* Is this vehicle a NewGRF vehicle? */
	if (e->GetGRF() != nullptr && (e->callbacks_used & SGCU_VEHICLE_REFIT_COST) != 0) {
		const CargoSpec *cs = CargoSpec::Get(new_cargo_type);
		uint32_t param1 = (cs->classes.base() << 16) | (new_subtype << 8) | e->GetGRF()->cargo_map[new_cargo_type];

		uint16_t cb_res = GetVehicleCallback(CBID_VEHICLE_REFIT_COST, param1, 0, engine_type, v);
		if (cb_res != CALLBACK_FAILED) {
			*auto_refit_allowed = HasBit(cb_res, 14);
			int factor = GB(cb_res, 0, 14);
			if (factor >= 0x2000) factor -= 0x4000; // Treat as signed integer.
			return factor;
		}
	}

	*auto_refit_allowed = e->info.refit_cost == 0;
	return (v == nullptr || v->cargo_type != new_cargo_type) ? e->info.refit_cost : 0;
}

/**
 * Learn the price of refitting a certain engine
 * @param v The vehicle we are refitting, can be nullptr.
 * @param engine_type Which engine to refit
 * @param new_cargo_type Cargo type we are refitting to.
 * @param new_subtype New cargo subtype.
 * @param[out] auto_refit_allowed The refit is allowed as an auto-refit.
 * @return Price for refitting
 */
static CommandCost GetRefitCost(const Vehicle *v, EngineID engine_type, CargoType new_cargo_type, uint8_t new_subtype, bool *auto_refit_allowed)
{
	ExpensesType expense_type;
	const Engine *e = Engine::Get(engine_type);
	Price base_price;
	int cost_factor = GetRefitCostFactor(v, engine_type, new_cargo_type, new_subtype, auto_refit_allowed);
	switch (e->type) {
		case VEH_SHIP:
			base_price = PR_BUILD_VEHICLE_SHIP;
			expense_type = EXPENSES_SHIP_RUN;
			break;

		case VEH_ROAD:
			base_price = PR_BUILD_VEHICLE_ROAD;
			expense_type = EXPENSES_ROADVEH_RUN;
			break;

		case VEH_AIRCRAFT:
			base_price = PR_BUILD_VEHICLE_AIRCRAFT;
			expense_type = EXPENSES_AIRCRAFT_RUN;
			break;

		case VEH_TRAIN:
			base_price = (e->VehInfo<RailVehicleInfo>().railveh_type == RAILVEH_WAGON) ? PR_BUILD_VEHICLE_WAGON : PR_BUILD_VEHICLE_TRAIN;
			cost_factor <<= 1;
			expense_type = EXPENSES_TRAIN_RUN;
			break;

		default: NOT_REACHED();
	}
	if (cost_factor < 0) {
		return CommandCost(expense_type, -GetPrice(base_price, -cost_factor, e->GetGRF(), -10));
	} else {
		return CommandCost(expense_type, GetPrice(base_price, cost_factor, e->GetGRF(), -10));
	}
}

/** Helper structure for RefitVehicle() */
struct RefitResult {
	Vehicle *v;         ///< Vehicle to refit
	uint capacity;      ///< New capacity of vehicle
	uint mail_capacity; ///< New mail capacity of aircraft
	uint8_t subtype;       ///< cargo subtype to refit to
};

/**
 * Refits a vehicle (chain).
 * This is the vehicle-type independent part of the CmdRefitXXX functions.
 * @param v            The vehicle to refit.
 * @param only_this    Whether to only refit this vehicle, or to check the rest of them.
 * @param num_vehicles Number of vehicles to refit (not counting articulated parts). Zero means the whole chain.
 * @param new_cargo_type Cargotype to refit to
 * @param new_subtype  Cargo subtype to refit to. 0xFF means to try keeping the same subtype according to GetBestFittingSubType().
 * @param flags        Command flags
 * @param auto_refit   Refitting is done as automatic refitting outside a depot.
 * @return Refit cost.
 */
static CommandCost RefitVehicle(Vehicle *v, bool only_this, uint8_t num_vehicles, CargoType new_cargo_type, uint8_t new_subtype, DoCommandFlags flags, bool auto_refit)
{
	CommandCost cost(v->GetExpenseType(false));
	uint total_capacity = 0;
	uint total_mail_capacity = 0;
	num_vehicles = num_vehicles == 0 ? UINT8_MAX : num_vehicles;
	_returned_vehicle_capacities.Clear();

	VehicleSet vehicles_to_refit;
	if (!only_this) {
		GetVehicleSet(vehicles_to_refit, v, num_vehicles);
		/* In this case, we need to check the whole chain. */
		v = v->First();
	}

	std::vector<RefitResult> refit_result;

	v->InvalidateNewGRFCacheOfChain();
	uint8_t actual_subtype = new_subtype;
	for (; v != nullptr; v = (only_this ? nullptr : v->Next())) {
		/* Reset actual_subtype for every new vehicle */
		if (!v->IsArticulatedPart()) actual_subtype = new_subtype;

		if (v->type == VEH_TRAIN && std::ranges::find(vehicles_to_refit, v->index) == vehicles_to_refit.end() && !only_this) continue;

		const Engine *e = v->GetEngine();
		if (!e->CanCarryCargo()) continue;

		/* If the vehicle is not refittable, or does not allow automatic refitting,
		 * count its capacity nevertheless if the cargo matches */
		bool refittable = HasBit(e->info.refit_mask, new_cargo_type) && (!auto_refit || e->info.misc_flags.Test(EngineMiscFlag::AutoRefit));
		if (!refittable && v->cargo_type != new_cargo_type) {
			uint amount = e->DetermineCapacity(v, nullptr);
			if (amount > 0) _returned_vehicle_capacities[v->cargo_type] += amount;
			continue;
		}

		/* Determine best fitting subtype if requested */
		if (actual_subtype == 0xFF) {
			actual_subtype = GetBestFittingSubType(v, v, new_cargo_type);
		}

		/* Back up the vehicle's cargo type */
		CargoType temp_cargo_type = v->cargo_type;
		uint8_t temp_subtype = v->cargo_subtype;
		if (refittable) {
			v->cargo_type = new_cargo_type;
			v->cargo_subtype = actual_subtype;
		}

		uint16_t mail_capacity = 0;
		uint amount = e->DetermineCapacity(v, &mail_capacity);
		total_capacity += amount;
		/* mail_capacity will always be zero if the vehicle is not an aircraft. */
		total_mail_capacity += mail_capacity;

		_returned_vehicle_capacities[new_cargo_type] += amount;
		CargoType mail = GetCargoTypeByLabel(CT_MAIL);
		if (IsValidCargoType(mail)) _returned_vehicle_capacities[mail] += mail_capacity;

		if (!refittable) continue;

		/* Restore the original cargo type */
		v->cargo_type = temp_cargo_type;
		v->cargo_subtype = temp_subtype;

		bool auto_refit_allowed;
		CommandCost refit_cost = GetRefitCost(v, v->engine_type, new_cargo_type, actual_subtype, &auto_refit_allowed);
		if (auto_refit && !flags.Test(DoCommandFlag::QueryCost) && !auto_refit_allowed) {
			/* Sorry, auto-refitting not allowed, subtract the cargo amount again from the total.
			 * When querrying cost/capacity (for example in order refit GUI), we always assume 'allowed'.
			 * It is not predictable. */
			total_capacity -= amount;
			total_mail_capacity -= mail_capacity;

			if (v->cargo_type == new_cargo_type) {
				/* Add the old capacity nevertheless, if the cargo matches */
				total_capacity += v->cargo_cap;
				if (v->type == VEH_AIRCRAFT) total_mail_capacity += v->Next()->cargo_cap;
			}
			continue;
		}
		cost.AddCost(std::move(refit_cost));

		/* Record the refitting.
		 * Do not execute the refitting immediately, so DetermineCapacity and GetRefitCost do the same in test and exec run.
		 * (weird NewGRFs)
		 * Note:
		 *  - If the capacity of vehicles depends on other vehicles in the chain, the actual capacity is
		 *    set after RefitVehicle() via ConsistChanged() and friends. The estimation via _returned_refit_capacity will be wrong.
		 *  - We have to call the refit cost callback with the pre-refit configuration of the chain because we want refit and
		 *    autorefit to behave the same, and we need its result for auto_refit_allowed.
		 */
		refit_result.emplace_back(v, amount, mail_capacity, actual_subtype);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Store the result */
		for (RefitResult &result : refit_result) {
			Vehicle *u = result.v;
			u->refit_cap = (u->cargo_type == new_cargo_type) ? std::min<uint16_t>(result.capacity, u->refit_cap) : 0;
			if (u->cargo.TotalCount() > u->refit_cap) u->cargo.Truncate(u->cargo.TotalCount() - u->refit_cap);
			u->cargo_type = new_cargo_type;
			u->cargo_cap = result.capacity;
			u->cargo_subtype = result.subtype;
			if (u->type == VEH_AIRCRAFT) {
				Vehicle *w = u->Next();
				assert(w != nullptr);
				w->refit_cap = std::min<uint16_t>(w->refit_cap, result.mail_capacity);
				w->cargo_cap = result.mail_capacity;
				if (w->cargo.TotalCount() > w->refit_cap) w->cargo.Truncate(w->cargo.TotalCount() - w->refit_cap);
			}
		}
	}

	refit_result.clear();
	_returned_refit_capacity = total_capacity;
	_returned_mail_refit_capacity = total_mail_capacity;
	return cost;
}

/**
 * Refits a vehicle to the specified cargo type.
 * @param flags type of operation
 * @param veh_id vehicle ID to refit
 * @param new_cargo_type New cargo type to refit to.
 * @param new_subtype New cargo subtype to refit to. 0xFF means to try keeping the same subtype according to GetBestFittingSubType().
 * @param auto_refit Automatic refitting.
 * @param only_this Refit only this vehicle. Used only for cloning vehicles.
 * @param num_vehicles Number of vehicles to refit (not counting articulated parts). Zero means all vehicles.
 *                     Only used if "refit only this vehicle" is false.
 * @return the cost of this operation or an error
 */
CommandCost CmdRefitVehicle(DoCommandFlags flags, VehicleID veh_id, CargoType new_cid, uint8_t new_subtype, bool auto_refit, bool only_this, uint8_t num_vehicles)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr) return CMD_ERROR;

	/* Don't allow disasters and sparks and such to be refitted.
	 * We cannot check for IsPrimaryVehicle as autoreplace also refits in free wagon chains. */
	if (!IsCompanyBuildableVehicleType(v->type)) return CMD_ERROR;

	Vehicle *front = v->First();

	bool is_virtual_train = v->type == VEH_TRAIN && Train::From(front)->IsVirtual();
	bool free_wagon = v->type == VEH_TRAIN && Train::From(front)->IsFreeWagon(); // used by autoreplace/renew

	if (is_virtual_train) {
		CommandCost ret = CheckOwnership(front->owner);
		if (ret.Failed()) return ret;
	} else {
		CommandCost ret = CheckVehicleControlAllowed(v);
		if (ret.Failed()) return ret;
	}

	/* Don't allow shadows and such to be refitted. */
	if (v != front && (v->type == VEH_AIRCRAFT)) return CMD_ERROR;

	/* Allow auto-refitting only during loading and normal refitting only in a depot. */
	if (!is_virtual_train) {
		if (!flags.Test(DoCommandFlag::QueryCost) && // used by the refit GUI, including the order refit GUI.
				!free_wagon && // used by autoreplace/renew
				(!auto_refit || !front->current_order.IsType(OT_LOADING)) && // refit inside stations
				!front->IsStoppedInDepot()) { // refit inside depots
			return CommandCost(STR_ERROR_TRAIN_MUST_BE_STOPPED_INSIDE_DEPOT + front->type);
		}
	}

	if (front->vehstatus.Test(VehState::Crashed)) return CommandCost(STR_ERROR_VEHICLE_IS_DESTROYED);

	/* Check cargo */
	if (new_cid >= NUM_CARGO) return CMD_ERROR;

	/* For aircraft there is always only one. */
	only_this |= front->type == VEH_AIRCRAFT || (front->type == VEH_SHIP && num_vehicles == 1);

	CommandCost cost = RefitVehicle(v, only_this, num_vehicles, new_cid, new_subtype, flags, auto_refit);
	if (is_virtual_train && !flags.Test(DoCommandFlag::QueryCost)) cost.MultiplyCost(0);

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Update the cached variables */
		switch (v->type) {
			case VEH_TRAIN:
				Train::From(front)->ConsistChanged(auto_refit ? CCF_AUTOREFIT : CCF_REFIT);
				break;
			case VEH_ROAD:
				RoadVehUpdateCache(RoadVehicle::From(front), auto_refit);
				if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) RoadVehicle::From(front)->CargoChanged();
				break;

			case VEH_SHIP:
				v->InvalidateNewGRFCacheOfChain();
				Ship::From(front)->UpdateCache();
				break;

			case VEH_AIRCRAFT:
				v->InvalidateNewGRFCacheOfChain();
				UpdateAircraftCache(Aircraft::From(v), true);
				break;

			default: NOT_REACHED();
		}
		front->MarkDirty();

		if (!free_wagon) {
			InvalidateWindowData(WC_VEHICLE_DETAILS, front->index);
			InvalidateVehicleListWindows(v->type);
		}
		/* virtual vehicles get their cargo changed by the TemplateCreateWindow, so set this dirty instead of a depot window */
		if (HasBit(front->subtype, GVSF_VIRTUAL)) {
			SetWindowClassesDirty(WC_CREATE_TEMPLATE);
		} else {
			SetWindowDirty(WC_VEHICLE_DEPOT, front->tile.base());
		}
	} else {
		/* Always invalidate the cache; querycost might have filled it. */
		v->InvalidateNewGRFCacheOfChain();
	}

	return cost;
}

/**
 * Start/Stop a vehicle
 * @param flags type of operation
 * @param veh_id vehicle to start/stop, don't forget to change CcStartStopVehicle if you modify this!
 * @param evaluate_startstop_cb Shall the start/stop newgrf callback be evaluated (only valid with DoCommandFlag::AutoReplace for network safety)
 * @return the cost of this operation or an error
 */
CommandCost CmdStartStopVehicle(DoCommandFlags flags, VehicleID veh_id, bool evaluate_startstop_cb)
{
	/* Disable the effect of evaluate_startstop_cb, when DoCommandFlag::AutoReplace is not set */
	if (!flags.Test(DoCommandFlag::AutoReplace)) evaluate_startstop_cb = true;

	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckVehicleControlAllowed(v);
	if (ret.Failed()) return ret;

	if (v->vehstatus.Test(VehState::Crashed)) return CommandCost(STR_ERROR_VEHICLE_IS_DESTROYED);

	switch (v->type) {
		case VEH_TRAIN:
			if (v->vehstatus.Test(VehState::Stopped) && Train::From(v)->gcache.cached_power == 0) return CommandCost(STR_ERROR_TRAIN_START_NO_POWER);
			break;

		case VEH_SHIP:
		case VEH_ROAD:
			break;

		case VEH_AIRCRAFT: {
			Aircraft *a = Aircraft::From(v);
			/* cannot stop airplane when in flight, or when taking off / landing */
			if (a->state >= STARTTAKEOFF && a->state < TERM7) return CommandCost(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
			if (HasBit(a->flags, VAF_HELI_DIRECT_DESCENT)) return CommandCost(STR_ERROR_AIRCRAFT_IS_IN_FLIGHT);
			break;
		}

		default: return CMD_ERROR;
	}

	if (evaluate_startstop_cb) {
		/* Check if this vehicle can be started/stopped. Failure means 'allow'. */
		uint16_t callback = GetVehicleCallback(CBID_VEHICLE_START_STOP_CHECK, 0, 0, v->engine_type, v);
		StringID error = STR_NULL;
		if (callback != CALLBACK_FAILED) {
			if (v->GetGRF()->grf_version < 8) {
				/* 8 bit result 0xFF means 'allow' */
				if (callback < 0x400 && GB(callback, 0, 8) != 0xFF) error = GetGRFStringID(v->GetGRF(), GRFSTR_MISC_GRF_TEXT + callback);
			} else {
				if (callback < 0x400) {
					error = GetGRFStringID(v->GetGRF(), GRFSTR_MISC_GRF_TEXT + callback);
				} else {
					switch (callback) {
						case 0x400: // allow
							break;

						case 0x40F:
							error = GetGRFStringID(v->GetGRFID(), static_cast<GRFStringID>(GetRegister(0x100)));
							break;

						default: // unknown reason -> disallow
							error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
							break;
					}
				}
			}
		}
		if (error != STR_NULL) return CommandCost(error);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		if (v->IsStoppedInDepot() && !flags.Test(DoCommandFlag::AutoReplace)) DeleteVehicleNews(veh_id, AdviceType::VehicleWaiting);

		v->ClearSeparation();
		if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) v->vehicle_flags.Reset(VehicleFlag::TimetableStarted);

		v->vehstatus.Flip(VehState::Stopped);
		if (v->type == VEH_ROAD) {
			if (!RoadVehicle::From(v)->IsRoadVehicleOnLevelCrossing()) v->cur_speed = 0;
		} else if (v->type != VEH_TRAIN) {
			v->cur_speed = 0; // trains can stop 'slowly'
		}
		if (v->type == VEH_TRAIN && !v->vehstatus.Test(VehState::Stopped) && v->cur_speed == 0 && Train::From(v)->lookahead != nullptr) {
			/* Starting train from stationary with a lookahead, refresh it */
			Train::From(v)->lookahead.reset();
			FillTrainReservationLookAhead(Train::From(v));
		}

		/* Unbunching data is no longer valid. */
		v->ResetDepotUnbunching();

		/* Prevent any attempt to update timetable for current order if now stopped in depot. */
		if (v->IsStoppedInDepot() && !flags.Test(DoCommandFlag::AutoReplace)) {
			v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
		}

		v->MarkDirty();
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		SetWindowDirty(WC_VEHICLE_DEPOT, v->tile.base());
		DirtyVehicleListWindowForVehicle(v);
		InvalidateWindowData(WC_VEHICLE_VIEW, v->index);
	}
	return CommandCost();
}

/**
 * Starts or stops a lot of vehicles
 * @param flags for command type
 * @param tile Tile of the depot where the vehicles are started/stopped (only used for depots)
 * @param do_start set = start vehicles, unset = stop vehicles
 * @param vehicle_list_window if set, then it's a vehicle list window, not a depot and Tile is ignored in this case
 * @param vli VehicleListIdentifier
 * @param cid Cargo filter (or CargoFilterCriteria::CF_ANY) (only used for vehicle list windows)
 * @return the cost of this operation or an error
 */
CommandCost CmdMassStartStopVehicle(DoCommandFlags flags, TileIndex tile, bool do_start, bool vehicle_list_window, VehicleListIdentifier vli, CargoType cargo_filter)
{
	VehicleList list;

	if (!IsCompanyBuildableVehicleType(vli.vtype)) return CMD_ERROR;

	if (vehicle_list_window) {
		if (!GenerateVehicleSortList(&list, vli, cargo_filter)) return CMD_ERROR;
	} else {
		if (!IsDepotTile(tile)) return CMD_ERROR;
		/* Get the list of vehicles in the depot */
		BuildDepotVehicleList(vli.vtype, tile, &list, nullptr);
	}

	for (const Vehicle *v : list) {
		if (v->vehstatus.Test(VehState::Stopped) != do_start) continue;

		if (!vehicle_list_window && !v->IsChainInDepot()) continue;

		/* Just try and don't care if some vehicle's can't be stopped. */
		Command<CMD_START_STOP_VEHICLE>::Do(flags, v->index, false);
	}

	return CommandCost();
}

/**
 * Sells all vehicles in a depot
 * @param flags type of operation
 * @param tile Tile of the depot where the depot is
 * @param vehicle_type Vehicle type
 * @return the cost of this operation or an error
 */
CommandCost CmdDepotSellAllVehicles(DoCommandFlags flags, TileIndex tile, VehicleType vehicle_type)
{
	VehicleList list;

	CommandCost cost(EXPENSES_NEW_VEHICLES);

	if (!IsCompanyBuildableVehicleType(vehicle_type)) return CMD_ERROR;
	if (!IsDepotTile(tile)) return CMD_ERROR;

	/* Get the list of vehicles in the depot */
	BuildDepotVehicleList(vehicle_type, tile, &list, &list);

	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	for (const Vehicle *v : list) {
		if (v->owner != _current_company) continue;
		CommandCost ret = Command<CMD_SELL_VEHICLE>::Do(flags, v->index, SellVehicleFlags::SellChain, INVALID_CLIENT_ID);
		if (ret.Succeeded()) {
			cost.AddCost(ret.GetCost());
			had_success = true;
		} else {
			last_error = std::move(ret);
		}
	}

	return had_success ? cost : last_error;
}

/**
 * Autoreplace all vehicles in the depot
 * @param flags type of operation
 * @param tile Tile of the depot where the vehicles are
 * @param vehicle_type Type of vehicle
 * @return the cost of this operation or an error
 */
CommandCost CmdDepotMassAutoReplace(DoCommandFlags flags, TileIndex tile, VehicleType vehicle_type)
{
	VehicleList list;
	CommandCost cost = CommandCost(EXPENSES_NEW_VEHICLES);

	if (!IsCompanyBuildableVehicleType(vehicle_type)) return CMD_ERROR;
	if (!IsDepotTile(tile) || !IsInfraUsageAllowed(vehicle_type, _current_company, GetTileOwner(tile))) return CMD_ERROR;

	/* Get the list of vehicles in the depot */
	BuildDepotVehicleList(vehicle_type, tile, &list, &list, true);

	for (const Vehicle *v : list) {
		/* Ensure that the vehicle completely in the depot */
		if (!v->IsChainInDepot()) continue;

		if (v->type == VEH_TRAIN) {
			CommandCost ret = Command<CMD_TEMPLATE_REPLACE_VEHICLE>::Do(flags, v->index);
			if (ret.Succeeded()) cost.AddCost(ret.GetCost());
			if (auto result_v = ret.GetResultData<VehicleID>(); result_v.has_value()) {
				v = Vehicle::Get(*result_v);
			}
		}

		CommandCost ret = Command<CMD_AUTOREPLACE_VEHICLE>::Do(flags, v->index, false);

		if (ret.Succeeded()) cost.AddCost(ret.GetCost());
	}
	return cost;
}

/**
 * Test if a name is unique among vehicle names.
 * @param name Name to test.
 * @return True if the name is unique.
 */
bool IsUniqueVehicleName(std::string_view name)
{
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (!v->name.empty() && v->name == name) return false;
	}

	return true;
}

/**
 * Clone the custom name of a vehicle, adding or incrementing a number.
 * @param src Source vehicle, with a custom name.
 * @param dst Destination vehicle.
 */
static void CloneVehicleName(const Vehicle *src, Vehicle *dst)
{
	std::string new_name = src->name.c_str();

	if (!std::isdigit(*new_name.rbegin())) {
		// No digit at the end, so start at number 1 (this will get incremented to 2)
		new_name += " 1";
	}

	int max_iterations = 1000;
	do {
		size_t pos = new_name.length() - 1;
		// Handle any carrying
		for (; pos != std::string::npos && new_name[pos] == '9'; --pos) {
			new_name[pos] = '0';
		}

		if (pos != std::string::npos && std::isdigit(new_name[pos])) {
			++new_name[pos];
		} else {
			new_name[++pos] = '1';
			new_name.push_back('0');
		}
		--max_iterations;
	} while(max_iterations > 0 && !IsUniqueVehicleName(new_name));

	if (max_iterations > 0) {
		dst->name = new_name;
	}

	/* All done. If we didn't find a name, it'll just use its default. */
}

/**
 * Change a flag of a template vehicle.
 * @param flags type of operation
 * @param template_id the template vehicle's index
 * @param change_flag the flag to change
 * @param set whether to set or clear the flag
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeFlagTemplateReplace(DoCommandFlags flags, TemplateID template_id, TemplateReplacementFlag change_flag, bool set)
{
	TemplateVehicle *tv = TemplateVehicle::GetIfValid(template_id);

	if (tv == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(tv->owner);
	if (ret.Failed()) return ret;

	switch (change_flag) {
		case TemplateReplacementFlag::ReuseDepotVehicles:
		case TemplateReplacementFlag::KeepRemaining:
		case TemplateReplacementFlag::RefitAsTemplate:
		case TemplateReplacementFlag::ReplaceOldOnly:
			break;

		default:
			return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		switch (change_flag) {
			case TemplateReplacementFlag::ReuseDepotVehicles:
				if (tv->IsSetReuseDepotVehicles() != set) {
					tv->SetReuseDepotVehicles(set);
				}
				break;

			case TemplateReplacementFlag::KeepRemaining:
				if (tv->IsSetKeepRemainingVehicles() != set) {
					tv->SetKeepRemainingVehicles(set);
				}
				break;

			case TemplateReplacementFlag::RefitAsTemplate:
				if (tv->IsSetRefitAsTemplate() != set) {
					tv->SetRefitAsTemplate(set);
					MarkTrainsUsingTemplateAsPendingTemplateReplacement(tv);
				}
				break;

			case TemplateReplacementFlag::ReplaceOldOnly:
				if (tv->IsReplaceOldOnly() != set) {
					tv->SetReplaceOldOnly(set);
					MarkTrainsUsingTemplateAsPendingTemplateReplacement(tv);
				}
				break;

			default:
				return CMD_ERROR;
		}
		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}

/**
 * Rename a template vehicle.
 * @param flags type of operation
 * @param template_id the template vehicle's index
 * @param name new name
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameTemplateReplace(DoCommandFlags flags, TemplateID template_id, const std::string &name)
{
	TemplateVehicle *template_vehicle = TemplateVehicle::GetIfValid(template_id);

	if (template_vehicle == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(template_vehicle->owner);
	if (ret.Failed()) return ret;

	bool reset = name.empty();

	if (!reset) {
		if (Utf8StringLength(name) >= MAX_LENGTH_GROUP_NAME_CHARS) return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Assign the new one */
		if (reset) {
			template_vehicle->name.clear();
		} else {
			template_vehicle->name = name;
		}

		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}

/**
 * Create a virtual train from a template vehicle.
 * @param flags type of operation
 * @param template_id template ID
 * @param client client ID
 * @return the cost of this operation or an error
 */
CommandCost CmdVirtualTrainFromTemplate(DoCommandFlags flags, TemplateID template_id, ClientID client)
{
	TemplateVehicle *tv = TemplateVehicle::GetIfValid(template_id);

	if (tv == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(tv->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		StringID err = INVALID_STRING_ID;
		Train *train = VirtualTrainFromTemplateVehicle(tv, err, client);

		if (train == nullptr) {
			return CommandCost(err);
		}

		CommandCost cost;
		cost.SetResultData(train->index);
		return cost;
	}

	return CommandCost();
}

template <typename T>
void UpdateNewVirtualTrainFromSource(Train *v, const T *src)
{
	struct helper {
		static bool IsTrainPartReversed(const Train *src) { return HasBit(src->flags, VRF_REVERSE_DIRECTION); }
		static bool IsTrainPartReversed(const TemplateVehicle *src) { return HasBit(src->ctrl_flags, TVCF_REVERSED); }
		static const Train *GetTrainMultiheadOtherPart(const Train *src) { return src->other_multiheaded_part; }
		static const TemplateVehicle *GetTrainMultiheadOtherPart(const TemplateVehicle *src) { return src; }
	};

	AssignBit(v->flags, VRF_REVERSE_DIRECTION, helper::IsTrainPartReversed(src));

	if (v->IsMultiheaded()) {
		const T *other = helper::GetTrainMultiheadOtherPart(src);
		/* For template vehicles, just use the front part, fix any discrepancy later */
		v->other_multiheaded_part->cargo_type = other->cargo_type;
		v->other_multiheaded_part->cargo_subtype = other->cargo_subtype;
	}

	while (true) {
		v->cargo_type = src->cargo_type;
		v->cargo_subtype = src->cargo_subtype;

		if (v->HasArticulatedPart()) {
			v = v->Next();
		} else {
			break;
		}

		if (src->HasArticulatedPart()) {
			src = src->Next();
		} else {
			break;
		}
	}

	v->First()->ConsistChanged(CCF_ARRANGE);
	InvalidateVehicleTickCaches();
}

Train *VirtualTrainFromTemplateVehicle(const TemplateVehicle *tv, StringID &err, ClientID user)
{
	const TemplateVehicle *tv_head = tv;

	assert(tv->owner == _current_company);

	Train *head = BuildVirtualRailVehicle(tv->engine_type, err, user, true);
	if (!head) return nullptr;

	UpdateNewVirtualTrainFromSource(head, tv);

	Train *tail = head;
	tv = tv->GetNextUnit();
	while (tv != nullptr) {
		Train *tmp = BuildVirtualRailVehicle(tv->engine_type, err, user, true);
		if (tmp == nullptr) {
			CmdDeleteVirtualTrain(DoCommandFlag::Execute, head->index);
			return nullptr;
		}

		UpdateNewVirtualTrainFromSource(tmp, tv);

		CmdMoveRailVehicle(DoCommandFlag::Execute, tmp->index, tail->index, MoveRailVehicleFlags::Virtual);
		tail = tmp;

		tv = tv->GetNextUnit();
	}

	Train *tmp = nullptr;
	for (tv = tv_head, tmp = head; tv != nullptr && tmp != nullptr; tv = tv->Next(), tmp = tmp->Next()) {
		tmp->cargo_type = tv->cargo_type;
		tmp->cargo_subtype = tv->cargo_subtype;
	}

	return head;
}

/**
 * Create a virtual train from a regular train.
 * @param flags type of operation
 * @param vehicle_id the train index
 * @param client user
 * @return the cost of this operation or an error
 */
CommandCost CmdVirtualTrainFromTrain(DoCommandFlags flags, VehicleID vehicle_id, ClientID client)
{
	Train *train = Train::GetIfValid(vehicle_id);
	if (train == nullptr) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		StringID err = INVALID_STRING_ID;

		Train *head = BuildVirtualRailVehicle(train->engine_type, err, client, true);
		if (head == nullptr) return CommandCost(err);

		UpdateNewVirtualTrainFromSource(head, train);

		Train *tail = head;
		train = train->GetNextUnit();
		while (train != nullptr) {
			Train *tmp = BuildVirtualRailVehicle(train->engine_type, err, client, true);
			if (tmp == nullptr) {
				CmdDeleteVirtualTrain(flags, head->index);
				return CommandCost(err);
			}

			UpdateNewVirtualTrainFromSource(tmp, train);

			CmdMoveRailVehicle(DoCommandFlag::Execute, tmp->index, tail->index, MoveRailVehicleFlags::Virtual);
			tail = tmp;

			train = train->GetNextUnit();
		}

		CommandCost cost;
		cost.SetResultData(head->index);
		return cost;
	}

	return CommandCost();
}

/**
 * Delete a virtual train
 * @param flags type of operation
 * @param vehicle_id the vehicle's index
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteVirtualTrain(DoCommandFlags flags, VehicleID vehicle_id)
{
	Train *train = Train::GetIfValid(vehicle_id);

	if (train == nullptr || !train->IsVirtual()) {
		return CMD_ERROR;
	}

	CommandCost ret = CheckOwnership(train->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		delete train->First();
	}

	return CommandCost();
}

/**
 * Replace a template vehicle with another one based on a virtual train.
 * @param flags type of operation
 * @param template_id the template vehicle's index
 * @param virtual_train_id the virtual train's index
 * @return the cost of this operation or an error
 */
CommandCost CmdReplaceTemplateVehicle(DoCommandFlags flags, TemplateID template_id, VehicleID virtual_train_id)
{
	TemplateVehicle *template_vehicle = TemplateVehicle::GetIfValid(template_id);
	Train *train = Train::GetIfValid(virtual_train_id);
	if (train == nullptr || !train->IsVirtual()) return CMD_ERROR;
	train = train->First();

	CommandCost ret = CheckOwnership(train->owner);
	if (ret.Failed()) return ret;
	if (template_vehicle != nullptr) {
		ret = CheckOwnership(template_vehicle->owner);
		if (ret.Failed()) return ret;
	}

	if (!TemplateVehicle::CanAllocateItem(CountVehiclesInChain(train))) {
		return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		TemplateID old_ID = INVALID_TEMPLATE;

		bool restore_flags = false;
		bool reuse_depot_vehicles = false;
		bool keep_remaining_vehicles = false;
		bool refit_as_template = true;
		bool replace_old_only = false;
		std::string name;

		if (template_vehicle != nullptr) {
			old_ID = template_vehicle->index;
			restore_flags = true;
			reuse_depot_vehicles = template_vehicle->reuse_depot_vehicles;
			keep_remaining_vehicles = template_vehicle->keep_remaining_vehicles;
			refit_as_template = template_vehicle->refit_as_template;
			replace_old_only = template_vehicle->replace_old_only;
			name = std::move(template_vehicle->name);
			delete template_vehicle;
			template_vehicle = nullptr;
		}

		template_vehicle = TemplateVehicleFromVirtualTrain(train);

		if (restore_flags) {
			template_vehicle->reuse_depot_vehicles = reuse_depot_vehicles;
			template_vehicle->keep_remaining_vehicles = keep_remaining_vehicles;
			template_vehicle->refit_as_template = refit_as_template;
			template_vehicle->replace_old_only = replace_old_only;
			template_vehicle->name = std::move(name);
		}

		/* Make sure our replacements still point to the correct thing. */
		if (old_ID != INVALID_TEMPLATE && old_ID != template_vehicle->index) {
			bool reindex = false;
			for (auto &it : _template_replacements) {
				if (it.second == old_ID) {
					it.second = template_vehicle->index;
					reindex = true;
				}
			}
			if (reindex) {
				ReindexTemplateReplacements();
				MarkTrainsUsingTemplateAsPendingTemplateReplacement(template_vehicle);
			}
		} else if (template_vehicle->NumGroupsUsingTemplate() > 0) {
			MarkTrainsUsingTemplateAsPendingTemplateReplacement(template_vehicle);
		}

		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}

/**
 * Clone a vehicle to create a template vehicle.
 * @param flags type of operation
 * @param veh_id the original vehicle's index
 * @return the cost of this operation or an error
 */
CommandCost CmdTemplateVehicleFromTrain(DoCommandFlags flags, VehicleID veh_id)
{
	Train *clicked = Train::GetIfValid(veh_id);
	if (clicked == nullptr) return CMD_ERROR;

	Train *init_clicked = clicked;

	uint len = CountVehiclesInChain(clicked);
	if (!TemplateVehicle::CanAllocateItem(len)) {
		return CMD_ERROR;
	}

	for (Train *v = clicked; v != nullptr; v = v->GetNextUnit()) {
		const Engine *e = Engine::GetIfValid(v->engine_type);
		if (e == nullptr || e->type != VEH_TRAIN) {
			return CommandCost(STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + VEH_TRAIN);
		}
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		TemplateVehicle *tmp = nullptr;
		TemplateVehicle *prev = nullptr;
		for (; clicked != nullptr; clicked = clicked->Next()) {
			tmp = new TemplateVehicle(clicked->engine_type);
			SetupTemplateVehicleFromVirtual(tmp, prev, clicked);
			tmp->owner = _current_company;
			prev = tmp;
		}

		tmp->First()->SetRealLength(CeilDiv(init_clicked->gcache.cached_total_length * 10, TILE_SIZE));

		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}

/**
 * Delete a template vehicle.
 * @param flags type of operation
 * @param template_id the template vehicle's index
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteTemplateVehicle(DoCommandFlags flags, TemplateID template_id)
{
	/* Identify template to delete */
	TemplateVehicle *del = TemplateVehicle::GetIfValid(template_id);

	if (del == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(del->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		/* Remove corresponding template replacements if existing */
		RemoveTemplateReplacementsReferencingTemplate(del->index);
		delete del;

		InvalidateWindowClassesData(WC_CREATE_TEMPLATE, 0);
		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}

/**
 * Issues a template replacement for a vehicle group
 * @param flags type of operation
 * @param group_id the group index
 * @param template_id the template vehicle's index
 * @return the cost of this operation or an error
 */
CommandCost CmdIssueTemplateReplacement(DoCommandFlags flags, GroupID group_id, TemplateID template_id)
{
	Group *g = Group::GetIfValid(group_id);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	TemplateVehicle *tv = TemplateVehicle::GetIfValid(template_id);
	if (tv == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(tv->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		IssueTemplateReplacement(group_id, template_id);
		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}

/**
 * Deletes a template replacement from a vehicle group
 * @param flags type of operation
 * @param group_id the group index
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteTemplateReplacement(DoCommandFlags flags, GroupID group_id)
{
	Group *g = Group::GetIfValid(group_id);
	if (g == nullptr || g->owner != _current_company) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		RemoveTemplateReplacement(group_id);
		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
	}

	return CommandCost();
}


/**
 * Clone a vehicle. If it is a train, it will clone all the cars too
 * @param flags type of operation
 * @param tile tile of the depot where the cloned vehicle is build
 * @param veh_id the original vehicle's index
 * @param share_orders shared orders, else copied orders
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneVehicle(DoCommandFlags flags, TileIndex tile, VehicleID veh_id, bool share_orders)
{
	CommandCost total_cost(EXPENSES_NEW_VEHICLES);

	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;
	Vehicle *v_front = v;
	Vehicle *w = nullptr;
	Vehicle *w_front = nullptr;
	Vehicle *w_rear = nullptr;

	/*
	 * v_front is the front engine in the original vehicle
	 * v is the car/vehicle of the original vehicle that is currently being copied
	 * w_front is the front engine of the cloned vehicle
	 * w is the car/vehicle currently being cloned
	 * w_rear is the rear end of the cloned train. It's used to add more cars and is only used by trains
	 */

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->type == VEH_TRAIN && (!v->IsFrontEngine() || Train::From(v)->crash_anim_pos >= 4400)) return CMD_ERROR;

	/* check that we can allocate enough vehicles */
	if (!flags.Test(DoCommandFlag::Execute)) {
		int veh_counter = 0;
		do {
			veh_counter++;
		} while ((v = v->Next()) != nullptr);

		if (!Vehicle::CanAllocateItem(veh_counter)) {
			return CommandCost(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
		}
	}

	v = v_front;

	do {
		if (v->type == VEH_TRAIN && Train::From(v)->IsRearDualheaded()) {
			/* we build the rear ends of multiheaded trains with the front ones */
			continue;
		}

		/* In case we're building a multi headed vehicle and the maximum number of
		 * vehicles is almost reached (e.g. max trains - 1) not all vehicles would
		 * be cloned. When the non-primary engines were build they were seen as
		 * 'new' vehicles whereas they would immediately be joined with a primary
		 * engine. This caused the vehicle to be not build as 'the limit' had been
		 * reached, resulting in partially build vehicles and such. */
		DoCommandFlags build_flags = flags;
		if (flags.Test(DoCommandFlag::Execute) && !v->IsPrimaryVehicle()) build_flags.Set(DoCommandFlag::AutoReplace);

		CommandCost cost = Command<CMD_BUILD_VEHICLE>::Do(build_flags, tile, v->engine_type, false, INVALID_CARGO, INVALID_CLIENT_ID);

		if (cost.Failed()) {
			/* Can't build a part, then sell the stuff we already made; clear up the mess */
			if (w_front != nullptr) Command<CMD_SELL_VEHICLE>::Do(flags, w_front->index, SellVehicleFlags::SellChain, INVALID_CLIENT_ID);
			return cost;
		}

		total_cost.AddCost(cost.GetCost());

		if (flags.Test(DoCommandFlag::Execute)) {
			auto veh_id = cost.GetResultData<VehicleID>();
			if (!veh_id.has_value()) return CMD_ERROR;
			w = Vehicle::Get(*veh_id);

			if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) {
				SetBit(Train::From(w)->flags, VRF_REVERSE_DIRECTION);
			}

			if (v->type == VEH_TRAIN && !v->IsFrontEngine()) {
				/* this s a train car
				 * add this unit to the end of the train */
				CommandCost result = Command<CMD_MOVE_RAIL_VEHICLE>::Do(flags, w->index, w_rear->index, MoveRailVehicleFlags::MoveChain);
				if (result.Failed()) {
					/* The train can't be joined to make the same consist as the original.
					 * Sell what we already made (clean up) and return an error.           */
					Command<CMD_SELL_VEHICLE>::Do(flags, w_front->index, SellVehicleFlags::SellChain, INVALID_CLIENT_ID);
					Command<CMD_SELL_VEHICLE>::Do(flags, w->index,       SellVehicleFlags::SellChain, INVALID_CLIENT_ID);
					return result; // return error and the message returned from CMD_MOVE_RAIL_VEHICLE
				}
			} else {
				/* this is a front engine or not a train. */
				w_front = w;
				w->service_interval = v->service_interval;
				w->SetServiceIntervalIsCustom(v->ServiceIntervalIsCustom());
				w->SetServiceIntervalIsPercent(v->ServiceIntervalIsPercent());
			}
			w_rear = w; // trains needs to know the last car in the train, so they can add more in next loop
		}
	} while (v->type == VEH_TRAIN && (v = v->GetNextVehicle()) != nullptr);

	if (flags.Test(DoCommandFlag::Execute)) {
		/* for trains this needs to be the front engine due to the callback function */
		total_cost.SetResultData(w_front->index);
	}

	const Company *owner = Company::GetIfValid(_current_company);
	if ((flags.Test(DoCommandFlag::Execute)) && (share_orders || owner == nullptr || owner->settings.copy_clone_add_to_group)) {
		/* Cloned vehicles belong to the same group */
		Command<CMD_ADD_VEHICLE_GROUP>::Do(flags, v_front->group_id, w_front->index, false);
	}


	/* Take care of refitting. */
	w = w_front;
	v = v_front;

	/* Both building and refitting are influenced by newgrf callbacks, which
	 * makes it impossible to accurately estimate the cloning costs. In
	 * particular, it is possible for engines of the same type to be built with
	 * different numbers of articulated parts, so when refitting we have to
	 * loop over real vehicles first, and then the articulated parts of those
	 * vehicles in a different loop. */
	do {
		do {
			if (flags.Test(DoCommandFlag::Execute)) {
				assert(w != nullptr);

				/* Find out what's the best sub type */
				uint8_t subtype = GetBestFittingSubType(v, w, v->cargo_type);
				if (w->cargo_type != v->cargo_type || w->cargo_subtype != subtype) {
					CommandCost cost = Command<CMD_REFIT_VEHICLE>::Do(flags, w->index, v->cargo_type, subtype, false, true, 0);
					if (cost.Succeeded()) total_cost.AddCost(cost.GetCost());
				}

				if (w->IsGroundVehicle() && w->HasArticulatedPart()) {
					w = w->GetNextArticulatedPart();
				} else {
					break;
				}
			} else {
				const Engine *e = v->GetEngine();
				CargoType initial_cargo = (e->CanCarryCargo() ? e->GetDefaultCargoType() : INVALID_CARGO);

				if (v->cargo_type != initial_cargo && initial_cargo != INVALID_CARGO) {
					bool dummy;
					total_cost.AddCost(GetRefitCost(nullptr, v->engine_type, v->cargo_type, v->cargo_subtype, &dummy));
				}
			}

			if (v->IsGroundVehicle() && v->HasArticulatedPart()) {
				v = v->GetNextArticulatedPart();
			} else {
				break;
			}
		} while (v != nullptr);

		if ((flags.Test(DoCommandFlag::Execute)) && (v->type == VEH_TRAIN || v->type == VEH_SHIP)) w = w->GetNextVehicle();
	} while ((v->type == VEH_TRAIN || v->type == VEH_SHIP) && (v = v->GetNextVehicle()) != nullptr);

	if (flags.Test(DoCommandFlag::Execute)) {
		/*
		 * Set the orders of the vehicle. Cannot do it earlier as we need
		 * the vehicle refitted before doing this, otherwise the moved
		 * cargo types might not match (passenger vs non-passenger)
		 */
		CommandCost result = Command<CMD_CLONE_ORDER>::Do(flags, (share_orders ? CO_SHARE : CO_COPY), w_front->index, v_front->index);
		if (result.Failed()) {
			/* The vehicle has already been bought, so now it must be sold again. */
			Command<CMD_SELL_VEHICLE>::Do(flags, w_front->index, SellVehicleFlags::SellChain, INVALID_CLIENT_ID);
			return result;
		}

		/* Now clone the vehicle's name, if it has one. */
		if (!v_front->name.empty()) CloneVehicleName(v_front, w_front);

		/* Since we can't estimate the cost of cloning a vehicle accurately we must
		 * check whether the company has enough money manually. */
		if (!CheckCompanyHasMoney(total_cost)) {
			/* The vehicle has already been bought, so now it must be sold again. */
			Command<CMD_SELL_VEHICLE>::Do(flags, w_front->index, SellVehicleFlags::SellChain, INVALID_CLIENT_ID);
			return total_cost;
		}
	}

	return total_cost;
}

/**
 * Clone a vehicle from a template.
 * @param flags type of operation
 * @param template_id the original template vehicle's index
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneVehicleFromTemplate(DoCommandFlags flags, TileIndex tile, TemplateID template_id)
{
	TemplateVehicle *tv = TemplateVehicle::GetIfValid(template_id);

	if (tv == nullptr) {
		return CMD_ERROR;
	}

	CommandCost ret = CheckOwnership(tv->owner);
	if (ret.Failed()) return ret;

	/* Vehicle construction needs random bits, so we have to save the random
	 * seeds to prevent desyncs. */
	SavedRandomSeeds saved_seeds;
	SaveRandomSeeds(&saved_seeds);

	auto guard = scope_guard([&]() {
		if (!flags.Test(DoCommandFlag::Execute)) RestoreRandomSeeds(saved_seeds);
	});

	ret = Command<CMD_VIRTUAL_TRAIN_FROM_TEMPLATE>::Do(DoCommandFlag::Execute, tv->index, INVALID_CLIENT_ID);
	if (ret.Failed()) return ret;

	auto result_v = ret.GetResultData<VehicleID>();
	if (!result_v.has_value()) return CMD_ERROR;

	Train *virt = Train::Get(*result_v);

	ret = Command<CMD_CLONE_VEHICLE>::Do(flags, tile, *result_v, false);

	delete virt;

	return ret;
}

/**
 * Send all vehicles of type to depots
 * @param flags   the flags used for DoCommand()
 * @param depot_flags depot command flags
 * @param vli     identifier of the vehicle list
 * @param cid Cargo filter (or CargoFilterCriteria::CF_ANY)
 * @return 0 for success and CMD_ERROR if no vehicle is able to go to depot
 */
static CommandCost SendAllVehiclesToDepot(DoCommandFlags flags, DepotCommandFlags depot_flags, const VehicleListIdentifier &vli, const CargoType cid)
{
	VehicleList list;

	if (!GenerateVehicleSortList(&list, vli, cid)) return CMD_ERROR;

	/* Send all the vehicles to a depot */
	bool had_success = false;
	for (uint i = 0; i < list.size(); i++) {
		const Vehicle *v = list[i];
		CommandCost ret = Command<CMD_SEND_VEHICLE_TO_DEPOT>::Do(flags, v->index, depot_flags, {});

		if (ret.Succeeded()) {
			had_success = true;

			/* Return 0 if DoCommandFlag::Execute is not set this is a valid goto depot command)
			 * In this case we know that at least one vehicle can be sent to a depot
			 * and we will issue the command. We can now safely quit the loop, knowing
			 * it will succeed at least once. With DoCommandFlag::Execute we really need to send them to the depot */
			if (!flags.Test(DoCommandFlag::Execute)) break;
		}
	}

	return had_success ? CommandCost() : CMD_ERROR;
}

/**
 * Send a vehicle to the depot.
 * @param flags for command type
 * @param veh_id vehicle ID to send to the depot
 * @param depot_cmd DEPOT_ flags (see vehicle_type.h)
 * @param p2 packed VehicleListIdentifier, or specific depot tile
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSendVehicleToDepot(DoCommandFlags flags, VehicleID veh_id, DepotCommandFlags depot_cmd, TileIndex specific_depot)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr) return CMD_ERROR;
	if (!v->IsPrimaryVehicle()) return CMD_ERROR;

	return v->SendToDepot(flags, depot_cmd, specific_depot);
}

/**
 * Send a vehicle to the depot.
 * @param flags for command type
 * @param depot_cmd DEPOT_ flags (see vehicle_type.h)
 * @param vli VehicleListIdentifier
 * @param cid Cargo filter (or CargoFilterCriteria::CF_ANY)
 * @return the cost of this operation or an error
 */
CommandCost CmdMassSendVehicleToDepot(DoCommandFlags flags, DepotCommandFlags depot_cmd, VehicleListIdentifier vli, CargoType cargo_filter)
{
	if ((depot_cmd & DepotCommandFlags{DepotCommandFlag::Service, DepotCommandFlag::Cancel, DepotCommandFlag::Sell}) != depot_cmd) return CMD_ERROR;
	if (!depot_cmd.Test(DepotCommandFlag::Cancel)) depot_cmd.Set(DepotCommandFlag::DontCancel);
	return SendAllVehiclesToDepot(flags, depot_cmd, vli, cargo_filter);
}

/**
 * Give a custom name to your vehicle
 * @param flags type of operation
 * @param veh_id vehicle ID to name
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameVehicle(DoCommandFlags flags, VehicleID veh_id, const std::string &text)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	bool reset = text.empty();

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;
		if (!flags.Test(DoCommandFlag::AutoReplace) && !IsUniqueVehicleName(text)) return CommandCost(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		if (reset) {
			v->name.clear();
		} else {
			v->name = text;
		}
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 1);
		InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
		MarkWholeScreenDirty();
	}

	return CommandCost();
}


/**
 * Change the service interval of a vehicle
 * @param flags type of operation
 * @param veh_id vehicle ID that is being service-interval-changed
 * @param serv_int new service interval
 * @param is_custom service interval is custom flag
 * @param is_percent service interval is percentage flag
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeServiceInt(DoCommandFlags flags, VehicleID veh_id, uint16_t serv_int, bool is_custom, bool is_percent)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	const Company *company = Company::Get(v->owner);
	is_percent = is_custom ? is_percent : company->settings.vehicle.servint_ispercent;

	if (is_custom) {
		if (serv_int != GetServiceIntervalClamped(serv_int, is_percent)) return CMD_ERROR;
	} else {
		serv_int = CompanyServiceInterval(company, v->type);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		v->SetServiceInterval(serv_int);
		v->SetServiceIntervalIsCustom(is_custom);
		v->SetServiceIntervalIsPercent(is_percent);
		SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	}

	return CommandCost();
}
