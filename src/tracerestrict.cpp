/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict.cpp Main file for Trace Restrict */

#include "stdafx.h"
#include "tracerestrict.h"
#include "train.h"
#include "core/bitmath_func.hpp"
#include "core/container_func.hpp"
#include "core/pool_func.hpp"
#include "command_func.h"
#include "company_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "order_base.h"
#include "cargotype.h"
#include "group.h"
#include "string_func.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "scope_info.h"
#include "vehicle_func.h"
#include "date_func.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include <vector>
#include <algorithm>

#include "safeguards.h"

/** @file
 *
 * Trace Restrict Data Storage Model Notes:
 *
 * Signals may have 0, 1 or 2 trace restrict programs attached to them,
 * up to one for each track. Two-way signals share the same program.
 *
 * The mapping between signals and programs is defined in terms of
 * TraceRestrictRefId to TraceRestrictProgramID,
 * where TraceRestrictRefId is formed of the tile index and track,
 * and TraceRestrictProgramID is an index into the program pool.
 *
 * If one or more mappings exist for a given signal tile, bit 12 of M3 will be set to 1.
 * This is updated whenever mappings are added/removed for that tile. This is to avoid
 * needing to do a mapping lookup for the common case where there is no trace restrict
 * program mapping for the given tile.
 *
 * Programs in the program pool are refcounted based on the number of mappings which exist.
 * When this falls to 0, the program is deleted from the pool.
 * If a program has a refcount greater than 1, it is a shared program.
 *
 * In all cases, an empty program is evaluated the same as the absence of a program.
 * Therefore it is not necessary to store mappings to empty unshared programs.
 * Any editing action which would otherwise result in a mapping to an empty program
 * which has no other references, instead removes the mapping.
 * This is not done for shared programs as this would delete the shared aspect whenever
 * the program became empty.
 *
 * Special case: In the case where an empty program with refcount 2 has one of its
 * mappings removed, the other mapping is left pointing to an empty unshared program.
 * This other mapping is then removed by performing a linear search of the mappings,
 * and removing the reference to that program ID.
 */

TraceRestrictProgramPool _tracerestrictprogram_pool("TraceRestrictProgram");
INSTANTIATE_POOL_METHODS(TraceRestrictProgram)

TraceRestrictSlotPool _tracerestrictslot_pool("TraceRestrictSlot");
INSTANTIATE_POOL_METHODS(TraceRestrictSlot)

TraceRestrictCounterPool _tracerestrictcounter_pool("TraceRestrictCounter");
INSTANTIATE_POOL_METHODS(TraceRestrictCounter)

/**
 * TraceRestrictRefId --> TraceRestrictProgramID (Pool ID) mapping
 * The indirection is mainly to enable shared programs
 * TODO: use a more efficient container/indirection mechanism
 */
TraceRestrictMapping _tracerestrictprogram_mapping;

/**
 * List of pre-defined pathfinder penalty values
 * This is indexed by TraceRestrictPathfinderPenaltyPresetIndex
 */
const uint16_t _tracerestrict_pathfinder_penalty_preset_values[] = {
	500,
	2000,
	8000,
};

static_assert(lengthof(_tracerestrict_pathfinder_penalty_preset_values) == TRPPPI_END);

/**
 * This should be used when all pools have been or are immediately about to be also cleared
 * Calling this at other times will leave dangling refcounts
 */
void ClearTraceRestrictMapping() {
	_tracerestrictprogram_mapping.clear();
}

/**
 * Flags used for the program execution condition stack
 * Each 'if' pushes onto the stack
 * Each 'end if' pops from the stack
 * Elif/orif/else may modify the stack top
 */
enum TraceRestrictCondStackFlags {
	TRCSF_DONE_IF         = 1<<0,       ///< The if/elif/else is "done", future elif/else branches will not be executed
	TRCSF_SEEN_ELSE       = 1<<1,       ///< An else branch has been seen already, error if another is seen afterwards
	TRCSF_ACTIVE          = 1<<2,       ///< The condition is currently active
	TRCSF_PARENT_INACTIVE = 1<<3,       ///< The parent condition is not active, thus this condition is also not active
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictCondStackFlags)

/**
 * Helper function to handle condition stack manipulatoin
 */
static void HandleCondition(std::vector<TraceRestrictCondStackFlags> &condstack, TraceRestrictCondFlags condflags, bool value)
{
	if (condflags & TRCF_OR) {
		assert(!condstack.empty());
		if (condstack.back() & TRCSF_ACTIVE) {
			// leave TRCSF_ACTIVE set
			return;
		}
	}

	if (condflags & (TRCF_OR | TRCF_ELSE)) {
		assert(!condstack.empty());
		if (condstack.back() & (TRCSF_DONE_IF | TRCSF_PARENT_INACTIVE)) {
			condstack.back() &= ~TRCSF_ACTIVE;
			return;
		}
	} else {
		if (!condstack.empty() && !(condstack.back() & TRCSF_ACTIVE)) {
			//this is a 'nested if', the 'parent if' is not active
			condstack.push_back(TRCSF_PARENT_INACTIVE);
			return;
		}
		condstack.push_back(static_cast<TraceRestrictCondStackFlags>(0));
	}

	if (value) {
		condstack.back() |= TRCSF_DONE_IF | TRCSF_ACTIVE;
	} else {
		condstack.back() &= ~TRCSF_ACTIVE;
	}
}

/**
 * Integer condition testing
 * Test value op condvalue
 */
static bool TestCondition(int value, TraceRestrictCondOp condop, int condvalue)
{
	switch (condop) {
		case TRCO_IS:
			return value == condvalue;
		case TRCO_ISNOT:
			return value != condvalue;
		case TRCO_LT:
			return value < condvalue;
		case TRCO_LTE:
			return value <= condvalue;
		case TRCO_GT:
			return value > condvalue;
		case TRCO_GTE:
			return value >= condvalue;
		default:
			NOT_REACHED();
			return false;
	}
}

/**
 * Binary condition testing helper function
 */
static bool TestBinaryConditionCommon(TraceRestrictItem item, bool input)
{
	switch (GetTraceRestrictCondOp(item)) {
		case TRCO_IS:
			return input;

		case TRCO_ISNOT:
			return !input;

		default:
			NOT_REACHED();
			return false;
	}
}

/**
 * Test order condition
 * @p order may be nullptr
 */
static bool TestOrderCondition(const Order *order, TraceRestrictItem item)
{
	bool result = false;

	if (order) {
		DestinationID condvalue = GetTraceRestrictValue(item);
		switch (static_cast<TraceRestrictOrderCondAuxField>(GetTraceRestrictAuxField(item))) {
			case TROCAF_STATION:
				result = (order->IsType(OT_GOTO_STATION) || order->IsType(OT_LOADING_ADVANCE))
						&& order->GetDestination() == condvalue;
				break;

			case TROCAF_WAYPOINT:
				result = order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == condvalue;
				break;

			case TROCAF_DEPOT:
				result = order->IsType(OT_GOTO_DEPOT) && order->GetDestination() == condvalue;
				break;

			default:
				NOT_REACHED();
		}
	}
	return TestBinaryConditionCommon(item, result);
}

/**
 * Test station condition
 */
static bool TestStationCondition(StationID station, TraceRestrictItem item)
{
	bool result = (GetTraceRestrictAuxField(item) == TROCAF_STATION) && (station == GetTraceRestrictValue(item));
	return TestBinaryConditionCommon(item, result);

}

/**
 * Execute program on train and store results in out
 * @p v may not be nullptr
 * @p out should be zero-initialised
 */
