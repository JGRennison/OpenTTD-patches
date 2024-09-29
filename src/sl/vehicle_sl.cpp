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
#include "../economy_base.h"
#include "../event_logs.h"
#include "../3rdparty/cpp-btree/btree_map.h"
#include "../3rdparty/robin_hood/robin_hood.h"
#include "../core/format.hpp"

#include "saveload.h"
#include "vehicle_sl.h"

#include <map>

#include "../safeguards.h"

extern btree::btree_multimap<VehicleID, PendingSpeedRestrictionChange> _pending_speed_restriction_change_map;

extern SaveLoadStructHandlerFactory MakeOrderExtraDataStructHandler();

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

	/* Clear aircraft from loading vehicles, if we bumped them into the air. */
	for (Station *st : Station::Iterate()) {
		for (auto iter = st->loading_vehicles.begin(); iter != st->loading_vehicles.end(); /* nothing */) {
			Vehicle *v = *iter;
			if (v->type == VEH_AIRCRAFT && !v->current_order.IsType(OT_LOADING)) {
				iter = st->loading_vehicles.erase(iter);
				delete v->cargo_payment;
			} else {
				++iter;
			}
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

extern uint8_t _age_cargo_skip_counter; // From misc_sl.cpp

static std::vector<Vehicle *> _load_invalid_vehicles_to_delete;

/** Called after load for phase 1 of vehicle initialisation */
void AfterLoadVehiclesPhase1(bool part_of_load)
{
	_load_invalid_vehicles_to_delete.clear();

	const Vehicle *si_v = nullptr;
	SCOPE_INFO_FMT([&si_v], "AfterLoadVehiclesPhase1: {}", scope_dumper().VehicleInfo(si_v));
	for (Vehicle *v : Vehicle::Iterate()) {
		si_v = v;
		/* Reinstate the previous pointer */
		if (v->Next() != nullptr) {
			v->Next()->previous = v;
#if OTTD_UPPER_TAGGED_PTR
			VehiclePoolOps::SetIsNonFrontVehiclePtr(_vehicle_pool.GetRawRef(v->Next()->index), true);
#endif
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
		robin_hood::unordered_flat_map<Order*, OrderList*> mapping;

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
			for (Vehicle *v : Vehicle::IterateFrontOnly()) {
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

		if (IsSavegameVersionBefore(SLV_VEHICLE_ECONOMY_AGE) && SlXvIsFeatureMissing(XSLFI_VEHICLE_ECONOMY_AGE)) {
			/* Set vehicle economy age based on calendar age. */
			for (Vehicle *v : Vehicle::Iterate()) {
				v->economy_age = v->age.base();
			}
		}
	}
	si_v = nullptr;

	CheckValidVehicles();
}

/** Called after load for phase 2 of vehicle initialisation */
void AfterLoadVehiclesPhase2(bool part_of_load)
{
	const Vehicle *si_v = nullptr;
	SCOPE_INFO_FMT([&si_v], "AfterLoadVehiclesPhase2: {}", scope_dumper().VehicleInfo(si_v));
	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		si_v = v;
		assert(v->First() != nullptr);

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
						if (IsSavegameVersionBefore(SLV_62)) {
							/* Use simplified check before trams were introduced */
							if (!MayTileTypeHaveRoad(GetTileType(u->tile))) is_invalid = true;
						} else {
							if (!MayHaveRoad(u->tile) || GetRoadType(u->tile, GetRoadTramType(u->roadtype)) == INVALID_ROADTYPE) is_invalid = true;
						}
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

	if (part_of_load && SlXvIsFeaturePresent(XSLFI_TEMPLATE_REPLACEMENT) && (_network_server || !_networking)) {
		for (Train *t : Train::IterateFrontOnly()) {
			si_v = t;
			if (t->IsVirtual()) {
				t->unitnumber = 0;
				delete t;
			}
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

	ResetDisasterVehicleTargeting();

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

			case VEH_DISASTER: {
				auto *dv = DisasterVehicle::From(v);
				if (dv->subtype == ST_SMALL_UFO && dv->state != 0) {
					RoadVehicle *u = RoadVehicle::GetIfValid(v->dest_tile);
					if (u != nullptr && u->IsFrontEngine()) {
						/* Delete UFO targeting a vehicle which is already a target. */
						if (!SetDisasterVehicleTargetingVehicle(u->index, dv->index)) {
							delete v;
							continue;
						}
					}
				}
				break;
			}

			default: break;
		}

		if (part_of_load && v->unitnumber != 0) {
			if (v->IsPrimaryVehicle()) {
				Company::Get(v->owner)->freeunits[v->type].UseID(v->unitnumber);
			} else {
				v->unitnumber = 0;
			}
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
		Debug(sl, 0, "Removing {} vehicles found to be uncorrectably invalid during load", _load_invalid_vehicles_to_delete.size());
		SetDParam(0, (uint)_load_invalid_vehicles_to_delete.size());
		ShowErrorMessage(STR_WARNING_LOADGAME_REMOVED_UNCORRECTABLE_VEHICLES, INVALID_STRING_ID, WL_CRITICAL);
		GroupStatistics::UpdateAfterLoad();

		RegisterGameEvents(GEF_RM_INVALID_RV);
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
	for (Train *v : Train::IterateFrontOnly()) {
		if (v->IsPrimaryVehicle()) {
			/* The vehicle center is now more to the front depending on vehicle length,
			 * so we need to move all vehicles forward to cover the difference to the
			 * old center, otherwise wagon spacing in trains would be broken upon load. */
			for (Train *u = v; u != nullptr; u = u->Next()) {
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
			v->ConsistChanged(CCF_TRACK);
		}
	}
}

static uint8_t  _cargo_periods;
static uint16_t _cargo_source;
static uint32_t _cargo_source_xy;
static uint16_t _cargo_count;
static uint16_t _cargo_paid_for;
static Money  _cargo_feeder_share;
CargoPacketList _cpp_packets;
std::map<VehicleID, CargoPacketList> _veh_cpp_packets;
static std::vector<Trackdir> _path_td;
static std::vector<TileIndex> _path_tile;
static uint32_t _path_layout_ctr;

static uint32_t _old_ahead_separation;
static uint16_t _old_timetable_start_subticks;

btree::btree_map<VehicleID, uint16_t> _old_timetable_start_subticks_map;

void IncludeBaseVehicleDescription(std::vector<SaveLoad> &slt)
{
	SlFilterNamedSaveLoadTable(GetVehicleDescription(VEH_END), slt);
}

struct VehicleCommonStructHandler final : public TypedSaveLoadStructHandler<VehicleCommonStructHandler, Vehicle> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetVehicleDescription(VEH_END);
	}

	void Save(Vehicle *v) const override
	{
		SlObjectSaveFiltered(v, this->GetLoadDescription());
	}

	void Load(Vehicle *v) const override
	{
		SlObjectLoadFiltered(v, this->GetLoadDescription());
	}

	void FixPointers(Vehicle *v) const override
	{
		SlObjectPtrOrNullFiltered(v, this->GetLoadDescription());
	}
};

struct VehicleTypeStructHandler final : public TypedSaveLoadStructHandler<VehicleTypeStructHandler, Vehicle> {
	const VehicleType type;

	VehicleTypeStructHandler(VehicleType type) : type(type) {}

	NamedSaveLoadTable GetDescription() const override
	{
		return GetVehicleDescription(this->type);
	}

	void Save(Vehicle *v) const override
	{
		if (v->type == this->type) SlObjectSaveFiltered(v, this->GetLoadDescription());
	}

	void Load(Vehicle *v) const override
	{
		if (v->type != this->type) SlErrorCorrupt("Vehicle load type mismatch");
		SlObjectLoadFiltered(v, this->GetLoadDescription());
	}

	void FixPointers(Vehicle *v) const override
	{
		if (v->type == this->type) SlObjectPtrOrNullFiltered(v, this->GetLoadDescription());
	}
};

struct VehicleOrderExtraDataStructHandler final : public TypedSaveLoadStructHandler<VehicleOrderExtraDataStructHandler, Vehicle> {
	NamedSaveLoadTable GetDescription() const override
	{
		extern NamedSaveLoadTable GetOrderExtraInfoDescription();
		return GetOrderExtraInfoDescription();
	}

	void Save(Vehicle *v) const override
	{
		if (!v->current_order.extra) return;

		SlObjectSaveFiltered(v->current_order.extra.get(), this->GetLoadDescription());
	}

	void Load(Vehicle *v) const override
	{
		v->current_order.AllocExtraInfo();
		SlObjectLoadFiltered(v->current_order.extra.get(), this->GetLoadDescription());
	}
};

const NamedSaveLoadTable GetVehicleUnbunchStateDescription()
{
	static const NamedSaveLoad _vehicle_unbunch_state_desc[] = {
		NSL("last_departure",  SLE_VAR(VehicleUnbunchState, depot_unbunching_last_departure,                SLE_INT64)),
		NSL("next_departure",  SLE_VAR(VehicleUnbunchState, depot_unbunching_next_departure,                SLE_INT64)),
		NSL("round_trip_time", SLE_VAR(VehicleUnbunchState, round_trip_time,                                SLE_INT32)),
	};
	return _vehicle_unbunch_state_desc;
}

struct VehicleUnbunchStateStructHandler final : public TypedSaveLoadStructHandler<VehicleUnbunchStateStructHandler, Vehicle> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetVehicleUnbunchStateDescription();
	}

	void Save(Vehicle *v) const override
	{
		if (v->unbunch_state != nullptr) {
			SlObjectSaveFiltered(v->unbunch_state.get(), this->GetLoadDescription());
		}
	}

	void Load(Vehicle *v) const override
	{
		v->unbunch_state.reset(new VehicleUnbunchState());
		SlObjectLoadFiltered(v->unbunch_state.get(), this->GetLoadDescription());
	}
};

const NamedSaveLoadTable GetVehicleLookAheadItemDescription()
{
	static const NamedSaveLoad _vehicle_look_ahead_item_desc[] = {
		NSL("start",          SLE_VAR(TrainReservationLookAheadItem, start,    SLE_INT32)),
		NSL("end",            SLE_VAR(TrainReservationLookAheadItem, end,      SLE_INT32)),
		NSL("z_pos",          SLE_VAR(TrainReservationLookAheadItem, z_pos,    SLE_INT16)),
		NSL("data_id",  SLE_CONDVAR_X(TrainReservationLookAheadItem, data_id,  SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 0, 9))),
		NSL("data_id",  SLE_CONDVAR_X(TrainReservationLookAheadItem, data_id,  SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 10))),
		NSL("data_aux", SLE_CONDVAR_X(TrainReservationLookAheadItem, data_aux, SLE_UINT16,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 9))),
		NSL("type",           SLE_VAR(TrainReservationLookAheadItem, type,     SLE_UINT8)),
	};

	return _vehicle_look_ahead_item_desc;
}

