/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_sl.cpp Code handling saving and loading of vehicles */

#include "../stdafx.h"
#include "../vehicle_func.h"
#include "../train.h"
#include "../roadveh.h"
#include "../ship.h"
#include "../aircraft.h"
#include "../station_base.h"
#include "../effectvehicle_base.h"
#include "../company_base.h"
#include "../company_func.h"
#include "../disaster_vehicle.h"
#include "../scope_info.h"
#include "../string_func.h"
#include "../error.h"
#include "../strings_func.h"
#include "../3rdparty/cpp-btree/btree_map.h"
#include "../core/format.hpp"

#include "saveload.h"

#include <map>

#include "../safeguards.h"

extern btree::btree_multimap<VehicleID, PendingSpeedRestrictionChange> _pending_speed_restriction_change_map;

/**
 * Link front and rear multiheaded engines to each other
 * This is done when loading a savegame
 */
void ConnectMultiheadedTrains()
{
	for (Train *v : Train::Iterate()) {
		v->other_multiheaded_part = nullptr;
	}

	for (Train *v : Train::Iterate()) {
		if (v->IsFrontEngine() || v->IsFreeWagon()) {
			/* Two ways to associate multiheaded parts to each other:
			 * sequential-matching: Trains shall be arranged to look like <..>..<..>..<..>..
			 * bracket-matching:    Free vehicle chains shall be arranged to look like ..<..<..>..<..>..>..
			 *
			 * Note: Old savegames might contain chains which do not comply with these rules, e.g.
			 *   - the front and read parts have invalid orders
			 *   - different engine types might be combined
			 *   - there might be different amounts of front and rear parts.
			 *
			 * Note: The multiheaded parts need to be matched exactly like they are matched on the server, else desyncs will occur.
			 *   This is why two matching strategies are needed.
			 */

			bool sequential_matching = v->IsFrontEngine();

			for (Train *u = v; u != nullptr; u = u->GetNextVehicle()) {
				if (u->other_multiheaded_part != nullptr) continue; // we already linked this one

				if (u->IsMultiheaded()) {
					if (!u->IsEngine()) {
						/* we got a rear car without a front car. We will convert it to a front one */
						u->SetEngine();
						u->spritenum--;
					}

					/* Find a matching back part */
					EngineID eid = u->engine_type;
					Train *w;
					if (sequential_matching) {
						for (w = u->GetNextVehicle(); w != nullptr; w = w->GetNextVehicle()) {
							if (w->engine_type != eid || w->other_multiheaded_part != nullptr || !w->IsMultiheaded()) continue;

							/* we found a car to partner with this engine. Now we will make sure it face the right way */
							if (w->IsEngine()) {
								w->ClearEngine();
								w->spritenum++;
							}
							break;
						}
					} else {
						uint stack_pos = 0;
						for (w = u->GetNextVehicle(); w != nullptr; w = w->GetNextVehicle()) {
							if (w->engine_type != eid || w->other_multiheaded_part != nullptr || !w->IsMultiheaded()) continue;

							if (w->IsEngine()) {
								stack_pos++;
							} else {
								if (stack_pos == 0) break;
								stack_pos--;
							}
						}
					}

					if (w != nullptr) {
						w->other_multiheaded_part = u;
						u->other_multiheaded_part = w;
					} else {
						/* we got a front car and no rear cars. We will fake this one for forget that it should have been multiheaded */
						u->ClearMultiheaded();
					}
				}
			}
		}
	}
}

/**
 *  Converts all trains to the new subtype format introduced in savegame 16.2
 *  It also links multiheaded engines or make them forget they are multiheaded if no suitable partner is found
 */
void ConvertOldMultiheadToNew()
{
	for (Train *t : Train::Iterate()) SetBit(t->subtype, 7); // indicates that it's the old format and needs to be converted in the next loop

	for (Train *t : Train::Iterate()) {
		if (HasBit(t->subtype, 7) && ((t->subtype & ~0x80) == 0 || (t->subtype & ~0x80) == 4)) {
			for (Train *u = t; u != nullptr; u = u->Next()) {
				const RailVehicleInfo *rvi = RailVehInfo(u->engine_type);

				ClrBit(u->subtype, 7);
				switch (u->subtype) {
					case 0: // TS_Front_Engine
						if (rvi->railveh_type == RAILVEH_MULTIHEAD) u->SetMultiheaded();
						u->SetFrontEngine();
						u->SetEngine();
						break;

					case 1: // TS_Artic_Part
						u->subtype = 0;
						u->SetArticulatedPart();
						break;

					case 2: // TS_Not_First
						u->subtype = 0;
						if (rvi->railveh_type == RAILVEH_WAGON) {
							/* normal wagon */
							u->SetWagon();
							break;
						}
						if (rvi->railveh_type == RAILVEH_MULTIHEAD && rvi->image_index == u->spritenum - 1) {
							/* rear end of a multiheaded engine */
							u->SetMultiheaded();
							break;
						}
						if (rvi->railveh_type == RAILVEH_MULTIHEAD) u->SetMultiheaded();
						u->SetEngine();
						break;

					case 4: // TS_Free_Car
						u->subtype = 0;
						u->SetWagon();
						u->SetFreeWagon();
						break;
					default: SlErrorCorrupt("Invalid train subtype");
				}
			}
		}
	}
}


/** need to be called to load aircraft from old version */
void UpdateOldAircraft()
{
	/* set airport_flags to 0 for all airports just to be sure */
	for (Station *st : Station::Iterate()) {
		st->airport.flags = 0; // reset airport
	}

	for (Aircraft *a : Aircraft::Iterate()) {
		/* airplane has another vehicle with subtype 4 (shadow), helicopter also has 3 (rotor)
		 * skip those */
		if (a->IsNormalAircraft()) {
			/* airplane in terminal stopped doesn't hurt anyone, so goto next */
			if ((a->vehstatus & VS_STOPPED) && a->state == 0) {
				a->state = HANGAR;
				continue;
			}

			AircraftLeaveHangar(a, a->direction); // make airplane visible if it was in a depot for example
			a->vehstatus &= ~VS_STOPPED; // make airplane moving
			UpdateAircraftCache(a);
			a->cur_speed = a->vcache.cached_max_speed; // so aircraft don't have zero speed while in air
			if (!a->current_order.IsType(OT_GOTO_STATION) && !a->current_order.IsType(OT_GOTO_DEPOT)) {
				/* reset current order so aircraft doesn't have invalid "station-only" order */
				a->current_order.MakeDummy();
			}
			a->state = FLYING;
			AircraftNextAirportPos_and_Order(a); // move it to the entry point of the airport
			GetNewVehiclePosResult gp = GetNewVehiclePos(a);
			a->tile = 0; // aircraft in air is tile=0

			/* correct speed of helicopter-rotors */
			if (a->subtype == AIR_HELICOPTER) a->Next()->Next()->cur_speed = 32;

			/* set new position x,y,z */
			GetAircraftFlightLevelBounds(a, &a->z_pos, nullptr);
			SetAircraftPosition(a, gp.x, gp.y, GetAircraftFlightLevel(a));
		}
	}
}

/**
 * Check all vehicles to ensure their engine type is valid
 * for the currently loaded NewGRFs (that includes none...)
 * This only makes a difference if NewGRFs are missing, otherwise
 * all vehicles will be valid. This does not make such a game
 * playable, it only prevents crash.
 */
static void CheckValidVehicles()
{
	size_t total_engines = Engine::GetPoolSize();
	EngineID first_engine[4] = { INVALID_ENGINE, INVALID_ENGINE, INVALID_ENGINE, INVALID_ENGINE };

	for (const Engine *e : Engine::IterateType(VEH_TRAIN)) { first_engine[VEH_TRAIN] = e->index; break; }
	for (const Engine *e : Engine::IterateType(VEH_ROAD)) { first_engine[VEH_ROAD] = e->index; break; }
	for (const Engine *e : Engine::IterateType(VEH_SHIP)) { first_engine[VEH_SHIP] = e->index; break; }
	for (const Engine *e : Engine::IterateType(VEH_AIRCRAFT)) { first_engine[VEH_AIRCRAFT] = e->index; break; }

	for (Vehicle *v : Vehicle::Iterate()) {
		/* Test if engine types match */
		switch (v->type) {
			case VEH_TRAIN:
			case VEH_ROAD:
			case VEH_SHIP:
			case VEH_AIRCRAFT:
				if (v->engine_type >= total_engines || v->type != v->GetEngine()->type) {
					v->engine_type = first_engine[v->type];
				}
				break;

			default:
				break;
		}
	}
}

extern byte _age_cargo_skip_counter; // From misc_sl.cpp

static std::vector<Vehicle *> _load_invalid_vehicles_to_delete;