void TraceRestrictProgram::Execute(const Train* v, const TraceRestrictProgramInput &input, TraceRestrictProgramResult& out) const
{
	/* static to avoid needing to re-alloc/resize on each execution */
	static std::vector<TraceRestrictCondStackFlags> condstack;
	condstack.clear();

	/* Only for use with TRPISP_PBS_RES_END_ACQ_DRY and TRPAUF_PBS_RES_END_SIMULATE */
	static TraceRestrictSlotTemporaryState pbs_res_end_acq_dry_slot_temporary_state;

	byte have_previous_signal = 0;
	TileIndex previous_signal_tile[3];

	size_t size = this->items.size();
	for (size_t i = 0; i < size; i++) {
		TraceRestrictItem item = this->items[i];
		TraceRestrictItemType type = GetTraceRestrictType(item);

		if (IsTraceRestrictConditional(item)) {
			TraceRestrictCondFlags condflags = GetTraceRestrictCondFlags(item);
			TraceRestrictCondOp condop = GetTraceRestrictCondOp(item);

			if (type == TRIT_COND_ENDIF) {
				assert(!condstack.empty());
				if (condflags & TRCF_ELSE) {
					// else
					assert(!(condstack.back() & TRCSF_SEEN_ELSE));
					HandleCondition(condstack, condflags, true);
					condstack.back() |= TRCSF_SEEN_ELSE;
				} else {
					// end if
					condstack.pop_back();
				}
			} else {
				uint16_t condvalue = GetTraceRestrictValue(item);
				bool result = false;
				switch(type) {
					case TRIT_COND_UNDEFINED:
						result = false;
						break;

					case TRIT_COND_TRAIN_LENGTH:
						result = TestCondition(CeilDiv(v->gcache.cached_total_length, TILE_SIZE), condop, condvalue);
						break;

					case TRIT_COND_MAX_SPEED:
						result = TestCondition(v->GetDisplayMaxSpeed(), condop, condvalue);
						break;

					case TRIT_COND_CURRENT_ORDER:
						result = TestOrderCondition(&(v->current_order), item);
						break;

					case TRIT_COND_NEXT_ORDER: {
						if (v->orders == nullptr) break;
						if (v->orders->GetNumOrders() == 0) break;

						const Order *current_order = v->GetOrder(v->cur_real_order_index);
						for (const Order *order = v->orders->GetNext(current_order); order != current_order; order = v->orders->GetNext(order)) {
							if (order->IsGotoOrder()) {
								result = TestOrderCondition(order, item);
								break;
							}
						}
						break;
					}

					case TRIT_COND_LAST_STATION:
						result = TestStationCondition(v->last_station_visited, item);
						break;

					case TRIT_COND_CARGO: {
						bool have_cargo = false;
						for (const Vehicle *v_iter = v; v_iter != nullptr; v_iter = v_iter->Next()) {
							if (v_iter->cargo_type == GetTraceRestrictValue(item) && v_iter->cargo_cap > 0) {
								have_cargo = true;
								break;
							}
						}
						result = TestBinaryConditionCommon(item, have_cargo);
						break;
					}

					case TRIT_COND_ENTRY_DIRECTION: {
						bool direction_match;
						switch (GetTraceRestrictValue(item)) {
							case TRNTSV_NE:
							case TRNTSV_SE:
							case TRNTSV_SW:
							case TRNTSV_NW:
								direction_match = (static_cast<DiagDirection>(GetTraceRestrictValue(item)) == TrackdirToExitdir(ReverseTrackdir(input.trackdir)));
								break;

							case TRDTSV_FRONT:
								direction_match = (IsTileType(input.tile, MP_RAILWAY) && HasSignalOnTrackdir(input.tile, input.trackdir)) || IsTileType(input.tile, MP_TUNNELBRIDGE);
								break;

							case TRDTSV_BACK:
								direction_match = IsTileType(input.tile, MP_RAILWAY) && !HasSignalOnTrackdir(input.tile, input.trackdir);
								break;

							case TRDTSV_TUNBRIDGE_ENTER:
								direction_match = IsTunnelBridgeSignalSimulationEntranceTile(input.tile) && TrackdirEntersTunnelBridge(input.tile, input.trackdir);
								break;

							case TRDTSV_TUNBRIDGE_EXIT:
								direction_match = IsTunnelBridgeSignalSimulationExitTile(input.tile) && TrackdirExitsTunnelBridge(input.tile, input.trackdir);
								break;

							default:
								NOT_REACHED();
								break;
						}
						result = TestBinaryConditionCommon(item, direction_match);
						break;
					}

					case TRIT_COND_PBS_ENTRY_SIGNAL: {
						// TRIT_COND_PBS_ENTRY_SIGNAL value type uses the next slot
						i++;
						TraceRestrictPBSEntrySignalAuxField mode = static_cast<TraceRestrictPBSEntrySignalAuxField>(GetTraceRestrictAuxField(item));
						assert(mode == TRPESAF_VEH_POS || mode == TRPESAF_RES_END || mode == TRPESAF_RES_END_TILE);
						uint32_t signal_tile = this->items[i];
						if (!HasBit(have_previous_signal, mode)) {
							if (input.previous_signal_callback) {
								previous_signal_tile[mode] = input.previous_signal_callback(v, input.previous_signal_ptr, mode);
							} else {
								previous_signal_tile[mode] = INVALID_TILE;
							}
							SetBit(have_previous_signal, mode);
						}
						bool match = (signal_tile != INVALID_TILE)
								&& (previous_signal_tile[mode] == signal_tile);
						result = TestBinaryConditionCommon(item, match);
						break;
					}

					case TRIT_COND_TRAIN_GROUP: {
						result = TestBinaryConditionCommon(item, GroupIsInGroup(v->group_id, GetTraceRestrictValue(item)));
						break;
					}

					case TRIT_COND_TRAIN_IN_SLOT: {
						const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GetTraceRestrictValue(item));
						result = TestBinaryConditionCommon(item, slot != nullptr && slot->IsOccupant(v->index));
						break;
					}

					case TRIT_COND_SLOT_OCCUPANCY: {
						// TRIT_COND_SLOT_OCCUPANCY value type uses the next slot
						i++;
						uint32_t value = this->items[i];
						const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GetTraceRestrictValue(item));
						switch (static_cast<TraceRestrictSlotOccupancyCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TRSOCAF_OCCUPANTS:
								result = TestCondition(slot != nullptr ? (uint)slot->occupants.size() : 0, condop, value);
								break;

							case TRSOCAF_REMAINING:
								result = TestCondition(slot != nullptr ? slot->max_occupancy - (uint)slot->occupants.size() : 0, condop, value);
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;
					}

					case TRIT_COND_PHYS_PROP: {
						switch (static_cast<TraceRestrictPhysPropCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TRPPCAF_WEIGHT:
								result = TestCondition(v->gcache.cached_weight, condop, condvalue);
								break;

							case TRPPCAF_POWER:
								result = TestCondition(v->gcache.cached_power, condop, condvalue);
								break;

							case TRPPCAF_MAX_TE:
								result = TestCondition(v->gcache.cached_max_te / 1000, condop, condvalue);
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;
					}

					case TRIT_COND_PHYS_RATIO: {
						switch (static_cast<TraceRestrictPhysPropRatioCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TRPPRCAF_POWER_WEIGHT:
								result = TestCondition(std::min<uint>(UINT16_MAX, (100 * v->gcache.cached_power) / std::max<uint>(1, v->gcache.cached_weight)), condop, condvalue);
								break;

							case TRPPRCAF_MAX_TE_WEIGHT:
								result = TestCondition(std::min<uint>(UINT16_MAX, (v->gcache.cached_max_te / 10) / std::max<uint>(1, v->gcache.cached_weight)), condop, condvalue);
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;
					}

					case TRIT_COND_TRAIN_OWNER: {
						result = TestBinaryConditionCommon(item, v->owner == condvalue);
						break;
					}

					case TRIT_COND_TRAIN_STATUS: {
						bool has_status = false;
						switch (static_cast<TraceRestrictTrainStatusValueField>(GetTraceRestrictValue(item))) {
							case TRTSVF_EMPTY:
								has_status = true;
								for (const Vehicle *v_iter = v; v_iter != nullptr; v_iter = v_iter->Next()) {
									if (v_iter->cargo.StoredCount() > 0) {
										has_status = false;
										break;
									}
								}
								break;

							case TRTSVF_FULL:
								has_status = true;
								for (const Vehicle *v_iter = v; v_iter != nullptr; v_iter = v_iter->Next()) {
									if (v_iter->cargo.StoredCount() < v_iter->cargo_cap) {
										has_status = false;
										break;
									}
								}
								break;

							case TRTSVF_BROKEN_DOWN:
								has_status = v->flags & VRF_IS_BROKEN;
								break;

							case TRTSVF_NEEDS_REPAIR:
								has_status = v->critical_breakdown_count > 0;
								break;

							case TRTSVF_REVERSING:
								has_status = v->reverse_distance > 0 || HasBit(v->flags, VRF_REVERSING);
								break;

							case TRTSVF_HEADING_TO_STATION_WAYPOINT:
								has_status = v->current_order.IsType(OT_GOTO_STATION) || v->current_order.IsType(OT_GOTO_WAYPOINT);
								break;

							case TRTSVF_HEADING_TO_DEPOT:
								has_status = v->current_order.IsType(OT_GOTO_DEPOT);
								break;

							case TRTSVF_LOADING: {
								extern const Order *_choose_train_track_saved_current_order;
								const Order *o = (_choose_train_track_saved_current_order != nullptr) ? _choose_train_track_saved_current_order : &(v->current_order);
								has_status = o->IsType(OT_LOADING) || o->IsType(OT_LOADING_ADVANCE);
								break;
							}

							case TRTSVF_WAITING:
								has_status = v->current_order.IsType(OT_WAITING);
								break;

							case TRTSVF_LOST:
								has_status = HasBit(v->vehicle_flags, VF_PATHFINDER_LOST);
								break;

							case TRTSVF_REQUIRES_SERVICE:
								has_status = v->NeedsServicing();
								break;

							case TRTSVF_STOPPING_AT_STATION_WAYPOINT:
								switch (v->current_order.GetType()) {
									case OT_GOTO_STATION:
									case OT_GOTO_WAYPOINT:
									case OT_LOADING_ADVANCE:
										has_status = v->current_order.ShouldStopAtStation(v, v->current_order.GetDestination(), v->current_order.IsType(OT_GOTO_WAYPOINT));
										break;

									default:
										has_status = false;
										break;
								}
								break;
						}
						result = TestBinaryConditionCommon(item, has_status);
						break;
					}

					case TRIT_COND_LOAD_PERCENT: {
						result = TestCondition(CalcPercentVehicleFilled(v, nullptr), condop, condvalue);
						break;
					}

					case TRIT_COND_COUNTER_VALUE: {
						// TRVT_COUNTER_INDEX_INT value type uses the next slot
						i++;
						uint32_t value = this->items[i];
						const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(GetTraceRestrictValue(item));
						result = TestCondition(ctr != nullptr ? ctr->value : 0, condop, value);
						break;
					}

					case TRIT_COND_TIME_DATE_VALUE: {
						// TRVT_TIME_DATE_INT value type uses the next slot
						i++;
						uint32_t value = this->items[i];
						result = TestCondition(GetTraceRestrictTimeDateValue(static_cast<TraceRestrictTimeDateValueField>(GetTraceRestrictValue(item))), condop, value);
						break;
					}

					case TRIT_COND_RESERVED_TILES: {
						uint tiles_ahead = 0;
						if (v->lookahead != nullptr) {
							tiles_ahead = std::max<int>(0, v->lookahead->reservation_end_position - v->lookahead->current_position) / TILE_SIZE;
						}
						result = TestCondition(tiles_ahead, condop, condvalue);
						break;
					}

					case TRIT_COND_CATEGORY: {
						switch (static_cast<TraceRestrictCatgeoryCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TRCCAF_ENGINE_CLASS: {
								EngineClass ec = (EngineClass)condvalue;
								result = (GetTraceRestrictCondOp(item) != TRCO_IS);
								for (const Train *u = v; u != nullptr; u = u->Next()) {
									/* Check if engine class present */
									if (u->IsEngine() && RailVehInfo(u->engine_type)->engclass == ec) {
										result = !result;
										break;
									}
								}
								break;
							}

							default:
								NOT_REACHED();
								break;
						}
						break;
					}

					case TRIT_COND_TARGET_DIRECTION: {
						const Order *o = nullptr;
						switch (static_cast<TraceRestrictTargetDirectionCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TRTDCAF_CURRENT_ORDER:
								o = &(v->current_order);
								break;

							case TRTDCAF_NEXT_ORDER:
								if (v->orders == nullptr) break;
								if (v->orders->GetNumOrders() == 0) break;

								const Order *current_order = v->GetOrder(v->cur_real_order_index);
								for (const Order *order = v->orders->GetNext(current_order); order != current_order; order = v->orders->GetNext(order)) {
									if (order->IsGotoOrder()) {
										o = order;
										break;
									}
								}
								break;
						}

						if (o == nullptr) break;

						TileIndex target = o->GetLocation(v, true);
						if (target == INVALID_TILE) break;

						switch (condvalue) {
							case DIAGDIR_NE:
								result = TestBinaryConditionCommon(item, TileX(target) < TileX(input.tile));
								break;
							case DIAGDIR_SE:
								result = TestBinaryConditionCommon(item, TileY(target) > TileY(input.tile));
								break;
							case DIAGDIR_SW:
								result = TestBinaryConditionCommon(item, TileX(target) > TileX(input.tile));
								break;
							case DIAGDIR_NW:
								result = TestBinaryConditionCommon(item, TileY(target) < TileY(input.tile));
								break;
						}
						break;
					}

					case TRIT_COND_RESERVATION_THROUGH: {
						// TRIT_COND_RESERVATION_THROUGH value type uses the next slot
						i++;
						uint32_t test_tile = this->items[i];
						result = TestBinaryConditionCommon(item, TrainReservationPassesThroughTile(v, test_tile));
						break;
					}

					default:
						NOT_REACHED();
				}
				HandleCondition(condstack, condflags, result);
			}
		} else {
			if (condstack.empty() || condstack.back() & TRCSF_ACTIVE) {
				switch(type) {
					case TRIT_PF_DENY:
						if (GetTraceRestrictValue(item)) {
							out.flags &= ~TRPRF_DENY;
						} else {
							out.flags |= TRPRF_DENY;
						}
						break;

					case TRIT_PF_PENALTY:
						switch (static_cast<TraceRestrictPathfinderPenaltyAuxField>(GetTraceRestrictAuxField(item))) {
							case TRPPAF_VALUE:
								out.penalty += GetTraceRestrictValue(item);
								break;

							case TRPPAF_PRESET: {
								uint16_t index = GetTraceRestrictValue(item);
								assert(index < TRPPPI_END);
								out.penalty += _tracerestrict_pathfinder_penalty_preset_values[index];
								break;
							}

							default:
								NOT_REACHED();
						}
						break;

					case TRIT_RESERVE_THROUGH:
						if (GetTraceRestrictValue(item)) {
							out.flags &= ~TRPRF_RESERVE_THROUGH;
						} else {
							out.flags |= TRPRF_RESERVE_THROUGH;
						}
						break;

					case TRIT_LONG_RESERVE:
						switch (static_cast<TraceRestrictLongReserveValueField>(GetTraceRestrictValue(item))) {
							case TRLRVF_LONG_RESERVE:
								out.flags |= TRPRF_LONG_RESERVE;
								break;

							case TRLRVF_CANCEL_LONG_RESERVE:
								out.flags &= ~TRPRF_LONG_RESERVE;
								break;

							case TRLRVF_LONG_RESERVE_UNLESS_STOPPING:
								if (!(input.input_flags & TRPIF_PASSED_STOP)) {
									out.flags |= TRPRF_LONG_RESERVE;
								}
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					case TRIT_WAIT_AT_PBS:
						switch (static_cast<TraceRestrictWaitAtPbsValueField>(GetTraceRestrictValue(item))) {
							case TRWAPVF_WAIT_AT_PBS:
								out.flags |= TRPRF_WAIT_AT_PBS;
								break;

							case TRWAPVF_CANCEL_WAIT_AT_PBS:
								out.flags &= ~TRPRF_WAIT_AT_PBS;
								break;

							case TRWAPVF_PBS_RES_END_WAIT:
								out.flags |= TRPRF_PBS_RES_END_WAIT;
								break;

							case TRWAPVF_CANCEL_PBS_RES_END_WAIT:
								out.flags &= ~TRPRF_PBS_RES_END_WAIT;
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					case TRIT_SLOT: {
						if (!input.permitted_slot_operations) break;
						TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GetTraceRestrictValue(item));
						if (slot == nullptr || slot->vehicle_type != v->type) break;
						switch (static_cast<TraceRestrictSlotSubtypeField>(GetTraceRestrictCombinedAuxCondOpField(item))) {
							case TRSCOF_ACQUIRE_WAIT:
								if (input.permitted_slot_operations & TRPISP_ACQUIRE) {
									if (!slot->Occupy(v)) out.flags |= TRPRF_WAIT_AT_PBS;
								} else if (input.permitted_slot_operations & TRPISP_ACQUIRE_TEMP_STATE) {
									if (!slot->OccupyUsingTemporaryState(v->index, input.slot_temporary_state)) out.flags |= TRPRF_WAIT_AT_PBS;
								}
								break;

							case TRSCOF_ACQUIRE_TRY:
								if (input.permitted_slot_operations & TRPISP_ACQUIRE) {
									slot->Occupy(v);
								} else if (input.permitted_slot_operations & TRPISP_ACQUIRE_TEMP_STATE) {
									slot->OccupyUsingTemporaryState(v->index, input.slot_temporary_state);
								}
								break;

							case TRSCOF_RELEASE_ON_RESERVE:
								if (input.permitted_slot_operations & TRPISP_ACQUIRE) {
									slot->Vacate(v);
								} else if (input.permitted_slot_operations & TRPISP_ACQUIRE_TEMP_STATE) {
									slot->VacateUsingTemporaryState(v->index, input.slot_temporary_state);
								}
								break;

							case TRSCOF_RELEASE_BACK:
								if (input.permitted_slot_operations & TRPISP_RELEASE_BACK) slot->Vacate(v);
								break;

							case TRSCOF_RELEASE_FRONT:
								if (input.permitted_slot_operations & TRPISP_RELEASE_FRONT) slot->Vacate(v);
								break;

							case TRSCOF_PBS_RES_END_ACQ_WAIT:
								if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQUIRE) {
									if (!slot->Occupy(v)) out.flags |= TRPRF_PBS_RES_END_WAIT;
								} else if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQ_DRY) {
									if (this->actions_used_flags & TRPAUF_PBS_RES_END_SIMULATE) {
										if (!slot->OccupyUsingTemporaryState(v->index, &pbs_res_end_acq_dry_slot_temporary_state)) out.flags |= TRPRF_PBS_RES_END_WAIT;
									} else {
										if (!slot->OccupyDryRun(v->index)) out.flags |= TRPRF_PBS_RES_END_WAIT;
									}
								}
								break;

							case TRSCOF_PBS_RES_END_ACQ_TRY:
								if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQUIRE) {
									slot->Occupy(v);
								} else if ((input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQ_DRY) && (this->actions_used_flags & TRPAUF_PBS_RES_END_SIMULATE)) {
									slot->OccupyUsingTemporaryState(v->index, &pbs_res_end_acq_dry_slot_temporary_state);
								}
								break;

							case TRSCOF_PBS_RES_END_RELEASE:
								if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQUIRE) {
									slot->Vacate(v);
								} else if ((input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQ_DRY) && (this->actions_used_flags & TRPAUF_PBS_RES_END_SIMULATE)) {
									slot->VacateUsingTemporaryState(v->index, &pbs_res_end_acq_dry_slot_temporary_state);
								}
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;
					}

					case TRIT_REVERSE:
						switch (static_cast<TraceRestrictReverseValueField>(GetTraceRestrictValue(item))) {
							case TRRVF_REVERSE:
								out.flags |= TRPRF_REVERSE;
								break;

							case TRRVF_CANCEL_REVERSE:
								out.flags &= ~TRPRF_REVERSE;
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					case TRIT_SPEED_RESTRICTION: {
						out.speed_restriction = GetTraceRestrictValue(item);
						out.flags |= TRPRF_SPEED_RESTRICTION_SET;
						break;
					}

					case TRIT_NEWS_CONTROL:
						switch (static_cast<TraceRestrictNewsControlField>(GetTraceRestrictValue(item))) {
							case TRNCF_TRAIN_NOT_STUCK:
								out.flags |= TRPRF_TRAIN_NOT_STUCK;
								break;

							case TRNCF_CANCEL_TRAIN_NOT_STUCK:
								out.flags &= ~TRPRF_TRAIN_NOT_STUCK;
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					case TRIT_COUNTER: {
						// TRVT_COUNTER_INDEX_INT value type uses the next slot
						i++;
						uint32_t value = this->items[i];
						if (!(input.permitted_slot_operations & TRPISP_CHANGE_COUNTER)) break;
						TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(GetTraceRestrictValue(item));
						if (ctr == nullptr) break;
						ctr->ApplyUpdate(static_cast<TraceRestrictCounterCondOpField>(GetTraceRestrictCondOp(item)), value);
						break;
					}

					case TRIT_PF_PENALTY_CONTROL:
						switch (static_cast<TraceRestrictPfPenaltyControlField>(GetTraceRestrictValue(item))) {
							case TRPPCF_NO_PBS_BACK_PENALTY:
								out.flags |= TRPRF_NO_PBS_BACK_PENALTY;
								break;

							case TRPPCF_CANCEL_NO_PBS_BACK_PENALTY:
								out.flags &= ~TRPRF_NO_PBS_BACK_PENALTY;
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					case TRIT_SPEED_ADAPTATION_CONTROL:
						switch (static_cast<TraceRestrictSpeedAdaptationControlField>(GetTraceRestrictValue(item))) {
							case TRSACF_SPEED_ADAPT_EXEMPT:
								out.flags |= TRPRF_SPEED_ADAPT_EXEMPT;
								out.flags &= ~TRPRF_RM_SPEED_ADAPT_EXEMPT;
								break;

							case TRSACF_REMOVE_SPEED_ADAPT_EXEMPT:
								out.flags &= ~TRPRF_SPEED_ADAPT_EXEMPT;
								out.flags |= TRPRF_RM_SPEED_ADAPT_EXEMPT;
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					case TRIT_SIGNAL_MODE_CONTROL:
						switch (static_cast<TraceRestrictSignalModeControlField>(GetTraceRestrictValue(item))) {
							case TRSMCF_NORMAL_ASPECT:
								out.flags |= TRPRF_SIGNAL_MODE_NORMAL;
								out.flags &= ~TRPRF_SIGNAL_MODE_SHUNT;
								break;

							case TRSMCF_SHUNT_ASPECT:
								out.flags &= ~TRPRF_SIGNAL_MODE_NORMAL;
								out.flags |= TRPRF_SIGNAL_MODE_SHUNT;
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;

					default:
						NOT_REACHED();
				}
			} else {
				if (IsTraceRestrictDoubleItem(type)) i++;
			}
		}
	}
	if ((input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQ_DRY) && (this->actions_used_flags & TRPAUF_PBS_RES_END_SIMULATE)) {
		pbs_res_end_acq_dry_slot_temporary_state.RevertTemporaryChanges(v->index);
	}
	assert(condstack.empty());
}

void TraceRestrictProgram::ClearRefIds()
{
	if (this->refcount > 4) free(this->ref_ids.ptr_ref_ids.buffer);
}

/**
 * Increment ref count, only use when creating a mapping
 */
void TraceRestrictProgram::IncrementRefCount(TraceRestrictRefId ref_id)
{
	if (this->refcount >= 4) {
		if (this->refcount == 4) {
			/* Transition from inline to allocated mode */

			TraceRestrictRefId *ptr = MallocT<TraceRestrictRefId>(8);
			MemCpyT<TraceRestrictRefId>(ptr, this->ref_ids.inline_ref_ids, 4);
			this->ref_ids.ptr_ref_ids.buffer = ptr;
			this->ref_ids.ptr_ref_ids.elem_capacity = 8;
		} else if (this->refcount == this->ref_ids.ptr_ref_ids.elem_capacity) {
			// grow buffer
			this->ref_ids.ptr_ref_ids.elem_capacity *= 2;
			this->ref_ids.ptr_ref_ids.buffer = ReallocT<TraceRestrictRefId>(this->ref_ids.ptr_ref_ids.buffer, this->ref_ids.ptr_ref_ids.elem_capacity);
		}
		this->ref_ids.ptr_ref_ids.buffer[this->refcount] = ref_id;
	} else {
		this->ref_ids.inline_ref_ids[this->refcount] = ref_id;
	}
	this->refcount++;
}

/**
 * Decrement ref count, only use when removing a mapping
 */
void TraceRestrictProgram::DecrementRefCount(TraceRestrictRefId ref_id) {
	assert(this->refcount > 0);
	if (this->refcount >= 2) {
		TraceRestrictRefId *data = this->GetRefIdsPtr();
		for (uint i = 0; i < this->refcount - 1; i++) {
			if (data[i] == ref_id) {
				data[i] = data[this->refcount - 1];
				break;
			}
		}
	}
	this->refcount--;
	if (this->refcount == 4) {
		/* Transition from allocated to inline mode */

		TraceRestrictRefId *ptr = this->ref_ids.ptr_ref_ids.buffer;
		MemCpyT<TraceRestrictRefId>(this->ref_ids.inline_ref_ids, ptr, 4);
		free(ptr);
	}
	if (this->refcount == 0) {
		extern const TraceRestrictProgram *_viewport_highlight_tracerestrict_program;
		if (_viewport_highlight_tracerestrict_program == this) {
			_viewport_highlight_tracerestrict_program = nullptr;
			InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		}
		delete this;
	}
}

/**
 * Validate a instruction list
 * Returns successful result if program seems OK
 * This only validates that conditional nesting is correct,
 * and that all instructions have a known type, at present
 */
CommandCost TraceRestrictProgram::Validate(const std::vector<TraceRestrictItem> &items, TraceRestrictProgramActionsUsedFlags &actions_used_flags) {
	// static to avoid needing to re-alloc/resize on each execution
	static std::vector<TraceRestrictCondStackFlags> condstack;
	condstack.clear();
	actions_used_flags = TRPAUF_NONE;

	static std::vector<TraceRestrictSlotID> pbs_res_end_released_slots;
	pbs_res_end_released_slots.clear();
	static std::vector<TraceRestrictSlotID> pbs_res_end_acquired_slots;
	pbs_res_end_acquired_slots.clear();

	size_t size = items.size();
	for (size_t i = 0; i < size; i++) {
		TraceRestrictItem item = items[i];
		TraceRestrictItemType type = GetTraceRestrictType(item);

		auto validation_error = [i](StringID str) -> CommandCost {
			CommandCost result(str);
			result.SetResultData((uint)i);
			return result;
		};

		auto unknown_instruction = [&]() {
			return validation_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_UNKNOWN_INSTRUCTION);
		};

		// check multi-word instructions
		if (IsTraceRestrictDoubleItem(item)) {
			i++;
			if (i >= size) {
				return validation_error(STR_TRACE_RESTRICT_ERROR_OFFSET_TOO_LARGE); // instruction ran off end
			}
		}

		if (IsTraceRestrictConditional(item)) {
			TraceRestrictCondFlags condflags = GetTraceRestrictCondFlags(item);

			if (type == TRIT_COND_ENDIF) {
				if (condstack.empty()) {
					return validation_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_NO_IF); // else/endif with no starting if
				}
				if (condflags & TRCF_ELSE) {
					// else
					if (condstack.back() & TRCSF_SEEN_ELSE) {
						return validation_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_DUP_ELSE); // Two else clauses
					}
					HandleCondition(condstack, condflags, true);
					condstack.back() |= TRCSF_SEEN_ELSE;
				} else {
					// end if
					condstack.pop_back();
				}
			} else {
				if (condflags & (TRCF_OR | TRCF_ELSE)) { // elif/orif
					if (condstack.empty()) {
						return validation_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_ELIF_NO_IF); // Pre-empt assertions in HandleCondition
					}
					if (condstack.back() & TRCSF_SEEN_ELSE) {
						return validation_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_DUP_ELSE); // else clause followed by elif/orif
					}
				}
				HandleCondition(condstack, condflags, true);
			}

			const TraceRestrictCondOp condop = GetTraceRestrictCondOp(item);
			auto invalid_condition = [&]() -> bool {
				switch (condop) {
					case TRCO_IS:
					case TRCO_ISNOT:
					case TRCO_LT:
					case TRCO_LTE:
					case TRCO_GT:
					case TRCO_GTE:
						return false;
					default:
						return true;
				}
			};
			auto invalid_binary_condition = [&]() -> bool {
				switch (condop) {
					case TRCO_IS:
					case TRCO_ISNOT:
						return false;
					default:
						return true;
				}
			};
			auto invalid_order_condition = [&]() -> bool {
				if (invalid_binary_condition()) return true;
				switch (static_cast<TraceRestrictOrderCondAuxField>(GetTraceRestrictAuxField(item))) {
					case TROCAF_STATION:
					case TROCAF_WAYPOINT:
					case TROCAF_DEPOT:
						return false;
					default:
						return true;
				}
			};

			/* Validation action instruction */
			switch (GetTraceRestrictType(item)) {
				case TRIT_COND_ENDIF:
				case TRIT_COND_UNDEFINED:
					break;

				case TRIT_COND_TRAIN_LENGTH:
				case TRIT_COND_MAX_SPEED:
				case TRIT_COND_LOAD_PERCENT:
				case TRIT_COND_COUNTER_VALUE:
				case TRIT_COND_RESERVED_TILES:
					if (invalid_condition()) return unknown_instruction();
					break;

				case TRIT_COND_CARGO:
				case TRIT_COND_TRAIN_GROUP:
				case TRIT_COND_TRAIN_IN_SLOT:
				case TRIT_COND_TRAIN_OWNER:
				case TRIT_COND_RESERVATION_THROUGH:
					if (invalid_binary_condition()) return unknown_instruction();
					break;

				case TRIT_COND_CURRENT_ORDER:
				case TRIT_COND_NEXT_ORDER:
				case TRIT_COND_LAST_STATION:
					if (invalid_order_condition()) return unknown_instruction();
					break;

				case TRIT_COND_ENTRY_DIRECTION:
					if (invalid_binary_condition()) return unknown_instruction();
					switch (GetTraceRestrictValue(item)) {
						case TRNTSV_NE:
						case TRNTSV_SE:
						case TRNTSV_SW:
						case TRNTSV_NW:
						case TRDTSV_FRONT:
						case TRDTSV_BACK:
						case TRDTSV_TUNBRIDGE_ENTER:
						case TRDTSV_TUNBRIDGE_EXIT:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_PBS_ENTRY_SIGNAL:
					if (invalid_binary_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictPBSEntrySignalAuxField>(GetTraceRestrictAuxField(item))) {
						case TRPESAF_VEH_POS:
						case TRPESAF_RES_END:
						case TRPESAF_RES_END_TILE:
							break;

						default:
							return unknown_instruction();
					}
					break;


				case TRIT_COND_PHYS_PROP:
					if (invalid_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictPhysPropCondAuxField>(GetTraceRestrictAuxField(item))) {
						case TRPPCAF_WEIGHT:
						case TRPPCAF_POWER:
						case TRPPCAF_MAX_TE:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_PHYS_RATIO:
					if (invalid_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictPhysPropRatioCondAuxField>(GetTraceRestrictAuxField(item))) {
						case TRPPRCAF_POWER_WEIGHT:
						case TRPPRCAF_MAX_TE_WEIGHT:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_TIME_DATE_VALUE:
					if (invalid_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictTimeDateValueField>(GetTraceRestrictValue(item))) {
						case TRTDVF_MINUTE:
						case TRTDVF_HOUR:
						case TRTDVF_HOUR_MINUTE:
						case TRTDVF_DAY:
						case TRTDVF_MONTH:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_CATEGORY:
					if (invalid_binary_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictCatgeoryCondAuxField>(GetTraceRestrictAuxField(item))) {
						case TRCCAF_ENGINE_CLASS:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_TARGET_DIRECTION:
					if (invalid_binary_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictTargetDirectionCondAuxField>(GetTraceRestrictAuxField(item))) {
						case TRTDCAF_CURRENT_ORDER:
						case TRTDCAF_NEXT_ORDER:
							break;

						default:
							return unknown_instruction();
					}
					switch (GetTraceRestrictValue(item)) {
						case DIAGDIR_NE:
						case DIAGDIR_SE:
						case DIAGDIR_SW:
						case DIAGDIR_NW:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_TRAIN_STATUS:
					if (invalid_binary_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictTrainStatusValueField>(GetTraceRestrictValue(item))) {
						case TRTSVF_EMPTY:
						case TRTSVF_FULL:
						case TRTSVF_BROKEN_DOWN:
						case TRTSVF_NEEDS_REPAIR:
						case TRTSVF_REVERSING:
						case TRTSVF_HEADING_TO_STATION_WAYPOINT:
						case TRTSVF_HEADING_TO_DEPOT:
						case TRTSVF_LOADING:
						case TRTSVF_WAITING:
						case TRTSVF_LOST:
						case TRTSVF_REQUIRES_SERVICE:
						case TRTSVF_STOPPING_AT_STATION_WAYPOINT:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COND_SLOT_OCCUPANCY:
					if (invalid_condition()) return unknown_instruction();
					switch (static_cast<TraceRestrictSlotOccupancyCondAuxField>(GetTraceRestrictAuxField(item))) {
						case TRSOCAF_OCCUPANTS:
						case TRSOCAF_REMAINING:
							break;

						default:
							return unknown_instruction();
					}
					break;

				default:
					return unknown_instruction();
			}

			/* Determine actions_used_flags */
			switch (GetTraceRestrictType(item)) {
				case TRIT_COND_ENDIF:
				case TRIT_COND_UNDEFINED:
				case TRIT_COND_TRAIN_LENGTH:
				case TRIT_COND_MAX_SPEED:
				case TRIT_COND_CARGO:
				case TRIT_COND_ENTRY_DIRECTION:
				case TRIT_COND_PBS_ENTRY_SIGNAL:
				case TRIT_COND_TRAIN_GROUP:
				case TRIT_COND_PHYS_PROP:
				case TRIT_COND_PHYS_RATIO:
				case TRIT_COND_TRAIN_OWNER:
				case TRIT_COND_LOAD_PERCENT:
				case TRIT_COND_COUNTER_VALUE:
				case TRIT_COND_TIME_DATE_VALUE:
				case TRIT_COND_RESERVED_TILES:
				case TRIT_COND_CATEGORY:
				case TRIT_COND_RESERVATION_THROUGH:
					break;

				case TRIT_COND_CURRENT_ORDER:
				case TRIT_COND_NEXT_ORDER:
				case TRIT_COND_LAST_STATION:
				case TRIT_COND_TARGET_DIRECTION:
					actions_used_flags |= TRPAUF_ORDER_CONDITIONALS;
					break;

				case TRIT_COND_TRAIN_STATUS:
					switch (static_cast<TraceRestrictTrainStatusValueField>(GetTraceRestrictValue(item))) {
						case TRTSVF_HEADING_TO_STATION_WAYPOINT:
						case TRTSVF_HEADING_TO_DEPOT:
						case TRTSVF_LOADING:
						case TRTSVF_WAITING:
						case TRTSVF_STOPPING_AT_STATION_WAYPOINT:
							actions_used_flags |= TRPAUF_ORDER_CONDITIONALS;
							break;

						default:
							break;
					}
					break;

				case TRIT_COND_TRAIN_IN_SLOT:
				case TRIT_COND_SLOT_OCCUPANCY:
					actions_used_flags |= TRPAUF_SLOT_CONDITIONALS;
					if (find_index(pbs_res_end_released_slots, GetTraceRestrictValue(item)) >= 0 || find_index(pbs_res_end_acquired_slots, GetTraceRestrictValue(item)) >= 0) {
						actions_used_flags |= TRPAUF_PBS_RES_END_SIMULATE;
					}
					break;

				default:
					/* Validation has already been done, above */
					NOT_REACHED();
			}
		} else {
			switch (GetTraceRestrictType(item)) {
				case TRIT_PF_DENY:
					actions_used_flags |= TRPAUF_PF;
					break;

				case TRIT_PF_PENALTY:
					actions_used_flags |= TRPAUF_PF;

					switch (static_cast<TraceRestrictPathfinderPenaltyAuxField>(GetTraceRestrictAuxField(item))) {
						case TRPPAF_VALUE:
							break;

						case TRPPAF_PRESET:
							if (GetTraceRestrictValue(item) >= TRPPPI_END) return unknown_instruction();
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_RESERVE_THROUGH:
					actions_used_flags |= TRPAUF_RESERVE_THROUGH;
					if (GetTraceRestrictValue(item)) {
						actions_used_flags &= ~TRPAUF_RESERVE_THROUGH_ALWAYS;
					} else if (condstack.empty()) {
						actions_used_flags |= TRPAUF_RESERVE_THROUGH_ALWAYS;
					}
					break;

				case TRIT_LONG_RESERVE:
					actions_used_flags |= TRPAUF_LONG_RESERVE;
					break;

				case TRIT_WAIT_AT_PBS:
					switch (static_cast<TraceRestrictWaitAtPbsValueField>(GetTraceRestrictValue(item))) {
						case TRWAPVF_WAIT_AT_PBS:
						case TRWAPVF_CANCEL_WAIT_AT_PBS:
							actions_used_flags |= TRPAUF_WAIT_AT_PBS;
							break;

						case TRWAPVF_PBS_RES_END_WAIT:
						case TRWAPVF_CANCEL_PBS_RES_END_WAIT:
							actions_used_flags |= TRPAUF_PBS_RES_END_WAIT;
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_SLOT:
					switch (static_cast<TraceRestrictSlotSubtypeField>(GetTraceRestrictCombinedAuxCondOpField(item))) {
						case TRSCOF_ACQUIRE_WAIT:
							actions_used_flags |= TRPAUF_SLOT_ACQUIRE | TRPAUF_SLOT_CONDITIONALS | TRPAUF_WAIT_AT_PBS;
							break;

						case TRSCOF_ACQUIRE_TRY:
							actions_used_flags |= TRPAUF_SLOT_ACQUIRE;
							break;

						case TRSCOF_RELEASE_ON_RESERVE:
							actions_used_flags |= TRPAUF_SLOT_ACQUIRE;
							break;

						case TRSCOF_RELEASE_BACK:
							actions_used_flags |= TRPAUF_SLOT_RELEASE_BACK;
							break;

						case TRSCOF_RELEASE_FRONT:
							actions_used_flags |= TRPAUF_SLOT_RELEASE_FRONT;
							break;

						case TRSCOF_PBS_RES_END_ACQ_WAIT:
							actions_used_flags |= TRPAUF_PBS_RES_END_SLOT | TRPAUF_PBS_RES_END_WAIT | TRPAUF_SLOT_CONDITIONALS ;
							if (find_index(pbs_res_end_released_slots, GetTraceRestrictValue(item)) >= 0) actions_used_flags |= TRPAUF_PBS_RES_END_SIMULATE;
							include(pbs_res_end_acquired_slots, GetTraceRestrictValue(item));
							break;

						case TRSCOF_PBS_RES_END_ACQ_TRY:
							actions_used_flags |= TRPAUF_PBS_RES_END_SLOT;
							if (find_index(pbs_res_end_released_slots, GetTraceRestrictValue(item)) >= 0) actions_used_flags |= TRPAUF_PBS_RES_END_SIMULATE;
							include(pbs_res_end_acquired_slots, GetTraceRestrictValue(item));
							break;

						case TRSCOF_PBS_RES_END_RELEASE:
							actions_used_flags |= TRPAUF_PBS_RES_END_SLOT;
							include(pbs_res_end_released_slots, GetTraceRestrictValue(item));
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_REVERSE:
					switch (static_cast<TraceRestrictReverseValueField>(GetTraceRestrictValue(item))) {
						case TRRVF_REVERSE:
							actions_used_flags |= TRPAUF_REVERSE;
							break;

						case TRRVF_CANCEL_REVERSE:
							if (condstack.empty()) actions_used_flags &= ~TRPAUF_REVERSE;
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_SPEED_RESTRICTION:
					actions_used_flags |= TRPAUF_SPEED_RESTRICTION;
					break;

				case TRIT_NEWS_CONTROL:
					actions_used_flags |= TRPAUF_TRAIN_NOT_STUCK;

					switch (static_cast<TraceRestrictNewsControlField>(GetTraceRestrictValue(item))) {
						case TRNCF_TRAIN_NOT_STUCK:
						case TRNCF_CANCEL_TRAIN_NOT_STUCK:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_COUNTER:
					actions_used_flags |= TRPAUF_CHANGE_COUNTER;

					switch (static_cast<TraceRestrictCounterCondOpField>(GetTraceRestrictCondOp(item))) {
						case TRCCOF_INCREASE:
						case TRCCOF_DECREASE:
						case TRCCOF_SET:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_PF_PENALTY_CONTROL:
					actions_used_flags |= TRPAUF_NO_PBS_BACK_PENALTY;

					switch (static_cast<TraceRestrictPfPenaltyControlField>(GetTraceRestrictValue(item))) {
						case TRPPCF_NO_PBS_BACK_PENALTY:
						case TRPPCF_CANCEL_NO_PBS_BACK_PENALTY:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_SPEED_ADAPTATION_CONTROL:
					actions_used_flags |= TRPAUF_SPEED_ADAPTATION;

					switch (static_cast<TraceRestrictSpeedAdaptationControlField>(GetTraceRestrictValue(item))) {
						case TRSACF_SPEED_ADAPT_EXEMPT:
						case TRSACF_REMOVE_SPEED_ADAPT_EXEMPT:
							break;

						default:
							return unknown_instruction();
					}
					break;

				case TRIT_SIGNAL_MODE_CONTROL:
					actions_used_flags |= TRPAUF_CMB_SIGNAL_MODE_CTRL;

					switch (static_cast<TraceRestrictSignalModeControlField>(GetTraceRestrictValue(item))) {
						case TRSMCF_NORMAL_ASPECT:
						case TRSMCF_SHUNT_ASPECT:
							break;

						default:
							return unknown_instruction();
					}
					break;

				default:
					return unknown_instruction();
			}
		}
	}
	if (!condstack.empty()) {
		return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_END_CONDSTACK);
	}
	return CommandCost();
}

/**
 * Convert an instruction index into an item array index
 */
size_t TraceRestrictProgram::InstructionOffsetToArrayOffset(const std::vector<TraceRestrictItem> &items, size_t offset)
{
	size_t output_offset = 0;
	size_t size = items.size();
	for (size_t i = 0; i < offset && output_offset < size; i++, output_offset++) {
		if (IsTraceRestrictDoubleItem(items[output_offset])) {
			output_offset++;
		}
	}
	return output_offset;
}

/**
 * Convert an item array index into an instruction index
 */
size_t TraceRestrictProgram::ArrayOffsetToInstructionOffset(const std::vector<TraceRestrictItem> &items, size_t offset)
{
	size_t output_offset = 0;
	for (size_t i = 0; i < offset; i++, output_offset++) {
		if (IsTraceRestrictDoubleItem(items[i])) {
			i++;
		}
	}
	return output_offset;
}

/**
 * Set the value and aux field of @p item, as per the value type in @p value_type
 */
void SetTraceRestrictValueDefault(TraceRestrictItem &item, TraceRestrictValueType value_type)
{
	switch (value_type) {
		case TRVT_NONE:
		case TRVT_INT:
		case TRVT_DENY:
		case TRVT_SPEED:
		case TRVT_TILE_INDEX:
		case TRVT_TILE_INDEX_THROUGH:
		case TRVT_RESERVE_THROUGH:
		case TRVT_LONG_RESERVE:
		case TRVT_WEIGHT:
		case TRVT_POWER:
		case TRVT_FORCE:
		case TRVT_POWER_WEIGHT_RATIO:
		case TRVT_FORCE_WEIGHT_RATIO:
		case TRVT_WAIT_AT_PBS:
		case TRVT_TRAIN_STATUS:
		case TRVT_REVERSE:
		case TRVT_PERCENT:
		case TRVT_NEWS_CONTROL:
		case TRVT_TIME_DATE_INT:
		case TRVT_ENGINE_CLASS:
		case TRVT_PF_PENALTY_CONTROL:
		case TRVT_SPEED_ADAPTATION_CONTROL:
		case TRVT_SIGNAL_MODE_CONTROL:
		case TRVT_ORDER_TARGET_DIAGDIR:
			SetTraceRestrictValue(item, 0);
			if (!IsTraceRestrictTypeAuxSubtype(GetTraceRestrictType(item))) {
				SetTraceRestrictAuxField(item, 0);
			}
			break;

		case TRVT_ORDER:
			SetTraceRestrictValue(item, INVALID_STATION);
			SetTraceRestrictAuxField(item, TROCAF_STATION);
			break;

		case TRVT_CARGO_ID:
			assert(_standard_cargo_mask != 0);
			SetTraceRestrictValue(item, FindFirstBit(_standard_cargo_mask));
			SetTraceRestrictAuxField(item, 0);
			break;

		case TRVT_DIRECTION:
			SetTraceRestrictValue(item, TRDTSV_FRONT);
			SetTraceRestrictAuxField(item, 0);
			break;

		case TRVT_PF_PENALTY:
			SetTraceRestrictValue(item, TRPPPI_SMALL);
			SetTraceRestrictAuxField(item, TRPPAF_PRESET);
			break;

		case TRVT_GROUP_INDEX:
			SetTraceRestrictValue(item, INVALID_GROUP);
			SetTraceRestrictAuxField(item, 0);
			break;

		case TRVT_OWNER:
			SetTraceRestrictValue(item, INVALID_OWNER);
			SetTraceRestrictAuxField(item, 0);
			break;

		case TRVT_SLOT_INDEX:
			SetTraceRestrictValue(item, INVALID_TRACE_RESTRICT_SLOT_ID);
			SetTraceRestrictAuxField(item, 0);
			break;

		case TRVT_SLOT_INDEX_INT:
			SetTraceRestrictValue(item, INVALID_TRACE_RESTRICT_SLOT_ID);
			break;

		case TRVT_COUNTER_INDEX_INT:
			SetTraceRestrictValue(item, INVALID_TRACE_RESTRICT_COUNTER_ID);
			break;

		default:
			NOT_REACHED();
			break;
	}
}

/**
 * Set the type field of a TraceRestrictItem, and resets any other fields which are no longer valid/meaningful to sensible defaults
 */
void SetTraceRestrictTypeAndNormalise(TraceRestrictItem &item, TraceRestrictItemType type, uint8_t aux_data)
{
	if (item != 0) {
		assert(GetTraceRestrictType(item) != TRIT_NULL);
		assert(IsTraceRestrictConditional(item) == IsTraceRestrictTypeConditional(type));
	}
	assert(type != TRIT_NULL);

	TraceRestrictTypePropertySet old_properties = GetTraceRestrictTypeProperties(item);
	SetTraceRestrictType(item, type);
	if (IsTraceRestrictTypeAuxSubtype(type)) {
		SetTraceRestrictAuxField(item, aux_data);
	} else {
		assert(aux_data == 0);
	}
	TraceRestrictTypePropertySet new_properties = GetTraceRestrictTypeProperties(item);

	if (old_properties.cond_type != new_properties.cond_type ||
			old_properties.value_type != new_properties.value_type) {
		SetTraceRestrictCondOp(item, TRCO_IS);
		SetTraceRestrictValueDefault(item, new_properties.value_type);
	}
	if (new_properties.value_type == TRVT_SLOT_INDEX || new_properties.value_type == TRVT_SLOT_INDEX_INT) {
		if (!IsTraceRestrictTypeNonMatchingVehicleTypeSlot(GetTraceRestrictType(item))) {
			const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GetTraceRestrictValue(item));
			if (slot != nullptr && slot->vehicle_type != VEH_TRAIN) SetTraceRestrictValue(item, INVALID_TRACE_RESTRICT_SLOT_ID);
		}
	}
	if (GetTraceRestrictType(item) == TRIT_COND_LAST_STATION && GetTraceRestrictAuxField(item) != TROCAF_STATION) {
		// if changing type from another order type to last visited station, reset value if not currently a station
		SetTraceRestrictValueDefault(item, TRVT_ORDER);
	}
}

/**
 * Sets the "signal has a trace restrict mapping" bit
 * This looks for mappings with that tile index
 */
void TraceRestrictSetIsSignalRestrictedBit(TileIndex t)
{
	// First mapping for this tile, or later
	TraceRestrictMapping::iterator lower_bound = _tracerestrictprogram_mapping.lower_bound(MakeTraceRestrictRefId(t, static_cast<Track>(0)));

	bool found = (lower_bound != _tracerestrictprogram_mapping.end()) && (GetTraceRestrictRefIdTileIndex(lower_bound->first) == t);

	// If iterators are the same, there are no mappings for this tile
	switch (GetTileType(t)) {
		case MP_RAILWAY:
			SetRestrictedSignal(t, found);
			break;

		case MP_TUNNELBRIDGE:
			SetTunnelBridgeRestrictedSignal(t, found);
			break;

		default:
			NOT_REACHED();
	}
}

/**
 * Create a new program mapping to an existing program
 * If a mapping already exists, it is removed
 */
void TraceRestrictCreateProgramMapping(TraceRestrictRefId ref, TraceRestrictProgram *prog)
{
	std::pair<TraceRestrictMapping::iterator, bool> insert_result =
			_tracerestrictprogram_mapping.insert(std::make_pair(ref, TraceRestrictMappingItem(prog->index)));

	if (!insert_result.second) {
		// value was not inserted, there is an existing mapping
		// unref the existing mapping before updating it
		_tracerestrictprogram_pool.Get(insert_result.first->second.program_id)->DecrementRefCount(ref);
		insert_result.first->second = prog->index;
	}
	prog->IncrementRefCount(ref);

	TileIndex tile = GetTraceRestrictRefIdTileIndex(ref);
	Track track = GetTraceRestrictRefIdTrack(ref);
	TraceRestrictSetIsSignalRestrictedBit(tile);
	MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	YapfNotifyTrackLayoutChange(tile, track);
}

/**
 * Remove a program mapping
 * @return true if a mapping was actually removed
 */
bool TraceRestrictRemoveProgramMapping(TraceRestrictRefId ref)
{
	TraceRestrictMapping::iterator iter = _tracerestrictprogram_mapping.find(ref);
	if (iter != _tracerestrictprogram_mapping.end()) {
		// Found
		TraceRestrictProgram *prog = _tracerestrictprogram_pool.Get(iter->second.program_id);

		bool update_reserve_through = (prog->actions_used_flags & TRPAUF_RESERVE_THROUGH_ALWAYS);

		// check to see if another mapping needs to be removed as well
		// do this before decrementing the refcount
		bool remove_other_mapping = prog->refcount == 2 && prog->items.empty();

		prog->DecrementRefCount(ref);
		_tracerestrictprogram_mapping.erase(iter);

		TileIndex tile = GetTraceRestrictRefIdTileIndex(ref);
		Track track = GetTraceRestrictRefIdTrack(ref);
		TraceRestrictSetIsSignalRestrictedBit(tile);
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		YapfNotifyTrackLayoutChange(tile, track);

		if (remove_other_mapping) {
			TraceRestrictRemoveProgramMapping(const_cast<const TraceRestrictProgram *>(prog)->GetRefIdsPtr()[0]);
		}

		if (update_reserve_through && IsTileType(tile, MP_RAILWAY)) {
			UpdateSignalReserveThroughBit(tile, track, true);
		}
		return true;
	} else {
		return false;
	}
}

void TraceRestrictCheckRefreshSignals(const TraceRestrictProgram *prog, size_t old_size, TraceRestrictProgramActionsUsedFlags old_actions_used_flags)
{
	if (((old_actions_used_flags ^ prog->actions_used_flags) & TRPAUF_RESERVE_THROUGH_ALWAYS)) {
		const TraceRestrictRefId *data = prog->GetRefIdsPtr();
		for (uint i = 0; i < prog->refcount; i++) {
			TileIndex tile = GetTraceRestrictRefIdTileIndex(data[i]);
			Track track = GetTraceRestrictRefIdTrack(data[i]);
			if (IsTileType(tile, MP_RAILWAY)) UpdateSignalReserveThroughBit(tile, track, true);
		}
	}

	if (_network_dedicated) return;

	if (!((old_actions_used_flags ^ prog->actions_used_flags) & (TRPAUF_RESERVE_THROUGH_ALWAYS | TRPAUF_REVERSE))) return;

	if (old_size == 0 && prog->refcount == 1) return; // Program is new, no need to refresh again

	const TraceRestrictRefId *data = prog->GetRefIdsPtr();
	for (uint i = 0; i < prog->refcount; i++) {
		MarkTileDirtyByTile(GetTraceRestrictRefIdTileIndex(data[i]), VMDF_NOT_MAP_MODE);
	}
}

void TraceRestrictCheckRefreshSingleSignal(const TraceRestrictProgram *prog, TraceRestrictRefId ref, TraceRestrictProgramActionsUsedFlags old_actions_used_flags)
{
	if (((old_actions_used_flags ^ prog->actions_used_flags) & TRPAUF_RESERVE_THROUGH_ALWAYS)) {
		TileIndex tile = GetTraceRestrictRefIdTileIndex(ref);
		Track track = GetTraceRestrictRefIdTrack(ref);
		if (IsTileType(tile, MP_RAILWAY)) UpdateSignalReserveThroughBit(tile, track, true);
	}
}

/**
 * Gets the signal program for the tile ref @p ref
 * An empty program will be constructed if none exists, and @p create_new is true, unless the pool is full
 */
TraceRestrictProgram *GetTraceRestrictProgram(TraceRestrictRefId ref, bool create_new)
{
	// Optimise for lookup, creating doesn't have to be that fast

	TraceRestrictMapping::iterator iter = _tracerestrictprogram_mapping.find(ref);
	if (iter != _tracerestrictprogram_mapping.end()) {
		// Found
		return _tracerestrictprogram_pool.Get(iter->second.program_id);
	} else if (create_new) {
		// Not found

		// Create new pool item
		if (!TraceRestrictProgram::CanAllocateItem()) {
			return nullptr;
		}
		TraceRestrictProgram *prog = new TraceRestrictProgram();

		// Create new mapping to pool item
		TraceRestrictCreateProgramMapping(ref, prog);
		return prog;
	} else {
		return nullptr;
	}
}

/**
 * Gets the first signal program for the given tile
 * This is for debug/display purposes only
 */
TraceRestrictProgram *GetFirstTraceRestrictProgramOnTile(TileIndex t)
{
	// First mapping for this tile, or later
	TraceRestrictMapping::iterator lower_bound = _tracerestrictprogram_mapping.lower_bound(MakeTraceRestrictRefId(t, static_cast<Track>(0)));

	if ((lower_bound != _tracerestrictprogram_mapping.end()) && (GetTraceRestrictRefIdTileIndex(lower_bound->first) == t)) {
		return _tracerestrictprogram_pool.Get(lower_bound->second.program_id);
	}
	return nullptr;
}

/**
 * Notify that a signal is being removed
 * Remove any trace restrict mappings associated with it
 */
void TraceRestrictNotifySignalRemoval(TileIndex tile, Track track)
{
	TraceRestrictRefId ref = MakeTraceRestrictRefId(tile, track);
	bool removed = TraceRestrictRemoveProgramMapping(ref);
	CloseWindowById(WC_TRACE_RESTRICT, ref);
	if (removed) InvalidateWindowClassesData(WC_TRACE_RESTRICT);
}

/**
 * Helper function to perform parameter bit-packing and call DoCommandP, for instruction modification actions
 */
void TraceRestrictDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32_t offset, uint32_t value, StringID error_msg)
{
	uint32_t p1 = 0;
	SB(p1, 0, 3, track);
	SB(p1, 3, 5, type);
	assert(offset < (1 << 16));
	SB(p1, 8, 16, offset);
	DoCommandP(tile, p1, value, CMD_PROGRAM_TRACERESTRICT_SIGNAL | CMD_MSG(error_msg));
}

/**
 * Check whether a tile/track pair contains a usable signal
 */
static CommandCost TraceRestrictCheckTileIsUsable(TileIndex tile, Track track)
{
	// Check that there actually is a signal here
	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (!IsPlainRailTile(tile) || !HasTrack(tile, track)) {
				return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
			}
			if (!HasSignalOnTrack(tile, track)) {
				return_cmd_error(STR_ERROR_THERE_ARE_NO_SIGNALS);
			}
			break;

		case MP_TUNNELBRIDGE:
			if (!IsRailTunnelBridgeTile(tile) || !HasBit(GetTunnelBridgeTrackBits(tile), track)) {
				return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
			}
			if (!IsTunnelBridgeWithSignalSimulation(tile) || !IsTrackAcrossTunnelBridge(tile, track)) {
				return_cmd_error(STR_ERROR_THERE_ARE_NO_SIGNALS);
			}
			break;

		default:
			return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	}

	// Check tile ownership, do this afterwards to avoid tripping up on house/industry tiles
	CommandCost ret = CheckTileOwnership(tile);
	if (ret.Failed()) {
		return ret;
	}

	return CommandCost();
}

/**
 * Returns an appropriate default value for the second item of a dual-item instruction
 * @p item is the first item of the instruction
 */
static uint32_t GetDualInstructionInitialValue(TraceRestrictItem item)
{
	switch (GetTraceRestrictType(item)) {
		case TRIT_COND_PBS_ENTRY_SIGNAL:
		case TRIT_COND_RESERVATION_THROUGH:
			return INVALID_TILE;

		case TRIT_COND_SLOT_OCCUPANCY:
		case TRIT_COND_COUNTER_VALUE:
		case TRIT_COND_TIME_DATE_VALUE:
			return 0;

		case TRIT_COUNTER:
			return 1;

		default:
			NOT_REACHED();
	}
}

template <typename T> T InstructionIteratorNext(T iter)
{
	return IsTraceRestrictDoubleItem(*iter) ? iter + 2 : iter + 1;
}

template <typename T> void InstructionIteratorAdvance(T &iter)
{
	iter = InstructionIteratorNext(iter);
}

CommandCost TraceRestrictProgramRemoveItemAt(std::vector<TraceRestrictItem> &items, uint32_t offset, bool shallow_mode)
{
	TraceRestrictItem old_item = *TraceRestrictProgram::InstructionAt(items, offset);
	if (IsTraceRestrictConditional(old_item) && GetTraceRestrictCondFlags(old_item) != TRCF_OR) {
		bool remove_whole_block = false;
		if (GetTraceRestrictCondFlags(old_item) == 0) {
			if (GetTraceRestrictType(old_item) == TRIT_COND_ENDIF) {
				// this is an end if, can't remove these
				return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_REMOVE_ENDIF);
			} else {
				// this is an opening if
				remove_whole_block = true;
			}
		}

		uint32_t recursion_depth = 1;
		std::vector<TraceRestrictItem>::iterator remove_start = TraceRestrictProgram::InstructionAt(items, offset);
		std::vector<TraceRestrictItem>::iterator remove_end = InstructionIteratorNext(remove_start);

		// iterate until matching end block found
		for (; remove_end != items.end(); InstructionIteratorAdvance(remove_end)) {
			TraceRestrictItem current_item = *remove_end;
			if (IsTraceRestrictConditional(current_item)) {
				if (GetTraceRestrictCondFlags(current_item) == 0) {
					if (GetTraceRestrictType(current_item) == TRIT_COND_ENDIF) {
						// this is an end if
						recursion_depth--;
						if (recursion_depth == 0) {
							if (remove_whole_block) {
								if (shallow_mode) {
									// must erase endif first, as it is later in the vector
									items.erase(remove_end, InstructionIteratorNext(remove_end));
								} else {
									// inclusively remove up to here
									InstructionIteratorAdvance(remove_end);
								}
								break;
							} else {
								// exclusively remove up to here
								break;
							}
						}
					} else {
						// this is an opening if
						recursion_depth++;
					}
				} else {
					// this is an else/or type block
					if (recursion_depth == 1 && !remove_whole_block) {
						// exclusively remove up to here
						recursion_depth = 0;
						break;
					}
					if (recursion_depth == 1 && remove_whole_block && shallow_mode) {
						// shallow-removing whole if block, and it contains an else/or if, bail out
						return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_SHALLOW_REMOVE_IF_ELIF);
					}
				}
			}
		}
		if (recursion_depth != 0) return CMD_ERROR; // ran off the end
		if (shallow_mode) {
			items.erase(remove_start, InstructionIteratorNext(remove_start));
		} else {
			items.erase(remove_start, remove_end);
		}
	} else {
		std::vector<TraceRestrictItem>::iterator remove_start = TraceRestrictProgram::InstructionAt(items, offset);
		std::vector<TraceRestrictItem>::iterator remove_end = InstructionIteratorNext(remove_start);

		items.erase(remove_start, remove_end);
	}
	return CommandCost();
}

static CommandCost AdvanceItemEndIteratorForBlock(const std::vector<TraceRestrictItem> &items,
		const std::vector<TraceRestrictItem>::iterator &move_start, std::vector<TraceRestrictItem>::iterator &move_end, bool allow_elif)
{
	TraceRestrictItem old_item = *move_start;
	if (IsTraceRestrictConditional(old_item)) {
		if (GetTraceRestrictType(old_item) == TRIT_COND_ENDIF) {
			// this is an else or end if, can't move these
			return CMD_ERROR;
		}
		if (GetTraceRestrictCondFlags(old_item) != 0) {
			if (allow_elif) {
				uint32_t recursion_depth = 0;
				for (; move_end != items.end(); InstructionIteratorAdvance(move_end)) {
					TraceRestrictItem current_item = *move_end;
					if (IsTraceRestrictConditional(current_item)) {
						if (GetTraceRestrictCondFlags(current_item) == 0) {
							if (GetTraceRestrictType(current_item) == TRIT_COND_ENDIF) {
								// this is an end if
								if (recursion_depth == 0) break;
								recursion_depth--;
							} else {
								// this is an opening if
								recursion_depth++;
							}
						} else if (recursion_depth == 0) {
							// next elif/orif
							break;
						}
					}
				}
				return CommandCost();
			}
			// can't move or/else blocks
			return CMD_ERROR;
		}

		uint32_t recursion_depth = 1;
		// iterate until matching end block found
		for (; move_end != items.end(); InstructionIteratorAdvance(move_end)) {
			TraceRestrictItem current_item = *move_end;
			if (IsTraceRestrictConditional(current_item)) {
				if (GetTraceRestrictCondFlags(current_item) == 0) {
					if (GetTraceRestrictType(current_item) == TRIT_COND_ENDIF) {
						// this is an end if
						recursion_depth--;
						if (recursion_depth == 0) {
							// inclusively remove up to here
							InstructionIteratorAdvance(move_end);
							break;
						}
					} else {
						// this is an opening if
						recursion_depth++;
					}
				}
			}
		}
		if (recursion_depth != 0) return CMD_ERROR; // ran off the end
	}
	return CommandCost();
}

CommandCost TraceRestrictProgramMoveItemAt(std::vector<TraceRestrictItem> &items, uint32_t &offset, bool up, bool shallow_mode)
{
	std::vector<TraceRestrictItem>::iterator move_start = TraceRestrictProgram::InstructionAt(items, offset);
	std::vector<TraceRestrictItem>::iterator move_end = InstructionIteratorNext(move_start);

	if (!shallow_mode) {
		CommandCost res = AdvanceItemEndIteratorForBlock(items, move_start, move_end, false);
		if (res.Failed()) return CommandCost(STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
	}

	if (up) {
		if (move_start == items.begin()) return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
		std::rotate(TraceRestrictProgram::InstructionAt(items, offset - 1), move_start, move_end);
		offset--;
	} else {
		if (move_end == items.end()) return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
		std::rotate(move_start, move_end, InstructionIteratorNext(move_end));
		offset++;
	}
	return CommandCost();
}

CommandCost TraceRestrictProgramDuplicateItemAt(std::vector<TraceRestrictItem> &items, uint32_t offset)
{
	std::vector<TraceRestrictItem>::iterator dup_start = TraceRestrictProgram::InstructionAt(items, offset);
	std::vector<TraceRestrictItem>::iterator dup_end = InstructionIteratorNext(dup_start);

	CommandCost res = AdvanceItemEndIteratorForBlock(items, dup_start, dup_end, true);
	if (res.Failed()) return CommandCost(STR_TRACE_RESTRICT_ERROR_CAN_T_DUPLICATE_ITEM);

	std::vector<TraceRestrictItem> new_items;
	new_items.reserve(items.size() + (dup_end - dup_start));
	new_items.insert(new_items.end(), items.begin(), dup_end);
	new_items.insert(new_items.end(), dup_start, dup_end);
	new_items.insert(new_items.end(), dup_end, items.end());
	items = std::move(new_items);
	return CommandCost();
}

bool TraceRestrictProgramDuplicateItemAtDryRun(const std::vector<TraceRestrictItem> &items, uint32_t offset)
{
	std::vector<TraceRestrictItem>::iterator dup_start = TraceRestrictProgram::InstructionAt(const_cast<std::vector<TraceRestrictItem> &>(items), offset);
	std::vector<TraceRestrictItem>::iterator dup_end = InstructionIteratorNext(dup_start);

	CommandCost res = AdvanceItemEndIteratorForBlock(items, dup_start, dup_end, true);
	return res.Succeeded();
}

/**
 * The main command for editing a signal tracerestrict program.
 * @param tile The tile which contains the signal.
 * @param flags Internal command handler stuff.
 * Below apply for instruction modification actions only
 * @param p1 Bitstuffed items
 * @param p2 Item, for insert and modify operations. Flags for instruction move operations
 * @return the cost of this operation (which is free), or an error
 */
CommandCost CmdProgramSignalTraceRestrict(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictDoCommandType type = static_cast<TraceRestrictDoCommandType>(GB(p1, 3, 5));

	if (type >= TRDCT_PROG_COPY) {
		return CmdProgramSignalTraceRestrictProgMgmt(tile, flags, p1, p2, text);
	}

	Track track = static_cast<Track>(GB(p1, 0, 3));
	uint32_t offset = GB(p1, 8, 16);
	TraceRestrictItem item = static_cast<TraceRestrictItem>(p2);

	CommandCost ret = TraceRestrictCheckTileIsUsable(tile, track);
	if (ret.Failed()) {
		return ret;
	}

	bool can_make_new = (type == TRDCT_INSERT_ITEM) && (flags & DC_EXEC);
	bool need_existing = (type != TRDCT_INSERT_ITEM);
	TraceRestrictProgram *prog = GetTraceRestrictProgram(MakeTraceRestrictRefId(tile, track), can_make_new);
	if (need_existing && !prog) {
		return_cmd_error(STR_TRACE_RESTRICT_ERROR_NO_PROGRAM);
	}

	uint32_t offset_limit_exclusive = ((type == TRDCT_INSERT_ITEM) ? 1 : 0);
	if (prog) offset_limit_exclusive += (uint)prog->items.size();

	if (offset >= offset_limit_exclusive) {
		return_cmd_error(STR_TRACE_RESTRICT_ERROR_OFFSET_TOO_LARGE);
	}

	// copy program
	std::vector<TraceRestrictItem> items;
	if (prog) items = prog->items;

	switch (type) {
		case TRDCT_INSERT_ITEM:
			items.insert(TraceRestrictProgram::InstructionAt(items, offset), item);
			if (IsTraceRestrictConditional(item) &&
					GetTraceRestrictCondFlags(item) == 0 &&
					GetTraceRestrictType(item) != TRIT_COND_ENDIF) {
				// this is an opening if block, insert a corresponding end if
				TraceRestrictItem endif_item = 0;
				SetTraceRestrictType(endif_item, TRIT_COND_ENDIF);
				items.insert(TraceRestrictProgram::InstructionAt(items, offset) + 1, endif_item);
			} else if (IsTraceRestrictDoubleItem(item)) {
				items.insert(TraceRestrictProgram::InstructionAt(items, offset) + 1, GetDualInstructionInitialValue(item));
			}
			break;

		case TRDCT_MODIFY_ITEM: {
			std::vector<TraceRestrictItem>::iterator old_item = TraceRestrictProgram::InstructionAt(items, offset);
			if (IsTraceRestrictConditional(*old_item) != IsTraceRestrictConditional(item)) {
				return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_CHANGE_CONDITIONALITY);
			}
			const TraceRestrictItem old_item_value = *old_item;
			bool old_is_dual = IsTraceRestrictDoubleItem(old_item_value);
			bool new_is_dual = IsTraceRestrictDoubleItem(item);
			*old_item = item;
			if (old_is_dual && !new_is_dual) {
				items.erase(old_item + 1);
			} else if (!old_is_dual && new_is_dual) {
				items.insert(old_item + 1, GetDualInstructionInitialValue(item));
			} else if (old_is_dual && new_is_dual && GetTraceRestrictType(old_item_value) != GetTraceRestrictType(item)) {
				*(old_item + 1) = GetDualInstructionInitialValue(item);
			}
			break;
		}

		case TRDCT_MODIFY_DUAL_ITEM: {
			std::vector<TraceRestrictItem>::iterator old_item = TraceRestrictProgram::InstructionAt(items, offset);
			if (!IsTraceRestrictDoubleItem(*old_item)) {
				return CMD_ERROR;
			}
			*(old_item + 1) = p2;
			break;
		}

		case TRDCT_REMOVE_ITEM:
		case TRDCT_SHALLOW_REMOVE_ITEM: {
			CommandCost res = TraceRestrictProgramRemoveItemAt(items, offset, type == TRDCT_SHALLOW_REMOVE_ITEM);
			if (res.Failed()) return res;
			break;
		}

		case TRDCT_MOVE_ITEM: {
			CommandCost res = TraceRestrictProgramMoveItemAt(items, offset, p2 & 1, p2 & 2);
			if (res.Failed()) return res;
			break;
		}

		case TRDCT_DUPLICATE_ITEM: {
			CommandCost res = TraceRestrictProgramDuplicateItemAt(items, offset);
			if (res.Failed()) return res;
			break;
		}

		default:
			return CMD_ERROR;
	}

	TraceRestrictProgramActionsUsedFlags actions_used_flags;
	CommandCost validation_result = TraceRestrictProgram::Validate(items, actions_used_flags);
	if (validation_result.Failed()) {
		return validation_result;
	}

	if (flags & DC_EXEC) {
		assert(prog);

		size_t old_size = prog->items.size();
		TraceRestrictProgramActionsUsedFlags old_actions_used_flags = prog->actions_used_flags;

		// move in modified program
		prog->items.swap(items);
		prog->actions_used_flags = actions_used_flags;

		if (prog->items.size() == 0 && prog->refcount == 1) {
			// program is empty, and this tile is the only reference to it
			// so delete it, as it's redundant
			TraceRestrictCheckRefreshSingleSignal(prog, MakeTraceRestrictRefId(tile, track), old_actions_used_flags);
			TraceRestrictRemoveProgramMapping(MakeTraceRestrictRefId(tile, track));
		} else {
			TraceRestrictCheckRefreshSignals(prog, old_size, old_actions_used_flags);
		}

		// update windows
		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	}

	return CommandCost();
}

/**
 * Helper function to perform parameter bit-packing and call DoCommandP, for program management actions
 */
void TraceRestrictProgMgmtWithSourceDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type,
		TileIndex source_tile, Track source_track, StringID error_msg)
{
	uint32_t p1 = 0;
	SB(p1, 0, 3, track);
	SB(p1, 3, 5, type);
	SB(p1, 8, 3, source_track);
	DoCommandP(tile, p1, source_tile, CMD_PROGRAM_TRACERESTRICT_SIGNAL | CMD_MSG(error_msg));
}

/**
 * Sub command for copy/share/unshare operations on signal tracerestrict programs.
 * @param tile The tile which contains the signal.
 * @param flags Internal command handler stuff.
 * @param p1 Bitstuffed items
 * @param p2 Source tile, for share/copy operations
 * @return the cost of this operation (which is free), or an error
 */
CommandCost CmdProgramSignalTraceRestrictProgMgmt(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictDoCommandType type = static_cast<TraceRestrictDoCommandType>(GB(p1, 3, 5));
	Track track = static_cast<Track>(GB(p1, 0, 3));
	Track source_track = static_cast<Track>(GB(p1, 8, 3));
	TileIndex source_tile = static_cast<TileIndex>(p2);

	TraceRestrictRefId self = MakeTraceRestrictRefId(tile, track);
	TraceRestrictRefId source = MakeTraceRestrictRefId(source_tile, source_track);

	assert(type >= TRDCT_PROG_COPY);

	CommandCost ret = TraceRestrictCheckTileIsUsable(tile, track);
	if (ret.Failed()) {
		return ret;
	}

	if (type == TRDCT_PROG_SHARE || type == TRDCT_PROG_SHARE_IF_UNMAPPED || type == TRDCT_PROG_COPY) {
		if (self == source) {
			return_cmd_error(STR_TRACE_RESTRICT_ERROR_SOURCE_SAME_AS_TARGET);
		}
	}
	if (type == TRDCT_PROG_SHARE || type == TRDCT_PROG_SHARE_IF_UNMAPPED || type == TRDCT_PROG_COPY || type == TRDCT_PROG_COPY_APPEND) {
		ret = TraceRestrictCheckTileIsUsable(source_tile, source_track);
		if (ret.Failed()) {
			return ret;
		}
	}

	if (type == TRDCT_PROG_SHARE_IF_UNMAPPED && GetTraceRestrictProgram(self, false) != nullptr) {
		return_cmd_error(STR_TRACE_RESTRICT_ERROR_TARGET_ALREADY_HAS_PROGRAM);
	}

	if (type != TRDCT_PROG_RESET && !TraceRestrictProgram::CanAllocateItem()) {
		return CMD_ERROR;
	}

	if (!(flags & DC_EXEC)) {
		return CommandCost();
	}

	switch (type) {
		case TRDCT_PROG_COPY: {
			TraceRestrictRemoveProgramMapping(self);

			TraceRestrictProgram *source_prog = GetTraceRestrictProgram(source, false);
			if (source_prog && !source_prog->items.empty()) {
				TraceRestrictProgram *prog = GetTraceRestrictProgram(self, true);
				if (!prog) {
					// allocation failed
					return CMD_ERROR;
				}
				prog->items = source_prog->items; // copy
				prog->Validate();

				TraceRestrictCheckRefreshSignals(prog, 0, TRPAUF_NONE);
			}
			break;
		}

		case TRDCT_PROG_COPY_APPEND: {
			TraceRestrictProgram *source_prog = GetTraceRestrictProgram(source, false);
			if (source_prog && !source_prog->items.empty()) {
				TraceRestrictProgram *prog = GetTraceRestrictProgram(self, true);
				if (!prog) {
					// allocation failed
					return CMD_ERROR;
				}

				size_t old_size = prog->items.size();
				TraceRestrictProgramActionsUsedFlags old_actions_used_flags = prog->actions_used_flags;

				prog->items.reserve(prog->items.size() + source_prog->items.size()); // this is in case prog == source_prog
				prog->items.insert(prog->items.end(), source_prog->items.begin(), source_prog->items.end()); // append
				prog->Validate();

				TraceRestrictCheckRefreshSignals(prog, old_size, old_actions_used_flags);
			}
			break;
		}

		case TRDCT_PROG_SHARE:
		case TRDCT_PROG_SHARE_IF_UNMAPPED: {
			TraceRestrictRemoveProgramMapping(self);
			TraceRestrictProgram *source_prog = GetTraceRestrictProgram(source, true);
			if (!source_prog) {
				// allocation failed
				return CMD_ERROR;
			}

			TraceRestrictCreateProgramMapping(self, source_prog);
			TraceRestrictCheckRefreshSingleSignal(source_prog, self, TRPAUF_NONE);
			break;
		}

		case TRDCT_PROG_UNSHARE: {
			std::vector<TraceRestrictItem> items;
			TraceRestrictProgram *prog = GetTraceRestrictProgram(self, false);
			if (prog) {
				// copy program into temporary
				items = prog->items;
			}
			// remove old program
			TraceRestrictRemoveProgramMapping(self);

			if (items.size()) {
				// if prog is non-empty, create new program and move temporary in
				TraceRestrictProgram *new_prog = GetTraceRestrictProgram(self, true);
				if (!new_prog) {
					// allocation failed
					return CMD_ERROR;
				}

				new_prog->items.swap(items);
				new_prog->Validate();
				TraceRestrictCheckRefreshSingleSignal(new_prog, self, TRPAUF_NONE);
			}
			break;
		}

		case TRDCT_PROG_RESET: {
			TraceRestrictRemoveProgramMapping(self);
			break;
		}

		default:
			return CMD_ERROR;
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);

	return CommandCost();
}

int GetTraceRestrictTimeDateValue(TraceRestrictTimeDateValueField type)
{
	const TickMinutes now = _settings_game.game_time.NowInTickMinutes();

	switch (type) {
		case TRTDVF_MINUTE:
			return now.ClockMinute();

		case TRTDVF_HOUR:
			return now.ClockHour();

		case TRTDVF_HOUR_MINUTE:
			return now.ClockHHMM();

		case TRTDVF_DAY:
			return _cur_date_ymd.day;

		case TRTDVF_MONTH:
			return _cur_date_ymd.month + 1;

		default:
			return 0;
	}
}

int GetTraceRestrictTimeDateValueFromDate(TraceRestrictTimeDateValueField type, DateTicksScaled scaled_date_ticks)
{
	const TickMinutes minutes = _settings_game.game_time.ToTickMinutes(scaled_date_ticks);

	switch (type) {
		case TRTDVF_MINUTE:
			return minutes.ClockMinute();

		case TRTDVF_HOUR:
			return minutes.ClockHour();

		case TRTDVF_HOUR_MINUTE:
			return minutes.ClockHHMM();

		case TRTDVF_DAY: {
			YearMonthDay ymd = ConvertDateToYMD(ScaledDateTicksToDate(scaled_date_ticks));
			return ymd.day;
		}

		case TRTDVF_MONTH: {
			YearMonthDay ymd = ConvertDateToYMD(ScaledDateTicksToDate(scaled_date_ticks));
			return ymd.month + 1;
		}

		default:
			return 0;
	}
}

/**
 * This is called when a station, waypoint or depot is about to be deleted
 * Scan program pool and change any references to it to the invalid station ID, to avoid dangling references
 */
void TraceRestrictRemoveDestinationID(TraceRestrictOrderCondAuxField type, uint16_t index)
{
	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		for (size_t i = 0; i < prog->items.size(); i++) {
			TraceRestrictItem &item = prog->items[i]; // note this is a reference,
			if (GetTraceRestrictType(item) == TRIT_COND_CURRENT_ORDER ||
					GetTraceRestrictType(item) == TRIT_COND_NEXT_ORDER ||
					GetTraceRestrictType(item) == TRIT_COND_LAST_STATION) {
				if (GetTraceRestrictAuxField(item) == type && GetTraceRestrictValue(item) == index) {
					SetTraceRestrictValueDefault(item, TRVT_ORDER); // this updates the instruction in-place
				}
			}
			if (IsTraceRestrictDoubleItem(item)) i++;
		}
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
}

/**
 * This is called when a group is about to be deleted
 * Scan program pool and change any references to it to the invalid group ID, to avoid dangling references
 */
void TraceRestrictRemoveGroupID(GroupID index)
{
	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		for (size_t i = 0; i < prog->items.size(); i++) {
			TraceRestrictItem &item = prog->items[i]; // note this is a reference,
			if (GetTraceRestrictType(item) == TRIT_COND_TRAIN_GROUP && GetTraceRestrictValue(item) == index) {
				SetTraceRestrictValueDefault(item, TRVT_GROUP_INDEX); // this updates the instruction in-place
			}
			if (IsTraceRestrictDoubleItem(item)) i++;
		}
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
}

/**
 * This is called when a company is about to be deleted or taken over
 * Scan program pool and change any references to it to the new company ID, to avoid dangling references
 * Change owner and/or delete slots
 */
void TraceRestrictUpdateCompanyID(CompanyID old_company, CompanyID new_company)
{
	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		for (size_t i = 0; i < prog->items.size(); i++) {
			TraceRestrictItem &item = prog->items[i]; // note this is a reference,
			if (GetTraceRestrictType(item) == TRIT_COND_TRAIN_OWNER) {
				if (GetTraceRestrictValue(item) == old_company) {
					SetTraceRestrictValue(item, new_company); // this updates the instruction in-place
				}
			}
			if (IsTraceRestrictDoubleItem(item)) i++;
		}
	}

	for (TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		if (slot->owner != old_company) continue;
		if (new_company == INVALID_OWNER) {
			TraceRestrictRemoveSlotID(slot->index);
			delete slot;
		} else {
			slot->owner = new_company;
		}
	}

	for (TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
		if (ctr->owner != old_company) continue;
		if (new_company == INVALID_OWNER) {
			TraceRestrictRemoveCounterID(ctr->index);
			delete ctr;
		} else {
			ctr->owner = new_company;
		}
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_COUNTERS);
}

static btree::btree_multimap<VehicleID, TraceRestrictSlotID> slot_vehicle_index;

/**
 * Add vehicle to occupants if possible and not already an occupant
 * @param v Vehicle
 * @param force Add the vehicle even if the slot is at/over capacity
 * @return whether vehicle is now an occupant
 */
bool TraceRestrictSlot::Occupy(const Vehicle *v, bool force)
{
	if (this->IsOccupant(v->index)) return true;
	if (this->occupants.size() >= this->max_occupancy && !force) return false;
	this->occupants.push_back(v->index);
	this->AddIndex(v);
	this->UpdateSignals();
	return true;
}

/**
 * Dry-run adding vehicle ID to occupants if possible and not already an occupant
 * @param id Vehicle ID
 * @return whether vehicle ID would be an occupant
 */
bool TraceRestrictSlot::OccupyDryRun(VehicleID id)
{
	if (this->IsOccupant(id)) return true;
	if (this->occupants.size() >= this->max_occupancy) return false;
	return true;
}

/**
 * Add vehicle ID to occupants if possible and not already an occupant, record any changes in the temporary state to be reverted later
 * @param id Vehicle ID
 * @param state Temporary state
 * @return whether vehicle ID is now an occupant
 */
bool TraceRestrictSlot::OccupyUsingTemporaryState(VehicleID id, TraceRestrictSlotTemporaryState *state)
{
	if (this->IsOccupant(id)) return true;
	if (this->occupants.size() >= this->max_occupancy) return false;

	this->occupants.push_back(id);

	if (find_index(state->veh_temporarily_removed, this->index) < 0) {
		include(state->veh_temporarily_added, this->index);
	}

	return true;
}

/**
 * Remove vehicle from occupants
 * @param v Vehicle
 */
void TraceRestrictSlot::Vacate(const Vehicle *v)
{
	if (container_unordered_remove(this->occupants, v->index)) {
		this->DeIndex(v->index, v);
		this->UpdateSignals();
	}
}

/**
 * Remove vehicle ID from occupants, record any changes in the temporary state to be reverted later
 * @param id Vehicle ID
 * @param state Temporary state
 */
void TraceRestrictSlot::VacateUsingTemporaryState(VehicleID id, TraceRestrictSlotTemporaryState *state)
{
	if (container_unordered_remove(this->occupants, id)) {
		if (find_index(state->veh_temporarily_added, this->index) < 0) {
			include(state->veh_temporarily_removed, this->index);
		}
	}
}

/** Remove all occupants */
void TraceRestrictSlot::Clear()
{
	for (VehicleID id : this->occupants) {
		this->DeIndex(id, nullptr);
	}
	this->occupants.clear();
}

void TraceRestrictSlot::UpdateSignals() {
	for (SignalReference sr : this->progsig_dependants) {
		AddTrackToSignalBuffer(sr.tile, sr.track, GetTileOwner(sr.tile));
		UpdateSignalsInBuffer();
	}
}

/**
 * Add vehicle to vehicle slot index
 * @param v Vehicle pointer
 */
void TraceRestrictSlot::AddIndex(const Vehicle *v)
{
	slot_vehicle_index.insert({ v->index, this->index });
	SetBit(const_cast<Vehicle *>(v)->vehicle_flags, VF_HAVE_SLOT);
	SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);

}

/**
 * Remove vehicle from vehicle slot index
 * @param id Vehicle ID
 * @param v Vehicle pointer (optional) or nullptr
 */
void TraceRestrictSlot::DeIndex(VehicleID id, const Vehicle *v)
{
	auto start = slot_vehicle_index.lower_bound(id);
	for (auto it = start; it != slot_vehicle_index.end() && it->first == id; ++it) {
		if (it->second == this->index) {
			bool is_first_in_range = (it == start);

			auto next = slot_vehicle_index.erase(it);

			if (is_first_in_range && (next == slot_vehicle_index.end() || next->first != id)) {
				/* Only one item, which we've just erased, clear the vehicle flag */
				if (v == nullptr) v = Vehicle::Get(id);
				ClrBit(const_cast<Vehicle *>(v)->vehicle_flags, VF_HAVE_SLOT);
			}
			break;
		}
	}
	SetWindowDirty(WC_VEHICLE_DETAILS, id);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
}

/** Rebuild slot vehicle index after loading */
void TraceRestrictSlot::RebuildVehicleIndex()
{
	slot_vehicle_index.clear();
	for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		for (VehicleID id : slot->occupants) {
			slot_vehicle_index.insert({ id, slot->index });
		}
	}
}

bool TraceRestrictSlot::ValidateVehicleIndex()
{
	btree::btree_multimap<VehicleID, TraceRestrictSlotID> saved_slot_vehicle_index = std::move(slot_vehicle_index);
	RebuildVehicleIndex();
	bool ok = multimaps_equalivalent(saved_slot_vehicle_index, slot_vehicle_index);
	slot_vehicle_index = std::move(saved_slot_vehicle_index);
	return ok;
}

void TraceRestrictSlot::ValidateSlotOccupants(std::function<void(const char *)> log)
{
	char cclog_buffer[1024];
#define CCLOG(...) { \
	seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	DEBUG(desync, 0, "%s", cclog_buffer); \
	if (log) log(cclog_buffer); \
}

	for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		for (VehicleID id : slot->occupants) {
			const Vehicle *v = Vehicle::GetIfValid(id);
			if (v) {
				if (v->type != slot->vehicle_type) CCLOG("Slot %u (%s) has wrong vehicle type (%u, %u): %s", slot->index, slot->name.c_str(), v->type, slot->vehicle_type, scope_dumper().VehicleInfo(v));
				if (!v->IsPrimaryVehicle()) CCLOG("Slot %u (%s) has non-primary vehicle: %s", slot->index, slot->name.c_str(), scope_dumper().VehicleInfo(v));
				if (!HasBit(v->vehicle_flags, VF_HAVE_SLOT)) CCLOG("Slot %u (%s) has vehicle without VF_HAVE_SLOT: %s", slot->index, slot->name.c_str(), scope_dumper().VehicleInfo(v));
			} else {
				CCLOG("Slot %u (%s) has non-existent vehicle ID: %u", slot->index, slot->name.c_str(), id);
			}
		}
	}
#undef CCLOG
}

/** Slot pool is about to be cleared */
void TraceRestrictSlot::PreCleanPool()
{
	slot_vehicle_index.clear();
}

/** Revert any temporary changes */
void TraceRestrictSlotTemporaryState::RevertTemporaryChanges(VehicleID veh)
{
	for (TraceRestrictSlotID id : this->veh_temporarily_added) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(id);
		container_unordered_remove(slot->occupants, veh);
	}
	for (TraceRestrictSlotID id : this->veh_temporarily_removed) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(id);
		include(slot->occupants, veh);
	}
	this->veh_temporarily_added.clear();
	this->veh_temporarily_removed.clear();
}

