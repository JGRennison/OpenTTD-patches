/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_engine.cpp NewGRF handling of engines. */

#include "stdafx.h"
#include "debug.h"
#include "train.h"
#include "roadveh.h"
#include "company_func.h"
#include "newgrf_badge.h"
#include "newgrf_cargo.h"
#include "newgrf_spritegroup.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "core/random_func.hpp"
#include "core/container_func.hpp"
#include "aircraft.h"
#include "station_base.h"
#include "company_base.h"
#include "newgrf_railtype.h"
#include "newgrf_roadtype.h"
#include "newgrf_cache_check.h"
#include "ship.h"
#include "scope_info.h"
#include "newgrf_extension.h"
#include "newgrf_analysis.h"
#include "newgrf_dump.h"
#include "engine_override.h"
#include "core/format.hpp"
#include "3rdparty/fmt/ranges.h"

#include "safeguards.h"

bool _sprite_group_resolve_check_veh_check = false;
bool _sprite_group_resolve_check_veh_curvature_check = false;

void SetWagonOverrideSprites(EngineID engine, CargoType cargo, const SpriteGroup *group, std::span<EngineID> engine_ids)
{
	Engine *e = Engine::Get(engine);

	assert(cargo < NUM_CARGO + 2); // Include SpriteGroupCargo::SG_DEFAULT and SpriteGroupCargo::SG_PURCHASE pseudo cargoes.

	WagonOverride *wo = &e->overrides.emplace_back();
	wo->group = group;
	wo->cargo = cargo;
	wo->engines.assign(engine_ids.begin(), engine_ids.end());
}

const SpriteGroup *GetWagonOverrideSpriteSet(EngineID engine, CargoType cargo, EngineID overriding_engine)
{
	const Engine *e = Engine::Get(engine);

	for (const WagonOverride &wo : e->overrides) {
		if (wo.cargo != cargo && wo.cargo != SpriteGroupCargo::SG_DEFAULT) continue;
		if (std::ranges::find(wo.engines, overriding_engine) != wo.engines.end()) return wo.group;
	}
	return nullptr;
}

void SetCustomEngineSprites(EngineID engine, CargoType cargo, const SpriteGroup *group)
{
	Engine *e = Engine::Get(engine);

	if (e->grf_prop.GetSpriteGroup(cargo) != nullptr) {
		GrfMsg(6, "SetCustomEngineSprites: engine {} cargo {} already has group -- replacing", engine, cargo);
	}
	e->grf_prop.SetSpriteGroup(cargo, group);
}


/**
 * Tie a GRFFile entry to an engine, to allow us to retrieve GRF parameters
 * etc during a game.
 * @param engine Engine ID to tie the GRFFile to.
 * @param file   Pointer of GRFFile to tie.
 */
void SetEngineGRF(EngineID engine, const GRFFile *file)
{
	Engine *e = Engine::Get(engine);
	e->grf_prop.grfid = file->grfid;
	e->grf_prop.grffile = file;
}


static int MapOldSubType(const Vehicle *v)
{
	switch (v->type) {
		case VEH_TRAIN:
			if (Train::From(v)->IsEngine()) return 0;
			if (Train::From(v)->IsFreeWagon()) return 4;
			return 2;
		case VEH_ROAD:
		case VEH_SHIP:     return 0;
		case VEH_AIRCRAFT:
		case VEH_DISASTER: return v->subtype;
		case VEH_EFFECT:   return v->subtype << 1;
		default: NOT_REACHED();
	}
}


/* TTDP style aircraft movement states for GRF Action 2 Var 0xE2 */
enum TTDPAircraftMovementStates : uint8_t {
	AMS_TTDP_HANGAR,
	AMS_TTDP_TO_HANGAR,
	AMS_TTDP_TO_PAD1,
	AMS_TTDP_TO_PAD2,
	AMS_TTDP_TO_PAD3,
	AMS_TTDP_TO_ENTRY_2_AND_3,
	AMS_TTDP_TO_ENTRY_2_AND_3_AND_H,
	AMS_TTDP_TO_JUNCTION,
	AMS_TTDP_LEAVE_RUNWAY,
	AMS_TTDP_TO_INWAY,
	AMS_TTDP_TO_RUNWAY,
	AMS_TTDP_TO_OUTWAY,
	AMS_TTDP_WAITING,
	AMS_TTDP_TAKEOFF,
	AMS_TTDP_TO_TAKEOFF,
	AMS_TTDP_CLIMBING,
	AMS_TTDP_FLIGHT_APPROACH,
	AMS_TTDP_UNUSED_0x11,
	AMS_TTDP_FLIGHT_TO_TOWER,
	AMS_TTDP_UNUSED_0x13,
	AMS_TTDP_FLIGHT_FINAL,
	AMS_TTDP_FLIGHT_DESCENT,
	AMS_TTDP_BRAKING,
	AMS_TTDP_HELI_TAKEOFF_AIRPORT,
	AMS_TTDP_HELI_TO_TAKEOFF_AIRPORT,
	AMS_TTDP_HELI_LAND_AIRPORT,
	AMS_TTDP_HELI_TAKEOFF_HELIPORT,
	AMS_TTDP_HELI_TO_TAKEOFF_HELIPORT,
	AMS_TTDP_HELI_LAND_HELIPORT,
};


/**
 * Map OTTD aircraft movement states to TTDPatch style movement states
 * (VarAction 2 Variable 0xE2)
 */
uint8_t MapAircraftMovementState(const Aircraft *v)
{
	const Station *st = GetTargetAirportIfValid(v);
	if (st == nullptr) return AMS_TTDP_FLIGHT_TO_TOWER;

	const AirportFTAClass *afc = st->airport.GetFTA();
	uint16_t amdflag = afc->MovingData(v->pos)->flag;

	switch (v->state) {
		case HANGAR:
			/* The international airport is a special case as helicopters can land in
			 * front of the hangar. Helicopters also change their air.state to
			 * AMED_HELI_LOWER some time before actually descending. */

			/* This condition only occurs for helicopters, during descent,
			 * to a landing by the hangar of an international airport. */
			if (amdflag & AMED_HELI_LOWER) return AMS_TTDP_HELI_LAND_AIRPORT;

			/* This condition only occurs for helicopters, before starting descent,
			 * to a landing by the hangar of an international airport. */
			if (amdflag & AMED_SLOWTURN) return AMS_TTDP_FLIGHT_TO_TOWER;

			/* The final two conditions apply to helicopters or aircraft.
			 * Has reached hangar? */
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_HANGAR;

			/* Still moving towards hangar. */
			return AMS_TTDP_TO_HANGAR;

		case TERM1:
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_TO_PAD1;
			return AMS_TTDP_TO_JUNCTION;

		case TERM2:
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_TO_PAD2;
			return AMS_TTDP_TO_ENTRY_2_AND_3_AND_H;

		case TERM3:
		case TERM4:
		case TERM5:
		case TERM6:
		case TERM7:
		case TERM8:
			/* TTDPatch only has 3 terminals, so treat these states the same */
			if (amdflag & AMED_EXACTPOS) return AMS_TTDP_TO_PAD3;
			return AMS_TTDP_TO_ENTRY_2_AND_3_AND_H;

		case HELIPAD1:
		case HELIPAD2:
		case HELIPAD3:
			/* Will only occur for helicopters.*/
			if (amdflag & AMED_HELI_LOWER) return AMS_TTDP_HELI_LAND_AIRPORT; // Descending.
			if (amdflag & AMED_SLOWTURN)   return AMS_TTDP_FLIGHT_TO_TOWER;   // Still hasn't started descent.
			return AMS_TTDP_TO_JUNCTION; // On the ground.

		case TAKEOFF: // Moving to takeoff position.
			return AMS_TTDP_TO_OUTWAY;

		case STARTTAKEOFF: // Accelerating down runway.
			return AMS_TTDP_TAKEOFF;

		case ENDTAKEOFF: // Ascent
			return AMS_TTDP_CLIMBING;

		case HELITAKEOFF: // Helicopter is moving to take off position.
			if (afc->delta_z == 0) {
				return amdflag & AMED_HELI_RAISE ?
					AMS_TTDP_HELI_TAKEOFF_AIRPORT : AMS_TTDP_TO_JUNCTION;
			} else {
				return AMS_TTDP_HELI_TAKEOFF_HELIPORT;
			}

		case FLYING:
			return amdflag & AMED_HOLD ? AMS_TTDP_FLIGHT_APPROACH : AMS_TTDP_FLIGHT_TO_TOWER;

		case LANDING: // Descent
			return AMS_TTDP_FLIGHT_DESCENT;

		case ENDLANDING: // On the runway braking
			if (amdflag & AMED_BRAKE) return AMS_TTDP_BRAKING;
			/* Landed - moving off runway */
			return AMS_TTDP_TO_INWAY;

		case HELILANDING:
		case HELIENDLANDING: // Helicoptor is descending.
			if (amdflag & AMED_HELI_LOWER) {
				return afc->delta_z == 0 ?
					AMS_TTDP_HELI_LAND_AIRPORT : AMS_TTDP_HELI_LAND_HELIPORT;
			} else {
				return AMS_TTDP_FLIGHT_TO_TOWER;
			}

		default:
			return AMS_TTDP_HANGAR;
	}
}