/** Called after load to update coordinates */
void AfterLoadVehicles(bool part_of_load)
{
	_load_invalid_vehicles_to_delete.clear();

	const Vehicle *si_v = nullptr;
	SCOPE_INFO_FMT([&si_v], "AfterLoadVehicles: %s", scope_dumper().VehicleInfo(si_v));
	for (Vehicle *v : Vehicle::Iterate()) {
		si_v = v;
		/* Reinstate the previous pointer */
		if (v->Next() != nullptr) {
			v->Next()->previous = v;
			if (v->type == VEH_TRAIN && (HasBit(v->subtype, GVSF_VIRTUAL) != HasBit(v->Next()->subtype, GVSF_VIRTUAL))) {
				SlErrorCorrupt("Mixed virtual/non-virtual train consist");
			}
		}
		if (v->NextShared() != nullptr) v->NextShared()->previous_shared = v;

		if (part_of_load) v->fill_percent_te_id = INVALID_TE_ID;
		v->first = nullptr;
		if (v->IsGroundVehicle()) v->GetGroundVehicleCache()->first_engine = INVALID_ENGINE;
	}

	/* AfterLoadVehicles may also be called in case of NewGRF reload, in this
	 * case we may not convert orders again. */
	if (part_of_load) {
		/* Create shared vehicle chain for very old games (pre 5,2) and create
		 * OrderList from shared vehicle chains. For this to work correctly, the
		 * following conditions must be fulfilled:
		 * a) both next_shared and previous_shared are not set for pre 5,2 games
		 * b) both next_shared and previous_shared are set for later games
		 */
		std::map<Order*, OrderList*> mapping;

		for (Vehicle *v : Vehicle::Iterate()) {
			si_v = v;
			if (v->old_orders != nullptr) {
				if (IsSavegameVersionBefore(SLV_105)) { // Pre-105 didn't save an OrderList
					if (mapping[v->old_orders] == nullptr) {
						/* This adds the whole shared vehicle chain for case b */

						/* Creating an OrderList here is safe because the number of vehicles
						 * allowed in these savegames matches the number of OrderLists. As
						 * such each vehicle can get an OrderList and it will (still) fit. */
						assert(OrderList::CanAllocateItem());
						v->orders = mapping[v->old_orders] = new OrderList(v->old_orders, v);
					} else {
						v->orders = mapping[v->old_orders];
						/* For old games (case a) we must create the shared vehicle chain */
						if (IsSavegameVersionBefore(SLV_5, 2)) {
							v->AddToShared(v->orders->GetFirstSharedVehicle());
						}
					}
				} else { // OrderList was saved as such, only recalculate not saved values
					if (v->PreviousShared() == nullptr) {
						v->orders->Initialize(v->orders->first, v);
					}
				}
			}
		}
	}

	for (Vehicle *v : Vehicle::Iterate()) {
		si_v = v;
		/* Fill the first pointers */
		if (v->Previous() == nullptr) {
			for (Vehicle *u = v; u != nullptr; u = u->Next()) {
				u->first = v;
			}
		}
	}

	if (part_of_load) {
		if (IsSavegameVersionBefore(SLV_105)) {
			/* Before 105 there was no order for shared orders, thus it messed up horribly */
			for (Vehicle *v : Vehicle::Iterate()) {
				si_v = v;
				if (v->First() != v || v->orders != nullptr || v->previous_shared != nullptr || v->next_shared == nullptr) continue;

				/* As above, allocating OrderList here is safe. */
				assert(OrderList::CanAllocateItem());
				v->orders = new OrderList(nullptr, v);
				for (Vehicle *u = v; u != nullptr; u = u->next_shared) {
					u->orders = v->orders;
				}
			}
		}

		if (IsSavegameVersionBefore(SLV_157)) {
			/* The road vehicle subtype was converted to a flag. */
			for (RoadVehicle *rv : RoadVehicle::Iterate()) {
				si_v = rv;
				if (rv->subtype == 0) {
					/* The road vehicle is at the front. */
					rv->SetFrontEngine();
				} else if (rv->subtype == 1) {
					/* The road vehicle is an articulated part. */
					rv->subtype = 0;
					rv->SetArticulatedPart();
				} else {
					SlErrorCorrupt("Invalid road vehicle subtype");
				}
			}
		}

		if (IsSavegameVersionBefore(SLV_160)) {
			/* In some old savegames there might be some "crap" stored. */
			for (Vehicle *v : Vehicle::Iterate()) {
				si_v = v;
				if (!v->IsPrimaryVehicle() && v->type != VEH_DISASTER) {
					v->current_order.Free();
					v->unitnumber = 0;
				}
			}
		}

		if (IsSavegameVersionBefore(SLV_162)) {
			/* Set the vehicle-local cargo age counter from the old global counter. */
			for (Vehicle *v : Vehicle::Iterate()) {
				si_v = v;
				v->cargo_age_counter = _age_cargo_skip_counter;
			}
		}

		if (IsSavegameVersionBefore(SLV_180)) {
			/* Set service interval flags */
			for (Vehicle *v : Vehicle::Iterate()) {
				si_v = v;
				if (!v->IsPrimaryVehicle()) continue;

				const Company *c = Company::Get(v->owner);
				int interval = CompanyServiceInterval(c, v->type);

				v->SetServiceIntervalIsCustom(v->GetServiceInterval() != interval);
				v->SetServiceIntervalIsPercent(c->settings.vehicle.servint_ispercent);
			}
		}

		if (IsSavegameVersionBefore(SLV_SHIP_ROTATION)) {
			/* Ship rotation added */
			for (Ship *s : Ship::Iterate()) {
				s->rotation = s->direction;
			}
		} else {
			for (Ship *s : Ship::Iterate()) {
				if (s->rotation == s->direction) continue;
				/* In case we are rotating on gameload, set the rotation position to
				 * the current position, otherwise the applied workaround offset would
				 * be with respect to 0,0.
				 */
				s->rotation_x_pos = s->x_pos;
				s->rotation_y_pos = s->y_pos;
			}
		}

		if (SlXvIsFeaturePresent(XSLFI_TEMPLATE_REPLACEMENT) && (_network_server || !_networking)) {
			for (Train *t : Train::Iterate()) {
				si_v = t;
				if (t->IsVirtual() && t->First() == t) {
					delete t;
				}
			}
		}
	}
	si_v = nullptr;

	CheckValidVehicles();

	for (Vehicle *v : Vehicle::Iterate()) {
		si_v = v;
		assert(v->first != nullptr);

		v->trip_occupancy = CalcPercentVehicleFilled(v, nullptr);

		switch (v->type) {
			case VEH_TRAIN: {
				Train *t = Train::From(v);
				if (t->IsFrontEngine() || t->IsFreeWagon()) {
					t->gcache.last_speed = t->cur_speed; // update displayed train speed
					t->ConsistChanged(CCF_SAVELOAD);
				}
				break;
			}

			case VEH_ROAD: {
				RoadVehicle *rv = RoadVehicle::From(v);
				if (rv->IsFrontEngine()) {
					rv->gcache.last_speed = rv->cur_speed; // update displayed road vehicle speed

					rv->roadtype = Engine::Get(rv->engine_type)->u.road.roadtype;
					rv->compatible_roadtypes = GetRoadTypeInfo(rv->roadtype)->powered_roadtypes;
					bool is_invalid = false;
					for (RoadVehicle *u = rv; u != nullptr; u = u->Next()) {
						u->roadtype = rv->roadtype;
						u->compatible_roadtypes = rv->compatible_roadtypes;
						if (GetRoadType(u->tile, GetRoadTramType(u->roadtype)) == INVALID_ROADTYPE) is_invalid = true;
					}

					if (is_invalid && part_of_load) {
						_load_invalid_vehicles_to_delete.push_back(rv);
						break;
					}

					RoadVehUpdateCache(rv);
					if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
						rv->CargoChanged();
					}
				}
				break;
			}

			case VEH_SHIP:
				if (Ship::From(v)->IsPrimaryVehicle()) {
					Ship::From(v)->UpdateCache();
				}
				break;

			default: break;
		}
	}

	/* Stop non-front engines */
	if (part_of_load && IsSavegameVersionBefore(SLV_112)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			si_v = v;
			if (v->type == VEH_TRAIN) {
				Train *t = Train::From(v);
				if (!t->IsFrontEngine()) {
					if (t->IsEngine()) t->vehstatus |= VS_STOPPED;
					/* cur_speed is now relevant for non-front parts - nonzero breaks
					 * moving-wagons-inside-depot- and autoreplace- code */
					t->cur_speed = 0;
				}
			}
			/* trains weren't stopping gradually in old OTTD versions (and TTO/TTD)
			 * other vehicle types didn't have zero speed while stopped (even in 'recent' OTTD versions) */
			if ((v->vehstatus & VS_STOPPED) && (v->type != VEH_TRAIN || IsSavegameVersionBefore(SLV_2, 1))) {
				v->cur_speed = 0;
			}
		}
	}

	for (Vehicle *v : Vehicle::Iterate()) {
		si_v = v;
		switch (v->type) {
			case VEH_ROAD:
			case VEH_TRAIN:
			case VEH_SHIP:
				v->GetImage(v->direction, EIT_ON_MAP, &v->sprite_seq);
				v->UpdateSpriteSeqBound();
				break;

			case VEH_AIRCRAFT:
				if (Aircraft::From(v)->IsNormalAircraft()) {
					v->GetImage(v->direction, EIT_ON_MAP, &v->sprite_seq);
					v->UpdateSpriteSeqBound();

					/* The aircraft's shadow will have the same image as the aircraft, but no colour */
					Vehicle *shadow = v->Next();
					if (shadow == nullptr) SlErrorCorrupt("Missing shadow for aircraft");
					shadow->sprite_seq.CopyWithoutPalette(v->sprite_seq);
					shadow->sprite_seq_bounds = v->sprite_seq_bounds;

					/* In the case of a helicopter we will update the rotor sprites */
					if (v->subtype == AIR_HELICOPTER) {
						Vehicle *rotor = shadow->Next();
						if (rotor == nullptr) SlErrorCorrupt("Missing rotor for helicopter");
						GetRotorImage(Aircraft::From(v), EIT_ON_MAP, &rotor->sprite_seq);
						rotor->UpdateSpriteSeqBound();
					}

					UpdateAircraftCache(Aircraft::From(v), true);
				}
				break;
			default: break;
		}

		v->UpdateDeltaXY();
		v->coord.left = INVALID_COORD;
		v->UpdatePosition();
		v->UpdateViewport(false);
		v->cargo.AssertCountConsistency();
	}
}