/** Apply any temporary changes */
void TraceRestrictSlotTemporaryState::ApplyTemporaryChanges(const Vehicle *v)
{
	VehicleID veh = v->index;
	for (TraceRestrictSlotID id : this->veh_temporarily_added) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(id);
		if (slot->IsOccupant(veh)) {
			slot->AddIndex(v);
			slot->UpdateSignals();
		}
	}
	for (TraceRestrictSlotID id : this->veh_temporarily_removed) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(id);
		if (!slot->IsOccupant(veh)) {
			slot->DeIndex(v->index, v);
			slot->UpdateSignals();
		}
	}

	this->veh_temporarily_added.clear();
	this->veh_temporarily_removed.clear();
}

/** Remove vehicle ID from all slot occupants */
void TraceRestrictRemoveVehicleFromAllSlots(VehicleID vehicle_id)
{
	const auto start = slot_vehicle_index.lower_bound(vehicle_id);
	auto it = start;
	for (; it != slot_vehicle_index.end() && it->first == vehicle_id; ++it) {
		auto slot = TraceRestrictSlot::Get(it->second);
		container_unordered_remove(slot->occupants, vehicle_id);
		slot->UpdateSignals();
	}

	const bool anything_to_erase = (start != it);

	slot_vehicle_index.erase(start, it);

	if (anything_to_erase) InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
}