/* TTDP style aircraft movement action for GRF Action 2 Var 0xE6 */
enum TTDPAircraftMovementActions : uint8_t {
	AMA_TTDP_IN_HANGAR,
	AMA_TTDP_ON_PAD1,
	AMA_TTDP_ON_PAD2,
	AMA_TTDP_ON_PAD3,
	AMA_TTDP_HANGAR_TO_PAD1,
	AMA_TTDP_HANGAR_TO_PAD2,
	AMA_TTDP_HANGAR_TO_PAD3,
	AMA_TTDP_LANDING_TO_PAD1,
	AMA_TTDP_LANDING_TO_PAD2,
	AMA_TTDP_LANDING_TO_PAD3,
	AMA_TTDP_PAD1_TO_HANGAR,
	AMA_TTDP_PAD2_TO_HANGAR,
	AMA_TTDP_PAD3_TO_HANGAR,
	AMA_TTDP_PAD1_TO_TAKEOFF,
	AMA_TTDP_PAD2_TO_TAKEOFF,
	AMA_TTDP_PAD3_TO_TAKEOFF,
	AMA_TTDP_HANGAR_TO_TAKOFF,
	AMA_TTDP_LANDING_TO_HANGAR,
	AMA_TTDP_IN_FLIGHT,
};


/**
 * Map OTTD aircraft movement states to TTDPatch style movement actions
 * (VarAction 2 Variable 0xE6)
 * This is not fully supported yet but it's enough for Planeset.
 */
static uint8_t MapAircraftMovementAction(const Aircraft *v)
{
	switch (v->state) {
		case HANGAR:
			return (v->cur_speed > 0) ? AMA_TTDP_LANDING_TO_HANGAR : AMA_TTDP_IN_HANGAR;

		case TERM1:
		case HELIPAD1:
			return (v->current_order.IsType(OT_LOADING)) ? AMA_TTDP_ON_PAD1 : AMA_TTDP_LANDING_TO_PAD1;

		case TERM2:
		case HELIPAD2:
			return (v->current_order.IsType(OT_LOADING)) ? AMA_TTDP_ON_PAD2 : AMA_TTDP_LANDING_TO_PAD2;

		case TERM3:
		case TERM4:
		case TERM5:
		case TERM6:
		case TERM7:
		case TERM8:
		case HELIPAD3:
			return (v->current_order.IsType(OT_LOADING)) ? AMA_TTDP_ON_PAD3 : AMA_TTDP_LANDING_TO_PAD3;

		case TAKEOFF:      // Moving to takeoff position
		case STARTTAKEOFF: // Accelerating down runway
		case ENDTAKEOFF:   // Ascent
		case HELITAKEOFF:
			/* @todo Need to find which terminal (or hangar) we've come from. How? */
			return AMA_TTDP_PAD1_TO_TAKEOFF;

		case FLYING:
			return AMA_TTDP_IN_FLIGHT;

		case LANDING:    // Descent
		case ENDLANDING: // On the runway braking
		case HELILANDING:
		case HELIENDLANDING:
			/* @todo Need to check terminal we're landing to. Is it known yet? */
			return (v->current_order.IsType(OT_GOTO_DEPOT)) ?
				AMA_TTDP_LANDING_TO_HANGAR : AMA_TTDP_LANDING_TO_PAD1;

		default:
			return AMA_TTDP_IN_HANGAR;
	}
}


/* virtual */ uint32_t VehicleScopeResolver::GetRandomBits() const
{
	return this->v == nullptr ? 0 : this->v->random_bits;
}

/* virtual */ uint32_t VehicleScopeResolver::GetTriggers() const
{
	if (this->v == nullptr) {
		return 0;
	} else {
		if (_sprite_group_resolve_check_veh_check) {
			SetBit(const_cast<Vehicle*>(this->v->First())->vcache.cached_veh_flags, VCF_REDRAW_ON_TRIGGER);
		}
		return this->v->waiting_triggers;
	}
	return this->v == nullptr ? 0 : this->v->waiting_triggers;
}


/* virtual */ ScopeResolver *VehicleResolverObject::GetScope(VarSpriteGroupScope scope, VarSpriteGroupScopeOffset relative)
{
	switch (scope) {
		case VSG_SCOPE_SELF:   return &this->self_scope;
		case VSG_SCOPE_PARENT: return &this->parent_scope;
		case VSG_SCOPE_RELATIVE: {
			int32_t count = GB(relative, 0, 8);
			if (this->self_scope.v != nullptr && (relative != this->cached_relative_count || HasBit(relative, 15))) {
				/* Note: This caching only works as long as the VSG_SCOPE_RELATIVE cannot be used in
				 *       VarAct2 with procedure calls. */
				/* Therefore procedure calls made from within a relative scope must save and restore the cached relative scope */
				if (HasBit(relative, 15)) count = GetRegister(0x100);

				const Vehicle *v = nullptr;
				switch (GB(relative, 8, 2)) {
					default: NOT_REACHED();
					case VSGSRM_BACKWARD_SELF: // count back (away from the engine), starting at this vehicle
						v = this->self_scope.v;
						break;
					case VSGSRM_FORWARD_SELF: // count forward (toward the engine), starting at this vehicle
						v = this->self_scope.v;
						count = -count;
						break;
					case VSGSRM_BACKWARD_ENGINE: // count back, starting at the engine
						v = this->parent_scope.v;
						break;
					case VSGSRM_BACKWARD_SAMEID: { // count back, starting at the first vehicle in this chain of vehicles with the same ID, as for vehicle variable 41
						const Vehicle *self = this->self_scope.v;
						for (const Vehicle *u = self->First(); u != self; u = u->Next()) {
							if (u->engine_type != self->engine_type) {
								v = nullptr;
							} else {
								if (v == nullptr) v = u;
							}
						}
						if (v == nullptr) v = self;
						break;
					}
				}
				this->relative_scope.SetVehicle(v->Move(count));
			}
			return &this->relative_scope;
		}
		default: return ResolverObject::GetScope(scope, relative);
	}
}

/**
 * Determines the livery of an engine.
 *
 * This always uses dual company colours independent of GUI settings. So it is desync-safe.
 *
 * @param engine Engine type
 * @param v Vehicle, nullptr in purchase list.
 * @return Livery to use
 */
static const Livery *LiveryHelper(EngineID engine, const Vehicle *v)
{
	const Livery *l;

	if (v == nullptr) {
		if (!Company::IsValidID(_current_company)) return nullptr;
		l = GetEngineLivery(engine, _current_company, INVALID_ENGINE, nullptr, LIT_ALL);
	} else if (v->IsGroundVehicle()) {
		l = GetEngineLivery(v->engine_type, v->owner, v->GetGroundVehicleCache()->first_engine, v, LIT_ALL);
	} else {
		l = GetEngineLivery(v->engine_type, v->owner, INVALID_ENGINE, v, LIT_ALL);
	}

	return l;
}

/**
 * Helper to get the position of a vehicle within a chain of vehicles.
 * @param v the vehicle to get the position of.
 * @param consecutive whether to look at the whole chain or the vehicles
 *                    with the same 'engine type'.
 * @return the position in the chain from front and tail and chain length.
 */
static uint32_t PositionHelper(const Vehicle *v, bool consecutive)
{
	const Vehicle *u;
	uint8_t chain_before = 0;
	uint8_t chain_after  = 0;

	for (u = v->First(); u != v; u = u->Next()) {
		chain_before++;
		if (consecutive && u->engine_type != v->engine_type) chain_before = 0;
	}

	while (u->Next() != nullptr && (!consecutive || u->Next()->engine_type == v->engine_type)) {
		chain_after++;
		u = u->Next();
	}

	return chain_before | chain_after << 8 | (chain_before + chain_after + consecutive) << 16;
}