void AfterLoadVehiclesRemoveAnyFoundInvalid()
{
	if (!_load_invalid_vehicles_to_delete.empty()) {
		DEBUG(sl, 0, "Removing %u vehicles found to be uncorrectably invalid during load", (uint)_load_invalid_vehicles_to_delete.size());
		SetDParam(0, (uint)_load_invalid_vehicles_to_delete.size());
		ShowErrorMessage(STR_WARNING_LOADGAME_REMOVED_UNCORRECTABLE_VEHICLES, INVALID_STRING_ID, WL_CRITICAL);
		GroupStatistics::UpdateAfterLoad();
	}

	for (Vehicle *v : _load_invalid_vehicles_to_delete) {
		delete v;
	}
	_load_invalid_vehicles_to_delete.clear();
}

bool TrainController(Train *v, Vehicle *nomove, bool reverse = true); // From train_cmd.cpp
void ReverseTrainDirection(Train *v);
void ReverseTrainSwapVeh(Train *v, int l, int r);

/** Fixup old train spacing. */
void FixupTrainLengths()
{
	/* Vehicle center was moved from 4 units behind the front to half the length
	 * behind the front. Move vehicles so they end up on the same spot. */
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->type == VEH_TRAIN && v->IsPrimaryVehicle()) {
			/* The vehicle center is now more to the front depending on vehicle length,
			 * so we need to move all vehicles forward to cover the difference to the
			 * old center, otherwise wagon spacing in trains would be broken upon load. */
			for (Train *u = Train::From(v); u != nullptr; u = u->Next()) {
				if (u->track == TRACK_BIT_DEPOT || (u->vehstatus & VS_CRASHED)) continue;

				Train *next = u->Next();

				/* Try to pull the vehicle half its length forward. */
				int diff = (VEHICLE_LENGTH - u->gcache.cached_veh_length) / 2;
				int done;
				for (done = 0; done < diff; done++) {
					if (!TrainController(u, next, false)) break;
				}

				if (next != nullptr && done < diff && u->IsFrontEngine()) {
					/* Pulling the front vehicle forwards failed, we either encountered a dead-end
					 * or a red signal. To fix this, we try to move the whole train the required
					 * space backwards and re-do the fix up of the front vehicle. */

					/* Ignore any signals when backtracking. */
					TrainForceProceeding old_tfp = u->force_proceed;
					u->force_proceed = TFP_SIGNAL;

					/* Swap start<>end, start+1<>end-1, ... */
					int r = CountVehiclesInChain(u) - 1; // number of vehicles - 1
					int l = 0;
					do ReverseTrainSwapVeh(u, l++, r--); while (l <= r);

					/* We moved the first vehicle which is now the last. Move it back to the
					 * original position as we will fix up the last vehicle later in the loop. */
					for (int i = 0; i < done; i++) TrainController(u->Last(), nullptr);

					/* Move the train backwards to get space for the first vehicle. As the stopping
					 * distance from a line end is rounded up, move the train one unit more to cater
					 * for front vehicles with odd lengths. */
					int moved;
					for (moved = 0; moved < diff + 1; moved++) {
						if (!TrainController(u, nullptr, false)) break;
					}

					/* Swap start<>end, start+1<>end-1, ... again. */
					r = CountVehiclesInChain(u) - 1; // number of vehicles - 1
					l = 0;
					do ReverseTrainSwapVeh(u, l++, r--); while (l <= r);

					u->force_proceed = old_tfp;

					/* Tracks are too short to fix the train length. The player has to fix the
					 * train in a depot. Bail out so we don't damage the vehicle chain any more. */
					if (moved < diff + 1) break;

					/* Re-do the correction for the first vehicle. */
					for (done = 0; done < diff; done++) TrainController(u, next, false);

					/* We moved one unit more backwards than needed for even-length front vehicles,
					 * try to move that unit forward again. We don't care if this step fails. */
					TrainController(u, nullptr, false);
				}

				/* If the next wagon is still in a depot, check if it shouldn't be outside already. */
				if (next != nullptr && next->track == TRACK_BIT_DEPOT) {
					int d = TicksToLeaveDepot(u);
					if (d <= 0) {
						/* Next vehicle should have left the depot already, show it and pull forward. */
						next->vehstatus &= ~VS_HIDDEN;
						next->track = TrackToTrackBits(GetRailDepotTrack(next->tile));
						for (int i = 0; i >= d; i--) TrainController(next, nullptr);
					}
				}
			}

			/* Update all cached properties after moving the vehicle chain around. */
			Train::From(v)->ConsistChanged(CCF_TRACK);
		}
	}
}

static uint8  _cargo_days;
static uint16 _cargo_source;
static uint32 _cargo_source_xy;
static uint16 _cargo_count;
static uint16 _cargo_paid_for;
static Money  _cargo_feeder_share;
static uint32 _cargo_loaded_at_xy;
CargoPacketList _cpp_packets;
std::map<VehicleID, CargoPacketList> _veh_cpp_packets;
static std::vector<Trackdir> _path_td;
static std::vector<TileIndex> _path_tile;
static uint32 _path_layout_ctr;

static uint32 _old_ahead_separation;

/**
 * Make it possible to make the saveload tables "friends" of other classes.
 * @param vt the vehicle type. Can be VEH_END for the common vehicle description data
 * @return the saveload description
 */