struct TrainLookaheadItemStructHandler final : public TypedSaveLoadStructHandler<TrainLookaheadItemStructHandler, TrainReservationLookAhead> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetVehicleLookAheadItemDescription();
	}

	void Save(TrainReservationLookAhead *lookahead) const override
	{
		SlSetStructListLength(lookahead->items.size());
		for (TrainReservationLookAheadItem &item : lookahead->items) {
			SlObjectSaveFiltered(&item, this->GetLoadDescription());
		}
	}

	void Load(TrainReservationLookAhead *lookahead) const override
	{
		lookahead->items.resize(SlGetStructListLength(UINT32_MAX));
		for (TrainReservationLookAheadItem &item : lookahead->items) {
			SlObjectLoadFiltered(&item, this->GetLoadDescription());
		}
	}
};

const NamedSaveLoadTable GetVehicleLookAheadCurveDescription()
{
	static const NamedSaveLoad _vehicle_look_ahead_curve_desc[] = {
		NSL("position",       SLE_VAR(TrainReservationLookAheadCurve, position, SLE_INT32)),
		NSL("dir_diff",       SLE_VAR(TrainReservationLookAheadCurve, dir_diff, SLE_UINT8)),
	};

	return _vehicle_look_ahead_curve_desc;
}

struct TrainLookaheadCurveStructHandler final : public TypedSaveLoadStructHandler<TrainLookaheadCurveStructHandler, TrainReservationLookAhead> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetVehicleLookAheadCurveDescription();
	}

	void Save(TrainReservationLookAhead *lookahead) const override
	{
		SlSetStructListLength(lookahead->curves.size());
		for (TrainReservationLookAheadCurve &curve : lookahead->curves) {
			SlObjectSaveFiltered(&curve, this->GetLoadDescription());
		}
	}

	void Load(TrainReservationLookAhead *lookahead) const override
	{
		lookahead->curves.resize(SlGetStructListLength(UINT32_MAX));
		for (TrainReservationLookAheadCurve &curve : lookahead->curves) {
			SlObjectLoadFiltered(&curve, this->GetLoadDescription());
		}
	}
};

const NamedSaveLoadTable GetVehicleLookAheadDescription()
{
	static const NamedSaveLoad _vehicle_look_ahead_desc[] = {
		NSL("reservation_end_tile",         SLE_VAR(TrainReservationLookAhead, reservation_end_tile,         SLE_UINT32)),
		NSL("reservation_end_trackdir",     SLE_VAR(TrainReservationLookAhead, reservation_end_trackdir,     SLE_UINT8)),
		NSL("current_position",             SLE_VAR(TrainReservationLookAhead, current_position,             SLE_INT32)),
		NSL("reservation_end_position",     SLE_VAR(TrainReservationLookAhead, reservation_end_position,     SLE_INT32)),
		NSL("lookahead_end_position", SLE_CONDVAR_X(TrainReservationLookAhead, lookahead_end_position,       SLE_INT32,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 9))),
		NSL("reservation_end_z",            SLE_VAR(TrainReservationLookAhead, reservation_end_z,            SLE_INT16)),
		NSL("tunnel_bridge_reserved_tiles", SLE_VAR(TrainReservationLookAhead, tunnel_bridge_reserved_tiles, SLE_INT16)),
		NSL("flags",                        SLE_VAR(TrainReservationLookAhead, flags,                        SLE_UINT16)),
		NSL("speed_restriction",            SLE_VAR(TrainReservationLookAhead, speed_restriction,            SLE_UINT16)),
		NSL("next_extend_position",   SLE_CONDVAR_X(TrainReservationLookAhead, next_extend_position,         SLE_INT32,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 5))),
		NSL("cached_zpos",            SLE_CONDVAR_X(TrainReservationLookAhead, cached_zpos,                  SLE_INT32,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 6))),
		NSL("zpos_refresh_remaining", SLE_CONDVAR_X(TrainReservationLookAhead, zpos_refresh_remaining,       SLE_UINT8,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 6))),

		NSLT_STRUCTLIST<TrainLookaheadItemStructHandler>("items"),
		NSLT_STRUCTLIST<TrainLookaheadCurveStructHandler>("curves"),
	};

	return _vehicle_look_ahead_desc;
}

struct TrainLookaheadStateStructHandler final : public TypedSaveLoadStructHandler<TrainLookaheadStateStructHandler, Train> {
	NamedSaveLoadTable GetDescription() const override
	{
		return GetVehicleLookAheadDescription();
	}

	void Save(Train *t) const override
	{
		if (t->lookahead != nullptr) {
			SlObjectSaveFiltered(t->lookahead.get(), this->GetLoadDescription());
		}
	}

	void Load(Train *t) const override
	{
		t->lookahead.reset(new TrainReservationLookAhead());
		SlObjectLoadFiltered(t->lookahead.get(), this->GetLoadDescription());
	}
};

NamedSaveLoadTable DispatchRecordsStructHandlerBase::GetDescription() const
{
	static const NamedSaveLoad _record_desc[] = {
		NSL("id",                           SLE_VAR(RecordPair, first,                 SLE_UINT16)),
		NSL("dispatched",                   SLE_VAR(RecordPair, second.dispatched,     SLE_INT64)),
		NSL("offset",                       SLE_VAR(RecordPair, second.offset,         SLE_UINT32)),
		NSL("slot_flags",                   SLE_VAR(RecordPair, second.slot_flags,     SLE_UINT16)),
		NSL("record_flags",                 SLE_VAR(RecordPair, second.record_flags,   SLE_UINT8)),
	};

	return _record_desc;
}