static uint32_t VehicleGetVariable(Vehicle *v, const VehicleScopeResolver *object, uint16_t variable, uint32_t parameter, GetVariableExtra &extra)
{
	if (_sprite_group_resolve_check_veh_check) {
		switch (variable) {
			case 0xC:
			case 0x10:
			case 0x18:
			case 0x1A:
			case 0x1C:
			case 0x25:
			case 0x40:
			case 0x41:
			case 0x42:
			case 0x43:
			case 0x47:
			case 0x48:
			case 0x49:
			case 0x4A:
			case 0x4B:
			case 0x4D:
			case 0x60:
			case 0x61:
			case 0x7A:
			case 0x7D:
			case 0x7F:
			case 0x80 + 0x0:
			case 0x80 + 0x1:
			case 0x80 + 0x4:
			case 0x80 + 0x5:
			case 0x80 + 0xA: // dubious
			case 0x80 + 0xB: // dubious
			case 0x80 + 0x39:
			case 0x80 + 0x3A:
			case 0x80 + 0x3B:
			case 0x80 + 0x3C:
			case 0x80 + 0x3D:
			case 0x80 + 0x44:
			case 0x80 + 0x45:
			case 0x80 + 0x46:
			case 0x80 + 0x47:
			case 0x80 + 0x5A:
			case 0x80 + 0x72:
			case 0x80 + 0x7A:
			case 0xFF:
				break;

			case 0x80 + 0x32:
				if (extra.mask & (VS_HIDDEN | VS_TRAIN_SLOWING)) {
					_sprite_group_resolve_check_veh_check = false;
				}
				break;

			case 0x80 + 0x34:
			case 0x80 + 0x35:
			case A2VRI_VEHICLE_CURRENT_SPEED_SCALED:
				if (v->type == VEH_AIRCRAFT) {
					_sprite_group_resolve_check_veh_check = false;
				} else {
					SetBit(v->First()->vcache.cached_veh_flags, VCF_REDRAW_ON_SPEED_CHANGE);
				}
				break;

			case 0x5F:
			case 0x80 + 0x7B:
				SetBit(v->First()->vcache.cached_veh_flags, VCF_REDRAW_ON_TRIGGER);
				break;

			case 0x80 + 0x48:
				// VRF_REVERSE_DIRECTION
				if (v->type != VEH_TRAIN) {
					_sprite_group_resolve_check_veh_check = false;
				}
				break;

			case 0x80 + 0x62:
				switch (v->type) {
					case VEH_TRAIN:
					case VEH_SHIP:
						if (extra.mask & 0x7F) {
							_sprite_group_resolve_check_veh_check = false;
						}
						break;

					case VEH_ROAD:
						break;

					case VEH_AIRCRAFT:
						if (v == v->First()) {
							SetBit(v->First()->vcache.cached_veh_flags, VCF_REDRAW_ON_SPEED_CHANGE);
						} else {
							_sprite_group_resolve_check_veh_check = false;
						}
						break;

					default:
						_sprite_group_resolve_check_veh_check = false;
						break;
				}
				break;

			case 0xFE:
				// vehicle is unloading, VF_CARGO_UNLOADING may disappear without the vehicle being marked dirty
				// the vehicle is always marked dirty when VF_CARGO_UNLOADING is set
				if (HasBit(v->vehicle_flags, VF_CARGO_UNLOADING)) {
					_sprite_group_resolve_check_veh_check = false;
				}
				break;

			default:
				_sprite_group_resolve_check_veh_check = false;
				break;
		}
	}

	/* Calculated vehicle parameters */
	switch (variable) {
		case 0x25: // Get engine GRF ID
			return v->GetGRFID();

		case 0x40: // Get length of consist
			if (!HasBit(v->grf_cache.cache_valid, NCVV_POSITION_CONSIST_LENGTH)) {
				v->grf_cache.position_consist_length = PositionHelper(v, false);
				SetBit(v->grf_cache.cache_valid, NCVV_POSITION_CONSIST_LENGTH);
			}
			return v->grf_cache.position_consist_length;

		case 0x41: // Get length of same consecutive wagons
			if (!HasBit(v->grf_cache.cache_valid, NCVV_POSITION_SAME_ID_LENGTH)) {
				v->grf_cache.position_same_id_length = PositionHelper(v, true);
				SetBit(v->grf_cache.cache_valid, NCVV_POSITION_SAME_ID_LENGTH);
			}
			return v->grf_cache.position_same_id_length;

		case 0x42: { // Consist cargo information
			if ((extra.mask & 0x00FFFFFF) == 0) {
				if (!HasBit(v->grf_cache.cache_valid, NCVV_CONSIST_CARGO_INFORMATION_UD)) {
					uint8_t user_def_data = 0;
					if (v->type == VEH_TRAIN) {
						for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
							user_def_data |= Train::From(u)->tcache.user_def_data;
						}
					}
					SB(v->grf_cache.consist_cargo_information, 24, 8, user_def_data);
					SetBit(v->grf_cache.cache_valid, NCVV_CONSIST_CARGO_INFORMATION_UD);
				}
				return (v->grf_cache.consist_cargo_information & 0xFF000000);
			}
			if (!HasBit(v->grf_cache.cache_valid, NCVV_CONSIST_CARGO_INFORMATION)) {
				std::array<uint8_t, NUM_CARGO> common_cargoes{};
				uint8_t cargo_classes = 0;
				uint8_t user_def_data = 0;

				for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
					if (v->type == VEH_TRAIN) user_def_data |= Train::From(u)->tcache.user_def_data;

					/* Skip empty engines */
					if (!u->GetEngine()->CanCarryCargo()) continue;

					cargo_classes |= CargoSpec::Get(u->cargo_type)->classes.base();
					common_cargoes[u->cargo_type]++;
				}

				/* Pick the most common cargo type */
				auto cargo_it = std::max_element(std::begin(common_cargoes), std::end(common_cargoes));
				/* Return INVALID_CARGO if nothing is carried */
				CargoType common_cargo_type = (*cargo_it == 0) ? INVALID_CARGO : static_cast<CargoType>(std::distance(std::begin(common_cargoes), cargo_it));

				/* Count subcargo types of common_cargo_type */
				std::array<uint8_t, UINT8_MAX + 1> common_subtypes{};
				for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
					/* Skip empty engines and engines not carrying common_cargo_type */
					if (u->cargo_type != common_cargo_type || !u->GetEngine()->CanCarryCargo()) continue;

					common_subtypes[u->cargo_subtype]++;
				}

				/* Pick the most common subcargo type*/
				auto subtype_it = std::max_element(std::begin(common_subtypes), std::end(common_subtypes));
				/* Return UINT8_MAX if nothing is carried */
				uint8_t common_subtype = (*subtype_it == 0) ? UINT8_MAX : static_cast<uint8_t>(std::distance(std::begin(common_subtypes), subtype_it));

				/* Note: We have to store the untranslated cargotype in the cache as the cache can be read by different NewGRFs,
				 *       which will need different translations */
				v->grf_cache.consist_cargo_information = cargo_classes | (common_cargo_type << 8) | (common_subtype << 16) | (user_def_data << 24);
				SetBit(v->grf_cache.cache_valid, NCVV_CONSIST_CARGO_INFORMATION);
				SetBit(v->grf_cache.cache_valid, NCVV_CONSIST_CARGO_INFORMATION_UD);
			}

			/* The cargo translation is specific to the accessing GRF, and thus cannot be cached. */
			CargoType common_cargo_type = (v->grf_cache.consist_cargo_information >> 8) & 0xFF;

			/* Note:
			 *  - Unlike everywhere else the cargo translation table is only used since grf version 8, not 7.
			 *  - For translating the cargo type we need to use the GRF which is resolving the variable, which
			 *    is object->ro.grffile.
			 *    In case of CBID_TRAIN_ALLOW_WAGON_ATTACH this is not the same as v->GetGRF().
			 *  - The grffile == nullptr case only happens if this function is called for default vehicles.
			 *    And this is only done by CheckCaches().
			 */
			const GRFFile *grffile = object->ro.grffile;
			uint8_t common_bitnum = (common_cargo_type == INVALID_CARGO) ? 0xFF :
				(grffile == nullptr || grffile->grf_version < 8) ? CargoSpec::Get(common_cargo_type)->bitnum : grffile->cargo_map[common_cargo_type];

			return (v->grf_cache.consist_cargo_information & 0xFFFF00FF) | common_bitnum << 8;
		}

		case 0x43: // Company information
			if (!HasBit(v->grf_cache.cache_valid, NCVV_COMPANY_INFORMATION)) {
				v->grf_cache.company_information = GetCompanyInfo(v->owner, LiveryHelper(v->engine_type, v));
				SetBit(v->grf_cache.cache_valid, NCVV_COMPANY_INFORMATION);
			}
			return v->grf_cache.company_information;

		case 0x44: // Aircraft information
			if (v->type != VEH_AIRCRAFT || !Aircraft::From(v)->IsNormalAircraft()) return UINT_MAX;

			{
				const Vehicle *w = v->Next();
				assert(w != nullptr);
				uint16_t altitude = ClampTo<uint16_t>(v->z_pos - w->z_pos); // Aircraft height - shadow height
				uint8_t airporttype = ATP_TTDP_LARGE;

				const Station *st = GetTargetAirportIfValid(Aircraft::From(v));

				if (st != nullptr && st->airport.tile != INVALID_TILE) {
					airporttype = st->airport.GetSpec()->ttd_airport_type;
				}

				return (ClampTo<uint8_t>(altitude) << 8) | airporttype;
			}

		case 0x45: { // Curvature info
			/* Format: xxxTxBxF
			 * F - previous wagon to current wagon, 0 if vehicle is first
			 * B - current wagon to next wagon, 0 if wagon is last
			 * T - previous wagon to next wagon, 0 in an S-bend
			 */
			if (!v->IsGroundVehicle()) return 0;

			_sprite_group_resolve_check_veh_curvature_check = false;

			const Vehicle *u_p = v->Previous();
			const Vehicle *u_n = v->Next();
			DirDiff f = (u_p == nullptr) ?  DIRDIFF_SAME : DirDifference(u_p->direction, v->direction);
			DirDiff b = (u_n == nullptr) ?  DIRDIFF_SAME : DirDifference(v->direction, u_n->direction);
			DirDiff t = ChangeDirDiff(f, b);

			return ((t > DIRDIFF_REVERSE ? t | 8 : t) << 16) |
			       ((b > DIRDIFF_REVERSE ? b | 8 : b) <<  8) |
			       ( f > DIRDIFF_REVERSE ? f | 8 : f);
		}

		case 0x46: // Motion counter
			return v->First()->motion_counter;

		case 0x47: { // Vehicle cargo info
			/* Format: ccccwwtt
			 * tt - the cargo type transported by the vehicle,
			 *     translated if a translation table has been installed.
			 * ww - cargo unit weight in 1/16 tons, same as cargo prop. 0F.
			 * cccc - the cargo class value of the cargo transported by the vehicle.
			 */
			const CargoSpec *cs = CargoSpec::Get(v->cargo_type);

			/* Note:
			 * For translating the cargo type we need to use the GRF which is resolving the variable, which
			 * is object->ro.grffile.
			 * In case of CBID_TRAIN_ALLOW_WAGON_ATTACH this is not the same as v->GetGRF().
			 */
			return (cs->classes.base() << 16) | (cs->weight << 8) | object->ro.grffile->cargo_map[v->cargo_type];
		}

		case 0x48: return v->GetEngine()->flags.base(); // Vehicle Type Info
		case 0x49: return v->build_year.base();

		case 0x4A:
			switch (v->type) {
				case VEH_TRAIN: {
					if (Train::From(v)->IsVirtual()) {
						return 0x1FF | (GetRailTypeInfo(Train::From(v)->railtype)->flags.Test(RailTypeFlag::Catenary) ? 0x200 : 0);
					}
					RailType rt = GetTileRailTypeByTrackBit(v->tile, Train::From(v)->track);
					const RailTypeInfo *rti = GetRailTypeInfo(rt);
					return (rti->flags.Test(RailTypeFlag::Catenary) ? 0x200 : 0) |
						(HasPowerOnRail(Train::From(v)->railtype, rt) ? 0x100 : 0) |
						GetReverseRailTypeTranslation(rt, object->ro.grffile);
				}

				case VEH_ROAD: {
					RoadType rt = GetRoadType(v->tile, GetRoadTramType(RoadVehicle::From(v)->roadtype));
					if (rt == INVALID_ROADTYPE) return 0xFF;
					const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
					return (rti->flags.Test(RoadTypeFlag::Catenary) ? 0x200 : 0) |
						0x100 |
						GetReverseRoadTypeTranslation(rt, object->ro.grffile);
				}

				default:
					return 0;
			}

		case 0x4B: // Long date of last service
			return v->date_of_last_service_newgrf.base();

		case 0x4C: // Current maximum speed in NewGRF units
			if (!v->IsPrimaryVehicle()) return 0;
			return v->GetCurrentMaxSpeed();

		case 0x4D: // Position within articulated vehicle
			if (!HasBit(v->grf_cache.cache_valid, NCVV_POSITION_IN_VEHICLE)) {
				uint8_t artic_before = 0;
				for (const Vehicle *u = v; u->IsArticulatedPart(); u = u->Previous()) artic_before++;
				uint8_t artic_after = 0;
				for (const Vehicle *u = v; u->HasArticulatedPart(); u = u->Next()) artic_after++;
				v->grf_cache.position_in_vehicle = artic_before | artic_after << 8;
				SetBit(v->grf_cache.cache_valid, NCVV_POSITION_IN_VEHICLE);
			}
			return v->grf_cache.position_in_vehicle;

		/* Variables which use the parameter */
		case 0x60: // Count consist's engine ID occurrence
			if (v->type != VEH_TRAIN && v->type != VEH_SHIP) return v->GetEngine()->grf_prop.local_id == parameter ? 1 : 0;

			{
				uint count = 0;
				for (; v != nullptr; v = v->Next()) {
					if (v->GetEngine()->grf_prop.local_id == parameter) count++;
				}
				return count;
			}

		case 0x61: // Get variable of n-th vehicle in chain [signed number relative to vehicle]
			if (!(v->IsGroundVehicle() || v->type == VEH_SHIP) || parameter == 0x61) {
				/* Not available */
				break;
			}

			/* Only allow callbacks that don't change properties to avoid circular dependencies. */
			if (object->ro.callback == CBID_NO_CALLBACK || object->ro.callback == CBID_RANDOM_TRIGGER || object->ro.callback == CBID_TRAIN_ALLOW_WAGON_ATTACH ||
					object->ro.callback == CBID_VEHICLE_START_STOP_CHECK || object->ro.callback == CBID_VEHICLE_32DAY_CALLBACK || object->ro.callback == CBID_VEHICLE_COLOUR_MAPPING ||
					object->ro.callback == CBID_VEHICLE_SPAWN_VISUAL_EFFECT) {
				Vehicle *u = v->Move((int32_t)GetRegister(0x10F));
				if (u == nullptr) return 0; // available, but zero

				if (parameter == 0x5F) {
					/* This seems to be the only variable that makes sense to access via var 61, but is not handled by VehicleGetVariable */
					if (_sprite_group_resolve_check_veh_check) {
						SetBit(u->First()->vcache.cached_veh_flags, VCF_REDRAW_ON_TRIGGER);
					}
					return (u->random_bits << 8) | u->waiting_triggers;
				} else {
					return VehicleGetVariable(u, object, parameter, GetRegister(0x10E), extra);
				}
			}
			/* Not available */
			break;

		case 0x62: { // Curvature/position difference for n-th vehicle in chain [signed number relative to vehicle]
			/* Format: zzyyxxFD
			 * zz - Signed difference of z position between the selected and this vehicle.
			 * yy - Signed difference of y position between the selected and this vehicle.
			 * xx - Signed difference of x position between the selected and this vehicle.
			 * F  - Flags, bit 7 corresponds to VS_HIDDEN.
			 * D  - Dir difference, like in 0x45.
			 */
			if (!v->IsGroundVehicle()) return 0;

			const Vehicle *u = v->Move((int8_t)parameter);
			if (u == nullptr) return 0;

			_sprite_group_resolve_check_veh_curvature_check = false;

			/* Get direction difference. */
			bool prev = (int8_t)parameter < 0;
			uint32_t ret = prev ? DirDifference(u->direction, v->direction) : DirDifference(v->direction, u->direction);
			if (ret > DIRDIFF_REVERSE) ret |= 0x08;

			if (u->vehstatus & VS_HIDDEN) ret |= 0x80;

			/* Get position difference. */
			ret |= ((prev ? u->x_pos - v->x_pos : v->x_pos - u->x_pos) & 0xFF) << 8;
			ret |= ((prev ? u->y_pos - v->y_pos : v->y_pos - u->y_pos) & 0xFF) << 16;
			ret |= ((prev ? u->z_pos - v->z_pos : v->z_pos - u->z_pos) & 0xFF) << 24;

			return ret;
		}

		case 0x63:
			/* Tile compatibility wrt. arbitrary track-type
			 * Format:
			 *  bit 0: Type 'parameter' is known.
			 *  bit 1: Engines with type 'parameter' are compatible with this tile.
			 *  bit 2: Engines with type 'parameter' are powered on this tile.
			 *  bit 3: This tile has type 'parameter' or it is considered equivalent (alternate labels).
			 */
			switch (v->type) {
				case VEH_TRAIN: {
					RailType param_type = GetRailTypeTranslation(parameter, object->ro.grffile);
					if (param_type == INVALID_RAILTYPE) return 0x00;
					RailType tile_type;
					if (Train::From(v)->IsVirtual()) {
						tile_type = Train::From(v)->railtype;
					} else {
						tile_type = GetTileRailTypeByTrackBit(v->tile, Train::From(v)->track);
					}
					if (tile_type == param_type) return 0x0F;
					return (HasPowerOnRail(param_type, tile_type) ? 0x04 : 0x00) |
							(IsCompatibleRail(param_type, tile_type) ? 0x02 : 0x00) |
							0x01;
				}
				case VEH_ROAD: {
					RoadTramType rtt = GetRoadTramType(RoadVehicle::From(v)->roadtype);
					RoadType param_type = GetRoadTypeTranslation(rtt, parameter, object->ro.grffile);
					if (param_type == INVALID_ROADTYPE) return 0x00;
					RoadType tile_type = GetRoadType(v->tile, rtt);
					if (tile_type == param_type) return 0x0F;
					return (HasPowerOnRoad(param_type, tile_type) ? 0x06 : 0x00) |
							0x01;
				}
				default: return 0x00;
			}

		case 0x64: { // Count consist's badge ID occurrence
			if (v->type != VEH_TRAIN) return GetBadgeVariableResult(*object->ro.grffile, v->GetEngine()->badges, parameter);

			/* Look up badge index. */
			if (parameter >= std::size(object->ro.grffile->badge_list)) return UINT_MAX;
			BadgeID index = object->ro.grffile->badge_list[parameter];

			/* Count number of vehicles that contain this badge index. */
			uint count = 0;
			for (; v != nullptr; v = v->Next()) {
				const auto &badges = v->GetEngine()->badges;
				if (std::ranges::find(badges, index) != std::end(badges)) count++;
			}

			return count;
		}

		case 0x7A: return GetBadgeVariableResult(*object->ro.grffile, v->GetEngine()->badges, parameter);

		case 0xFE:
		case 0xFF: {
			uint16_t modflags = 0;

			if (v->type == VEH_TRAIN) {
				const Train *t = Train::From(v);
				bool is_powered_wagon = HasBit(t->flags, VRF_POWEREDWAGON);
				const Train *u = is_powered_wagon ? t->First() : t; // for powered wagons the engine defines the type of engine (i.e. railtype)
				bool powered = t->IsEngine() || is_powered_wagon;
				bool has_power;
				if (u->IsVirtual()) {
					has_power = true;
				} else {
					RailType railtype = GetRailTypeByTrackBit(v->tile, t->track);
					has_power = HasPowerOnRail(u->railtype, railtype);
				}

				if (powered && has_power) SetBit(modflags, 5);
				if (powered && !has_power) SetBit(modflags, 6);
				if (HasBit(t->flags, VRF_TOGGLE_REVERSE)) SetBit(modflags, 8);
			}
			if (HasBit(v->vehicle_flags, VF_CARGO_UNLOADING)) SetBit(modflags, 1);
			if (HasBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE)) SetBit(modflags, 10);

			return variable == 0xFE ? modflags : GB(modflags, 8, 8);
		}

		case A2VRI_VEHICLE_CURRENT_SPEED_SCALED:
			return (v->cur_speed * parameter) >> 16;
	}

	/*
	 * General vehicle properties
	 *
	 * Some parts of the TTD Vehicle structure are omitted for various reasons
	 * (see http://marcin.ttdpatch.net/sv1codec/TTD-locations.html#_VehicleArray)
	 */
	switch (variable - 0x80) {
		case 0x00: return v->type + 0x10;
		case 0x01: return MapOldSubType(v);
		case 0x02: break; // not implemented
		case 0x03: break; // not implemented
		case 0x04: return v->index;
		case 0x05: return GB(v->index, 8, 8);
		case 0x06: break; // not implemented
		case 0x07: break; // not implemented
		case 0x08: break; // not implemented
		case 0x09: break; // not implemented
		case 0x0A: return v->current_order.MapOldOrder();
		case 0x0B: return v->current_order.GetDestination().value;
		case 0x0C: return v->GetNumOrders();
		case 0x0D: return v->cur_real_order_index;
		case 0x0E: break; // not implemented
		case 0x0F: break; // not implemented
		case 0x10:
		case 0x11: {
			uint ticks;
			if (v->current_order.IsType(OT_LOADING)) {
				ticks = v->load_unload_ticks;
			} else {
				switch (v->type) {
					case VEH_TRAIN:    ticks = Train::From(v)->wait_counter; break;
					case VEH_AIRCRAFT: ticks = Aircraft::From(v)->turn_counter; break;
					default:           ticks = 0; break;
				}
			}
			return (variable - 0x80) == 0x10 ? ticks : GB(ticks, 8, 8);
		}
		case 0x12: return ClampTo<uint16_t>(v->date_of_last_service_newgrf - CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR);
		case 0x13: return GB(ClampTo<uint16_t>(v->date_of_last_service_newgrf - CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR), 8, 8);
		case 0x14: return v->GetServiceInterval();
		case 0x15: return GB(v->GetServiceInterval(), 8, 8);
		case 0x16: return v->last_station_visited;
		case 0x17: return v->tick_counter;
		case 0x18:
		case 0x19: {
			uint max_speed;
			switch (v->type) {
				case VEH_AIRCRAFT:
					max_speed = Aircraft::From(v)->GetSpeedOldUnits(); // Convert to old units.
					break;

				default:
					max_speed = v->vcache.cached_max_speed;
					break;
			}
			return (variable - 0x80) == 0x18 ? max_speed : GB(max_speed, 8, 8);
		}
		case 0x1A: return v->x_pos;
		case 0x1B: return GB(v->x_pos, 8, 8);
		case 0x1C: return v->y_pos;
		case 0x1D: return GB(v->y_pos, 8, 8);
		case 0x1E: return v->z_pos;
		case 0x1F: return object->rotor_in_gui ? DIR_W : v->direction; // for rotors the spriteset contains animation frames, so NewGRF need a different way to tell the helicopter orientation.
		case 0x20: break; // not implemented
		case 0x21: break; // not implemented
		case 0x22: break; // not implemented
		case 0x23: break; // not implemented
		case 0x24: break; // not implemented
		case 0x25: break; // not implemented
		case 0x26: break; // not implemented
		case 0x27: break; // not implemented
		case 0x28: return 0; // cur_image is a potential desyncer due to Action1 in static NewGRFs.
		case 0x29: return 0; // cur_image is a potential desyncer due to Action1 in static NewGRFs.
		case 0x2A: break; // not implemented
		case 0x2B: break; // not implemented
		case 0x2C: break; // not implemented
		case 0x2D: break; // not implemented
		case 0x2E: break; // not implemented
		case 0x2F: break; // not implemented
		case 0x30: break; // not implemented
		case 0x31: break; // not implemented
		case 0x32: return v->vehstatus;
		case 0x33: return 0; // non-existent high byte of vehstatus
		case 0x34: return v->type == VEH_AIRCRAFT ? (v->cur_speed * 10) / 128 : v->cur_speed;
		case 0x35: return GB(v->type == VEH_AIRCRAFT ? (v->cur_speed * 10) / 128 : v->cur_speed, 8, 8);
		case 0x36: return v->subspeed;
		case 0x37: return v->acceleration;
		case 0x38: break; // not implemented
		case 0x39: return v->cargo_type;
		case 0x3A: return v->cargo_cap;
		case 0x3B: return GB(v->cargo_cap, 8, 8);
		case 0x3C: return ClampTo<uint16_t>(v->cargo.StoredCount());
		case 0x3D: return GB(ClampTo<uint16_t>(v->cargo.StoredCount()), 8, 8);
		case 0x3E: return v->cargo.GetFirstStation();
		case 0x3F: return ClampTo<uint8_t>(v->cargo.PeriodsInTransit());
		case 0x40: return ClampTo<uint16_t>(v->age);
		case 0x41: return GB(ClampTo<uint16_t>(v->age), 8, 8);
		case 0x42: return ClampTo<uint16_t>(v->max_age);
		case 0x43: return GB(ClampTo<uint16_t>(v->max_age), 8, 8);
		case 0x44: return (Clamp(v->build_year, CalTime::ORIGINAL_BASE_YEAR, CalTime::ORIGINAL_MAX_YEAR) - CalTime::ORIGINAL_BASE_YEAR).base();
		case 0x45: return v->unitnumber;
		case 0x46: return v->GetEngine()->grf_prop.local_id;
		case 0x47: return GB(v->GetEngine()->grf_prop.local_id, 8, 8);
		case 0x48:
			if (v->type != VEH_TRAIN || v->spritenum != 0xFD) return v->spritenum;
			return HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION) ? 0xFE : 0xFD;

		case 0x49: return v->day_counter;
		case 0x4A: return v->breakdowns_since_last_service;
		case 0x4B: return v->breakdown_ctr;
		case 0x4C: return v->breakdown_delay;
		case 0x4D: return v->breakdown_chance;
		case 0x4E: return v->reliability;
		case 0x4F: return GB(v->reliability, 8, 8);
		case 0x50: return v->reliability_spd_dec;
		case 0x51: return GB(v->reliability_spd_dec, 8, 8);
		case 0x52: return ClampTo<int32_t>(v->GetDisplayProfitThisYear());
		case 0x53: return GB(ClampTo<int32_t>(v->GetDisplayProfitThisYear()),  8, 24);
		case 0x54: return GB(ClampTo<int32_t>(v->GetDisplayProfitThisYear()), 16, 16);
		case 0x55: return GB(ClampTo<int32_t>(v->GetDisplayProfitThisYear()), 24,  8);
		case 0x56: return ClampTo<int32_t>(v->GetDisplayProfitLastYear());
		case 0x57: return GB(ClampTo<int32_t>(v->GetDisplayProfitLastYear()),  8, 24);
		case 0x58: return GB(ClampTo<int32_t>(v->GetDisplayProfitLastYear()), 16, 16);
		case 0x59: return GB(ClampTo<int32_t>(v->GetDisplayProfitLastYear()), 24,  8);
		case 0x5A: return v->Next() == nullptr ? INVALID_VEHICLE : v->Next()->index;
		case 0x5B: break; // not implemented
		case 0x5C: return ClampTo<int32_t>(v->value);
		case 0x5D: return GB(ClampTo<int32_t>(v->value),  8, 24);
		case 0x5E: return GB(ClampTo<int32_t>(v->value), 16, 16);
		case 0x5F: return GB(ClampTo<int32_t>(v->value), 24,  8);
		case 0x60: break; // not implemented
		case 0x61: break; // not implemented
		case 0x62: break; // vehicle specific, see below
		case 0x63: break; // not implemented
		case 0x64: break; // vehicle specific, see below
		case 0x65: break; // vehicle specific, see below
		case 0x66: break; // vehicle specific, see below
		case 0x67: break; // vehicle specific, see below
		case 0x68: break; // vehicle specific, see below
		case 0x69: break; // vehicle specific, see below
		case 0x6A: break; // not implemented
		case 0x6B: break; // not implemented
		case 0x6C: break; // not implemented
		case 0x6D: break; // not implemented
		case 0x6E: break; // not implemented
		case 0x6F: break; // not implemented
		case 0x70: break; // not implemented
		case 0x71: break; // not implemented
		case 0x72: return v->cargo_subtype;
		case 0x73: break; // vehicle specific, see below
		case 0x74: break; // vehicle specific, see below
		case 0x75: break; // vehicle specific, see below
		case 0x76: break; // vehicle specific, see below
		case 0x77: break; // vehicle specific, see below
		case 0x78: break; // not implemented
		case 0x79: break; // not implemented
		case 0x7A: return v->random_bits;
		case 0x7B: return v->waiting_triggers;
		case 0x7C: break; // vehicle specific, see below
		case 0x7D: break; // vehicle specific, see below
		case 0x7E: break; // not implemented
		case 0x7F: break; // vehicle specific, see below
	}

	/* Vehicle specific properties */
	switch (v->type) {
		case VEH_TRAIN: {
			Train *t = Train::From(v);
			switch (variable - 0x80) {
				case 0x62: return t->track;
				case 0x66: return t->railtype;
				case 0x73: return 0x80 + VEHICLE_LENGTH - t->gcache.cached_veh_length;
				case 0x74: return t->gcache.cached_power;
				case 0x75: return GB(t->gcache.cached_power,  8, 24);
				case 0x76: return GB(t->gcache.cached_power, 16, 16);
				case 0x77: return GB(t->gcache.cached_power, 24,  8);
				case 0x7C: return t->First()->index;
				case 0x7D: return GB(t->First()->index, 8, 8);
				case 0x7F: return 0; // Used for vehicle reversing hack in TTDP
			}
			break;
		}

		case VEH_ROAD: {
			RoadVehicle *rv = RoadVehicle::From(v);
			switch (variable - 0x80) {
				case 0x62: return rv->state;
				case 0x64: return rv->blocked_ctr;
				case 0x65: return GB(rv->blocked_ctr, 8, 8);
				case 0x66: return rv->overtaking;
				case 0x67: return rv->overtaking_ctr;
				case 0x68: return rv->crashed_ctr;
				case 0x69: return GB(rv->crashed_ctr, 8, 8);
			}
			break;
		}

		case VEH_SHIP: {
			Ship *s = Ship::From(v);
			switch (variable - 0x80) {
				case 0x62: return s->state;
			}
			break;
		}

		case VEH_AIRCRAFT: {
			Aircraft *a = Aircraft::From(v);
			switch (variable - 0x80) {
				case 0x62: return MapAircraftMovementState(a);  // Current movement state
				case 0x63: return a->targetairport;             // Airport to which the action refers
				case 0x66: return MapAircraftMovementAction(a); // Current movement action
			}
			break;
		}

		default: break;
	}

	Debug(grf, 1, "Unhandled vehicle variable 0x{:X}, type 0x{:X}", variable, (uint)v->type);

	extra.available = false;
	return UINT_MAX;
}