SaveLoadTable GetVehicleDescription(VehicleType vt)
{
	/** Save and load of vehicles */
	static const SaveLoad _common_veh_desc[] = {
		     SLE_VAR(Vehicle, subtype,               SLE_UINT8),

		     SLE_REF(Vehicle, next,                  REF_VEHICLE_OLD),
		 SLE_CONDVAR(Vehicle, name,                  SLE_CNAME,                    SL_MIN_VERSION,  SLV_84),
		 SLE_CONDSTR(Vehicle, name,                  SLE_STR | SLF_ALLOW_CONTROL, 0, SLV_84, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, unitnumber,            SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_8),
		 SLE_CONDVAR(Vehicle, unitnumber,            SLE_UINT16,                   SLV_8, SL_MAX_VERSION),
		     SLE_VAR(Vehicle, owner,                 SLE_UINT8),
		 SLE_CONDVAR(Vehicle, tile,                  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, tile,                  SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, dest_tile,             SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, dest_tile,             SLE_UINT32,                   SLV_6, SL_MAX_VERSION),

		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, z_pos,                SLE_FILE_U8  | SLE_VAR_I32,   SL_MIN_VERSION, SLV_164, SlXvFeatureTest(XSLFTO_AND, XSLFI_ZPOS_32_BIT, 0, 0)),
		SLE_CONDVAR_X(Vehicle, z_pos,                SLE_INT32,                    SLV_164, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_ZPOS_32_BIT)),
		     SLE_VAR(Vehicle, direction,             SLE_UINT8),

		SLE_CONDNULL(2,                                                            SL_MIN_VERSION,  SLV_58),
		     SLE_VAR(Vehicle, spritenum,             SLE_UINT8),
		SLE_CONDNULL(5,                                                            SL_MIN_VERSION,  SLV_58),
		     SLE_VAR(Vehicle, engine_type,           SLE_UINT16),

		SLE_CONDNULL(2,                                                            SL_MIN_VERSION,  SLV_152),
		     SLE_VAR(Vehicle, cur_speed,             SLE_UINT16),
		     SLE_VAR(Vehicle, subspeed,              SLE_UINT8),
		     SLE_VAR(Vehicle, acceleration,          SLE_UINT8),
		 SLE_CONDVAR(Vehicle, motion_counter,        SLE_UINT32,                   SLV_VEH_MOTION_COUNTER, SL_MAX_VERSION),
		     SLE_VAR(Vehicle, progress,              SLE_UINT8),

		     SLE_VAR(Vehicle, vehstatus,             SLE_UINT8),
		 SLE_CONDVAR(Vehicle, last_station_visited,  SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_5),
		 SLE_CONDVAR(Vehicle, last_station_visited,  SLE_UINT16,                   SLV_5, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, last_loading_station, SLE_UINT16,                 SLV_182, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_CHILLPP, SL_CHILLPP_232)),

		     SLE_VAR(Vehicle, cargo_type,            SLE_UINT8),
		 SLE_CONDVAR(Vehicle, cargo_subtype,         SLE_UINT8,                   SLV_35, SL_MAX_VERSION),
		SLEG_CONDVAR(         _cargo_days,           SLE_UINT8,                    SL_MIN_VERSION,  SLV_68),
		SLEG_CONDVAR(         _cargo_source,         SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_7),
		SLEG_CONDVAR(         _cargo_source,         SLE_UINT16,                   SLV_7,  SLV_68),
		SLEG_CONDVAR(         _cargo_source_xy,      SLE_UINT32,                  SLV_44,  SLV_68),
		     SLE_VAR(Vehicle, cargo_cap,             SLE_UINT16),
		 SLE_CONDVAR(Vehicle, refit_cap,             SLE_UINT16,                 SLV_182, SL_MAX_VERSION),
		SLEG_CONDVAR(         _cargo_count,          SLE_UINT16,                   SL_MIN_VERSION,  SLV_68),
		SLE_CONDPTRDEQ(Vehicle, cargo.packets,         REF_CARGO_PACKET,            SLV_68, SL_MAX_VERSION),
		SLEG_CONDPTRDEQ_X(    _cpp_packets,            REF_CARGO_PACKET,           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
		 SLE_CONDARR(Vehicle, cargo.action_counts,   SLE_UINT, VehicleCargoList::NUM_MOVE_TO_ACTION, SLV_181, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, cargo_age_counter,     SLE_UINT16,                 SLV_162, SL_MAX_VERSION),

		     SLE_VAR(Vehicle, day_counter,           SLE_UINT8),
		     SLE_VAR(Vehicle, tick_counter,          SLE_UINT8),
		SLE_CONDVAR_X(Vehicle, running_ticks,        SLE_FILE_U8  | SLE_VAR_U16,  SLV_88, SL_MAX_VERSION, SlXvFeatureTest([](uint16 version, bool version_in_range, const std::array<uint16, XSLFI_SIZE> &feature_versions) -> bool {
			return version_in_range && !(SlXvIsFeaturePresent(feature_versions, XSLFI_SPRINGPP, 3) || SlXvIsFeaturePresent(feature_versions, XSLFI_JOKERPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_CHILLPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_VARIABLE_DAY_LENGTH, 2));
		})),
		SLE_CONDVAR_X(Vehicle, running_ticks,        SLE_UINT16,                  SLV_88, SL_MAX_VERSION, SlXvFeatureTest([](uint16 version, bool version_in_range, const std::array<uint16, XSLFI_SIZE> &feature_versions) -> bool {
			return version_in_range && (SlXvIsFeaturePresent(feature_versions, XSLFI_SPRINGPP, 2) || SlXvIsFeaturePresent(feature_versions, XSLFI_JOKERPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_CHILLPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_VARIABLE_DAY_LENGTH, 2));
		})),

		     SLE_VAR(Vehicle, cur_implicit_order_index,   SLE_VEHORDERID),
		 SLE_CONDVAR(Vehicle, cur_real_order_index,       SLE_VEHORDERID,        SLV_158, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, cur_timetable_order_index, SLE_VEHORDERID, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA)),
		/* num_orders is now part of OrderList and is not saved but counted */
		SLE_CONDNULL(1,                                                            SL_MIN_VERSION, SLV_105),

		/* This next line is for version 4 and prior compatibility.. it temporarily reads
		 type and flags (which were both 4 bits) into type. Later on this is
		 converted correctly */
		 SLE_CONDVAR(Vehicle, current_order.type,    SLE_UINT8,                    SL_MIN_VERSION,   SLV_5),
		 SLE_CONDVAR(Vehicle, current_order.dest,    SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION,   SLV_5),

		/* Orders for version 5 and on */
		 SLE_CONDVAR(Vehicle, current_order.type,    SLE_UINT8,                    SLV_5, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, current_order.flags,  SLE_FILE_U8 | SLE_VAR_U16,    SLV_5, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 0, 0)),
		SLE_CONDVAR_X(Vehicle, current_order.flags,  SLE_UINT16,                   SLV_5, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 1)),
		 SLE_CONDVAR(Vehicle, current_order.dest,    SLE_UINT16,                   SLV_5, SL_MAX_VERSION),

		/* Refit in current order */
		 SLE_CONDVAR(Vehicle, current_order.refit_cargo,   SLE_UINT8,             SLV_36, SL_MAX_VERSION),
		SLE_CONDNULL(1,                                                           SLV_36, SLV_182), // refit_subtype

		/* Timetable in current order */
		SLE_CONDVAR_X(Vehicle, current_order.wait_time,    SLE_FILE_U16 | SLE_VAR_U32, SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5)),
		SLE_CONDVAR_X(Vehicle, current_order.wait_time,    SLE_UINT32,                 SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6)),
		SLE_CONDVAR_X(Vehicle, current_order.travel_time,  SLE_FILE_U16 | SLE_VAR_U32, SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5)),
		SLE_CONDVAR_X(Vehicle, current_order.travel_time,  SLE_UINT32,                 SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6)),
		 SLE_CONDVAR(Vehicle, current_order.max_speed,     SLE_UINT16,           SLV_174, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, timetable_start,       SLE_INT32,                  SLV_129, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, timetable_start_subticks,   SLE_UINT16,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 2)),

		 SLE_CONDREF(Vehicle, orders,                REF_ORDER,                    SL_MIN_VERSION, SLV_105),
		 SLE_CONDREF(Vehicle, orders,                REF_ORDERLIST,              SLV_105, SL_MAX_VERSION),

		 SLE_CONDVAR(Vehicle, age,                   SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		 SLE_CONDVAR(Vehicle, age,                   SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, max_age,               SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		 SLE_CONDVAR(Vehicle, max_age,               SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, date_of_last_service,  SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		 SLE_CONDVAR(Vehicle, date_of_last_service,  SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, date_of_last_service_newgrf, SLE_INT32,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEWGRF_LAST_SERVICE)),
		 SLE_CONDVAR(Vehicle, service_interval,      SLE_UINT16,                   SL_MIN_VERSION,  SLV_31),
		 SLE_CONDVAR(Vehicle, service_interval,      SLE_FILE_U32 | SLE_VAR_U16,  SLV_31, SLV_180),
		 SLE_CONDVAR(Vehicle, service_interval,      SLE_UINT16,                 SLV_180, SL_MAX_VERSION),
		     SLE_VAR(Vehicle, reliability,           SLE_UINT16),
		     SLE_VAR(Vehicle, reliability_spd_dec,   SLE_UINT16),
		     SLE_VAR(Vehicle, breakdown_ctr,         SLE_UINT8),
		     SLE_VAR(Vehicle, breakdown_delay,       SLE_UINT8),
		     SLE_VAR(Vehicle, breakdowns_since_last_service, SLE_UINT8),
		     SLE_VAR(Vehicle, breakdown_chance,      SLE_UINT8),
		SLE_CONDVAR_X(Vehicle, breakdown_chance_factor, SLE_UINT8,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 3)),
		SLE_CONDVAR_X(Vehicle, breakdown_type,       SLE_UINT8,                    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS)),
		SLE_CONDVAR_X(Vehicle, breakdown_severity,   SLE_UINT8,                    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS)),
		 SLE_CONDVAR(Vehicle, build_year,            SLE_FILE_U8 | SLE_VAR_I32,    SL_MIN_VERSION,  SLV_31),
		 SLE_CONDVAR(Vehicle, build_year,            SLE_INT32,                   SLV_31, SL_MAX_VERSION),

		     SLE_VAR(Vehicle, load_unload_ticks,     SLE_UINT16),
		SLEG_CONDVAR(         _cargo_paid_for,       SLE_UINT16,                  SLV_45, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, vehicle_flags,         SLE_FILE_U8  | SLE_VAR_U32,  SLV_40, SLV_180),
		SLE_CONDVAR_X(Vehicle, vehicle_flags,        SLE_FILE_U16 | SLE_VAR_U32,          SLV_180, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 0, 0)),
		SLE_CONDVAR_X(Vehicle, vehicle_flags,        SLE_UINT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 1)),

		 SLE_CONDVAR(Vehicle, profit_this_year,      SLE_FILE_I32 | SLE_VAR_I64,   SL_MIN_VERSION,  SLV_65),
		 SLE_CONDVAR(Vehicle, profit_this_year,      SLE_INT64,                   SLV_65, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, profit_last_year,      SLE_FILE_I32 | SLE_VAR_I64,   SL_MIN_VERSION,  SLV_65),
		 SLE_CONDVAR(Vehicle, profit_last_year,      SLE_INT64,                   SLV_65, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle,profit_lifetime,       SLE_INT64,                    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEH_LIFETIME_PROFIT)),
		SLEG_CONDVAR(         _cargo_feeder_share,   SLE_FILE_I32 | SLE_VAR_I64,  SLV_51,  SLV_65),
		SLEG_CONDVAR(         _cargo_feeder_share,   SLE_INT64,                   SLV_65,  SLV_68),
		SLEG_CONDVAR(         _cargo_loaded_at_xy,   SLE_UINT32,                  SLV_51,  SLV_68),
		 SLE_CONDVAR(Vehicle, value,                 SLE_FILE_I32 | SLE_VAR_I64,   SL_MIN_VERSION,  SLV_65),
		 SLE_CONDVAR(Vehicle, value,                 SLE_INT64,                   SLV_65, SL_MAX_VERSION),
		SLE_CONDNULL_X(8,                                                          SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_REPAIR_COST, 1, 1)),

		SLE_CONDVAR_X(Vehicle, random_bits,          SLE_FILE_U8 | SLE_VAR_U16,    SLV_2, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTEND_VEHICLE_RANDOM, 0, 0)),
		SLE_CONDVAR_X(Vehicle, random_bits,          SLE_UINT16,                   SLV_2, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTEND_VEHICLE_RANDOM, 1)),
		 SLE_CONDVAR(Vehicle, waiting_triggers,      SLE_UINT8,                    SLV_2, SL_MAX_VERSION),

		SLEG_CONDVAR_X(_old_ahead_separation,        SLE_UINT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE, 1, 4)),
		SLE_CONDNULL_X(4,                                                          SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE, 1, 4)),

		 SLE_CONDREF(Vehicle, next_shared,           REF_VEHICLE,                  SLV_2, SL_MAX_VERSION),
		SLE_CONDNULL(2,                                                            SLV_2,  SLV_69),
		SLE_CONDNULL(4,                                                           SLV_69, SLV_101),

		 SLE_CONDVAR(Vehicle, group_id,              SLE_UINT16,                  SLV_60, SL_MAX_VERSION),

		 SLE_CONDVAR(Vehicle, current_order_time,    SLE_UINT32,                  SLV_67, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, current_loading_time, SLE_UINT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE)),
		SLE_CONDVAR_X(Vehicle, current_loading_time, SLE_UINT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_23)),
		SLE_CONDVAR_X(Vehicle, last_loading_tick,    SLE_UINT64,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LAST_LOADING_TICK)),
		SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		 SLE_CONDVAR(Vehicle, lateness_counter,      SLE_INT32,                   SLV_67, SL_MAX_VERSION),

		SLE_CONDNULL(10,                                                           SLV_2, SLV_144), // old reserved space

		SLE_CONDNULL_X((8 + 8 + 2 + 2 + 4 + 4 + 1 + 1) * 30, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		SLE_CONDNULL_X((8 + 8 + 2 + 2 + 4 + 4 + 1 + 1) * 70, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4)),
		SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),

		SLE_CONDNULL_X(160, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	};

	static const SaveLoad _train_desc[] = {
		SLE_WRITEBYTE(Vehicle, type),
		SLE_VEH_INCLUDE(),
		     SLE_VAR(Train, crash_anim_pos,      SLE_UINT16),
		     SLE_VAR(Train, force_proceed,       SLE_UINT8),
		     SLE_VAR(Train, railtype,            SLE_UINT8),
		     SLE_VAR(Train, track,               SLE_UINT8),

		 SLE_CONDVAR(Train, flags,               SLE_FILE_U8  | SLE_VAR_U32,            SLV_2, SLV_100),
		SLE_CONDVAR_X(Train, flags,              SLE_FILE_U16 | SLE_VAR_U32,          SLV_100, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_FLAGS_EXTRA, 0, 0)),
		SLE_CONDVAR_X(Train, flags,              SLE_UINT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_FLAGS_EXTRA, 1)),
		SLE_CONDNULL(2, SLV_2, SLV_60),

		 SLE_CONDVAR(Train, wait_counter,        SLE_UINT16,                 SLV_136, SL_MAX_VERSION),
		 SLE_CONDVAR_X(Train, tunnel_bridge_signal_num, SLE_UINT16,   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SIG_TUNNEL_BRIDGE, 5)),

		SLE_CONDNULL(2, SLV_2, SLV_20),
		 SLE_CONDVAR(Train, gv_flags,            SLE_UINT16,                 SLV_139, SL_MAX_VERSION),
		 SLE_CONDNULL_X(2                                           , SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
		SLE_CONDNULL(11, SLV_2, SLV_144), // old reserved space
		SLE_CONDVAR_X(Train, reverse_distance,    SLE_UINT16,         SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REVERSE_AT_WAYPOINT)),
		SLE_CONDVAR_X(Train, speed_restriction,   SLE_UINT16,         SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPEED_RESTRICTION)),
		SLE_CONDVAR_X(Train, signal_speed_restriction, SLE_UINT16,    SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_SPEED_ADAPTATION)),
		SLE_CONDVAR_X(Train, critical_breakdown_count, SLE_UINT8,     SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 2)),
	};

	static const SaveLoad _roadveh_desc[] = {
		SLE_WRITEBYTE(Vehicle, type),
		SLE_VEH_INCLUDE(),
		      SLE_VAR(RoadVehicle, state,                SLE_UINT8),
		      SLE_VAR(RoadVehicle, frame,                SLE_UINT8),
		      SLE_VAR(RoadVehicle, blocked_ctr,          SLE_UINT16),
		      SLE_VAR(RoadVehicle, overtaking,           SLE_UINT8),
		      SLE_VAR(RoadVehicle, overtaking_ctr,       SLE_UINT8),
		      SLE_VAR(RoadVehicle, crashed_ctr,          SLE_UINT16),
		      SLE_VAR(RoadVehicle, reverse_ctr,          SLE_UINT8),
		SLEG_CONDVARVEC(_path_td,                        SLE_UINT8,                  SLV_ROADVEH_PATH_CACHE, SL_MAX_VERSION),
		SLEG_CONDVARVEC(_path_tile,                      SLE_UINT32,                 SLV_ROADVEH_PATH_CACHE, SL_MAX_VERSION),
		SLEG_CONDVAR_X(_path_layout_ctr,                 SLE_UINT32,                     SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_LAYOUT_CHANGE_CTR)),

		 SLE_CONDNULL(2,                                                               SLV_6,  SLV_69),
		  SLE_CONDVAR(RoadVehicle, gv_flags,             SLE_UINT16,                 SLV_139, SL_MAX_VERSION),
		 SLE_CONDNULL(4,                                                              SLV_69, SLV_131),
		 SLE_CONDNULL(2,                                                               SLV_6, SLV_131),
		 SLE_CONDNULL(16,                                                              SLV_2, SLV_144), // old reserved space
		SLE_CONDVAR_X(RoadVehicle, critical_breakdown_count, SLE_UINT8,       SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 6)),
	};

	static const SaveLoad _ship_desc[] = {
		SLE_WRITEBYTE(Vehicle, type),
		SLE_VEH_INCLUDE(),
		      SLE_VAR(Ship, state,                     SLE_UINT8),
		SLE_CONDDEQUE(Ship, path,                      SLE_UINT8,                  SLV_SHIP_PATH_CACHE, SL_MAX_VERSION),
		  SLE_CONDVAR(Ship, rotation,                  SLE_UINT8,                  SLV_SHIP_ROTATION, SL_MAX_VERSION),
		SLE_CONDVAR_X(Ship, lost_count,                SLE_UINT8,                     SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SHIP_LOST_COUNTER)),
		SLE_CONDVAR_X(Ship, critical_breakdown_count,  SLE_UINT8,                     SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 8)),

		SLE_CONDNULL(16, SLV_2, SLV_144), // old reserved space
	};

	static const SaveLoad _aircraft_desc[] = {
		SLE_WRITEBYTE(Vehicle, type),
		SLE_VEH_INCLUDE(),
		     SLE_VAR(Aircraft, crashed_counter,       SLE_UINT16),
		     SLE_VAR(Aircraft, pos,                   SLE_UINT8),

		 SLE_CONDVAR(Aircraft, targetairport,         SLE_FILE_U8  | SLE_VAR_U16,   SL_MIN_VERSION, SLV_5),
		 SLE_CONDVAR(Aircraft, targetairport,         SLE_UINT16,                   SLV_5, SL_MAX_VERSION),

		     SLE_VAR(Aircraft, state,                 SLE_UINT8),

		 SLE_CONDVAR(Aircraft, previous_pos,          SLE_UINT8,                    SLV_2, SL_MAX_VERSION),
		 SLE_CONDVAR(Aircraft, last_direction,        SLE_UINT8,                    SLV_2, SL_MAX_VERSION),
		 SLE_CONDVAR(Aircraft, number_consecutive_turns, SLE_UINT8,                 SLV_2, SL_MAX_VERSION),
		 SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		 SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),

		 SLE_CONDVAR(Aircraft, turn_counter,          SLE_UINT8,                  SLV_136, SL_MAX_VERSION),
		 SLE_CONDVAR(Aircraft, flags,                 SLE_UINT8,                  SLV_167, SL_MAX_VERSION),

		SLE_CONDNULL(13,                                                           SLV_2, SLV_144), // old reserved space
	};

	static const SaveLoad _special_desc[] = {
		SLE_WRITEBYTE(Vehicle, type),

		     SLE_VAR(Vehicle, subtype,               SLE_UINT8),

		SLE_CONDNULL_X(5, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),

		 SLE_CONDVAR(Vehicle, tile,                  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, tile,                  SLE_UINT32,                   SLV_6, SL_MAX_VERSION),

		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, z_pos,                SLE_FILE_U8  | SLE_VAR_I32,   SL_MIN_VERSION, SLV_164, SlXvFeatureTest(XSLFTO_AND, XSLFI_ZPOS_32_BIT, 0, 0)),
		SLE_CONDVAR_X(Vehicle, z_pos,                SLE_INT32,                    SLV_164, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_ZPOS_32_BIT)),

		     SLE_VAR(Vehicle, sprite_seq.seq[0].sprite, SLE_FILE_U16 | SLE_VAR_U32),
		SLE_CONDNULL(5,                                                            SL_MIN_VERSION,  SLV_59),
		     SLE_VAR(Vehicle, progress,              SLE_UINT8),
		     SLE_VAR(Vehicle, vehstatus,             SLE_UINT8),

		     SLE_VAR(EffectVehicle, animation_state,    SLE_UINT16),
		     SLE_VAR(EffectVehicle, animation_substate, SLE_UINT8),

		 SLE_CONDVAR(Vehicle, spritenum,             SLE_UINT8,                    SLV_2, SL_MAX_VERSION),

		SLE_CONDNULL(15,                                                           SLV_2, SLV_144), // old reserved space
	};

	static const SaveLoad _disaster_desc[] = {
		SLE_WRITEBYTE(Vehicle, type),

		     SLE_REF(Vehicle, next,                  REF_VEHICLE_OLD),

		     SLE_VAR(Vehicle, subtype,               SLE_UINT8),
		 SLE_CONDVAR(Vehicle, tile,                  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, tile,                  SLE_UINT32,                   SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, dest_tile,             SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, dest_tile,             SLE_UINT32,                   SLV_6, SL_MAX_VERSION),

		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, x_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_FILE_I16 | SLE_VAR_I32,   SL_MIN_VERSION,   SLV_6),
		 SLE_CONDVAR(Vehicle, y_pos,                 SLE_INT32,                    SLV_6, SL_MAX_VERSION),
		SLE_CONDVAR_X(Vehicle, z_pos,                SLE_FILE_U8  | SLE_VAR_I32,   SL_MIN_VERSION, SLV_164, SlXvFeatureTest(XSLFTO_AND, XSLFI_ZPOS_32_BIT, 0, 0)),
		SLE_CONDVAR_X(Vehicle, z_pos,                SLE_INT32,                    SLV_164, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_ZPOS_32_BIT)),
		     SLE_VAR(Vehicle, direction,             SLE_UINT8),

		SLE_CONDNULL(5,                                                            SL_MIN_VERSION,  SLV_58),
		     SLE_VAR(Vehicle, owner,                 SLE_UINT8),
		     SLE_VAR(Vehicle, vehstatus,             SLE_UINT8),
		SLE_CONDVAR_X(Vehicle, current_order.dest,    SLE_FILE_U8 | SLE_VAR_U16,    SL_MIN_VERSION,          SLV_5, SlXvFeatureTest(XSLFTO_AND, XSLFI_DISASTER_VEH_STATE, 0, 0)),
		SLE_CONDVAR_X(Vehicle, current_order.dest,    SLE_UINT16,                            SLV_5, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_DISASTER_VEH_STATE, 0, 0)),
		SLE_CONDVAR_X(DisasterVehicle, state,         SLE_UINT16,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_DISASTER_VEH_STATE, 1)),

		     SLE_VAR(Vehicle, sprite_seq.seq[0].sprite, SLE_FILE_U16 | SLE_VAR_U32),
		 SLE_CONDVAR(Vehicle, age,                   SLE_FILE_U16 | SLE_VAR_I32,   SL_MIN_VERSION,  SLV_31),
		 SLE_CONDVAR(Vehicle, age,                   SLE_INT32,                   SLV_31, SL_MAX_VERSION),
		     SLE_VAR(Vehicle, tick_counter,          SLE_UINT8),

		 SLE_CONDVAR(DisasterVehicle, image_override,            SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION, SLV_191),
		 SLE_CONDVAR(DisasterVehicle, image_override,            SLE_UINT32,                 SLV_191, SL_MAX_VERSION),
		 SLE_CONDVAR(DisasterVehicle, big_ufo_destroyer_target,  SLE_FILE_U16 | SLE_VAR_U32,   SL_MIN_VERSION, SLV_191),
		 SLE_CONDVAR(DisasterVehicle, big_ufo_destroyer_target,  SLE_UINT32,                 SLV_191, SL_MAX_VERSION),
		 SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
		 SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP)),
		 SLE_CONDVAR(DisasterVehicle, flags,                     SLE_UINT8,                  SLV_194, SL_MAX_VERSION),

		SLE_CONDNULL(16,                                                           SLV_2, SLV_144), // old reserved space
	};


	static const SaveLoadTable _veh_descs[] = {
		_train_desc,
		_roadveh_desc,
		_ship_desc,
		_aircraft_desc,
		_special_desc,
		_disaster_desc,
		_common_veh_desc,
	};

	return _veh_descs[vt];
}