void DispatchRecordsStructHandlerBase::SaveDispatchRecords(btree::btree_map<uint16_t, LastDispatchRecord> &records) const
{
	SlSetStructListLength(records.size());
	for (RecordPair &it : records) {
		SlObjectSaveFiltered(&it, this->GetLoadDescription());
	}
}

void DispatchRecordsStructHandlerBase::LoadDispatchRecords(btree::btree_map<uint16_t, LastDispatchRecord> &records) const
{
	size_t count = SlGetStructListLength(UINT32_MAX);
	for (size_t i = 0; i < count; i++) {
		RecordPair it{};
		SlObjectLoadFiltered(&it, this->GetLoadDescription());
		records.insert(it);
	}
}

struct VehicleDispatchRecordsStructHandlerBase final : public DispatchRecordsStructHandlerBase {
	void Save(void *object) const override { this->SaveDispatchRecords(static_cast<Vehicle *>(object)->dispatch_records); }

	void Load(void *object) const override { this->LoadDispatchRecords(static_cast<Vehicle *>(object)->dispatch_records); }
};

/**
 * Make it possible to make the saveload tables "friends" of other classes.
 * @param vt the vehicle type. Can be VEH_END for the common vehicle description data
 * @return the saveload description
 */
NamedSaveLoadTable GetVehicleDescription(VehicleType vt)
{
	/** Save and load of vehicles */
	static const NamedSaveLoad _common_veh_desc[] = {
		NSL("subtype",                        SLE_VAR(Vehicle, subtype,                   SLE_UINT8)),

		NSL("next",                           SLE_REF(Vehicle, next,                      REF_VEHICLE_OLD)),
		NSL("name",                       SLE_CONDVAR(Vehicle, name,                      SLE_CNAME,                  SL_MIN_VERSION, SLV_84)),
		NSL("name",                       SLE_CONDSTR(Vehicle, name,                      SLE_STR | SLF_ALLOW_CONTROL, 0, SLV_84, SL_MAX_VERSION)),
		NSL("unitnumber",                 SLE_CONDVAR(Vehicle, unitnumber,                SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_8)),
		NSL("unitnumber",                 SLE_CONDVAR(Vehicle, unitnumber,                SLE_UINT16,                 SLV_8, SL_MAX_VERSION)),
		NSL("owner",                          SLE_VAR(Vehicle, owner,                     SLE_UINT8)),
		NSL("tile",                       SLE_CONDVAR(Vehicle, tile,                      SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6)),
		NSL("tile",                       SLE_CONDVAR(Vehicle, tile,                      SLE_UINT32,                 SLV_6, SL_MAX_VERSION)),
		NSL("dest_tile",                  SLE_CONDVAR(Vehicle, dest_tile,                 SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6)),
		NSL("dest_tile",                  SLE_CONDVAR(Vehicle, dest_tile,                 SLE_UINT32,                 SLV_6, SL_MAX_VERSION)),

		NSL("x_pos",                      SLE_CONDVAR(Vehicle, x_pos,                     SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6)),
		NSL("x_pos",                      SLE_CONDVAR(Vehicle, x_pos,                     SLE_UINT32,                 SLV_6, SL_MAX_VERSION)),
		NSL("y_pos",                      SLE_CONDVAR(Vehicle, y_pos,                     SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION,SLV_6)),
		NSL("y_pos",                      SLE_CONDVAR(Vehicle, y_pos,                     SLE_UINT32,                 SLV_6, SL_MAX_VERSION)),
		NSL("z_pos",                    SLE_CONDVAR_X(Vehicle, z_pos,                     SLE_FILE_U8  | SLE_VAR_I32, SL_MIN_VERSION, SLV_164, SlXvFeatureTest(XSLFTO_AND, XSLFI_ZPOS_32_BIT, 0, 0))),
		NSL("z_pos",                    SLE_CONDVAR_X(Vehicle, z_pos,                     SLE_INT32,                  SLV_164, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_ZPOS_32_BIT))),
		NSL("direction",                      SLE_VAR(Vehicle, direction,                 SLE_UINT8)),

		NSL("",                          SLE_CONDNULL(2,                                                              SL_MIN_VERSION, SLV_58)),
		NSL("spritenum",                      SLE_VAR(Vehicle, spritenum,                 SLE_UINT8)),
		NSL("",                          SLE_CONDNULL(5,                                                              SL_MIN_VERSION, SLV_58)),
		NSL("engine_type",                    SLE_VAR(Vehicle, engine_type,               SLE_UINT16)),

		NSL("",                          SLE_CONDNULL(2,                                                              SL_MIN_VERSION, SLV_152)),
		NSL("cur_speed",                      SLE_VAR(Vehicle, cur_speed,                 SLE_UINT16)),
		NSL("subspeed",                       SLE_VAR(Vehicle, subspeed,                  SLE_UINT8)),
		NSL("acceleration",                   SLE_VAR(Vehicle, acceleration,              SLE_UINT8)),
		NSL("motion_counter",             SLE_CONDVAR(Vehicle, motion_counter,            SLE_UINT32,                 SLV_VEH_MOTION_COUNTER, SL_MAX_VERSION)),
		NSL("progress",                       SLE_VAR(Vehicle, progress,                  SLE_UINT8)),

		NSL("vehstatus",                      SLE_VAR(Vehicle, vehstatus,                 SLE_UINT8)),
		NSL("last_station_visited",       SLE_CONDVAR(Vehicle, last_station_visited,      SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_5)),
		NSL("last_station_visited",       SLE_CONDVAR(Vehicle, last_station_visited,      SLE_UINT16,                 SLV_5, SL_MAX_VERSION)),
		NSL("last_loading_station",     SLE_CONDVAR_X(Vehicle, last_loading_station,      SLE_UINT16,                 SLV_182, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_CHILLPP, SL_CHILLPP_232))),

		NSL("cargo_type",                     SLE_VAR(Vehicle, cargo_type,                SLE_UINT8)),
		NSL("cargo_subtype",              SLE_CONDVAR(Vehicle, cargo_subtype,             SLE_UINT8,                  SLV_35, SL_MAX_VERSION)),
		NSL("",                          SLEG_CONDVAR(         _cargo_periods,            SLE_UINT8,                  SL_MIN_VERSION, SLV_68)),
		NSL("",                          SLEG_CONDVAR(         _cargo_source,             SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_7)),
		NSL("",                          SLEG_CONDVAR(         _cargo_source,             SLE_UINT16,                 SLV_7, SLV_68)),
		NSL("",                          SLEG_CONDVAR(         _cargo_source_xy,          SLE_UINT32,                 SLV_44, SLV_68)),
		NSL("cargo_cap",                      SLE_VAR(Vehicle, cargo_cap,                 SLE_UINT16)),
		NSL("refit_cap",                  SLE_CONDVAR(Vehicle, refit_cap,                 SLE_UINT16,                 SLV_182, SL_MAX_VERSION)),
		NSL("",                          SLEG_CONDVAR(_cargo_count,                       SLE_UINT16,                 SL_MIN_VERSION,  SLV_68)),
		NSL("cargo.packets",          SLE_CONDPTRRING(Vehicle, cargo.packets,             REF_CARGO_PACKET,           SLV_68, SL_MAX_VERSION)),
		NSL("",                    SLEG_CONDPTRRING_X(_cpp_packets,                       REF_CARGO_PACKET,           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP))),
		NSL("cargo.action_counts",        SLE_CONDARR(Vehicle, cargo.action_counts,       SLE_UINT, VehicleCargoList::NUM_MOVE_TO_ACTION, SLV_181, SL_MAX_VERSION)),
		NSL("cargo_age_counter",          SLE_CONDVAR(Vehicle, cargo_age_counter,         SLE_UINT16,                 SLV_162, SL_MAX_VERSION)),

		NSL("day_counter",                    SLE_VAR(Vehicle, day_counter,               SLE_UINT8)),
		NSL("tick_counter",                   SLE_VAR(Vehicle, tick_counter,              SLE_UINT8)),
		NSL("running_ticks",            SLE_CONDVAR_X(Vehicle, running_ticks,             SLE_FILE_U8  | SLE_VAR_U16, SLV_88, SL_MAX_VERSION, SlXvFeatureTest([](uint16_t version, bool version_in_range, const std::array<uint16_t, XSLFI_SIZE> &feature_versions) -> bool {
			return version_in_range && !(SlXvIsFeaturePresent(feature_versions, XSLFI_SPRINGPP, 3) || SlXvIsFeaturePresent(feature_versions, XSLFI_JOKERPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_CHILLPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_VARIABLE_DAY_LENGTH, 2));
		}))),
		NSL("running_ticks",            SLE_CONDVAR_X(Vehicle, running_ticks,             SLE_UINT16,                 SLV_88, SL_MAX_VERSION, SlXvFeatureTest([](uint16_t version, bool version_in_range, const std::array<uint16_t, XSLFI_SIZE> &feature_versions) -> bool {
			return version_in_range && (SlXvIsFeaturePresent(feature_versions, XSLFI_SPRINGPP, 2) || SlXvIsFeaturePresent(feature_versions, XSLFI_JOKERPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_CHILLPP) || SlXvIsFeaturePresent(feature_versions, XSLFI_VARIABLE_DAY_LENGTH, 2));
		}))),

		NSL("cur_implicit_order_index",       SLE_VAR(Vehicle, cur_implicit_order_index,   SLE_VEHORDERID)),
		NSL("cur_real_order_index",       SLE_CONDVAR(Vehicle, cur_real_order_index,       SLE_VEHORDERID,            SLV_158, SL_MAX_VERSION)),
		NSL("cur_timetable_order_index", SLE_CONDVAR_X(Vehicle, cur_timetable_order_index, SLE_VEHORDERID,            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA))),
		/* num_orders is now part of OrderList and is not saved but counted */
		NSL("",                          SLE_CONDNULL(1,                                                              SL_MIN_VERSION, SLV_105)),

		/* This next line is for version 4 and prior compatibility.. it temporarily reads
		 type and flags (which were both 4 bits) into type. Later on this is
		 converted correctly */
		NSL("current_order.type",         SLE_CONDVAR(Vehicle, current_order.type,        SLE_UINT8,                  SL_MIN_VERSION, SLV_5)),
		NSL("current_order.dest",         SLE_CONDVAR(Vehicle, current_order.dest,        SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_5)),

		/* Orders for version 5 and on */
		NSL("current_order.type",         SLE_CONDVAR(Vehicle, current_order.type,        SLE_UINT8,                  SLV_5, SL_MAX_VERSION)),
		NSL("current_order.flags",      SLE_CONDVAR_X(Vehicle, current_order.flags,       SLE_FILE_U8 | SLE_VAR_U16,  SLV_5, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 0, 0))),
		NSL("current_order.flags",      SLE_CONDVAR_X(Vehicle, current_order.flags,       SLE_UINT16,                 SLV_5, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ORDER_FLAGS_EXTRA, 1))),
		NSL("current_order.dest",         SLE_CONDVAR(Vehicle, current_order.dest,        SLE_UINT16,                 SLV_5, SL_MAX_VERSION)),

		/* Refit in current order */
		NSL("current_order.refit_cargo",  SLE_CONDVAR(Vehicle, current_order.refit_cargo, SLE_UINT8,                  SLV_36, SL_MAX_VERSION)),
		NSL("", SLE_CONDNULL(1,                                                                                       SLV_36, SLV_182)), // refit_subtype

		/* Timetable in current order */
		NSL("current_order.wait_time",  SLE_CONDVAR_X(Vehicle, current_order.wait_time,   SLE_FILE_U16 | SLE_VAR_U32, SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5))),
		NSL("current_order.wait_time",  SLE_CONDVAR_X(Vehicle, current_order.wait_time,   SLE_UINT32,                 SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6))),
		NSL("current_order.travel_time",SLE_CONDVAR_X(Vehicle, current_order.travel_time, SLE_FILE_U16 | SLE_VAR_U32, SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 0, 5))),
		NSL("current_order.travel_time",SLE_CONDVAR_X(Vehicle, current_order.travel_time, SLE_UINT32,                 SLV_67, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLE_EXTRA, 6))),
		NSL("current_order.max_speed",    SLE_CONDVAR(Vehicle, current_order.max_speed,   SLE_UINT16,                 SLV_174, SL_MAX_VERSION)),

		NSLT_STRUCT<VehicleOrderExtraDataStructHandler>("current_order.extra"),

		NSL("timetable_start",          SLE_CONDVAR_X(Vehicle, timetable_start,           SLE_FILE_I32 | SLE_VAR_I64, SLV_129, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 0, 2))),
		NSL("timetable_start",          SLE_CONDVAR_X(Vehicle, timetable_start,           SLE_INT64,                  SLV_129, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 3))),
		NSL("",                        SLEG_CONDVAR_X(_old_timetable_start_subticks,      SLE_UINT16,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TIMETABLES_START_TICKS, 2, 2))),

		NSL("orders",                     SLE_CONDREF(Vehicle, orders,                    REF_ORDER,                  SL_MIN_VERSION, SLV_105)),
		NSL("orders",                     SLE_CONDREF(Vehicle, orders,                    REF_ORDERLIST,              SLV_105, SL_MAX_VERSION)),

		NSL("age",                        SLE_CONDVAR(Vehicle, age,                       SLE_FILE_U16 | SLE_VAR_I32, SL_MIN_VERSION, SLV_31)),
		NSL("age",                        SLE_CONDVAR(Vehicle, age,                       SLE_INT32,                  SLV_31, SL_MAX_VERSION)),
		NSL("economy_age",              SLE_CONDVAR_X(Vehicle, economy_age,               SLE_INT32,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_ECONOMY_AGE))),
		NSL("max_age",                    SLE_CONDVAR(Vehicle, max_age,                   SLE_FILE_U16 | SLE_VAR_I32, SL_MIN_VERSION, SLV_31)),
		NSL("max_age",                    SLE_CONDVAR(Vehicle, max_age,                   SLE_INT32,                  SLV_31, SL_MAX_VERSION)),
		NSL("date_of_last_service",       SLE_CONDVAR(Vehicle, date_of_last_service,      SLE_FILE_U16 | SLE_VAR_I32, SL_MIN_VERSION, SLV_31)),
		NSL("date_of_last_service",       SLE_CONDVAR(Vehicle, date_of_last_service,      SLE_INT32,                  SLV_31, SL_MAX_VERSION)),
		NSL("date_of_last_service_newgrf",SLE_CONDVAR_X(Vehicle, date_of_last_service_newgrf, SLE_INT32,              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEWGRF_LAST_SERVICE))),
		NSL("service_interval",           SLE_CONDVAR(Vehicle, service_interval,          SLE_UINT16,                 SL_MIN_VERSION, SLV_31)),
		NSL("service_interval",           SLE_CONDVAR(Vehicle, service_interval,          SLE_FILE_U32 | SLE_VAR_U16, SLV_31, SLV_180)),
		NSL("service_interval",           SLE_CONDVAR(Vehicle, service_interval,          SLE_UINT16,                 SLV_180, SL_MAX_VERSION)),
		NSL("reliability",                    SLE_VAR(Vehicle, reliability,               SLE_UINT16)),
		NSL("reliability_spd_dec",            SLE_VAR(Vehicle, reliability_spd_dec,       SLE_UINT16)),
		NSL("breakdown_ctr",                  SLE_VAR(Vehicle, breakdown_ctr,             SLE_UINT8)),
		NSL("breakdown_delay",                SLE_VAR(Vehicle, breakdown_delay,           SLE_UINT8)),
		NSL("breakdowns_since_last_service",  SLE_VAR(Vehicle, breakdowns_since_last_service, SLE_UINT8)),
		NSL("breakdown_chance",               SLE_VAR(Vehicle, breakdown_chance,          SLE_UINT8)),
		NSL("breakdown_chance_factor",  SLE_CONDVAR_X(Vehicle, breakdown_chance_factor,   SLE_UINT8,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 3))),
		NSL("breakdown_type",           SLE_CONDVAR_X(Vehicle, breakdown_type,            SLE_UINT8,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS))),
		NSL("breakdown_severity",       SLE_CONDVAR_X(Vehicle, breakdown_severity,        SLE_UINT8,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS))),
		NSL("build_year",                 SLE_CONDVAR(Vehicle, build_year,                SLE_FILE_U8 | SLE_VAR_I32,  SL_MIN_VERSION, SLV_31)),
		NSL("build_year",                 SLE_CONDVAR(Vehicle, build_year,                SLE_INT32,                  SLV_31, SL_MAX_VERSION)),

		NSL("load_unload_ticks",              SLE_VAR(Vehicle, load_unload_ticks,         SLE_UINT16)),
		NSL("cargo_paid_for",            SLEG_CONDVAR(         _cargo_paid_for,           SLE_UINT16,                 SLV_45, SL_MAX_VERSION)),
		NSL("vehicle_flags",              SLE_CONDVAR(Vehicle, vehicle_flags,             SLE_FILE_U8  | SLE_VAR_U32, SLV_40, SLV_180)),
		NSL("vehicle_flags",            SLE_CONDVAR_X(Vehicle, vehicle_flags,             SLE_FILE_U16 | SLE_VAR_U32, SLV_180, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 0, 0))),
		NSL("vehicle_flags",            SLE_CONDVAR_X(Vehicle, vehicle_flags,             SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_FLAGS_EXTRA, 1))),

		NSL("profit_this_year",           SLE_CONDVAR(Vehicle, profit_this_year,          SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SLV_65)),
		NSL("profit_this_year",           SLE_CONDVAR(Vehicle, profit_this_year,          SLE_INT64,                  SLV_65, SL_MAX_VERSION)),
		NSL("profit_last_year",           SLE_CONDVAR(Vehicle, profit_last_year,          SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SLV_65)),
		NSL("profit_last_year",           SLE_CONDVAR(Vehicle, profit_last_year,          SLE_INT64,                  SLV_65, SL_MAX_VERSION)),
		NSL("profit_lifetime",          SLE_CONDVAR_X(Vehicle, profit_lifetime,           SLE_INT64,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEH_LIFETIME_PROFIT))),
		NSL("",                          SLEG_CONDVAR(         _cargo_feeder_share,       SLE_FILE_I32 | SLE_VAR_I64, SLV_51, SLV_65)),
		NSL("",                          SLEG_CONDVAR(         _cargo_feeder_share,       SLE_INT64,                  SLV_65, SLV_68)),
		NSL("",                          SLE_CONDNULL(4                                           ,                   SLV_51, SLV_68)), // _cargo_loaded_at_xy
		NSL("value",                      SLE_CONDVAR(Vehicle, value,                     SLE_FILE_I32 | SLE_VAR_I64, SL_MIN_VERSION, SLV_65)),
		NSL("value",                      SLE_CONDVAR(Vehicle, value,                     SLE_INT64,                  SLV_65, SL_MAX_VERSION)),
		NSL("",                        SLE_CONDNULL_X(8,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VEHICLE_REPAIR_COST, 1, 1))),

		NSL("random_bits",              SLE_CONDVAR_X(Vehicle, random_bits,               SLE_FILE_U8 | SLE_VAR_U16,  SLV_2, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTEND_VEHICLE_RANDOM, 0, 0))),
		NSL("random_bits",              SLE_CONDVAR_X(Vehicle, random_bits,               SLE_UINT16,                 SLV_2, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTEND_VEHICLE_RANDOM, 1))),
		NSL("waiting_triggers",           SLE_CONDVAR(Vehicle, waiting_triggers,          SLE_UINT8,                  SLV_2, SL_MAX_VERSION)),

		NSL("",                        SLEG_CONDVAR_X(_old_ahead_separation,              SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE, 1, 4))),
		NSL("",                        SLE_CONDNULL_X(4,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE, 1, 4))),

		NSL("next_shared",                SLE_CONDREF(Vehicle, next_shared,               REF_VEHICLE,                SLV_2, SL_MAX_VERSION)),
		NSL("",                          SLE_CONDNULL(2,                                                              SLV_2, SLV_69)),
		NSL("",                          SLE_CONDNULL(4,                                                              SLV_69, SLV_101)),

		NSL("group_id",                   SLE_CONDVAR(Vehicle, group_id,                  SLE_UINT16,                 SLV_60, SL_MAX_VERSION)),

		NSL("current_order_time",         SLE_CONDVAR(Vehicle, current_order_time,        SLE_UINT32,                 SLV_67, SL_MAX_VERSION)),
		NSL("current_loading_time",     SLE_CONDVAR_X(Vehicle, current_loading_time,      SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUTO_TIMETABLE))),
		NSL("current_loading_time",     SLE_CONDVAR_X(Vehicle, current_loading_time,      SLE_UINT32,                 SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_23))),
		NSL("last_loading_tick",        SLE_CONDVAR_X(Vehicle, last_loading_tick,         SLE_INT64,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_LAST_LOADING_TICK))),
		NSL("",                        SLE_CONDNULL_X(4,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("lateness_counter",           SLE_CONDVAR(Vehicle, lateness_counter,          SLE_INT32,                  SLV_67, SL_MAX_VERSION)),

		NSL("",                          SLE_CONDNULL(10,                                                             SLV_2, SLV_144)), // old reserved space

		NSL("",                        SLE_CONDNULL_X((8 + 8 + 2 + 2 + 4 + 4 + 1 + 1) * 30,                           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("",                        SLE_CONDNULL_X((8 + 8 + 2 + 2 + 4 + 4 + 1 + 1) * 70,                           SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP, 4))),
		NSL("",                        SLE_CONDNULL_X(1,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("",                        SLE_CONDNULL_X(1,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("",                        SLE_CONDNULL_X(2,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),

		NSL("",                        SLE_CONDNULL_X(160,                                                            SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP))),

		NSLT_STRUCT<VehicleUnbunchStateStructHandler>("depot_unbunch_state"),
		NSLT_STRUCTLIST<VehicleDispatchRecordsStructHandlerBase>("dispatch_records"),
	};

	static const NamedSaveLoad _train_desc[] = {
		NSL("", SLE_WRITEBYTE(Vehicle, type)),
		NSL("", SLE_INCLUDE(IncludeBaseVehicleDescription)),
		NSLT_STRUCT<VehicleCommonStructHandler>("common"),

		NSL("crash_anim_pos",                 SLE_VAR(Train, crash_anim_pos,            SLE_UINT16)),
		NSL("force_proceed",                  SLE_VAR(Train, force_proceed,             SLE_UINT8)),
		NSL("railtype",                       SLE_VAR(Train, railtype,                  SLE_UINT8)),
		NSL("track",                          SLE_VAR(Train, track,                     SLE_UINT8)),

		NSL("flags",                      SLE_CONDVAR(Train, flags,                     SLE_FILE_U8  | SLE_VAR_U32,  SLV_2, SLV_100)),
		NSL("flags",                    SLE_CONDVAR_X(Train, flags,                     SLE_FILE_U16 | SLE_VAR_U32,  SLV_100, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_FLAGS_EXTRA, 0, 0))),
		NSL("flags",                    SLE_CONDVAR_X(Train, flags,                     SLE_UINT32,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_FLAGS_EXTRA, 1))),
		NSL("",                          SLE_CONDNULL(2,                                                             SLV_2, SLV_60)),

		NSL("wait_counter",               SLE_CONDVAR(Train, wait_counter,              SLE_UINT16,                  SLV_136, SL_MAX_VERSION)),
		NSL("tunnel_bridge_signal_num", SLE_CONDVAR_X(Train, tunnel_bridge_signal_num,  SLE_UINT16,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SIG_TUNNEL_BRIDGE, 5))),

		NSL("",                          SLE_CONDNULL(2,                                                             SLV_2, SLV_20)),
		NSL("gv_flags",                   SLE_CONDVAR(Train, gv_flags,                  SLE_UINT16,                  SLV_139, SL_MAX_VERSION)),
		NSL("",                        SLE_CONDNULL_X(2,                                                             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232))),
		NSL("",                          SLE_CONDNULL(11,                                                            SLV_2, SLV_144)), // old reserved space
		NSL("reverse_distance",         SLE_CONDVAR_X(Train, reverse_distance,          SLE_UINT16,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REVERSE_AT_WAYPOINT))),
		NSL("speed_restriction",        SLE_CONDVAR_X(Train, speed_restriction,         SLE_UINT16,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPEED_RESTRICTION))),
		NSL("signal_speed_restriction", SLE_CONDVAR_X(Train, signal_speed_restriction,  SLE_UINT16,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TRAIN_SPEED_ADAPTATION))),
		NSL("critical_breakdown_count", SLE_CONDVAR_X(Train, critical_breakdown_count,  SLE_UINT8,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 2))),

		NSLT_STRUCT<TrainLookaheadStateStructHandler>("lookahead"),
	};

	static const NamedSaveLoad _roadveh_desc[] = {
		NSL("", SLE_WRITEBYTE(Vehicle, type)),
		NSL("", SLE_INCLUDE(IncludeBaseVehicleDescription)),
		NSLT_STRUCT<VehicleCommonStructHandler>("common"),

		NSL("state",                          SLE_VAR(RoadVehicle, state,                    SLE_UINT8)),
		NSL("frame",                          SLE_VAR(RoadVehicle, frame,                    SLE_UINT8)),
		NSL("blocked_ctr",                    SLE_VAR(RoadVehicle, blocked_ctr,              SLE_UINT16)),
		NSL("overtaking",                     SLE_VAR(RoadVehicle, overtaking,               SLE_UINT8)),
		NSL("overtaking_ctr",                 SLE_VAR(RoadVehicle, overtaking_ctr,           SLE_UINT8)),
		NSL("crashed_ctr",                    SLE_VAR(RoadVehicle, crashed_ctr,              SLE_UINT16)),
		NSL("reverse_ctr",                    SLE_VAR(RoadVehicle, reverse_ctr,              SLE_UINT8)),
		NSL("path.td",                SLEG_CONDVARVEC(_path_td,                              SLE_UINT8,              SLV_ROADVEH_PATH_CACHE, SL_MAX_VERSION)),
		NSL("path.tile",              SLEG_CONDVARVEC(_path_tile,                            SLE_UINT32,             SLV_ROADVEH_PATH_CACHE, SL_MAX_VERSION)),
		NSL("path.layout_ctr",         SLEG_CONDVAR_X(_path_layout_ctr,                      SLE_UINT32,             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_LAYOUT_CHANGE_CTR))),

		NSL("",                          SLE_CONDNULL(2,                                                             SLV_6,  SLV_69)),
		NSL("gv_flags",                   SLE_CONDVAR(RoadVehicle, gv_flags,                 SLE_UINT16,             SLV_139, SL_MAX_VERSION)),
		NSL("",                          SLE_CONDNULL(4,                                                             SLV_69, SLV_131)),
		NSL("",                          SLE_CONDNULL(2,                                                             SLV_6, SLV_131)),
		NSL("",                          SLE_CONDNULL(16,                                                            SLV_2, SLV_144)), // old reserved space
		NSL("critical_breakdown_count", SLE_CONDVAR_X(RoadVehicle, critical_breakdown_count, SLE_UINT8,              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 6))),
		NSL("rvflags",                  SLE_CONDVAR_X(RoadVehicle, rvflags,                  SLE_UINT8,              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_VEH_FLAGS))),
	};

	static const NamedSaveLoad _ship_desc[] = {
		NSL("", SLE_WRITEBYTE(Vehicle, type)),
		NSL("", SLE_INCLUDE(IncludeBaseVehicleDescription)),
		NSLT_STRUCT<VehicleCommonStructHandler>("common"),

		NSL("state",                          SLE_VAR(Ship, state,                      SLE_UINT8)),
		NSL("cached_path",               SLE_CONDRING(Ship, cached_path,                SLE_UINT8,                   SLV_SHIP_PATH_CACHE, SL_MAX_VERSION)),
		NSL("rotation",                   SLE_CONDVAR(Ship, rotation,                   SLE_UINT8,                   SLV_SHIP_ROTATION, SL_MAX_VERSION)),
		NSL("lost_count",               SLE_CONDVAR_X(Ship, lost_count,                 SLE_UINT8,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SHIP_LOST_COUNTER))),
		NSL("critical_breakdown_count", SLE_CONDVAR_X(Ship, critical_breakdown_count,   SLE_UINT8,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_IMPROVED_BREAKDOWNS, 8))),

		NSL("",                          SLE_CONDNULL(16,                                                            SLV_2, SLV_144)), // old reserved space
	};

	static const NamedSaveLoad _aircraft_desc[] = {
		NSL("", SLE_WRITEBYTE(Vehicle, type)),
		NSL("", SLE_INCLUDE(IncludeBaseVehicleDescription)),
		NSLT_STRUCT<VehicleCommonStructHandler>("common"),

		NSL("crashed_counter",               SLE_VAR(Aircraft, crashed_counter,         SLE_UINT16)),
		NSL("pos",                           SLE_VAR(Aircraft, pos,                     SLE_UINT8)),

		NSL("targetairport",             SLE_CONDVAR(Aircraft, targetairport,           SLE_FILE_U8  | SLE_VAR_U16,  SL_MIN_VERSION, SLV_5)),
		NSL("targetairport",             SLE_CONDVAR(Aircraft, targetairport,           SLE_UINT16,                  SLV_5, SL_MAX_VERSION)),

		NSL("state",                         SLE_VAR(Aircraft, state,                   SLE_UINT8)),

		NSL("previous_pos",              SLE_CONDVAR(Aircraft, previous_pos,            SLE_UINT8,                   SLV_2, SL_MAX_VERSION)),
		NSL("last_direction",            SLE_CONDVAR(Aircraft, last_direction,          SLE_UINT8,                   SLV_2, SL_MAX_VERSION)),
		NSL("number_consecutive_turns",  SLE_CONDVAR(Aircraft, number_consecutive_turns,SLE_UINT8,                   SLV_2, SL_MAX_VERSION)),
		NSL("",                       SLE_CONDNULL_X(2,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("",                       SLE_CONDNULL_X(2,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP))),

		NSL("turn_counter",              SLE_CONDVAR(Aircraft, turn_counter,            SLE_UINT8,                   SLV_136, SL_MAX_VERSION)),
		NSL("flags",                     SLE_CONDVAR(Aircraft, flags,                   SLE_UINT8,                   SLV_167, SL_MAX_VERSION)),

		NSL("",                         SLE_CONDNULL(13,                                                             SLV_2, SLV_144)), // old reserved space
	};

	static const NamedSaveLoad _special_desc[] = {
		NSL("", SLE_WRITEBYTE(Vehicle, type)),

		NSL("subtype",                       SLE_VAR(Vehicle, subtype,                  SLE_UINT8)),

		NSL("",                       SLE_CONDNULL_X(5,                                                              SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),

		NSL("tile",                      SLE_CONDVAR(Vehicle, tile,                     SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6)),
		NSL("tile",                      SLE_CONDVAR(Vehicle, tile,                     SLE_UINT32,                  SLV_6, SL_MAX_VERSION)),

		NSL("x_pos",                     SLE_CONDVAR(Vehicle, x_pos,                    SLE_FILE_I16 | SLE_VAR_I32,  SL_MIN_VERSION, SLV_6)),
		NSL("x_pos",                     SLE_CONDVAR(Vehicle, x_pos,                    SLE_INT32,                   SLV_6, SL_MAX_VERSION)),
		NSL("y_pos",                     SLE_CONDVAR(Vehicle, y_pos,                    SLE_FILE_I16 | SLE_VAR_I32,  SL_MIN_VERSION, SLV_6)),
		NSL("y_pos",                     SLE_CONDVAR(Vehicle, y_pos,                    SLE_INT32,                   SLV_6, SL_MAX_VERSION)),
		NSL("z_pos",                   SLE_CONDVAR_X(Vehicle, z_pos,                    SLE_FILE_U8  | SLE_VAR_I32,  SL_MIN_VERSION, SLV_164, SlXvFeatureTest(XSLFTO_AND, XSLFI_ZPOS_32_BIT, 0, 0))),
		NSL("z_pos",                   SLE_CONDVAR_X(Vehicle, z_pos,                    SLE_INT32,                   SLV_164, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_ZPOS_32_BIT))),

		NSL("sprite[0]",                     SLE_VAR(Vehicle, sprite_seq.seq[0].sprite, SLE_FILE_U16 | SLE_VAR_U32)),
		NSL("",                         SLE_CONDNULL(5,                                                              SL_MIN_VERSION, SLV_59)),
		NSL("progress",                      SLE_VAR(Vehicle, progress,                 SLE_UINT8)),
		NSL("vehstatus",                     SLE_VAR(Vehicle, vehstatus,                SLE_UINT8)),

		NSL("animation_state",               SLE_VAR(EffectVehicle, animation_state,    SLE_UINT16)),
		NSL("animation_substate",            SLE_VAR(EffectVehicle, animation_substate, SLE_UINT8)),

		NSL("spritenum",                 SLE_CONDVAR(Vehicle, spritenum,                SLE_UINT8,                   SLV_2, SL_MAX_VERSION)),

		NSL("", SLE_CONDNULL(15,                                                                                     SLV_2, SLV_144)), // old reserved space
	};

	static const NamedSaveLoad _disaster_desc[] = {
		NSL("", SLE_WRITEBYTE(Vehicle, type)),

		NSL("next",                          SLE_REF(Vehicle, next,                     REF_VEHICLE_OLD)),

		NSL("subtype",                       SLE_VAR(Vehicle, subtype,                  SLE_UINT8)),
		NSL("tile",                      SLE_CONDVAR(Vehicle, tile,                     SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6)),
		NSL("tile",                      SLE_CONDVAR(Vehicle, tile,                     SLE_UINT32,                  SLV_6, SL_MAX_VERSION)),
		NSL("dest_tile",                 SLE_CONDVAR(Vehicle, dest_tile,                SLE_FILE_U16 | SLE_VAR_U32,  SL_MIN_VERSION, SLV_6)),
		NSL("dest_tile",                 SLE_CONDVAR(Vehicle, dest_tile,                SLE_UINT32,                  SLV_6, SL_MAX_VERSION)),

		NSL("x_pos",                     SLE_CONDVAR(Vehicle, x_pos,                    SLE_FILE_I16 | SLE_VAR_I32,  SL_MIN_VERSION, SLV_6)),
		NSL("x_pos",                     SLE_CONDVAR(Vehicle, x_pos,                    SLE_INT32,                   SLV_6, SL_MAX_VERSION)),
		NSL("y_pos",                     SLE_CONDVAR(Vehicle, y_pos,                    SLE_FILE_I16 | SLE_VAR_I32,  SL_MIN_VERSION, SLV_6)),
		NSL("y_pos",                     SLE_CONDVAR(Vehicle, y_pos,                    SLE_INT32,                   SLV_6, SL_MAX_VERSION)),
		NSL("z_pos",                   SLE_CONDVAR_X(Vehicle, z_pos,                    SLE_FILE_U8  | SLE_VAR_I32,  SL_MIN_VERSION, SLV_164, SlXvFeatureTest(XSLFTO_AND, XSLFI_ZPOS_32_BIT, 0, 0))),
		NSL("z_pos",                   SLE_CONDVAR_X(Vehicle, z_pos,                    SLE_INT32,                   SLV_164, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_OR, XSLFI_ZPOS_32_BIT))),
		NSL("direction",                     SLE_VAR(Vehicle, direction,                SLE_UINT8)),

		NSL("",                         SLE_CONDNULL(5,                                                              SL_MIN_VERSION, SLV_58)),
		NSL("owner",                         SLE_VAR(Vehicle, owner,                    SLE_UINT8)),
		NSL("vehstatus",                     SLE_VAR(Vehicle, vehstatus,                SLE_UINT8)),
		NSL("",                        SLE_CONDVAR_X(Vehicle, current_order.dest,       SLE_FILE_U8 | SLE_VAR_U16,   SL_MIN_VERSION, SLV_5, SlXvFeatureTest(XSLFTO_AND, XSLFI_DISASTER_VEH_STATE, 0, 0))),
		NSL("",                        SLE_CONDVAR_X(Vehicle, current_order.dest,       SLE_UINT16,                  SLV_5, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_DISASTER_VEH_STATE, 0, 0))),
		NSL("state",                   SLE_CONDVAR_X(DisasterVehicle, state,            SLE_UINT16,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_DISASTER_VEH_STATE, 1))),

		NSL("sprite[0]",                     SLE_VAR(Vehicle, sprite_seq.seq[0].sprite, SLE_FILE_U16 | SLE_VAR_U32)),
		NSL("age",                       SLE_CONDVAR(Vehicle, age,                      SLE_FILE_U16 | SLE_VAR_I32,  SL_MIN_VERSION, SLV_31)),
		NSL("age",                       SLE_CONDVAR(Vehicle, age,                      SLE_INT32,                   SLV_31, SL_MAX_VERSION)),
		NSL("tick_counter",                  SLE_VAR(Vehicle, tick_counter,             SLE_UINT8)),

		NSL("image_override",            SLE_CONDVAR(DisasterVehicle, image_override,            SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_191)),
		NSL("image_override",            SLE_CONDVAR(DisasterVehicle, image_override,            SLE_UINT32,                 SLV_191, SL_MAX_VERSION)),
		NSL("big_ufo_destroyer_target",  SLE_CONDVAR(DisasterVehicle, big_ufo_destroyer_target,  SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_191)),
		NSL("big_ufo_destroyer_target",  SLE_CONDVAR(DisasterVehicle, big_ufo_destroyer_target,  SLE_UINT32,                 SLV_191, SL_MAX_VERSION)),
		NSL("",                       SLE_CONDNULL_X(2,                                                                      SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP))),
		NSL("",                       SLE_CONDNULL_X(2,                                                                      SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP))),
		NSL("flags",                     SLE_CONDVAR(DisasterVehicle, flags,                     SLE_UINT8,                  SLV_194, SL_MAX_VERSION)),

		NSL("",                         SLE_CONDNULL(16,                                                                     SLV_2, SLV_144)), // old reserved space
	};


	static const NamedSaveLoadTable _veh_descs[] = {
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

static const NamedSaveLoad _table_vehicle_desc[] = {
	NSLT("type", SLE_WRITEBYTE(Vehicle, type)),
	NSL_STRUCT("train",    MakeSaveLoadStructHandlerFactory<VehicleTypeStructHandler, VEH_TRAIN>()),
	NSL_STRUCT("roadveh",  MakeSaveLoadStructHandlerFactory<VehicleTypeStructHandler, VEH_ROAD>()),
	NSL_STRUCT("ship",     MakeSaveLoadStructHandlerFactory<VehicleTypeStructHandler, VEH_SHIP>()),
	NSL_STRUCT("aircraft", MakeSaveLoadStructHandlerFactory<VehicleTypeStructHandler, VEH_AIRCRAFT>()),
	NSL_STRUCT("effect",   MakeSaveLoadStructHandlerFactory<VehicleTypeStructHandler, VEH_EFFECT>()),
	NSL_STRUCT("disaster", MakeSaveLoadStructHandlerFactory<VehicleTypeStructHandler, VEH_DISASTER>()),
};

/** Will be called when the vehicles need to be saved. */
static void Save_VEHS()
{
	SaveLoadTableData slt = SlTableHeader(_table_vehicle_desc);

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
		SlObjectSaveFiltered(v, slt);
	}
}