/* virtual */ uint32_t VehicleScopeResolver::GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const
{
	if (this->v == nullptr) {
		/* Vehicle does not exist, so we're in a purchase list */
		switch (variable) {
			case 0x43: return GetCompanyInfo(_current_company, LiveryHelper(this->self_type, nullptr)); // Owner information
			case 0x46: return 0;               // Motion counter
			case 0x47: { // Vehicle cargo info
				const Engine *e = Engine::Get(this->self_type);
				CargoType cargo_type = e->GetDefaultCargoType();
				if (cargo_type != INVALID_CARGO) {
					const CargoSpec *cs = CargoSpec::Get(cargo_type);
					return (cs->classes.base() << 16) | (cs->weight << 8) | this->ro.grffile->cargo_map[cargo_type];
				} else {
					return 0x000000FF;
				}
			}
			case 0x48: return Engine::Get(this->self_type)->flags.base(); // Vehicle Type Info
			case 0x49: return CalTime::CurYear().base(); // 'Long' format build year
			case 0x4B: return CalTime::CurDate().base(); // Long date of last service

			case 0x7A: return GetBadgeVariableResult(*this->ro.grffile, Engine::Get(this->self_type)->badges, parameter);

			case 0x92: return ClampTo<uint16_t>(CalTime::CurDate() - CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR); // Date of last service
			case 0x93: return GB(ClampTo<uint16_t>(CalTime::CurDate() - CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR), 8, 8);
			case 0xC4: return (Clamp(CalTime::CurYear(), CalTime::ORIGINAL_BASE_YEAR, CalTime::ORIGINAL_MAX_YEAR) - CalTime::ORIGINAL_BASE_YEAR).base(); // Build year
			case 0xC6: return Engine::Get(this->self_type)->grf_prop.local_id;
			case 0xC7: return GB(Engine::Get(this->self_type)->grf_prop.local_id, 8, 8);
			case 0xDA: return INVALID_VEHICLE; // Next vehicle
			case 0xF2: return 0; // Cargo subtype
		}

		extra.available = false;
		return UINT_MAX;
	}

	return VehicleGetVariable(const_cast<Vehicle*>(this->v), this, variable, parameter, extra);
}


