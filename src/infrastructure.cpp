/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file infrastructure.cpp Implementation of infrastructure sharing */

#include "stdafx.h"
#include "infrastructure_func.h"
#include "train.h"
#include "aircraft.h"
#include "error.h"
#include "vehicle_func.h"
#include "station_base.h"
#include "depot_base.h"
#include "pbs.h"
#include "signal_func.h"
#include "window_func.h"
#include "gui.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "company_base.h"
#include "string_func.h"
#include "scope_info.h"

#include "table/strings.h"

/**
 * Helper function for transferring sharing fees
 * @param v The vehicle involved
 * @param infra_owner The owner of the infrastructure
 * @param cost Amount to transfer as money fraction (shifted 8 bits to the left)
 */
static void PaySharingFee(Vehicle *v, Owner infra_owner, Money cost)
{
	Company *c = Company::Get(v->owner);
	if (!_settings_game.economy.sharing_payment_in_debt) {
		/* Do not allow fee payment to drop (money - loan) below 0. */
		cost = min(cost, (c->money - c->current_loan) << 8);
		if (cost <= 0) return;
	}
	v->profit_this_year -= cost;
	SubtractMoneyFromCompanyFract(v->owner, CommandCost(EXPENSES_SHARING_COST, cost));
	SubtractMoneyFromCompanyFract(infra_owner, CommandCost(EXPENSES_SHARING_INC, -cost));
}

/**
 * Pay the fee for spending a single tick inside a station.
 * @param v The vehicle that is using the station.
 * @param st The station that it uses.
 */
void PayStationSharingFee(Vehicle *v, const Station *st)
{
	if (v->owner == st->owner || st->owner == OWNER_NONE || v->type == VEH_TRAIN) return;
	Money cost = _settings_game.economy.sharing_fee[v->type];
	PaySharingFee(v, st->owner, (cost << 8) / DAY_TICKS);
}

uint16 is2_GetWeight(Train *v)
{
	uint16 weight = (CargoSpec::Get(v->cargo_type)->weight * v->cargo.StoredCount() * FreightWagonMult(v->cargo_type)) / 16;
		/* Vehicle weight is not added for articulated parts. */
	if (!v->IsArticulatedPart()) {
		weight += GetVehicleProperty(v, PROP_TRAIN_WEIGHT, RailVehInfo(v->engine_type)->weight);
	}
		/* Powered wagons have extra weight added. */
	if (HasBit(v->flags, VRF_POWEREDWAGON)) {
		weight += RailVehInfo(v->gcache.first_engine)->pow_wag_weight;
	}
		return weight;
}


/**
 * Pay the daily fee for trains on foreign tracks.
 * @param v The vehicle to pay the fee for.
 */
void PayDailyTrackSharingFee(Train *v)
{
	Owner owner = GetTileOwner(v->tile);
	if (owner == v->owner) return;
	Money cost = _settings_game.economy.sharing_fee[VEH_TRAIN] << 8;
	/* Cost is calculated per 1000 tonnes */
	cost = cost * is2_GetWeight(v) / 1000;
	/* Only pay the required fraction */
	cost = cost * v->running_ticks / DAY_TICKS;
	if (cost != 0) PaySharingFee(v, owner, cost);
}

/**
 * Check whether a vehicle is in an allowed position.
 * @param v     The vehicle to check.
 * @param owner Owner whose infrastructure is not allowed, because the company will be removed. Ignored if INVALID_OWNER.
 * @return      True if the vehicle is compeletely in an allowed position.
 */
static bool VehiclePositionIsAllowed(const Vehicle *v, Owner owner = INVALID_OWNER)
{
	if (!IsValidTile(v->tile)) return true;
	switch (v->type) {
		case VEH_TRAIN:
			if (HasBit(Train::From(v)->subtype, GVSF_VIRTUAL)) return true;
			for (const Vehicle *u = v; u != NULL; u = u->Next()) {
				if (!IsInfraTileUsageAllowed(VEH_TRAIN, v->owner, u->tile) || GetTileOwner(u->tile) == owner) return false;
			}
			return true;
		case VEH_ROAD:
			for (const Vehicle *u = v; u != NULL; u = u->Next()) {
				if (IsRoadDepotTile(u->tile) || IsStandardRoadStopTile(u->tile)) {
					if (!IsInfraTileUsageAllowed(VEH_ROAD, v->owner, u->tile) || GetTileOwner(u->tile) == owner) return false;
				}
			}
			return true;
		case VEH_SHIP:
			if (IsShipDepotTile(v->tile) && v->IsStoppedInDepot()) {
				if (!IsInfraTileUsageAllowed(VEH_SHIP, v->owner, v->tile) || GetTileOwner(v->tile) == owner) return false;
			}
			return true;
		case VEH_AIRCRAFT: {
			const Aircraft *a = Aircraft::From(v);
			if (a->state != FLYING && Station::IsValidID(a->targetairport)) {
				Owner station_owner = Station::Get(a->targetairport)->owner;
				if (!IsInfraUsageAllowed(VEH_AIRCRAFT, a->owner, station_owner) || station_owner == owner) return false;
			}
			return true;
		}
		default: return true;
	}
}