/** Replace all instance of a vehicle ID with another, in all slot occupants */
void TraceRestrictTransferVehicleOccupantInAllSlots(VehicleID from, VehicleID to)
{
	std::vector<TraceRestrictSlotID> slots;
	const auto start = slot_vehicle_index.lower_bound(from);
	auto it = start;
	for (; it != slot_vehicle_index.end() && it->first == from; ++it) {
		slots.push_back(it->second);
	}
	slot_vehicle_index.erase(start, it);
	for (TraceRestrictSlotID slot_id : slots) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(slot_id);
		for (VehicleID &id : slot->occupants) {
			if (id == from) {
				id = to;
				slot_vehicle_index.insert({ to, slot_id });
			}
		}
	}
	if (!slots.empty()) InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
}

/** Get list of slots occupied by a vehicle ID */
void TraceRestrictGetVehicleSlots(VehicleID id, std::vector<TraceRestrictSlotID> &out)
{
	for (auto it = slot_vehicle_index.lower_bound(id); it != slot_vehicle_index.end() && it->first == id; ++it) {
		out.push_back(it->second);
	}
}

/**
 * This is called when a slot is about to be deleted
 * Scan program pool and change any references to it to the invalid group ID, to avoid dangling references
 * Scan order list and change any references to it to the invalid group ID, to avoid dangling slot condition references
 */