/* virtual */ const SpriteGroup *VehicleResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	const Vehicle *v = this->self_scope.v;

	if (v == nullptr) {
		if (!group->loading.empty()) return group->loading[0];
		if (!group->loaded.empty())  return group->loaded[0];
		return nullptr;
	}

	bool in_motion = !v->First()->current_order.IsType(OT_LOADING);

	uint totalsets = in_motion ? (uint)group->loaded.size() : (uint)group->loading.size();

	if (totalsets == 0) return nullptr;
	if (totalsets == 1) return in_motion ? group->loaded[0] : group->loading[0];

	uint stored = v->cargo.StoredCount();
	uint capacity = v->cargo_cap;
	if (v->type == VEH_SHIP) {
		for (const Vehicle *u = v->Next(); u != nullptr; u = u->Next()) {
			stored += u->cargo.StoredCount();
			capacity += u->cargo_cap;
		}
	}

	uint set = (stored * totalsets) / std::max<uint16_t>(1u, capacity);
	set = std::min(set, totalsets - 1);

	return in_motion ? group->loaded[set] : group->loading[set];
}

GrfSpecFeature VehicleResolverObject::GetFeature() const
{
	switch (Engine::Get(this->self_scope.self_type)->type) {
		case VEH_TRAIN: return GSF_TRAINS;
		case VEH_ROAD: return GSF_ROADVEHICLES;
		case VEH_SHIP: return GSF_SHIPS;
		case VEH_AIRCRAFT: return GSF_AIRCRAFT;
		default: return GSF_INVALID;
	}
}