/** Will be called when vehicles need to be loaded. */
void Load_VEHS()
{
	_cargo_count = 0;

	_cpp_packets.clear();
	_veh_cpp_packets.clear();

	_path_td.clear();
	_path_tile.clear();
	_path_layout_ctr = 0;

	_old_timetable_start_subticks = 0;
	_old_timetable_start_subticks_map.clear();

	SaveLoadTableData slt;
	std::vector<std::vector<SaveLoad>> non_table_descs;

	const bool is_table = SlIsTableChunk();
	if (is_table) {
		slt = SlTableHeaderOrRiff(_table_vehicle_desc);
	} else {
		for (VehicleType vt = VEH_BEGIN; vt < VEH_END; vt++) {
			non_table_descs.push_back(SlFilterNamedSaveLoadTable(GetVehicleDescription(vt)));
		}
	}

	int index;
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

		SlObjectLoadFiltered(v, is_table ? slt : non_table_descs[vtype]);

		if (_cargo_count != 0 && IsCompanyBuildableVehicleType(v) && CargoPacket::CanAllocateItem()) {
			/* Don't construct the packet with station here, because that'll fail with old savegames */
			CargoPacket *cp = new CargoPacket(_cargo_count, _cargo_periods, _cargo_source, _cargo_source_xy, _cargo_feeder_share);
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
			AssignBit(v->vehicle_flags, VF_SEPARATION_ACTIVE, _old_ahead_separation);
		}

		if (SlXvIsFeaturePresent(XSLFI_TIMETABLES_START_TICKS, 2, 2) && v->timetable_start != 0 && _old_timetable_start_subticks != 0) {
			_old_timetable_start_subticks_map[v->index] = _old_timetable_start_subticks;
		}

		if (vtype == VEH_ROAD && !_path_td.empty() && _path_td.size() <= RV_PATH_CACHE_SEGMENTS && _path_td.size() == _path_tile.size()) {
			RoadVehicle *rv = RoadVehicle::From(v);
			rv->cached_path.reset(new RoadVehPathCache());
			rv->cached_path->count = (uint8_t)_path_td.size();
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
	SaveLoadTableData slt = SlPrepareNamedSaveLoadTableForPtrOrNull(_table_vehicle_desc);

	for (Vehicle *v : Vehicle::Iterate()) {
		if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) _cpp_packets = std::move(_veh_cpp_packets[v->index]);
		SlObjectPtrOrNullFiltered(v, slt);
		if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) _veh_cpp_packets[v->index] = std::move(_cpp_packets);
	}
}