static std::vector<SaveLoad> _filtered_train_desc;
static std::vector<SaveLoad> _filtered_roadveh_desc;
static std::vector<SaveLoad> _filtered_ship_desc;
static std::vector<SaveLoad> _filtered_aircraft_desc;
static std::vector<SaveLoad> _filtered_special_desc;
static std::vector<SaveLoad> _filtered_disaster_desc;

static std::vector<SaveLoad> * const _filtered_veh_descs[] = {
	&_filtered_train_desc,
	&_filtered_roadveh_desc,
	&_filtered_ship_desc,
	&_filtered_aircraft_desc,
	&_filtered_special_desc,
	&_filtered_disaster_desc,
};

const SaveLoadTable GetVehicleDescriptionFiltered(VehicleType vt)
{
	return *(_filtered_veh_descs[vt]);
}

static void SetupDescs_VEHS()
{
	for (size_t i = 0; i < lengthof(_filtered_veh_descs); i++) {
		*(_filtered_veh_descs[i]) = SlFilterObject(GetVehicleDescription((VehicleType) i));
	}
}

/** Will be called when the vehicles need to be saved. */
static void Save_VEHS()
{
	SetupDescs_VEHS();
	/* Write the vehicles */
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->type == VEH_ROAD) {
			_path_td.clear();
			_path_tile.clear();
			_path_layout_ctr = 0;

			RoadVehicle *rv = RoadVehicle::From(v);
			if (rv->cached_path != nullptr && !rv->cached_path->empty()) {
				uint idx = rv->cached_path->start;
				for (uint i = 0; i < rv->cached_path->size(); i++) {
					_path_td.push_back(rv->cached_path->td[idx]);
					_path_tile.push_back(rv->cached_path->tile[idx]);
					idx = (idx + 1) & RV_PATH_CACHE_SEGMENT_MASK;
				}
				_path_layout_ctr = rv->cached_path->layout_ctr;
			}
		}
		SlSetArrayIndex(v->index);
		SlObjectSaveFiltered(v, GetVehicleDescriptionFiltered(v->type));
	}
}

