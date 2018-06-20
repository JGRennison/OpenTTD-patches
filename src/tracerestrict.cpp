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
const uint16 _tracerestrict_pathfinder_penalty_preset_values[] = {
	500,
	2000,
	8000,
};

assert_compile(lengthof(_tracerestrict_pathfinder_penalty_preset_values) == TRPPPI_END);

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
 * @p order may be NULL
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

			case OT_GOTO_DEPOT:
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
 * @p v may not be NULL
 * @p out should be zero-initialised
 */
void TraceRestrictProgram::Execute(const Train* v, const TraceRestrictProgramInput &input, TraceRestrictProgramResult& out) const
{
	// static to avoid needing to re-alloc/resize on each execution
	static std::vector<TraceRestrictCondStackFlags> condstack;
	condstack.clear();

	bool have_previous_signal = false;
	TileIndex previous_signal_tile = INVALID_TILE;

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
				uint16 condvalue = GetTraceRestrictValue(item);
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
						if (v->orders.list == NULL) break;
						if (v->orders.list->GetNumOrders() == 0) break;

						const Order *current_order = v->GetOrder(v->cur_real_order_index);
						for (const Order *order = v->orders.list->GetNext(current_order); order != current_order; order = v->orders.list->GetNext(order)) {
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
						for (const Vehicle *v_iter = v; v_iter != NULL; v_iter = v_iter->Next()) {
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
								direction_match = IsTileType(input.tile, MP_RAILWAY) && HasSignalOnTrackdir(input.tile, input.trackdir);
								break;

							case TRDTSV_BACK:
								direction_match = IsTileType(input.tile, MP_RAILWAY) && !HasSignalOnTrackdir(input.tile, input.trackdir);
								break;

							default:
								NOT_REACHED();
								break;
						}
						result = TestBinaryConditionCommon(item, direction_match);
						break;
					}

					case TRIT_COND_PBS_ENTRY_SIGNAL: {
						// TRVT_TILE_INDEX value type uses the next slot
						i++;
						uint32_t signal_tile = this->items[i];
						if (!have_previous_signal) {
							if (input.previous_signal_callback) {
								previous_signal_tile = input.previous_signal_callback(v, input.previous_signal_ptr);
							}
							have_previous_signal = true;
						}
						bool match = (signal_tile != INVALID_TILE)
								&& (previous_signal_tile == signal_tile);
						result = TestBinaryConditionCommon(item, match);
						break;
					}

					case TRIT_COND_TRAIN_GROUP: {
						result = TestBinaryConditionCommon(item, GroupIsInGroup(v->group_id, GetTraceRestrictValue(item)));
						break;
					}

					case TRIT_COND_TRAIN_IN_SLOT: {
						const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GetTraceRestrictValue(item));
						result = TestBinaryConditionCommon(item, slot != NULL && slot->IsOccupant(v->index));
						break;
					}

					case TRIT_COND_SLOT_OCCUPANCY: {
						// TRIT_COND_SLOT_OCCUPANCY value type uses the next slot
						i++;
						uint32_t value = this->items[i];
						const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GetTraceRestrictValue(item));
						switch (static_cast<TraceRestrictSlotOccupancyCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TRSOCAF_OCCUPANTS:
								result = TestCondition(slot != NULL ? slot->occupants.size() : 0, condop, value);
								break;

							case TRSOCAF_REMAINING:
								result = TestCondition(slot != NULL ? slot->max_occupancy - slot->occupants.size() : 0, condop, value);
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
								result = TestCondition(min<uint>(UINT16_MAX, (100 * v->gcache.cached_power) / max<uint>(1, v->gcache.cached_weight)), condop, condvalue);
								break;

							case TRPPRCAF_MAX_TE_WEIGHT:
								result = TestCondition(min<uint>(UINT16_MAX, (v->gcache.cached_max_te / 10) / max<uint>(1, v->gcache.cached_weight)), condop, condvalue);
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
								uint16 index = GetTraceRestrictValue(item);
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
						if (GetTraceRestrictValue(item)) {
							out.flags &= ~TRPRF_LONG_RESERVE;
						} else {
							out.flags |= TRPRF_LONG_RESERVE;
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
						if (slot == NULL) break;
						switch (static_cast<TraceRestrictSlotCondOpField>(GetTraceRestrictCondOp(item))) {
							case TRSCOF_ACQUIRE_WAIT:
								if (input.permitted_slot_operations & TRPISP_ACQUIRE) {
									if (!slot->Occupy(v->index)) out.flags |= TRPRF_WAIT_AT_PBS;
								}
								break;

							case TRSCOF_ACQUIRE_TRY:
								if (input.permitted_slot_operations & TRPISP_ACQUIRE) slot->Occupy(v->index);
								break;

							case TRSCOF_RELEASE_BACK:
								if (input.permitted_slot_operations & TRPISP_RELEASE_BACK) slot->Vacate(v->index);
								break;

							case TRSCOF_RELEASE_FRONT:
								if (input.permitted_slot_operations & TRPISP_RELEASE_FRONT) slot->Vacate(v->index);
								break;

							case TRSCOF_PBS_RES_END_ACQ_WAIT:
								if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQUIRE) {
									if (!slot->Occupy(v->index)) out.flags |= TRPRF_PBS_RES_END_WAIT;
								} else if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQ_DRY) {
									if (!slot->OccupyDryRun(v->index)) out.flags |= TRPRF_PBS_RES_END_WAIT;
								}
								break;

							case TRSCOF_PBS_RES_END_ACQ_TRY:
								if (input.permitted_slot_operations & TRPISP_PBS_RES_END_ACQUIRE) slot->Occupy(v->index);
								break;

							case TRSCOF_PBS_RES_END_RELEASE:
								if (input.permitted_slot_operations & TRPISP_PBS_RES_END_RELEASE) slot->Vacate(v->index);
								break;

							default:
								NOT_REACHED();
								break;
						}
						break;
					}

					default:
						NOT_REACHED();
				}
			}
		}
	}
	assert(condstack.empty());
}