void Load_VEOX()
{
	extern NamedSaveLoadTable GetOrderExtraInfoDescription();
	std::vector<SaveLoad> slt = SlFilterNamedSaveLoadTable(GetOrderExtraInfoDescription());

	/* load extended order info for vehicle current order */
	int index;
	while ((index = SlIterateArray()) != -1) {
		Vehicle *v = Vehicle::GetIfValid(index);
		assert(v != nullptr);
		v->current_order.AllocExtraInfo();
		SlObject(v->current_order.extra.get(), slt);
	}
}

const NamedSaveLoadTable GetVehicleSpeedRestrictionDescription()
{
	static const NamedSaveLoad _vehicle_speed_restriction_desc[] = {
		NSL("distance",   SLE_VAR(PendingSpeedRestrictionChange, distance,                 SLE_UINT16)),
		NSL("new_speed",  SLE_VAR(PendingSpeedRestrictionChange, new_speed,                SLE_UINT16)),
		NSL("prev_speed", SLE_VAR(PendingSpeedRestrictionChange, prev_speed,               SLE_UINT16)),
		NSL("flags",      SLE_VAR(PendingSpeedRestrictionChange, flags,                    SLE_UINT16)),
	};

	return _vehicle_speed_restriction_desc;
}

void Save_VESR()
{
	SaveLoadTableData slt = SlTableHeader(GetVehicleSpeedRestrictionDescription());

	for (auto &it : _pending_speed_restriction_change_map) {
		SlSetArrayIndex(it.first);
		PendingSpeedRestrictionChange *ptr = &(it.second);
		SlObjectSaveFiltered(ptr, slt);
	}
}