/** Will be called when vehicles need to be loaded. */
void Load_VEHS()
{
	SetupDescs_VEHS();

	int index;

	_cargo_count = 0;

	_cpp_packets.clear();
	_veh_cpp_packets.clear();

	_path_td.clear();
	_path_tile.clear();
	_path_layout_ctr = 0;

	while ((index = SlIterateArray()) != -1) {
		Vehicle *v;
		VehicleType vtype = (VehicleType)SlReadByte();

		switch (vtype) {
			case VEH_TRAIN:    v = new (index) Train();           break;
			case VEH_ROAD:     v = new (index) RoadVehicle();     break;
			case VEH_SHIP:     v = new (index) Ship();            break;
			case VEH_AIRCRAFT: v = new (index) Aircraft();        break;
			case VEH_EFFECT:   v = new (index) EffectVehicle();   break;
			case VEH_DISASTER: v = new (index) DisasterVehicle(); break;
			case VEH_INVALID: // Savegame shouldn't contain invalid vehicles
			default: SlErrorCorrupt("Invalid vehicle type");
		}

		SlObjectLoadFiltered(v, GetVehicleDescriptionFiltered(vtype));

		if (_cargo_count != 0 && IsCompanyBuildableVehicleType(v) && CargoPacket::CanAllocateItem()) {
			/* Don't construct the packet with station here, because that'll fail with old savegames */
			CargoPacket *cp = new CargoPacket(_cargo_count, _cargo_days, _cargo_source, _cargo_source_xy, _cargo_loaded_at_xy, _cargo_feeder_share);
			v->cargo.Append(cp);
		}

		/* Old savegames used 'last_station_visited = 0xFF' */
		if (IsSavegameVersionBefore(SLV_5) && v->last_station_visited == 0xFF) {
			v->last_station_visited = INVALID_STATION;
		}

		if (IsSavegameVersionBefore(SLV_182) && !SlXvIsFeaturePresent(XSLFI_CHILLPP)) v->last_loading_station = INVALID_STATION;

		if (IsSavegameVersionBefore(SLV_5)) {
			/* Convert the current_order.type (which is a mix of type and flags, because
			 *  in those versions, they both were 4 bits big) to type and flags */
			v->current_order.flags = GB(v->current_order.type, 4, 4);
			v->current_order.type &= 0x0F;
		}

		/* Advanced vehicle lists got added */
		if (IsSavegameVersionBefore(SLV_60)) v->group_id = DEFAULT_GROUP;

		if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
			_veh_cpp_packets[index] = std::move(_cpp_packets);
			_cpp_packets.clear();
		}

		if (SlXvIsFeaturePresent(XSLFI_AUTO_TIMETABLE, 1, 4)) {
			SB(v->vehicle_flags, VF_SEPARATION_ACTIVE, 1, _old_ahead_separation ? 1 : 0);
		}

		if (vtype == VEH_ROAD && !_path_td.empty() && _path_td.size() <= RV_PATH_CACHE_SEGMENTS && _path_td.size() == _path_tile.size()) {
			RoadVehicle *rv = RoadVehicle::From(v);
			rv->cached_path.reset(new RoadVehPathCache());
			rv->cached_path->count = _path_td.size();
			for (size_t i = 0; i < _path_td.size(); i++) {
				rv->cached_path->td[i] = _path_td[i];
				rv->cached_path->tile[i] = _path_tile[i];
			}
			rv->cached_path->layout_ctr = _path_layout_ctr;
		}
	}
}

static void Ptrs_VEHS()
{
	SetupDescs_VEHS();

	for (Vehicle *v : Vehicle::Iterate()) {
		if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) _cpp_packets = std::move(_veh_cpp_packets[v->index]);
		SlObjectPtrOrNullFiltered(v, GetVehicleDescriptionFiltered(v->type));
		if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) _veh_cpp_packets[v->index] = std::move(_cpp_packets);
	}
}

const SaveLoadTable GetOrderExtraInfoDescription();

void Save_VEOX()
{
	/* save extended order info for vehicle current order */
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->current_order.extra) {
			SlSetArrayIndex(v->index);
			SlObject(v->current_order.extra.get(), GetOrderExtraInfoDescription());
		}
	}
}