void TraceRestrictRemoveSlotID(TraceRestrictSlotID index)
{
	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		for (size_t i = 0; i < prog->items.size(); i++) {
			TraceRestrictItem &item = prog->items[i]; // note this is a reference,
			if ((GetTraceRestrictType(item) == TRIT_SLOT || GetTraceRestrictType(item) == TRIT_COND_TRAIN_IN_SLOT) && GetTraceRestrictValue(item) == index) {
				SetTraceRestrictValueDefault(item, TRVT_SLOT_INDEX); // this updates the instruction in-place
			}
			if ((GetTraceRestrictType(item) == TRIT_COND_SLOT_OCCUPANCY) && GetTraceRestrictValue(item) == index) {
				SetTraceRestrictValueDefault(item, TRVT_SLOT_INDEX_INT); // this updates the instruction in-place
			}
			if (IsTraceRestrictDoubleItem(item)) i++;
		}
	}

	bool changed_order = false;
	for (Order *o : Order::Iterate()) {
		if (o->IsType(OT_CONDITIONAL) &&
				(o->GetConditionVariable() == OCV_SLOT_OCCUPANCY || o->GetConditionVariable() == OCV_VEH_IN_SLOT) &&
				o->GetXData() == index) {
			o->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_ID;
			changed_order = true;
		}
		if (o->IsType(OT_SLOT) && o->GetDestination() == index) {
			o->SetDestination(INVALID_TRACE_RESTRICT_SLOT_ID);
			changed_order = true;
		}
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	if (changed_order) {
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
		InvalidateWindowClassesData(WC_VEHICLE_TIMETABLE);
	}

	for (SignalReference sr : TraceRestrictSlot::Get(index)->progsig_dependants) {
		if (IsProgrammableSignal(GetSignalType(sr.tile, sr.track))) {
			extern void RemoveProgramSlotDependencies(TraceRestrictSlotID slot_being_removed, SignalReference signal_to_update);
			RemoveProgramSlotDependencies(index, sr);
		}
	}
}