void Load_VESR()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(GetVehicleSpeedRestrictionDescription());

	int index;
	while ((index = SlIterateArray()) != -1) {
		auto iter = _pending_speed_restriction_change_map.insert({ static_cast<VehicleID>(index), {} });
		PendingSpeedRestrictionChange *ptr = &(iter->second);
		SlObjectLoadFiltered(ptr, slt);
	}
}

struct vehicle_venc {
	VehicleID id;
	VehicleCache vcache;
};

struct train_venc {
	VehicleID id;
	GroundVehicleCache gvcache;
	uint8_t cached_tflags;
	uint8_t cached_num_engines;
	uint16_t cached_centre_mass;
	uint16_t cached_braking_length;
	uint16_t cached_veh_weight;
	uint16_t cached_uncapped_decel;
	uint8_t cached_deceleration;
	uint8_t user_def_data;
	int16_t cached_curve_speed_mod;
	uint16_t cached_max_curve_speed;
};

struct roadvehicle_venc {
	VehicleID id;
	GroundVehicleCache gvcache;
};

struct aircraft_venc {
	VehicleID id;
	uint16_t cached_max_range;
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

	SlAutolength([]() {
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
			SlWriteByte(t->tcache.cached_tflags);
			SlWriteByte(t->tcache.cached_num_engines);
			SlWriteUint16(t->tcache.cached_centre_mass);
			SlWriteUint16(t->tcache.cached_braking_length);
			SlWriteUint16(t->tcache.cached_veh_weight);
			SlWriteUint16(t->tcache.cached_uncapped_decel);
			SlWriteByte(t->tcache.cached_deceleration);
			SlWriteByte(t->tcache.user_def_data);
			SlWriteUint16((uint16_t)t->tcache.cached_curve_speed_mod);
			SlWriteUint16(t->tcache.cached_max_curve_speed);
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
	});
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
		venc.cached_tflags = SlReadByte();
		venc.cached_num_engines = SlReadByte();
		venc.cached_centre_mass = SlReadUint16();
		venc.cached_braking_length = SlReadUint16();
		venc.cached_veh_weight = SlReadUint16();
		venc.cached_uncapped_decel = SlReadUint16();
		venc.cached_deceleration = SlReadByte();
		venc.user_def_data = SlReadByte();
		venc.cached_curve_speed_mod = (int16_t)SlReadUint16();
		venc.cached_max_curve_speed = SlReadUint16();
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
	Debug(desync, 0, "{}", log_buffer);
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
			AssignBit(v->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT, HasBit(venc.vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT));
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
			a->acache.cached_max_range_sqr = (uint32_t)venc.cached_max_range * (uint32_t)venc.cached_max_range;
			LogVehicleVENCMessage(a, "cached_max_range");
		}
	}
}