/**
 * Decrement ref count, only use when removing a mapping
 */
void TraceRestrictProgram::DecrementRefCount() {
	assert(this->refcount > 0);
	this->refcount--;
	if (this->refcount == 0) {
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
	actions_used_flags = static_cast<TraceRestrictProgramActionsUsedFlags>(0);

	size_t size = items.size();
	for (size_t i = 0; i < size; i++) {
		TraceRestrictItem item = items[i];
		TraceRestrictItemType type = GetTraceRestrictType(item);

		// check multi-word instructions
		if (IsTraceRestrictDoubleItem(item)) {
			i++;
			if (i >= size) {
				return_cmd_error(STR_TRACE_RESTRICT_ERROR_OFFSET_TOO_LARGE); // instruction ran off end
			}
		}

		if (IsTraceRestrictConditional(item)) {
			TraceRestrictCondFlags condflags = GetTraceRestrictCondFlags(item);

			if (type == TRIT_COND_ENDIF) {
				if (condstack.empty()) {
					return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_NO_IF); // else/endif with no starting if
				}
				if (condflags & TRCF_ELSE) {
					// else
					if (condstack.back() & TRCSF_SEEN_ELSE) {
						return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_DUP_ELSE); // Two else clauses
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
						return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_ELIF_NO_IF); // Pre-empt assertions in HandleCondition
					}
					if (condstack.back() & TRCSF_SEEN_ELSE) {
						return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_DUP_ELSE); // else clause followed by elif/orif
					}
				}
				HandleCondition(condstack, condflags, true);
			}

			switch (GetTraceRestrictType(item)) {
				case TRIT_COND_ENDIF:
				case TRIT_COND_UNDEFINED:
				case TRIT_COND_TRAIN_LENGTH:
				case TRIT_COND_MAX_SPEED:
				case TRIT_COND_CURRENT_ORDER:
				case TRIT_COND_NEXT_ORDER:
				case TRIT_COND_LAST_STATION:
				case TRIT_COND_CARGO:
				case TRIT_COND_ENTRY_DIRECTION:
				case TRIT_COND_PBS_ENTRY_SIGNAL:
				case TRIT_COND_TRAIN_GROUP:
				case TRIT_COND_TRAIN_IN_SLOT:
				case TRIT_COND_SLOT_OCCUPANCY:
				case TRIT_COND_PHYS_PROP:
				case TRIT_COND_PHYS_RATIO:
				case TRIT_COND_TRAIN_OWNER:
					break;

				default:
					return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_UNKNOWN_INSTRUCTION);
			}
		} else {
			switch (GetTraceRestrictType(item)) {
				case TRIT_PF_DENY:
				case TRIT_PF_PENALTY:
					actions_used_flags |= TRPAUF_PF;
					break;

				case TRIT_RESERVE_THROUGH:
					actions_used_flags |= TRPAUF_RESERVE_THROUGH;
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
							NOT_REACHED();
							break;
					}
					break;

				case TRIT_SLOT:
					switch (static_cast<TraceRestrictSlotCondOpField>(GetTraceRestrictCondOp(item))) {
						case TRSCOF_ACQUIRE_WAIT:
							actions_used_flags |= TRPAUF_SLOT_ACQUIRE | TRPAUF_WAIT_AT_PBS;
							break;

						case TRSCOF_ACQUIRE_TRY:
							actions_used_flags |= TRPAUF_SLOT_ACQUIRE;
							break;

						case TRSCOF_RELEASE_BACK:
							actions_used_flags |= TRPAUF_SLOT_RELEASE_BACK;
							break;

						case TRSCOF_RELEASE_FRONT:
							actions_used_flags |= TRPAUF_SLOT_RELEASE_FRONT;
							break;

						case TRSCOF_PBS_RES_END_ACQ_WAIT:
							actions_used_flags |= TRPAUF_PBS_RES_END_SLOT | TRPAUF_PBS_RES_END_WAIT;
							break;

						case TRSCOF_PBS_RES_END_ACQ_TRY:
						case TRSCOF_PBS_RES_END_RELEASE:
							actions_used_flags |= TRPAUF_PBS_RES_END_SLOT;
							break;

						default:
							NOT_REACHED();
							break;
					}
					break;

				default:
					return_cmd_error(STR_TRACE_RESTRICT_ERROR_VALIDATE_UNKNOWN_INSTRUCTION);
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
		case TRVT_RESERVE_THROUGH:
		case TRVT_LONG_RESERVE:
		case TRVT_WEIGHT:
		case TRVT_POWER:
		case TRVT_FORCE:
		case TRVT_POWER_WEIGHT_RATIO:
		case TRVT_FORCE_WEIGHT_RATIO:
		case TRVT_WAIT_AT_PBS:
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
			assert(_sorted_standard_cargo_specs_size > 0);
			SetTraceRestrictValue(item, _sorted_cargo_specs[0]->Index());
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

		default:
			NOT_REACHED();
			break;
	}
}