static bool IsUniqueSlotName(const char *name)
{
	for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		if (slot->name == name) return false;
	}
	return true;
}

/**
 * Create a new slot.
 * @param tile unused
 * @param flags type of operation
 * @param p1 bitstuffed elements
 * - p2 = (bit 0 - 2) - vehicle type
 * @param p2   unused
 * @param text new slot name
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (!TraceRestrictSlot::CanAllocateItem()) return CMD_ERROR;
	if (StrEmpty(text)) return CMD_ERROR;

	VehicleType vehtype = Extract<VehicleType, 0, 3>(p1);
	if (vehtype >= VEH_COMPANY_END) return CMD_ERROR;

	size_t length = Utf8StringLength(text);
	if (length <= 0) return CMD_ERROR;
	if (length >= MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS) return CMD_ERROR;
	if (!IsUniqueSlotName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);

	if (flags & DC_EXEC) {
		TraceRestrictSlot *slot = new TraceRestrictSlot(_current_company, vehtype);
		slot->name = text;

		// update windows
		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
	}

	return CommandCost();
}


/**
 * Deletes a slot.
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *      - p1 bit 0-15 : Slot ID
 * @param p2   unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(p1);
	if (slot == nullptr || slot->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* notify tracerestrict that group is about to be deleted */
		TraceRestrictRemoveSlotID(slot->index);

		delete slot;

		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
	}

	return CommandCost();
}