static ChunkSaveLoadSpecialOpResult Special_VENC(uint32_t chunk_id, ChunkSaveLoadSpecialOp op)
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

void Load_VLKA()
{
	std::vector<SaveLoad> lookahead_desc = SlFilterNamedSaveLoadTable(GetVehicleLookAheadDescription());
	std::vector<SaveLoad> item_desc = SlFilterNamedSaveLoadTable(GetVehicleLookAheadItemDescription());
	std::vector<SaveLoad> curve_desc = SlFilterNamedSaveLoadTable(GetVehicleLookAheadCurveDescription());

	int index;
	while ((index = SlIterateArray()) != -1) {
		Train *t = Train::GetIfValid(index);
		assert(t != nullptr);
		t->lookahead.reset(new TrainReservationLookAhead());
		SlObjectLoadFiltered(t->lookahead.get(), lookahead_desc);
		uint32_t items = SlReadUint32();
		t->lookahead->items.resize(items);
		for (uint i = 0; i < items; i++) {
			SlObjectLoadFiltered(&t->lookahead->items[i], item_desc);
		}
		uint32_t curves = SlReadUint32();
		t->lookahead->curves.resize(curves);
		for (uint i = 0; i < curves; i++) {
			SlObjectLoadFiltered(&t->lookahead->curves[i], curve_desc);
		}
	}
}