/**
 * Set the type field of a TraceRestrictItem, and resets any other fields which are no longer valid/meaningful to sensible defaults
 */
void SetTraceRestrictTypeAndNormalise(TraceRestrictItem &item, TraceRestrictItemType type, uint8 aux_data)
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

	// First mapping for next tile, or later
	TraceRestrictMapping::iterator upper_bound = _tracerestrictprogram_mapping.lower_bound(MakeTraceRestrictRefId(t + 1, static_cast<Track>(0)));

	// If iterators are the same, there are no mappings for this tile
	SetRestrictedSignal(t, lower_bound != upper_bound);
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
		_tracerestrictprogram_pool.Get(insert_result.first->second.program_id)->DecrementRefCount();
		insert_result.first->second = prog->index;
	}
	prog->IncrementRefCount();

	TileIndex tile = GetTraceRestrictRefIdTileIndex(ref);
	Track track = GetTraceRestrictRefIdTrack(ref);
	TraceRestrictSetIsSignalRestrictedBit(tile);
	MarkTileDirtyByTile(tile);
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

		// check to see if another mapping needs to be removed as well
		// do this before decrementing the refcount
		bool remove_other_mapping = prog->refcount == 2 && prog->items.empty();

		prog->DecrementRefCount();
		_tracerestrictprogram_mapping.erase(iter);

		TileIndex tile = GetTraceRestrictRefIdTileIndex(ref);
		Track track = GetTraceRestrictRefIdTrack(ref);
		TraceRestrictSetIsSignalRestrictedBit(tile);
		MarkTileDirtyByTile(tile);
		YapfNotifyTrackLayoutChange(tile, track);

		if (remove_other_mapping) {
			TraceRestrictProgramID id = prog->index;
			for (TraceRestrictMapping::iterator rm_iter = _tracerestrictprogram_mapping.begin();
					rm_iter != _tracerestrictprogram_mapping.end(); ++rm_iter) {
				if (rm_iter->second.program_id == id) {
					TraceRestrictRemoveProgramMapping(rm_iter->first);
					break;
				}
			}
		}
		return true;
	} else {
		return false;
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
			return NULL;
		}
		TraceRestrictProgram *prog = new TraceRestrictProgram();

		// Create new mapping to pool item
		TraceRestrictCreateProgramMapping(ref, prog);
		return prog;
	} else {
		return NULL;
	}
}