/**
 * Alter a slot
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 *   - p1 bit 16: 0 - Rename group
 *                1 - Change max occupancy
 * @param p2   new max occupancy
 * @param text the new name
 * @return the cost of this operation or an error
 */
CommandCost CmdAlterTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GB(p1, 0, 16));
	if (slot == nullptr || slot->owner != _current_company) return CMD_ERROR;

	if (!HasBit(p1, 16)) {
		/* Rename slot */

		if (StrEmpty(text)) return CMD_ERROR;
		size_t length = Utf8StringLength(text);
		if (length <= 0) return CMD_ERROR;
		if (length >= MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueSlotName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);

		if (flags & DC_EXEC) {
			slot->name = text;
		}
	} else {
		/* Change max occupancy */

		if (flags & DC_EXEC) {
			slot->max_occupancy = p2;
			slot->UpdateSignals();
		}
	}

	if (flags & DC_EXEC) {
		// update windows
		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
		InvalidateWindowClassesData(WC_SIGNAL_PROGRAM);
	}

	return CommandCost();
}

/**
 * Add a vehicle to a slot
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 * @param p2   index of vehicle
 *   - p2 bit 0-19 : VehicleID
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdAddVehicleTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(p1);
	Vehicle *v = Vehicle::GetIfValid(p2);
	if (slot == nullptr || slot->owner != _current_company) return CMD_ERROR;
	if (v == nullptr || v->owner != _current_company) return CMD_ERROR;
	if (v->type != slot->vehicle_type || !v->IsPrimaryVehicle()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		slot->Occupy(v, true);
	}

	return CommandCost();
}

/**
 * Remove a vehicle from a slot
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *   - p1 bit 0-15 : GroupID
 * @param p2   index of vehicle
 *   - p2 bit 0-19 : VehicleID
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveVehicleTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(p1);
	Vehicle *v = Vehicle::GetIfValid(p2);
	if (slot == nullptr || slot->owner != _current_company) return CMD_ERROR;
	if (v == nullptr) return CMD_ERROR; // permit removing vehicles of other owners from your own slot

	if (flags & DC_EXEC) {
		slot->Vacate(v);
	}

	return CommandCost();
}

void TraceRestrictCounter::UpdateValue(int32_t new_value)
{
	new_value = std::max<int32_t>(0, new_value);
	if (new_value != this->value) {
		this->value = new_value;
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_COUNTERS);
		for (SignalReference sr : this->progsig_dependants) {
			AddTrackToSignalBuffer(sr.tile, sr.track, GetTileOwner(sr.tile));
			UpdateSignalsInBuffer();
		}
	}
}

int32_t TraceRestrictCounter::ApplyValue(int32_t current, TraceRestrictCounterCondOpField op, int32_t value)
{
	switch (op) {
		case TRCCOF_INCREASE:
			return std::max<int32_t>(0, current + value);

		case TRCCOF_DECREASE:
			return std::max<int32_t>(0, current - value);

		case TRCCOF_SET:
			return std::max<int32_t>(0, value);

		default:
			NOT_REACHED();
			break;
	}
}

static bool IsUniqueCounterName(const char *name)
{
	for (const TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
		if (ctr->name == name) return false;
	}
	return true;
}

/**
 * This is called when a counter is about to be deleted
 * Scan program pool and change any references to it to the invalid counter ID, to avoid dangling references
 */