void Load_VEOX()
{
	/* load extended order info for vehicle current order */
	int index;
	while ((index = SlIterateArray()) != -1) {
		Vehicle *v = Vehicle::GetIfValid(index);
		assert(v != nullptr);
		v->current_order.AllocExtraInfo();
		SlObject(v->current_order.extra.get(), GetOrderExtraInfoDescription());
	}
}

const SaveLoadTable GetVehicleSpeedRestrictionDescription()
{
	static const SaveLoad _vehicle_speed_restriction_desc[] = {
		     SLE_VAR(PendingSpeedRestrictionChange, distance,                 SLE_UINT16),
		     SLE_VAR(PendingSpeedRestrictionChange, new_speed,                SLE_UINT16),
		     SLE_VAR(PendingSpeedRestrictionChange, prev_speed,               SLE_UINT16),
		     SLE_VAR(PendingSpeedRestrictionChange, flags,                    SLE_UINT16),
	};

	return _vehicle_speed_restriction_desc;
}

void Save_VESR()
{
	for (auto &it : _pending_speed_restriction_change_map) {
		SlSetArrayIndex(it.first);
		PendingSpeedRestrictionChange *ptr = &(it.second);
		SlObject(ptr, GetVehicleSpeedRestrictionDescription());
	}
}

void Load_VESR()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		auto iter = _pending_speed_restriction_change_map.insert({ static_cast<VehicleID>(index), {} });
		PendingSpeedRestrictionChange *ptr = &(iter->second);
		SlObject(ptr, GetVehicleSpeedRestrictionDescription());
	}
}

struct vehicle_venc {
	VehicleID id;
	VehicleCache vcache;
};

struct train_venc {
	VehicleID id;
	GroundVehicleCache gvcache;
	int cached_curve_speed_mod;
	uint8 cached_tflags;
	uint8 cached_num_engines;
	uint16 cached_centre_mass;
	uint16 cached_braking_length;
	uint16 cached_veh_weight;
	uint16 cached_uncapped_decel;
	uint8 cached_deceleration;
	byte user_def_data;
	int cached_max_curve_speed;
};

struct roadvehicle_venc {
	VehicleID id;
	GroundVehicleCache gvcache;
};

struct aircraft_venc {
	VehicleID id;
	uint16 cached_max_range;
};

static std::vector<vehicle_venc> _vehicle_vencs;
static std::vector<train_venc> _train_vencs;
static std::vector<roadvehicle_venc> _roadvehicle_vencs;
static std::vector<aircraft_venc> _aircraft_vencs;

void Save_VENC()
{
	assert(_sl_xv_feature_versions[XSLFI_VENC_CHUNK] != 0);

	if (!IsNetworkServerSave()) {
		SlSetLength(0);
		return;
	}

	SlAutolength([](void *) {
		int types[4] = {};
		int total = 0;
		for (Vehicle *v : Vehicle::Iterate()) {
			total++;
			if (v->type < VEH_COMPANY_END) types[v->type]++;
		}

		/* vehicle cache */
		SlWriteUint32(total);
		for (Vehicle *v : Vehicle::Iterate()) {
			SlWriteUint32(v->index);
			SlWriteUint16(v->vcache.cached_max_speed);
			SlWriteUint16(v->vcache.cached_cargo_age_period);
			SlWriteByte(v->vcache.cached_vis_effect);
			SlWriteByte(v->vcache.cached_veh_flags);
		}

		auto write_gv_cache = [&](const GroundVehicleCache &cache) {
			SlWriteUint32(cache.cached_weight);
			SlWriteUint32(cache.cached_slope_resistance);
			SlWriteUint32(cache.cached_max_te);
			SlWriteUint32(cache.cached_axle_resistance);
			SlWriteUint32(cache.cached_max_track_speed);
			SlWriteUint32(cache.cached_power);
			SlWriteUint32(cache.cached_air_drag);
			SlWriteUint16(cache.cached_total_length);
			SlWriteUint16(cache.first_engine);
			SlWriteByte(cache.cached_veh_length);
		};

		/* train */
		SlWriteUint32(types[VEH_TRAIN]);
		for (Train *t : Train::Iterate()) {
			SlWriteUint32(t->index);
			write_gv_cache(t->gcache);
			SlWriteUint32(t->tcache.cached_curve_speed_mod);
			SlWriteByte(t->tcache.cached_tflags);
			SlWriteByte(t->tcache.cached_num_engines);
			SlWriteUint16(t->tcache.cached_centre_mass);
			SlWriteUint16(t->tcache.cached_braking_length);
			SlWriteUint16(t->tcache.cached_veh_weight);
			SlWriteUint16(t->tcache.cached_uncapped_decel);
			SlWriteByte(t->tcache.cached_deceleration);
			SlWriteByte(t->tcache.user_def_data);
			SlWriteUint32(t->tcache.cached_max_curve_speed);
		}

		/* road vehicle */
		SlWriteUint32(types[VEH_ROAD]);
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			SlWriteUint32(rv->index);
			write_gv_cache(rv->gcache);
		}

		/* aircraft */
		SlWriteUint32(types[VEH_AIRCRAFT]);
		for (Aircraft *a : Aircraft::Iterate()) {
			SlWriteUint32(a->index);
			SlWriteUint16(a->acache.cached_max_range);
		}
	}, nullptr);
}

void Load_VENC()
{
	if (SlGetFieldLength() == 0) return;

	if (!_networking || _network_server) {
		SlSkipBytes(SlGetFieldLength());
		return;
	}

	_vehicle_vencs.resize(SlReadUint32());
	for (vehicle_venc &venc : _vehicle_vencs) {
		venc.id = SlReadUint32();
		venc.vcache.cached_max_speed = SlReadUint16();
		venc.vcache.cached_cargo_age_period = SlReadUint16();
		venc.vcache.cached_vis_effect = SlReadByte();
		venc.vcache.cached_veh_flags = SlReadByte();
	}

	auto read_gv_cache = [&](GroundVehicleCache &cache) {
		cache.cached_weight = SlReadUint32();
		cache.cached_slope_resistance = SlReadUint32();
		cache.cached_max_te = SlReadUint32();
		cache.cached_axle_resistance = SlReadUint32();
		cache.cached_max_track_speed = SlReadUint32();
		cache.cached_power = SlReadUint32();
		cache.cached_air_drag = SlReadUint32();
		cache.cached_total_length = SlReadUint16();
		cache.first_engine = SlReadUint16();
		cache.cached_veh_length = SlReadByte();
	};

	_train_vencs.resize(SlReadUint32());
	for (train_venc &venc : _train_vencs) {
		venc.id = SlReadUint32();
		read_gv_cache(venc.gvcache);
		venc.cached_curve_speed_mod = SlReadUint32();
		venc.cached_tflags = SlReadByte();
		venc.cached_num_engines = SlReadByte();
		venc.cached_centre_mass = SlReadUint16();
		venc.cached_braking_length = SlReadUint16();
		venc.cached_veh_weight = SlReadUint16();
		venc.cached_uncapped_decel = SlReadUint16();
		venc.cached_deceleration = SlReadByte();
		venc.user_def_data = SlReadByte();
		venc.cached_max_curve_speed = SlReadUint32();
	}

	_roadvehicle_vencs.resize(SlReadUint32());
	for (roadvehicle_venc &venc : _roadvehicle_vencs) {
		venc.id = SlReadUint32();
		read_gv_cache(venc.gvcache);
	}

	_aircraft_vencs.resize(SlReadUint32());
	for (aircraft_venc &venc : _aircraft_vencs) {
		venc.id = SlReadUint32();
		venc.cached_max_range = SlReadUint16();
	}
}

void SlResetVENC()
{
	_vehicle_vencs.clear();
	_train_vencs.clear();
	_roadvehicle_vencs.clear();
	_aircraft_vencs.clear();
}

static void LogVehicleVENCMessage(const Vehicle *v, const char *var)
{
	char log_buffer[1024];

	char *p = log_buffer + seprintf(log_buffer, lastof(log_buffer), "[load]: vehicle cache mismatch: %s", var);

	extern void WriteVehicleInfo(char *&p, const char *last, const Vehicle *u, const Vehicle *v, uint length);
	uint length = 0;
	for (const Vehicle *u = v->First(); u != v; u = u->Next()) {
		length++;
	}
	WriteVehicleInfo(p, lastof(log_buffer), v, v->First(), length);
	DEBUG(desync, 0, "%s", log_buffer);
	LogDesyncMsg(log_buffer);
}

template <typename T>
void CheckVehicleVENCProp(T &v_prop, T venc_prop, const Vehicle *v, const char *var)
{
	if (v_prop != venc_prop) {
		std::string data = fmt::format("{} [{:X} != {:X}]", var, v_prop, venc_prop);
		v_prop = venc_prop;
		LogVehicleVENCMessage(v, data.c_str());
	}
}