/**
 * Check whether an order has a destination that is allowed.
 * I.e. it refers to a station/depot/waypoint the vehicle is allowed to visit.
 * @param order The order to check
 * @param v     The vehicle this order belongs to.
 * @param owner Owner whose infrastructure is not allowed, because the company will be removed. Ignored if INVALID_OWNER.
 * @return      True if the order has an allowed destination.
 */
static bool OrderDestinationIsAllowed(const Order *order, const Vehicle *v, Owner owner = INVALID_OWNER)
{
	Owner dest_owner;
	switch (order->GetType()) {
		case OT_GOTO_STATION:
		case OT_GOTO_WAYPOINT: dest_owner = BaseStation::Get(order->GetDestination())->owner; break;
		case OT_GOTO_DEPOT:    dest_owner = (v->type == VEH_AIRCRAFT) ? Station::Get(order->GetDestination())->owner : GetTileOwner(Depot::Get(order->GetDestination())->xy); break;
		case OT_LOADING:       dest_owner = Station::Get(v->last_station_visited)->owner; break;
		default: return true;
	}
	return dest_owner != owner && IsInfraUsageAllowed(v->type, v->owner, dest_owner);
}

/**
 * Sell a vehicle, no matter where it may be.
 * @param v The vehicle to sell
 * @param give_money Do we actually need to give money to the vehicle owner?
 */
static void RemoveAndSellVehicle(Vehicle *v, bool give_money)
{
	assert(v->Previous() == NULL);

	if (give_money) {
		/* compute total value and give that to the owner */
		Money value = 0;
		for (Vehicle *u = v->First(); u != NULL; u = u->Next()) {
			value += v->value;
		}
		CompanyID old = _current_company;
		_current_company = v->owner;
		SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, -value));
		_current_company = old;
	}

	/* take special measures for trains, but not when sharing is disabled or when the train is a free wagon chain */
	if (_settings_game.economy.infrastructure_sharing[VEH_TRAIN] && v->type == VEH_TRAIN && Train::From(v)->IsFrontEngine()) {
 		DeleteVisibleTrain(Train::From(v));
	} else {
		delete v;
	}
}

/**
 * Check all path reservations, and reserve a new path if the current path is invalid.
 */
static void FixAllReservations()
{
	/* if this function is called, we can safely assume that sharing of rails is being switched off */
	assert(!_settings_game.economy.infrastructure_sharing[VEH_TRAIN]);
	Train *v;
	FOR_ALL_TRAINS(v) {
		if (!v->IsPrimaryVehicle() || (v->vehstatus & VS_CRASHED) != 0) continue;
		/* It might happen that the train reserved additional tracks,
		 * but FollowTrainReservation can't detect those because they are no longer reachable.
		 * detect this by first finding the end of the reservation,
		 * then switch sharing on and try again. If these two ends differ,
		 * unreserve the path, switch sharing off and try to reserve a new path */
		PBSTileInfo end_tile_info = FollowTrainReservation(v);

		/* first do a quick test to determine whether the next tile has any reservation at all */
		TileIndex next_tile = end_tile_info.tile + TileOffsByDiagDir(TrackdirToExitdir(end_tile_info.trackdir));
		/* If the next tile doesn't have a reservation at all, the reservation surely ends here. Thus all is well */
		if (GetReservedTrackbits(next_tile) == TRACK_BIT_NONE) continue;

		/* change sharing setting temporarily */
		_settings_game.economy.infrastructure_sharing[VEH_TRAIN] = true;
		PBSTileInfo end_tile_info2 = FollowTrainReservation(v);
		/* if these two reservation ends differ, unreserve the path and try to reserve a new path */
		if (end_tile_info.tile != end_tile_info2.tile || end_tile_info.trackdir != end_tile_info2.trackdir) {
			FreeTrainTrackReservation(v);
			_settings_game.economy.infrastructure_sharing[VEH_TRAIN] = false;
			TryPathReserve(v, true);
		} else {
			_settings_game.economy.infrastructure_sharing[VEH_TRAIN] = false;
		}
	}
}

/**
 * Check if a sharing change is possible.
 * If vehicles are still on others' infrastructure or using others' stations,
 * The change is not possible and false is returned.
 * @param type The type of vehicle whose setting will be changed.
 * @return True if the change can take place, false otherwise.
 */