uint32_t VehicleResolverObject::GetDebugID() const
{
	return Engine::Get(this->self_scope.self_type)->grf_prop.local_id;
}

/**
 * Get the grf file associated with an engine type.
 * @param engine_type Engine to query.
 * @return grf file associated with the engine.
 */
static const GRFFile *GetEngineGrfFile(EngineID engine_type)
{
	const Engine *e = Engine::Get(engine_type);
	return (e != nullptr) ? e->GetGRF() : nullptr;
}

/**
 * Resolver of a vehicle (chain).
 * @param engine_type Engine type
 * @param v %Vehicle being resolved.
 * @param wagon_override Application of wagon overrides.
 * @param rotor_in_gui Helicopter rotor is drawn in GUI.
 * @param callback Callback ID.
 * @param callback_param1 First parameter (var 10) of the callback.
 * @param callback_param2 Second parameter (var 18) of the callback.
 */
VehicleResolverObject::VehicleResolverObject(EngineID engine_type, const Vehicle *v, WagonOverride wagon_override, bool rotor_in_gui,
		CallbackID callback, uint32_t callback_param1, uint32_t callback_param2)
	: ResolverObject(GetEngineGrfFile(engine_type), callback, callback_param1, callback_param2),
	self_scope(*this, engine_type, v, rotor_in_gui),
	parent_scope(*this, engine_type, ((v != nullptr) ? v->First() : v), rotor_in_gui),
	relative_scope(*this, engine_type, v, rotor_in_gui),
	cached_relative_count(0)
{
	if (wagon_override == WO_SELF) {
		this->root_spritegroup = GetWagonOverrideSpriteSet(engine_type, SpriteGroupCargo::SG_DEFAULT, engine_type);
	} else {
		if (wagon_override != WO_NONE && v != nullptr && v->IsGroundVehicle()) {
			assert(v->engine_type == engine_type); // overrides make little sense with fake scopes

			/* For trains we always use cached value, except for callbacks because the override spriteset
			 * to use may be different than the one cached. It happens for callback 0x15 (refit engine),
			 * as v->cargo_type is temporary changed to the new type */
			if (wagon_override == WO_CACHED && v->type == VEH_TRAIN) {
				this->root_spritegroup = Train::From(v)->tcache.cached_override;
			} else {
				this->root_spritegroup = GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->GetGroundVehicleCache()->first_engine);
			}
		}

		if (this->root_spritegroup == nullptr) {
			const Engine *e = Engine::Get(engine_type);
			CargoType cargo = v != nullptr ? v->cargo_type : SpriteGroupCargo::SG_PURCHASE;
			this->root_spritegroup = e->grf_prop.GetSpriteGroup(cargo);
			if (this->root_spritegroup == nullptr) this->root_spritegroup = e->grf_prop.GetSpriteGroup(SpriteGroupCargo::SG_DEFAULT);
		}
	}
}



void GetCustomEngineSprite(EngineID engine, const Vehicle *v, Direction direction, EngineImageType image_type, VehicleSpriteSeq *result)
{
	VehicleResolverObject object(engine, v, VehicleResolverObject::WO_CACHED, false, CBID_NO_CALLBACK);
	result->Clear();

	bool sprite_stack = EngInfo(engine)->misc_flags.Test(EngineMiscFlag::SpriteStack);
	uint max_stack = sprite_stack ? lengthof(result->seq) : 1;
	for (uint stack = 0; stack < max_stack; ++stack) {
		object.ResetState();
		object.callback_param1 = image_type | (stack << 8);
		const SpriteGroup *group = object.Resolve();
		uint32_t reg100 = sprite_stack ? GetRegister(0x100) : 0;
		if (group != nullptr && group->GetNumResults() != 0) {
			result->seq[result->count].sprite = group->GetResult() + (direction % group->GetNumResults());
			result->seq[result->count].pal    = GB(reg100, 0, 16); // zero means default recolouring
			result->count++;
		}
		if (!HasBit(reg100, 31)) break;
	}
}


void GetRotorOverrideSprite(EngineID engine, const struct Aircraft *v, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);

	/* Only valid for helicopters */
	assert(e->type == VEH_AIRCRAFT);
	assert(!(e->u.air.subtype & AIR_CTOL));

	/* We differ from TTDPatch by resolving the sprite using the primary vehicle 'v', and not using the rotor vehicle 'v->Next()->Next()'.
	 * TTDPatch copies some variables between the vehicles each time, to somehow synchronize the rotor vehicle with the primary vehicle.
	 * We use 'rotor_in_gui' to replicate when the variables differ.
	 * But some other variables like 'rotor state' and 'rotor speed' are not available in OpenTTD, while they are in TTDPatch. */
	bool rotor_in_gui = image_type != EIT_ON_MAP;
	VehicleResolverObject object(engine, v, VehicleResolverObject::WO_SELF, rotor_in_gui, CBID_NO_CALLBACK);
	result->Clear();
	uint rotor_pos = v == nullptr || rotor_in_gui ? 0 : v->Next()->Next()->state;

	bool sprite_stack = e->info.misc_flags.Test(EngineMiscFlag::SpriteStack);
	uint max_stack = sprite_stack ? lengthof(result->seq) : 1;
	for (uint stack = 0; stack < max_stack; ++stack) {
		object.ResetState();
		object.callback_param1 = image_type | (stack << 8);
		const SpriteGroup *group = object.Resolve();
		uint32_t reg100 = sprite_stack ? GetRegister(0x100) : 0;
		if (group != nullptr && group->GetNumResults() != 0) {
			result->seq[result->count].sprite = group->GetResult() + (rotor_pos % group->GetNumResults());
			result->seq[result->count].pal    = GB(reg100, 0, 16); // zero means default recolouring
			result->count++;
		}
		if (!HasBit(reg100, 31)) break;
	}
}