void TraceRestrictRemoveCounterID(TraceRestrictCounterID index)
{
	for (TraceRestrictProgram *prog : TraceRestrictProgram::Iterate()) {
		for (size_t i = 0; i < prog->items.size(); i++) {
			TraceRestrictItem &item = prog->items[i]; // note this is a reference,
			if ((GetTraceRestrictType(item) == TRIT_COUNTER || GetTraceRestrictType(item) == TRIT_COND_COUNTER_VALUE) && GetTraceRestrictValue(item) == index) {
				SetTraceRestrictValueDefault(item, TRVT_COUNTER_INDEX_INT); // this updates the instruction in-place
			}
			if (IsTraceRestrictDoubleItem(item)) i++;
		}
	}

	bool changed_order = false;
	for (Order *o : Order::Iterate()) {
		if (o->IsType(OT_CONDITIONAL) &&
				(o->GetConditionVariable() == OCV_COUNTER_VALUE) &&
				GB(o->GetXData(), 16, 16) == index) {
			SB(o->GetXDataRef(), 16, 16, INVALID_TRACE_RESTRICT_COUNTER_ID);
			changed_order = true;
		}
		if (o->IsType(OT_COUNTER) && o->GetDestination() == index) {
			o->SetDestination(INVALID_TRACE_RESTRICT_COUNTER_ID);
			changed_order = true;
		}
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	if (changed_order) {
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
		InvalidateWindowClassesData(WC_VEHICLE_TIMETABLE);
	}

	for (SignalReference sr : TraceRestrictCounter::Get(index)->progsig_dependants) {
		if (IsProgrammableSignal(GetSignalType(sr.tile, sr.track))) {
			extern void RemoveProgramCounterDependencies(TraceRestrictCounterID ctr_being_removed, SignalReference signal_to_update);
			RemoveProgramCounterDependencies(index, sr);
		}
	}
}

/**
 * Create a new counter.
 * @param tile unused
 * @param flags type of operation
 * @param p1   unused
 * @param p2   unused
 * @param text new counter name
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateTraceRestrictCounter(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (!TraceRestrictCounter::CanAllocateItem()) return CMD_ERROR;
	if (StrEmpty(text)) return CMD_ERROR;

	size_t length = Utf8StringLength(text);
	if (length <= 0) return CMD_ERROR;
	if (length >= MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS) return CMD_ERROR;
	if (!IsUniqueCounterName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);

	if (flags & DC_EXEC) {
		TraceRestrictCounter *ctr = new TraceRestrictCounter(_current_company);
		ctr->name = text;

		// update windows
		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_COUNTERS);
	}

	return CommandCost();
}


/**
 * Deletes a counter.
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array group
 *      - p1 bit 0-15 : Counter ID
 * @param p2   unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteTraceRestrictCounter(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(p1);
	if (ctr == nullptr || ctr->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* notify tracerestrict that counter is about to be deleted */
		TraceRestrictRemoveCounterID(ctr->index);

		delete ctr;

		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_COUNTERS);
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
	}

	return CommandCost();
}

/**
 * Alter a counter
 * @param tile unused
 * @param flags type of operation
 * @param p1   index of array counter
 *   - p1 bit 0-15 : Counter ID
 *   - p1 bit 16: 0 - Rename counter
 *                1 - Change value
 * @param p2   new value
 * @param text the new name
 * @return the cost of this operation or an error
 */
CommandCost CmdAlterTraceRestrictCounter(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(GB(p1, 0, 16));
	if (ctr == nullptr || ctr->owner != _current_company) return CMD_ERROR;

	if (!HasBit(p1, 16)) {
		/* Rename counter */

		if (StrEmpty(text)) return CMD_ERROR;
		size_t length = Utf8StringLength(text);
		if (length <= 0) return CMD_ERROR;
		if (length >= MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueCounterName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);

		if (flags & DC_EXEC) {
			ctr->name = text;
		}
	} else {
		/* Change value */

		if (flags & DC_EXEC) {
			ctr->UpdateValue(p2);
		}
	}

	if (flags & DC_EXEC) {
		// update windows
		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_COUNTERS);
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
		InvalidateWindowClassesData(WC_SIGNAL_PROGRAM);
	}

	return CommandCost();
}