/**
 * Notify that a signal is being removed
 * Remove any trace restrict mappings associated with it
 */
void TraceRestrictNotifySignalRemoval(TileIndex tile, Track track)
{
	TraceRestrictRefId ref = MakeTraceRestrictRefId(tile, track);
	bool removed = TraceRestrictRemoveProgramMapping(ref);
	DeleteWindowById(WC_TRACE_RESTRICT, ref);
	if (removed) InvalidateWindowClassesData(WC_TRACE_RESTRICT);
}

/**
 * Helper function to perform parameter bit-packing and call DoCommandP, for instruction modification actions
 */
void TraceRestrictDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32 offset, uint32 value, StringID error_msg)
{
	uint32 p1 = 0;
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
	if (!IsPlainRailTile(tile) || !HasTrack(tile, track)) {
		return_cmd_error(STR_ERROR_THERE_IS_NO_RAILROAD_TRACK);
	}
	if (!HasSignalOnTrack(tile, track)) {
		return_cmd_error(STR_ERROR_THERE_ARE_NO_SIGNALS);
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
static uint32 GetDualInstructionInitialValue(TraceRestrictItem item)
{
	switch (GetTraceRestrictType(item)) {
		case TRIT_COND_PBS_ENTRY_SIGNAL:
			return INVALID_TILE;

		case TRIT_COND_SLOT_OCCUPANCY:
			return 0;

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

CommandCost TraceRestrictProgramRemoveItemAt(std::vector<TraceRestrictItem> &items, uint32 offset, bool shallow_mode)
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

		uint32 recursion_depth = 1;
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

CommandCost TraceRestrictProgramMoveItemAt(std::vector<TraceRestrictItem> &items, uint32 &offset, bool up, bool shallow_mode)
{
	std::vector<TraceRestrictItem>::iterator move_start = TraceRestrictProgram::InstructionAt(items, offset);
	std::vector<TraceRestrictItem>::iterator move_end = InstructionIteratorNext(move_start);

	TraceRestrictItem old_item = *move_start;
	if (!shallow_mode) {
		if (IsTraceRestrictConditional(old_item)) {
			if (GetTraceRestrictCondFlags(old_item) != 0) {
				// can't move or/else blocks
				return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
			}
			if (GetTraceRestrictType(old_item) == TRIT_COND_ENDIF) {
				// this is an end if, can't move these
				return_cmd_error(STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
			}

			uint32 recursion_depth = 1;
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

/**
 * The main command for editing a signal tracerestrict program.
 * @param tile The tile which contains the signal.
 * @param flags Internal command handler stuff.
 * Below apply for instruction modification actions only
 * @param p1 Bitstuffed items
 * @param p2 Item, for insert and modify operations. Flags for instruction move operations
 * @return the cost of this operation (which is free), or an error
 */
CommandCost CmdProgramSignalTraceRestrict(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TraceRestrictDoCommandType type = static_cast<TraceRestrictDoCommandType>(GB(p1, 3, 5));

	if (type >= TRDCT_PROG_COPY) {
		return CmdProgramSignalTraceRestrictProgMgmt(tile, flags, p1, p2, text);
	}

	Track track = static_cast<Track>(GB(p1, 0, 3));
	uint32 offset = GB(p1, 8, 16);
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

	uint32 offset_limit_exclusive = ((type == TRDCT_INSERT_ITEM) ? 1 : 0);
	if (prog) offset_limit_exclusive += prog->items.size();

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
			bool old_is_dual = IsTraceRestrictDoubleItem(*old_item);
			bool new_is_dual = IsTraceRestrictDoubleItem(item);
			*old_item = item;
			if (old_is_dual && !new_is_dual) {
				items.erase(old_item + 1);
			} else if (!old_is_dual && new_is_dual) {
				items.insert(old_item + 1, GetDualInstructionInitialValue(item));
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

		default:
			NOT_REACHED();
			break;
	}

	TraceRestrictProgramActionsUsedFlags actions_used_flags;
	CommandCost validation_result = TraceRestrictProgram::Validate(items, actions_used_flags);
	if (validation_result.Failed()) {
		return validation_result;
	}

	if (flags & DC_EXEC) {
		assert(prog);

		// move in modified program
		prog->items.swap(items);
		prog->actions_used_flags = actions_used_flags;

		if (prog->items.size() == 0 && prog->refcount == 1) {
			// program is empty, and this tile is the only reference to it
			// so delete it, as it's redundant
			TraceRestrictRemoveProgramMapping(MakeTraceRestrictRefId(tile, track));
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
	uint32 p1 = 0;
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
CommandCost CmdProgramSignalTraceRestrictProgMgmt(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
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

	if (type == TRDCT_PROG_SHARE || type == TRDCT_PROG_COPY) {
		if (self == source) {
			return_cmd_error(STR_TRACE_RESTRICT_ERROR_SOURCE_SAME_AS_TARGET);
		}
	}
	if (type == TRDCT_PROG_SHARE || type == TRDCT_PROG_COPY || type == TRDCT_PROG_COPY_APPEND) {
		ret = TraceRestrictCheckTileIsUsable(source_tile, source_track);
		if (ret.Failed()) {
			return ret;
		}
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
				prog->items.reserve(prog->items.size() + source_prog->items.size()); // this is in case prog == source_prog
				prog->items.insert(prog->items.end(), source_prog->items.begin(), source_prog->items.end()); // append
				prog->Validate();
			}
			break;
		}

		case TRDCT_PROG_SHARE: {
			TraceRestrictRemoveProgramMapping(self);
			TraceRestrictProgram *source_prog = GetTraceRestrictProgram(source, true);
			if (!source_prog) {
				// allocation failed
				return CMD_ERROR;
			}

			TraceRestrictCreateProgramMapping(self, source_prog);
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
			}
			break;
		}

		case TRDCT_PROG_RESET: {
			TraceRestrictRemoveProgramMapping(self);
			break;
		}

		default:
			NOT_REACHED();
			break;
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);

	return CommandCost();
}

/**
 * This is called when a station, waypoint or depot is about to be deleted
 * Scan program pool and change any references to it to the invalid station ID, to avoid dangling references
 */
void TraceRestrictRemoveDestinationID(TraceRestrictOrderCondAuxField type, uint16 index)
{
	TraceRestrictProgram *prog;

	FOR_ALL_TRACE_RESTRICT_PROGRAMS(prog) {
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
	TraceRestrictProgram *prog;

	FOR_ALL_TRACE_RESTRICT_PROGRAMS(prog) {
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
 */
void TraceRestrictUpdateCompanyID(CompanyID old_company, CompanyID new_company)
{
	TraceRestrictProgram *prog;

	FOR_ALL_TRACE_RESTRICT_PROGRAMS(prog) {
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

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
}

static std::unordered_multimap<VehicleID, TraceRestrictSlotID> slot_vehicle_index;

/**
 * Add vehicle ID to occupants if possible and not already an occupant
 * @param id Vehicle ID
 * @param force Add the vehicle even if the slot is at/over capacity
 * @return whether vehicle ID is now an occupant
 */
bool TraceRestrictSlot::Occupy(VehicleID id, bool force)
{
	if (this->IsOccupant(id)) return true;
	if (this->occupants.size() >= this->max_occupancy && !force) return false;
	this->occupants.push_back(id);
	slot_vehicle_index.emplace(id, this->index);
	SetBit(Train::Get(id)->flags, VRF_HAVE_SLOT);
	SetWindowDirty(WC_VEHICLE_DETAILS, id);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
	return true;
}

/**
 * Dry-run adding vehicle ID to occupants if possible and not already an occupant
 * @param id Vehicle ID
 * @return whether vehicle IDwould be an occupant
 */
bool TraceRestrictSlot::OccupyDryRun(VehicleID id)
{
	if (this->IsOccupant(id)) return true;
	if (this->occupants.size() >= this->max_occupancy) return false;
	return true;
}

/**
 * Remove vehicle ID from occupants
 * @param id Vehicle ID
 */
void TraceRestrictSlot::Vacate(VehicleID id)
{
	if (container_unordered_remove(this->occupants, id)) {
		this->DeIndex(id);
	}
}

/** Remove all occupants */
void TraceRestrictSlot::Clear()
{
	for (VehicleID id : this->occupants) {
		this->DeIndex(id);
	}
	this->occupants.clear();
}

void TraceRestrictSlot::DeIndex(VehicleID id)
{
	auto range = slot_vehicle_index.equal_range(id);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second == this->index) {
			bool is_first_in_range = (it == range.first);

			auto next = slot_vehicle_index.erase(it);

			if (is_first_in_range && next == range.second) {
				/* Only one item, which we've just erased, clear the vehicle flag */
				ClrBit(Train::Get(id)->flags, VRF_HAVE_SLOT);
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
	const TraceRestrictSlot *slot;
	FOR_ALL_TRACE_RESTRICT_SLOTS(slot) {
		for (VehicleID id : slot->occupants) {
			slot_vehicle_index.emplace(id, slot->index);
		}
	}
}

/** Slot pool is about to be cleared */
void TraceRestrictSlot::PreCleanPool()
{
	slot_vehicle_index.clear();
}

/** Remove vehicle ID from all slot occupants */
void TraceRestrictRemoveVehicleFromAllSlots(VehicleID id)
{
	auto range = slot_vehicle_index.equal_range(id);
	for (auto it = range.first; it != range.second; ++it) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(it->second);
		container_unordered_remove(slot->occupants, id);
	}
	slot_vehicle_index.erase(range.first, range.second);
	if (range.first != range.second) InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
}

/** Replace all instance of a vehicle ID with another, in all slot occupants */
void TraceRestrictTransferVehicleOccupantInAllSlots(VehicleID from, VehicleID to)
{
	auto range = slot_vehicle_index.equal_range(from);
	std::vector<TraceRestrictSlotID> slots;
	for (auto it = range.first; it != range.second; ++it) {
		slots.push_back(it->second);
	}
	slot_vehicle_index.erase(range.first, range.second);
	for (TraceRestrictSlotID slot_id : slots) {
		TraceRestrictSlot *slot = TraceRestrictSlot::Get(slot_id);
		for (VehicleID &id : slot->occupants) {
			if (id == from) {
				id = to;
				slot_vehicle_index.emplace(to, slot_id);
			}
		}
	}
	if (!slots.empty()) InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
}

/** Get list of slots occupied by a vehicle ID */
void TraceRestrictGetVehicleSlots(VehicleID id, std::vector<TraceRestrictSlotID> &out)
{
	auto range = slot_vehicle_index.equal_range(id);
	for (auto it = range.first; it != range.second; ++it) {
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
	TraceRestrictProgram *prog;

	FOR_ALL_TRACE_RESTRICT_PROGRAMS(prog) {
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
	Order *o;
	FOR_ALL_ORDERS(o) {
		if (o->IsType(OT_CONDITIONAL) && o->GetConditionVariable() == OCV_SLOT_OCCUPANCY && o->GetXData() == index) {
			o->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_ID;
			changed_order = true;
		}
	}

	// update windows
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	if (changed_order) {
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
		InvalidateWindowClassesData(WC_VEHICLE_TIMETABLE);
	}
}

static bool IsUniqueSlotName(const char *name)
{
	const TraceRestrictSlot *slot;
	FOR_ALL_TRACE_RESTRICT_SLOTS(slot) {
		if (slot->name == name) return false;
	}
	return true;
}

/**
 * Create a new slot.
 * @param tile unused
 * @param flags type of operation
 * @param p1   unused
 * @param p2   unused
 * @param text new slot name
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!TraceRestrictSlot::CanAllocateItem()) return CMD_ERROR;
	if (StrEmpty(text)) return CMD_ERROR;

	size_t length = Utf8StringLength(text);
	if (length <= 0) return CMD_ERROR;
	if (length >= MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS) return CMD_ERROR;
	if (!IsUniqueSlotName(text)) return_cmd_error(STR_ERROR_NAME_MUST_BE_UNIQUE);

	if (flags & DC_EXEC) {
		TraceRestrictSlot *slot = new TraceRestrictSlot(_current_company);
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
CommandCost CmdDeleteTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(p1);
	if (slot == NULL || slot->owner != _current_company) return CMD_ERROR;

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
 *   - p1 bit 16: 0 - Rename grouop
 *                1 - Change max occupancy
 * @param p2   new max occupancy
 * @param text the new name
 * @return the cost of this operation or an error
 */
CommandCost CmdAlterTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(GB(p1, 0, 16));
	if (slot == NULL || slot->owner != _current_company) return CMD_ERROR;

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
		}
	}

	if (flags & DC_EXEC) {
		// update windows
		InvalidateWindowClassesData(WC_TRACE_RESTRICT);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);
		InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
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
CommandCost CmdAddVehicleTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(p1);
	Vehicle *v = Vehicle::GetIfValid(p2);
	if (slot == NULL || slot->owner != _current_company) return CMD_ERROR;
	if (v == NULL || v->owner != _current_company) return CMD_ERROR;

	if (flags & DC_EXEC) {
		slot->Occupy(v->index, true);
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
CommandCost CmdRemoveVehicleTraceRestrictSlot(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(p1);
	Vehicle *v = Vehicle::GetIfValid(p2);
	if (slot == NULL || slot->owner != _current_company) return CMD_ERROR;
	if (v == NULL) return CMD_ERROR; // permit removing vehicles of other owners from your own slot

	if (flags & DC_EXEC) {
		slot->Vacate(v->index);
	}

	return CommandCost();
}