void SlProcessVENC()
{
	for (const vehicle_venc &venc : _vehicle_vencs) {
		Vehicle *v = Vehicle::GetIfValid(venc.id);
		if (v == nullptr) continue;
		CheckVehicleVENCProp(v->vcache.cached_max_speed, venc.vcache.cached_max_speed, v, "cached_max_speed");
		CheckVehicleVENCProp(v->vcache.cached_cargo_age_period, venc.vcache.cached_cargo_age_period, v, "cached_cargo_age_period");
		CheckVehicleVENCProp(v->vcache.cached_vis_effect, venc.vcache.cached_vis_effect, v, "cached_vis_effect");
		if (HasBit(v->vcache.cached_veh_flags ^ venc.vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT)) {
			SB(v->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT, 1, HasBit(venc.vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT) ? 1 : 0);
			LogVehicleVENCMessage(v, "VCF_LAST_VISUAL_EFFECT");
		}
	}

	auto check_gv_cache = [&](GroundVehicleCache &v_gvcache, const GroundVehicleCache &venc_gvcache, const Vehicle *v) {
		CheckVehicleVENCProp(v_gvcache.cached_weight, venc_gvcache.cached_weight, v, "cached_weight");
		CheckVehicleVENCProp(v_gvcache.cached_slope_resistance, venc_gvcache.cached_slope_resistance, v, "cached_slope_resistance");
		CheckVehicleVENCProp(v_gvcache.cached_max_te, venc_gvcache.cached_max_te, v, "cached_max_te");
		CheckVehicleVENCProp(v_gvcache.cached_axle_resistance, venc_gvcache.cached_axle_resistance, v, "cached_axle_resistance");
		CheckVehicleVENCProp(v_gvcache.cached_max_track_speed, venc_gvcache.cached_max_track_speed, v, "cached_max_track_speed");
		CheckVehicleVENCProp(v_gvcache.cached_power, venc_gvcache.cached_power, v, "cached_power");
		CheckVehicleVENCProp(v_gvcache.cached_air_drag, venc_gvcache.cached_air_drag, v, "cached_air_drag");
		CheckVehicleVENCProp(v_gvcache.cached_total_length, venc_gvcache.cached_total_length, v, "cached_total_length");
		CheckVehicleVENCProp(v_gvcache.first_engine, venc_gvcache.first_engine, v, "first_engine");
		CheckVehicleVENCProp(v_gvcache.cached_veh_length, venc_gvcache.cached_veh_length, v, "cached_veh_length");
	};

	for (const train_venc &venc : _train_vencs) {
		Train *t = Train::GetIfValid(venc.id);
		if (t == nullptr) continue;
		check_gv_cache(t->gcache, venc.gvcache, t);
		CheckVehicleVENCProp(t->tcache.cached_curve_speed_mod, venc.cached_curve_speed_mod, t, "cached_curve_speed_mod");
		CheckVehicleVENCProp(t->tcache.cached_tflags, (TrainCacheFlags)venc.cached_tflags, t, "cached_tflags");
		CheckVehicleVENCProp(t->tcache.cached_num_engines, venc.cached_num_engines, t, "cached_num_engines");
		CheckVehicleVENCProp(t->tcache.cached_centre_mass, venc.cached_centre_mass, t, "cached_centre_mass");
		CheckVehicleVENCProp(t->tcache.cached_braking_length, venc.cached_braking_length, t, "cached_braking_length");
		CheckVehicleVENCProp(t->tcache.cached_veh_weight, venc.cached_veh_weight, t, "cached_veh_weight");
		CheckVehicleVENCProp(t->tcache.cached_uncapped_decel, venc.cached_uncapped_decel, t, "cached_uncapped_decel");
		CheckVehicleVENCProp(t->tcache.cached_deceleration, venc.cached_deceleration, t, "cached_deceleration");
		CheckVehicleVENCProp(t->tcache.user_def_data, venc.user_def_data, t, "user_def_data");
		CheckVehicleVENCProp(t->tcache.cached_max_curve_speed, venc.cached_max_curve_speed, t, "cached_max_curve_speed");
	}

	for (const roadvehicle_venc &venc : _roadvehicle_vencs) {
		RoadVehicle *rv = RoadVehicle::GetIfValid(venc.id);
		if (rv == nullptr) continue;
		check_gv_cache(rv->gcache, venc.gvcache, rv);
	}

	for (const aircraft_venc &venc : _aircraft_vencs) {
		Aircraft *a = Aircraft::GetIfValid(venc.id);
		if (a == nullptr) continue;
		if (a->acache.cached_max_range != venc.cached_max_range) {
			a->acache.cached_max_range = venc.cached_max_range;
			a->acache.cached_max_range_sqr = venc.cached_max_range * venc.cached_max_range;
			LogVehicleVENCMessage(a, "cached_max_range");
		}
	}
}

static ChunkSaveLoadSpecialOpResult Special_VENC(uint32 chunk_id, ChunkSaveLoadSpecialOp op)
{
	switch (op) {
		case CSLSO_SHOULD_SAVE_CHUNK:
			if (_sl_xv_feature_versions[XSLFI_VENC_CHUNK] == 0) return CSLSOR_DONT_SAVE_CHUNK;
			break;

		default:
			break;
	}
	return CSLSOR_NONE;
}

const SaveLoadTable GetVehicleLookAheadDescription()
{
	static const SaveLoad _vehicle_look_ahead_desc[] = {
		     SLE_VAR(TrainReservationLookAhead, reservation_end_tile,         SLE_UINT32),
		     SLE_VAR(TrainReservationLookAhead, reservation_end_trackdir,     SLE_UINT8),
		     SLE_VAR(TrainReservationLookAhead, current_position,             SLE_INT32),
		     SLE_VAR(TrainReservationLookAhead, reservation_end_position,     SLE_INT32),
		SLE_CONDVAR_X(TrainReservationLookAhead, lookahead_end_position,      SLE_INT32,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 9)),
		     SLE_VAR(TrainReservationLookAhead, reservation_end_z,            SLE_INT16),
		     SLE_VAR(TrainReservationLookAhead, tunnel_bridge_reserved_tiles, SLE_INT16),
		     SLE_VAR(TrainReservationLookAhead, flags,                        SLE_UINT16),
		     SLE_VAR(TrainReservationLookAhead, speed_restriction,            SLE_UINT16),
		SLE_CONDVAR_X(TrainReservationLookAhead, next_extend_position,        SLE_INT32,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 5)),
		SLE_CONDVAR_X(TrainReservationLookAhead, cached_zpos,                 SLE_INT32,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 6)),
		SLE_CONDVAR_X(TrainReservationLookAhead, zpos_refresh_remaining,      SLE_UINT8,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 6)),
	};

	return _vehicle_look_ahead_desc;
}

const SaveLoadTable GetVehicleLookAheadItemDescription()
{
	static const SaveLoad _vehicle_look_ahead_item_desc[] = {
		     SLE_VAR(TrainReservationLookAheadItem, start,                    SLE_INT32),
		     SLE_VAR(TrainReservationLookAheadItem, end,                      SLE_INT32),
		     SLE_VAR(TrainReservationLookAheadItem, z_pos,                    SLE_INT16),
		     SLE_VAR(TrainReservationLookAheadItem, data_id,                  SLE_UINT16),
		SLE_CONDVAR_X(TrainReservationLookAheadItem, data_aux,                SLE_UINT16,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 9)),
		     SLE_VAR(TrainReservationLookAheadItem, type,                     SLE_UINT8),
	};

	return _vehicle_look_ahead_item_desc;
}

const SaveLoadTable GetVehicleLookAheadCurveDescription()
{
	static const SaveLoad _vehicle_look_ahead_curve_desc[] = {
		     SLE_VAR(TrainReservationLookAheadCurve, position,                SLE_INT32),
		     SLE_VAR(TrainReservationLookAheadCurve, dir_diff,                SLE_UINT8),
	};

	return _vehicle_look_ahead_curve_desc;
}

static void RealSave_VLKA(TrainReservationLookAhead *lookahead)
{
	SlObject(lookahead, GetVehicleLookAheadDescription());
	SlWriteUint32((uint32)lookahead->items.size());
	for (TrainReservationLookAheadItem &item : lookahead->items) {
		SlObject(&item, GetVehicleLookAheadItemDescription());
	}
	SlWriteUint32((uint32)lookahead->curves.size());
	for (TrainReservationLookAheadCurve &curve : lookahead->curves) {
		SlObject(&curve, GetVehicleLookAheadCurveDescription());
	}
}

void Save_VLKA()
{
	for (Train *t : Train::Iterate()) {
		if (t->lookahead != nullptr) {
			SlSetArrayIndex(t->index);
			SlAutolength((AutolengthProc*) RealSave_VLKA, t->lookahead.get());
		}
	}
}

void Load_VLKA()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Train *t = Train::GetIfValid(index);
		assert(t != nullptr);
		t->lookahead.reset(new TrainReservationLookAhead());
		SlObject(t->lookahead.get(), GetVehicleLookAheadDescription());
		uint32 items = SlReadUint32();
		t->lookahead->items.resize(items);
		for (uint i = 0; i < items; i++) {
			SlObject(&t->lookahead->items[i], GetVehicleLookAheadItemDescription());
		}
		uint32 curves = SlReadUint32();
		t->lookahead->curves.resize(curves);
		for (uint i = 0; i < curves; i++) {
			SlObject(&t->lookahead->curves[i], GetVehicleLookAheadCurveDescription());
		}
	}
}

static const ChunkHandler veh_chunk_handlers[] = {
	{ 'VEHS', Save_VEHS, Load_VEHS, Ptrs_VEHS, nullptr, CH_SPARSE_ARRAY },
	{ 'VEOX', Save_VEOX, Load_VEOX, nullptr,   nullptr, CH_SPARSE_ARRAY },
	{ 'VESR', Save_VESR, Load_VESR, nullptr,   nullptr, CH_SPARSE_ARRAY },
	{ 'VENC', Save_VENC, Load_VENC, nullptr,   nullptr, CH_RIFF,         Special_VENC },
	{ 'VLKA', Save_VLKA, Load_VLKA, nullptr,   nullptr, CH_SPARSE_ARRAY },
};

extern const ChunkHandlerTable _veh_chunk_handlers(veh_chunk_handlers);