/**
 * Check if a wagon is currently using a wagon override
 * @param v The wagon to check
 * @return true if it is using an override, false otherwise
 */
bool UsesWagonOverride(const Vehicle *v)
{
	assert(v->type == VEH_TRAIN);
	return Train::From(v)->tcache.cached_override != nullptr;
}

/**
 * Evaluate a newgrf callback for vehicles
 * @param callback The callback to evaluate
 * @param param1   First parameter of the callback
 * @param param2   Second parameter of the callback
 * @param engine   Engine type of the vehicle to evaluate the callback for
 * @param v        The vehicle to evaluate the callback for, or nullptr if it doesn't exist yet
 * @return The value the callback returned, or CALLBACK_FAILED if it failed
 */
uint16_t GetVehicleCallback(CallbackID callback, uint32_t param1, uint32_t param2, EngineID engine, const Vehicle *v)
{
	VehicleResolverObject object(engine, v, VehicleResolverObject::WO_UNCACHED, false, callback, param1, param2);
	return object.ResolveCallback();
}

/**
 * Evaluate a newgrf callback for vehicles with a different vehicle for parent scope.
 * @param callback The callback to evaluate
 * @param param1   First parameter of the callback
 * @param param2   Second parameter of the callback
 * @param engine   Engine type of the vehicle to evaluate the callback for
 * @param v        The vehicle to evaluate the callback for, or nullptr if it doesn't exist yet
 * @param parent   The vehicle to use for parent scope
 * @return The value the callback returned, or CALLBACK_FAILED if it failed
 */
uint16_t GetVehicleCallbackParent(CallbackID callback, uint32_t param1, uint32_t param2, EngineID engine, const Vehicle *v, const Vehicle *parent)
{
	VehicleResolverObject object(engine, v, VehicleResolverObject::WO_NONE, false, callback, param1, param2);
	object.parent_scope.SetVehicle(parent);
	return object.ResolveCallback();
}


/* Callback 36 handlers */
int GetVehicleProperty(const Vehicle *v, PropertyID property, int orig_value, bool is_signed)
{
	return GetEngineProperty(v->engine_type, property, orig_value, v, is_signed);
}


int GetEngineProperty(EngineID engine, PropertyID property, int orig_value, const Vehicle *v, bool is_signed)
{
	const Engine *e = Engine::Get(engine);
	if (static_cast<uint>(property) < 64 && !HasBit(e->cb36_properties_used, property)) return orig_value;

	VehicleResolverObject object(engine, v, VehicleResolverObject::WO_UNCACHED, false, CBID_VEHICLE_MODIFY_PROPERTY, property, 0);
	if (static_cast<uint>(property) < 64 && !e->sprite_group_cb36_properties_used.empty()) {
		auto iter = e->sprite_group_cb36_properties_used.find(object.root_spritegroup);
		if (iter != e->sprite_group_cb36_properties_used.end()) {
			if (!HasBit(iter->second, property)) return orig_value;
		}
	}
	uint16_t callback = object.ResolveCallback();
	if (callback != CALLBACK_FAILED) {
		if (is_signed) {
			/* Sign extend 15 bit integer */
			return static_cast<int16_t>(callback << 1) / 2;
		} else {
			return callback;
		}
	}

	return orig_value;
}

/**
 * Test for vehicle build probablity type.
 * @param v Vehicle whose build probability to test.
 * @param type Build probability type to test for.
 * @returns True iff the probability result says so.
 */
bool TestVehicleBuildProbability(Vehicle *v, EngineID engine, BuildProbabilityType type)
{
	uint16_t p = GetVehicleCallback(CBID_VEHICLE_BUILD_PROBABILITY, to_underlying(type), 0, engine, v);
	if (p == CALLBACK_FAILED) return false;

	const uint16_t PROBABILITY_RANGE = 100;
	return p + RandomRange(PROBABILITY_RANGE) >= PROBABILITY_RANGE;
}

static void DoTriggerVehicle(Vehicle *v, VehicleTrigger trigger, uint16_t base_random_bits, bool first)
{
	/* We can't trigger a non-existent vehicle... */
	assert(v != nullptr);

	uint32_t reseed = 0;
	if (Engine::Get(v->engine_type)->callbacks_used & SGCU_RANDOM_TRIGGER) {
		VehicleResolverObject object(v->engine_type, v, VehicleResolverObject::WO_CACHED, false, CBID_RANDOM_TRIGGER);
		object.waiting_triggers = v->waiting_triggers | trigger;
		v->waiting_triggers = object.waiting_triggers; // store now for var 5F

		const SpriteGroup *group = object.Resolve();
		if (group == nullptr) return;

		/* Store remaining triggers. */
		v->waiting_triggers = object.GetRemainingTriggers();

		reseed = object.GetReseedSum();
	} else {
		v->waiting_triggers |= trigger;

		const Engine *e = Engine::Get(v->engine_type);
		if (e->grf_prop.GetSpriteGroup(v->cargo_type) == nullptr && e->grf_prop.GetSpriteGroup(SpriteGroupCargo::SG_DEFAULT) == nullptr) return;
	}

	/* Rerandomise bits. Scopes other than SELF are invalid for rerandomisation. For bug-to-bug-compatibility with TTDP we ignore the scope. */
	uint8_t new_random_bits = Random();
	v->random_bits &= ~reseed;
	v->random_bits |= (first ? new_random_bits : base_random_bits) & reseed;

	switch (trigger) {
		case VEHICLE_TRIGGER_NEW_CARGO:
			/* All vehicles in chain get ANY_NEW_CARGO trigger now.
			 * So we call it for the first one and they will recurse.
			 * Indexing part of vehicle random bits needs to be
			 * same for all triggered vehicles in the chain (to get
			 * all the random-cargo wagons carry the same cargo,
			 * i.e.), so we give them all the NEW_CARGO triggered
			 * vehicle's portion of random bits. */
			assert(first);
			DoTriggerVehicle(v->First(), VEHICLE_TRIGGER_ANY_NEW_CARGO, new_random_bits, false);
			break;

		case VEHICLE_TRIGGER_DEPOT:
			/* We now trigger the next vehicle in chain recursively.
			 * The random bits portions may be different for each
			 * vehicle in chain. */
			if (v->Next() != nullptr) DoTriggerVehicle(v->Next(), trigger, 0, true);
			break;

		case VEHICLE_TRIGGER_EMPTY:
			/* We now trigger the next vehicle in chain
			 * recursively.  The random bits portions must be same
			 * for each vehicle in chain, so we give them all
			 * first chained vehicle's portion of random bits. */
			if (v->Next() != nullptr) DoTriggerVehicle(v->Next(), trigger, first ? new_random_bits : base_random_bits, false);
			break;

		case VEHICLE_TRIGGER_ANY_NEW_CARGO:
			/* Now pass the trigger recursively to the next vehicle
			 * in chain. */
			assert(!first);
			if (v->Next() != nullptr) DoTriggerVehicle(v->Next(), VEHICLE_TRIGGER_ANY_NEW_CARGO, base_random_bits, false);
			break;

		case VEHICLE_TRIGGER_CALLBACK_32:
			/* Do not do any recursion */
			break;
	}
}

void TriggerVehicle(Vehicle *v, VehicleTrigger trigger)
{
	if (trigger == VEHICLE_TRIGGER_DEPOT) {
		/* store that the vehicle entered a depot this tick */
		VehicleEnteredDepotThisTick(v);
	}

	v->InvalidateNewGRFCacheOfChain();
	DoTriggerVehicle(v, trigger, 0, true);
	if (HasBit(v->First()->vcache.cached_veh_flags, VCF_REDRAW_ON_TRIGGER)) {
		v->First()->InvalidateImageCacheOfChain();
	}
	v->InvalidateNewGRFCacheOfChain();
}

/* Functions for changing the order of vehicle purchase lists */

struct ListOrderChange {
	EngineID engine; ///< Engine ID
	uint16_t target; ///< GRF-local ID

	ListOrderChange(EngineID engine, uint16_t target) : engine(engine), target(target) {}
};

static std::vector<ListOrderChange> _list_order_changes;

/**
 * Record a vehicle ListOrderChange.
 * @param engine Engine to move
 * @param target Local engine ID to move \a engine in front of
 * @note All sorting is done later in CommitVehicleListOrderChanges
 */
void AlterVehicleListOrder(EngineID engine, uint16_t target)
{
	/* Add the list order change to a queue */
	_list_order_changes.emplace_back(engine, target);
}

/**
 * Comparator function to sort engines via scope-GRFID and local ID.
 * @param a left side
 * @param b right side
 * @return comparison result
 */