bool CheckSharingChangePossible(VehicleType type)
{
	if (type != VEH_AIRCRAFT) YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
	/* Only do something when sharing is being disabled */
	if (_settings_game.economy.infrastructure_sharing[type]) return true;

	StringID error_message = STR_NULL;
	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (type != v->type) continue;
		if (v->Previous() != NULL) continue;

		/* Check vehicle positiion */
		if (!VehiclePositionIsAllowed(v)) {
			error_message = STR_CONFIG_SETTING_SHARING_USED_BY_VEHICLES;
			/* Break immediately, this error message takes precedence over the others. */
			break;
		}

		/* Check current order */
		if (!OrderDestinationIsAllowed(&v->current_order, v)) {
			error_message = STR_CONFIG_SETTING_SHARING_ORDERS_TO_OTHERS;
		}

		/* Check order list */
		if (v->FirstShared() != v) continue;
		Order *o;
		FOR_VEHICLE_ORDERS(v, o) {
			if (!OrderDestinationIsAllowed(o, v)) {
				error_message = STR_CONFIG_SETTING_SHARING_ORDERS_TO_OTHERS;
			}
		}
	}

	if (error_message != STR_NULL) {
		ShowErrorMessage(error_message, INVALID_STRING_ID, WL_ERROR);
		return false;
	}

	if (type == VEH_TRAIN) FixAllReservations();

	return true;
}

/**
 * Handle the removal (through reset_company or bankruptcy) of a company.
 * i.e. remove all vehicles owned by that company or on its infrastructure,
 * and delete all now-invalid orders.
 * @param Owner the company to be removed.
 */
void HandleSharingCompanyDeletion(Owner owner)
{
	YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);

	Vehicle *v = NULL;
	SCOPE_INFO_FMT([&v], "HandleSharingCompanyDeletion: veh: %s", scope_dumper().VehicleInfo(v));
	FOR_ALL_VEHICLES(v) {
		if (!IsCompanyBuildableVehicleType(v) || v->Previous() != NULL) continue;
		/* vehicle position */
		if (v->owner == owner || !VehiclePositionIsAllowed(v, owner)) {
			RemoveAndSellVehicle(v, v->owner != owner);
			continue;
		}
		/* current order */
		if (!OrderDestinationIsAllowed(&v->current_order, v, owner)) {
			if (v->current_order.IsType(OT_LOADING)) {
				v->LeaveStation();
			} else {
				v->current_order.MakeDummy();
			}
			SetWindowDirty(WC_VEHICLE_VIEW, v->index);
		}

		/* order list */
		if (v->FirstShared() != v) continue;

		Order *o = NULL;
		int id = -1;
		SCOPE_INFO_FMT([&], "HandleSharingCompanyDeletion: veh: %s, order: %d, %X", scope_dumper().VehicleInfo(v), id, o ? o->Pack() : 0);
		FOR_VEHICLE_ORDERS(v, o) {
			id++;
			if (OrderDestinationIsAllowed(o, v, owner)) continue;

			o->MakeDummy();
			for (const Vehicle *w = v; w != NULL; w = w->NextShared()) {
				/* In GUI, simulate by removing the order and adding it back */
				InvalidateVehicleOrder(w, id | (INVALID_VEH_ORDER_ID << 8));
				InvalidateVehicleOrder(w, (id << 8) | INVALID_VEH_ORDER_ID);
			}
		}
	}
}

/**
 * Update all block signals on the map.
 * To be called after the setting for sharing of rails changes.
 * @param owner Owner whose signals to update. If INVALID_OWNER, update everything.
 */
void UpdateAllBlockSignals(Owner owner)
{
	Owner last_owner = INVALID_OWNER;
	TileIndex tile = 0;
	do {
		if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile)) {
			Owner track_owner = GetTileOwner(tile);
			if (owner != INVALID_OWNER && track_owner != owner) continue;

			if (!IsOneSignalBlock(track_owner, last_owner)) {
				/* Cannot update signals of two different companies in one run,
				 * if these signal blocks are not joined */
				UpdateSignalsInBuffer();
				last_owner = track_owner;
			}
			TrackBits bits = GetTrackBits(tile);
			do {
				Track track = RemoveFirstTrack(&bits);
				if (HasSignalOnTrack(tile, track)) {
					AddTrackToSignalBuffer(tile, track, track_owner);
				}
			} while (bits != TRACK_BIT_NONE);
		} else if (IsLevelCrossingTile(tile) && (owner == INVALID_OWNER || GetTileOwner(tile) == owner)) {
			UpdateLevelCrossing(tile);
		}
	} while (++tile != MapSize());

	UpdateSignalsInBuffer();
}