void Load_VUBS()
{
	std::vector<SaveLoad> unbunch_desc = SlFilterNamedSaveLoadTable(GetVehicleUnbunchStateDescription());

	int index;
	while ((index = SlIterateArray()) != -1) {
		Vehicle *v = Vehicle::GetIfValid(index);
		assert(v != nullptr);
		v->unbunch_state.reset(new VehicleUnbunchState());
		SlObjectLoadFiltered(v->unbunch_state.get(), unbunch_desc);
	}
}

static const ChunkHandler veh_chunk_handlers[] = {
	{ 'VEHS', Save_VEHS, Load_VEHS, Ptrs_VEHS, nullptr, CH_SPARSE_TABLE },
	{ 'VEOX', nullptr,   Load_VEOX, nullptr,   nullptr, CH_READONLY },
	{ 'VESR', Save_VESR, Load_VESR, nullptr,   nullptr, CH_SPARSE_TABLE },
	{ 'VENC', Save_VENC, Load_VENC, nullptr,   nullptr, CH_RIFF,         Special_VENC },
	{ 'VLKA', nullptr,   Load_VLKA, nullptr,   nullptr, CH_READONLY },
	{ 'VUBS', nullptr,   Load_VUBS, nullptr,   nullptr, CH_READONLY },
};

extern const ChunkHandlerTable _veh_chunk_handlers(veh_chunk_handlers);