static bool EnginePreSort(const EngineID &a, const EngineID &b)
{
	const EngineIDMapping &id_a = _engine_mngr.mappings.at(a);
	const EngineIDMapping &id_b = _engine_mngr.mappings.at(b);

	/* 1. Sort by engine type */
	if (id_a.type != id_b.type) return (int)id_a.type < (int)id_b.type;

	/* 2. Sort by scope-GRFID */
	if (id_a.grfid != id_b.grfid) return id_a.grfid < id_b.grfid;

	/* 3. Sort by local ID */
	return (int)id_a.internal_id < (int)id_b.internal_id;
}

/**
 * Determine default engine sorting and execute recorded ListOrderChanges from AlterVehicleListOrder.
 */
void CommitVehicleListOrderChanges()
{
	/* Build a list of EngineIDs. EngineIDs are sequential from 0 up to the number of pool items with no gaps. */
	std::vector<EngineID> ordering(Engine::GetNumItems());
	std::iota(std::begin(ordering), std::end(ordering), 0);

	/* Pre-sort engines by scope-grfid and local index */
	std::ranges::sort(ordering, EnginePreSort);

	/* Apply Insertion-Sort operations */
	for (const ListOrderChange &loc : _list_order_changes) {
		EngineID source = loc.engine;

		const EngineIDMapping &id_source = _engine_mngr.mappings[source];
		if (id_source.internal_id == loc.target) continue;

		EngineID target = _engine_mngr.GetID(id_source.type, loc.target, id_source.grfid);
		if (target == INVALID_ENGINE) continue;

		auto it_source = std::ranges::find(ordering, source);
		auto it_target = std::ranges::find(ordering, target);

		assert(it_source != std::end(ordering) && it_target != std::end(ordering));
		assert(it_source != it_target);

		/* Move just this item to before the target. */
		Slide(it_source, std::next(it_source), it_target);
	}

	/* Store final sort-order */
	for (uint16_t index = 0; const EngineID &eid : ordering) {
		Engine::Get(eid)->list_position = index;
		++index;
	}

	/* Clear out the queue */
	_list_order_changes.clear();
	_list_order_changes.shrink_to_fit();
}

/**
 * Fill the grf_cache of the given vehicle.
 * @param v The vehicle to fill the cache for.
 */
void FillNewGRFVehicleCache(const Vehicle *v)
{
	VehicleResolverObject ro(v->engine_type, v, VehicleResolverObject::WO_NONE);

	/* These variables we have to check; these are the ones with a cache. */
	static const int cache_entries[][2] = {
		{ 0x40, NCVV_POSITION_CONSIST_LENGTH },
		{ 0x41, NCVV_POSITION_SAME_ID_LENGTH },
		{ 0x42, NCVV_CONSIST_CARGO_INFORMATION },
		{ 0x43, NCVV_COMPANY_INFORMATION },
		{ 0x4D, NCVV_POSITION_IN_VEHICLE },
	};
	static const int partial_cache_entries[] = {
		NCVV_CONSIST_CARGO_INFORMATION_UD,
	};
	static_assert(NCVV_END == lengthof(cache_entries) + lengthof(partial_cache_entries));

	/* Resolve all the variables, so their caches are set. */
	for (const auto &cache_entry : cache_entries) {
		/* Only resolve when the cache isn't valid. */
		if (HasBit(v->grf_cache.cache_valid, cache_entry[1])) continue;
		GetVariableExtra extra;
		ro.GetScope(VSG_SCOPE_SELF)->GetVariable(cache_entry[0], 0, extra);
	}

	/* Make sure really all bits are set. */
	assert(v->grf_cache.cache_valid == (1 << NCVV_END) - 1);
}

void AnalyseEngineCallbacks()
{
	btree::btree_map<const SpriteGroup *, uint64_t> sg_cb36;
	btree::btree_map<uint32_t, CargoTypes> cb_refit_cap_values;
	for (Engine *e : Engine::Iterate()) {
		sg_cb36.clear();
		e->sprite_group_cb36_properties_used.clear();
		e->refit_capacity_values.reset();

		SpriteGroupCallbacksUsed callbacks_used = SGCU_NONE;
		uint64_t cb36_properties_used = 0;
		bool refit_cap_whitelist_ok = true;
		bool refit_cap_no_var_47 = true;
		uint non_purchase_groups = 0;
		auto process_sg = [&](const SpriteGroup *sg, bool is_purchase) {
			if (sg == nullptr) return;

			CallbackOperationAnalyser op(ACOM_CB_VAR);
			op.AnalyseGroup(sg);
			callbacks_used |= op.callbacks_used;
			cb36_properties_used |= op.cb36_properties_used;
			sg_cb36[sg] = op.cb36_properties_used;
			if ((op.result_flags & ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND) && !is_purchase) refit_cap_whitelist_ok = false;
			if ((op.result_flags & ACORF_CB_REFIT_CAP_SEEN_VAR_47) && !is_purchase) refit_cap_no_var_47 = false;
			if (!is_purchase) non_purchase_groups++;
		};

		for (const auto &[cargo, spritegroup] : e->grf_prop) {
			process_sg(spritegroup, cargo == SpriteGroupCargo::SG_PURCHASE);
		}
		for (const WagonOverride &wo : e->overrides) {
			process_sg(wo.group, false);
		}
		e->callbacks_used = callbacks_used;
		e->cb36_properties_used = cb36_properties_used;
		for (auto iter : sg_cb36) {
			if (iter.second != cb36_properties_used) {
				e->sprite_group_cb36_properties_used[iter.first] = iter.second;
			}
		}

		if (refit_cap_whitelist_ok && non_purchase_groups <= 1 && e->info.callback_mask.Test(VehicleCallbackMask::RefitCapacity) && e->grf_prop.GetSpriteGroup(SpriteGroupCargo::SG_DEFAULT) != nullptr) {
			const SpriteGroup **purchase_sg_ptr = e->grf_prop.GetSpriteGroupPtr(SpriteGroupCargo::SG_PURCHASE);
			const SpriteGroup *purchase_sg = nullptr;
			if (purchase_sg_ptr != nullptr) {
				purchase_sg = *purchase_sg_ptr;
				*purchase_sg_ptr = nullptr; // Temporarily disable separate purchase sprite group
			}

			if (refit_cap_no_var_47) {
				cb_refit_cap_values[GetVehicleCallback(CBID_VEHICLE_REFIT_CAPACITY, 0, 0, e->index, nullptr)] = ALL_CARGOTYPES;
			} else {
				const CargoType default_cb = e->info.cargo_type;
				for (CargoType c = 0; c < NUM_CARGO; c++) {
					e->info.cargo_type = c;
					cb_refit_cap_values[GetVehicleCallback(CBID_VEHICLE_REFIT_CAPACITY, 0, 0, e->index, nullptr)] |= (static_cast<CargoTypes>(1) << c);
				}
				e->info.cargo_type = default_cb;
			}

			if (purchase_sg_ptr != nullptr) {
				*purchase_sg_ptr = purchase_sg;
			}

			bool all_ok = true;
			uint index = 0;
			e->refit_capacity_values.reset(MallocT<EngineRefitCapacityValue>(cb_refit_cap_values.size()));
			for (const auto &iter : cb_refit_cap_values) {
				if (iter.first == CALLBACK_FAILED) all_ok = false;
				e->refit_capacity_values.get()[index] = { iter.second, iter.first };
				index++;
			}
			if (all_ok) e->callbacks_used |= SGCU_REFIT_CB_ALL_CARGOES;

			cb_refit_cap_values.clear();
		}
	}
}

void DumpVehicleSpriteGroup(const Vehicle *v, SpriteGroupDumper &dumper)
{
	const Engine *e = Engine::Get(v->engine_type);
	const SpriteGroup *root_spritegroup = nullptr;

	if (v->IsGroundVehicle()) {
		root_spritegroup = GetWagonOverrideSpriteSet(v->engine_type, v->cargo_type, v->GetGroundVehicleCache()->first_engine);
		if (root_spritegroup != nullptr) {
			dumper.Print(fmt::format("Wagon Override for cargo: {}, engine type: {}", v->cargo_type, v->GetGroundVehicleCache()->first_engine));
		}
	}

	if (root_spritegroup == nullptr) {
		const SpriteGroup *cargo_spritegroup = e->grf_prop.GetSpriteGroup(v->cargo_type);
		if (cargo_spritegroup != nullptr) {
			root_spritegroup = cargo_spritegroup;
			dumper.Print(fmt::format("Cargo: {}", v->cargo_type));
		} else {
			root_spritegroup = e->grf_prop.GetSpriteGroup(SpriteGroupCargo::SG_DEFAULT);
			dumper.Print("SG_DEFAULT");
		}
	}

	dumper.DumpSpriteGroup(root_spritegroup, 0);

	for (const auto &[cargo, spritegroup] : e->grf_prop) {
		if (spritegroup != root_spritegroup) {
			dumper.Print("");
			switch (cargo) {
				case SpriteGroupCargo::SG_DEFAULT:
					dumper.Print("OTHER SPRITE GROUP: SG_DEFAULT");
					break;
				case SpriteGroupCargo::SG_PURCHASE:
					dumper.Print("OTHER SPRITE GROUP: SG_PURCHASE");
					break;
				default:
					dumper.Print(fmt::format("OTHER SPRITE GROUP: Cargo: {}", cargo));
					break;
			}
			dumper.DumpSpriteGroup(spritegroup, 0);
		}
	}
	for (const WagonOverride &wo : e->overrides) {
		if (wo.group != root_spritegroup && wo.group != nullptr) {
			dumper.Print("");
			dumper.Print(fmt::format("OTHER SPRITE GROUP: Wagon override, cargo: {}, engines: {}", wo.cargo, wo.engines));
			dumper.DumpSpriteGroup(wo.group, 0);
		}
	}
}
