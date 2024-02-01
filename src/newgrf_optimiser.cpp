/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_optimiser.cpp NewGRF optimiser. */

#include "stdafx.h"

#include "newgrf_internal.h"
#include "newgrf_extension.h"
#include "debug_settings.h"
#include "core/y_combinator.hpp"
#include "scope.h"
#include "newgrf_station.h"

#include <tuple>

#include "safeguards.h"

static bool IsExpensiveVehicleVariable(uint16_t variable)
{
	switch (variable) {
		case 0x45:
		case 0x4A:
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0xFE:
		case 0xFF:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveStationVariable(uint16_t variable)
{
	switch (variable) {
		case 0x66:
		case 0x67:
		case 0x68:
		case 0x6A:
		case A2VRI_STATION_INFO_NEARBY_TILES_V2:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveIndustryTileVariable(uint16_t variable)
{
	switch (variable) {
		case 0x60:
		case 0x61:
		case 0x62:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveObjectVariable(uint16_t variable)
{
	switch (variable) {
		case 0x41:
		case 0x45:
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case A2VRI_OBJECT_FOUNDATION_SLOPE:
		case A2VRI_OBJECT_FOUNDATION_SLOPE_CHANGE:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveRoadStopsVariable(uint16_t variable)
{
	switch (variable) {
		case 0x45:
		case 0x46:
		case 0x66:
		case 0x67:
		case 0x68:
		case 0x6A:
		case 0x6B:
		case A2VRI_ROADSTOP_INFO_NEARBY_TILES_EXT:
		case A2VRI_ROADSTOP_INFO_NEARBY_TILES_V2:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveRailtypeVariable(uint16_t variable)
{
	switch (variable) {
		case A2VRI_RAILTYPE_SIGNAL_VERTICAL_CLEARANCE:
		case A2VRI_RAILTYPE_ADJACENT_CROSSING:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveSignalVariable(uint16_t variable)
{
	switch (variable) {
		case A2VRI_SIGNALS_SIGNAL_VERTICAL_CLEARANCE:
			return true;

		default:
			return false;
	}
}

static bool IsExpensiveVariable(uint16_t variable, GrfSpecFeature scope_feature)
{
	switch (scope_feature) {
		case GSF_TRAINS:
		case GSF_ROADVEHICLES:
		case GSF_SHIPS:
		case GSF_AIRCRAFT:
			return IsExpensiveVehicleVariable(variable);

		case GSF_STATIONS:
			return IsExpensiveStationVariable(variable);

		case GSF_INDUSTRYTILES:
			return IsExpensiveIndustryTileVariable(variable);

		case GSF_OBJECTS:
			return IsExpensiveObjectVariable(variable);

		case GSF_ROADSTOPS:
			return IsExpensiveRoadStopsVariable(variable);

		case GSF_RAILTYPES:
			return IsExpensiveRailtypeVariable(variable);

		case GSF_SIGNALS:
			return IsExpensiveSignalVariable(variable);

		default:
			return false;
	}
}

static bool IsVariableVeryCheap(uint16_t variable, GrfSpecFeature scope_feature)
{
	switch (variable) {
		case 0x0C:
		case 0x10:
		case 0x18:
		case 0x1C:
			return true;
	}
	return false;
}

static bool IsFeatureUsableForDSE(GrfSpecFeature feature)
{
	return true;
}

static bool IsFeatureUsableForCBQuickExit(GrfSpecFeature feature)
{
	return true;
}

static bool IsIdenticalValueLoad(const DeterministicSpriteGroupAdjust *a, const DeterministicSpriteGroupAdjust *b)
{
	if (a == nullptr && b == nullptr) return true;
	if (a == nullptr || b == nullptr) return false;

	if (a->variable == 0x7B || a->variable == 0x7E) return false;

	return std::tie(a->type, a->variable, a->shift_num, a->parameter, a->and_mask, a->add_val, a->divmod_val) ==
			std::tie(b->type, b->variable, b->shift_num, b->parameter, b->and_mask, b->add_val, b->divmod_val);
}

static const DeterministicSpriteGroupAdjust *GetVarAction2PreviousSingleLoadAdjust(const std::vector<DeterministicSpriteGroupAdjust> &adjusts, int start_index, bool *is_inverted)
{
	bool passed_store_perm = false;
	if (is_inverted != nullptr) *is_inverted = false;
	std::bitset<256> seen_stores;
	for (int i = start_index; i >= 0; i--) {
		const DeterministicSpriteGroupAdjust &prev = adjusts[i];
		if (prev.variable == 0x7E) {
			/* Procedure call, don't use or go past this */
			break;
		}
		if (prev.operation == DSGA_OP_RST) {
			if (prev.variable == 0x7B) {
				/* Can't use this previous load as it depends on the last value */
				return nullptr;
			}
			if (prev.variable == 0x7C && passed_store_perm) {
				/* If we passed a store perm then a load from permanent storage is not a valid previous load as we may have clobbered it */
				return nullptr;
			}
			if (prev.variable == 0x7D && seen_stores[prev.parameter & 0xFF]) {
				/* If we passed a store then a load from that same store is not valid */
				return nullptr;
			}
			return &prev;
		} else if (prev.operation == DSGA_OP_STO) {
			if (prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask < 0x100) {
				/* Temp store */
				seen_stores.set(prev.and_mask, true);
				continue;
			} else {
				/* Special register store or unpredictable store, don't try to optimise following load */
				break;
			}
		} else if (prev.operation == DSGA_OP_STOP) {
			/* Permanent storage store */
			passed_store_perm = true;
			continue;
		} else if (prev.operation == DSGA_OP_XOR && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == 1 && is_inverted != nullptr) {
			/* XOR invert */
			*is_inverted = !(*is_inverted);
			continue;
		} else {
			break;
		}
	}
	return nullptr;
}

static const DeterministicSpriteGroupAdjust *GetVarAction2PreviousSingleStoreAdjust(const std::vector<DeterministicSpriteGroupAdjust> &adjusts, int start_index, bool *is_inverted)
{
	if (is_inverted != nullptr) *is_inverted = false;
	for (int i = start_index; i >= 0; i--) {
		const DeterministicSpriteGroupAdjust &prev = adjusts[i];
		if (prev.variable == 0x7E) {
			/* Procedure call, don't use or go past this */
			break;
		}
		if (prev.operation == DSGA_OP_STO) {
			if (prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask < 0x100) {
				/* Temp store */
				return &prev;
			} else {
				/* Special register store or unpredictable store, don't try to optimise following load */
				break;
			}
		} else if (prev.operation == DSGA_OP_XOR && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == 1 && is_inverted != nullptr) {
			/* XOR invert */
			*is_inverted = !(*is_inverted);
			continue;
		} else {
			break;
		}
	}
	return nullptr;
}

static int GetVarAction2AdjustOfPreviousTempStoreSource(const DeterministicSpriteGroupAdjust *adjusts, int start_index, uint8_t store_var)
{
	for (int i = start_index - 1; i >= 0; i--) {
		const DeterministicSpriteGroupAdjust &prev = adjusts[i];
		if (prev.variable == 0x7E) {
			/* Procedure call, don't use or go past this */
			return -1;
		}
		if (prev.operation == DSGA_OP_STO) {
			if (prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask < 0x100) {
				/* Temp store */
				if (prev.and_mask == (store_var & 0xFF)) {
					return i;
				}
			} else {
				/* Special register store or unpredictable store, don't use or go past this */
				return -1;
			}
		}
	}
	return -1;
}

struct VarAction2AdjustDescriptor {
	DeterministicSpriteGroupAdjust *adjust_array = nullptr;
	DeterministicSpriteGroupAdjust *override_first = nullptr;
	int index = 0;

	inline bool IsValid() const { return this->adjust_array != nullptr; }
	inline const DeterministicSpriteGroupAdjust &GetCurrent() const { return this->override_first != nullptr ? *(this->override_first) : this->adjust_array[this->index]; };
};

static bool AdvanceVarAction2AdjustDescriptor(VarAction2AdjustDescriptor &desc)
{
	const DeterministicSpriteGroupAdjust &adj = desc.GetCurrent();
	if (adj.variable == 0x7E || adj.variable == 0x7B || adj.operation == DSGA_OP_STOP) {
		/* Procedure call or load depends on the last value, or a permanent store, don't use or go past this */
		desc.index = -1;
		desc.override_first = nullptr;
		return true;
	}
	if (adj.operation == DSGA_OP_STO) {
		if (adj.type == DSGA_TYPE_NONE && adj.variable == 0x1A && adj.shift_num == 0 && adj.and_mask < 0x100) {
			/* Temp store, skip */
			desc.index--;
		} else {
			/* Special register store or unpredictable store, don't use or go past this */
			desc.index = -1;
		}
		desc.override_first = nullptr;
		return true;
	}
	return false;
}

static bool AreVarAction2AdjustsEquivalent(VarAction2AdjustDescriptor a, VarAction2AdjustDescriptor b)
{
	if (!a.IsValid() || !b.IsValid()) return false;

	while (a.index >= 0 && b.index >= 0) {
		if (a.adjust_array == b.adjust_array && a.index == b.index) return true;

		if (AdvanceVarAction2AdjustDescriptor(a)) continue;
		if (AdvanceVarAction2AdjustDescriptor(b)) continue;

		const DeterministicSpriteGroupAdjust &adj_a = a.GetCurrent();
		const DeterministicSpriteGroupAdjust &adj_b = b.GetCurrent();

		if (std::tie(adj_a.operation, adj_a.type, adj_a.variable, adj_a.shift_num, adj_a.and_mask, adj_a.add_val, adj_a.divmod_val) !=
			std::tie(adj_b.operation, adj_b.type, adj_b.variable, adj_b.shift_num, adj_b.and_mask, adj_b.add_val, adj_b.divmod_val)) return false;

		if (adj_a.parameter != adj_b.parameter) {
			if (adj_a.variable == 0x7D) {
				int store_index_a = GetVarAction2AdjustOfPreviousTempStoreSource(a.adjust_array, a.index - 1, (adj_a.parameter & 0xFF));
				if (store_index_a < 1) {
					return false;
				}
				int store_index_b = GetVarAction2AdjustOfPreviousTempStoreSource(b.adjust_array, b.index - 1, (adj_b.parameter & 0xFF));
				if (store_index_b < 1) {
					return false;
				}
				if (!AreVarAction2AdjustsEquivalent({ a.adjust_array, nullptr, store_index_a - 1 }, { b.adjust_array, nullptr, store_index_b - 1 })) return false;
			} else {
				return false;
			}
		}

		if (adj_b.operation == DSGA_OP_RST) return true;

		a.index--;
		b.index--;
		a.override_first = nullptr;
		b.override_first = nullptr;
	}

	return false;
}

enum VarAction2AdjustsBooleanInverseResult {
	VA2ABIR_NO,                               ///< Adjusts are not inverse
	VA2ABIR_CCAT,                             ///< Adjusts are inverse (constant comparison adjust type)
	VA2ABIR_XOR_A,                            ///< Adjusts are inverse (a has an additional XOR 1 or EQ 0 compared to b)
	VA2ABIR_XOR_B,                            ///< Adjusts are inverse (b has an additional XOR 1 or EQ 0 compared to a)
};

static VarAction2AdjustsBooleanInverseResult AreVarAction2AdjustsBooleanInverse(VarAction2AdjustDescriptor a, VarAction2AdjustDescriptor b)
{
	if (!a.IsValid() || !b.IsValid()) return VA2ABIR_NO;

	if (a.index < 0 || b.index < 0) return VA2ABIR_NO;

	AdvanceVarAction2AdjustDescriptor(a);
	AdvanceVarAction2AdjustDescriptor(b);

	if (a.index < 0 || b.index < 0) return VA2ABIR_NO;

	const DeterministicSpriteGroupAdjust &adj_a = a.GetCurrent();
	const DeterministicSpriteGroupAdjust &adj_b = b.GetCurrent();

	if (adj_a.operation == DSGA_OP_RST && adj_b.operation == DSGA_OP_RST &&
			IsConstantComparisonAdjustType(adj_a.type) && InvertConstantComparisonAdjustType(adj_a.type) == adj_b.type &&
			(std::tie(adj_a.variable, adj_a.shift_num, adj_a.parameter, adj_a.and_mask, adj_a.add_val, adj_a.divmod_val) ==
			std::tie(adj_b.variable, adj_b.shift_num, adj_b.parameter, adj_b.and_mask, adj_b.add_val, adj_b.divmod_val))) {
		return VA2ABIR_CCAT;
	}

	auto check_inverse = [&]() -> bool {
		auto check_inner = [](VarAction2AdjustDescriptor &a, VarAction2AdjustDescriptor &b) -> bool {
			if (a.index >= 0) AdvanceVarAction2AdjustDescriptor(a);
			if (a.index >= 0) {
				const DeterministicSpriteGroupAdjust &a_adj = a.GetCurrent();
				/* Check that the value was bool prior to the XOR */
				if (IsEvalAdjustOperationRelationalComparison(a_adj.operation) || IsConstantComparisonAdjustType(a_adj.type)) {
					if (AreVarAction2AdjustsEquivalent(a, b)) return true;
				}
			}
			return false;
		};
		const DeterministicSpriteGroupAdjust &adj = a.GetCurrent();
		if (adj.operation == DSGA_OP_XOR && adj.type == DSGA_TYPE_NONE && adj.variable == 0x1A && adj.shift_num == 0 && adj.and_mask == 1) {
			VarAction2AdjustDescriptor tmp = { a.adjust_array, nullptr, a.index - 1 };
			if (check_inner(tmp, b)) return true;
		}
		if (adj.operation == DSGA_OP_RST && adj.type == DSGA_TYPE_EQ && adj.variable == 0x7D && adj.shift_num == 0 && adj.and_mask == 0xFFFFFFFF && adj.add_val == 0) {
			int store_index = GetVarAction2AdjustOfPreviousTempStoreSource(a.adjust_array, a.index - 1, (adj.parameter & 0xFF));
			if (store_index >= 1) {
				/* Found the referenced temp store, use that */
				VarAction2AdjustDescriptor tmp = { a.adjust_array, nullptr, store_index - 1 };
				if (check_inner(tmp, b)) return true;
			}
		}
		return false;
	};

	if (check_inverse()) return VA2ABIR_XOR_A;

	std::swap(a, b);

	if (check_inverse()) return VA2ABIR_XOR_B;

	return VA2ABIR_NO;
}

static void GetBoolMulSourceAdjusts(std::vector<DeterministicSpriteGroupAdjust> &adjusts, int start_index, uint store_var, DeterministicSpriteGroupAdjust &synth_adjust,
	VarAction2AdjustDescriptor &found1, VarAction2AdjustDescriptor &found2, uint *mul_index)
{
	bool have_mul = false;
	for (int i = start_index; i >= 0; i--) {
		const DeterministicSpriteGroupAdjust &prev = adjusts[i];
		if (prev.variable == 0x7E || prev.variable == 0x7B) {
			/* Procedure call or load depends on the last value, don't use or go past this */
			return;
		}
		if (prev.operation == DSGA_OP_STO) {
			if (prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask < 0x100) {
				/* Temp store */
				if (prev.and_mask == (store_var & 0xFF)) return;
			} else {
				/* Special register store or unpredictable store, don't use or go past this */
				return;
			}
		} else if (prev.operation == DSGA_OP_MUL && !have_mul) {
			/* First source is the variable of mul, if it's a temporary storage load, try to follow it */
			if (mul_index != nullptr) *mul_index = i;
			if (prev.variable == 0x7D && prev.type == DSGA_TYPE_NONE && prev.shift_num == 0 && prev.and_mask == 0xFFFFFFFF) {
				int store_index = GetVarAction2AdjustOfPreviousTempStoreSource(adjusts.data(), i - 1, (prev.parameter & 0xFF));
				if (store_index >= 1) {
					/* Found the referenced temp store, use that */
					found1 = { adjusts.data(), nullptr, store_index - 1 };
					have_mul = true;
				}
			}
			if (!have_mul) {
				/* It's not a temporary storage load which can be followed, synthesise an RST */
				synth_adjust = prev;
				synth_adjust.operation = DSGA_OP_RST;
				synth_adjust.adjust_flags = DSGAF_NONE;
				found1 = { adjusts.data(), &synth_adjust, i };
				have_mul = true;
			}
		} else if (prev.operation == DSGA_OP_STOP) {
			/* Don't try to handle writes to permanent storage */
			return;
		} else if (have_mul) {
			/* Found second source */
			found2 = { adjusts.data(), nullptr, i };
			return;
		} else {
			return;
		}
	}
}

/*
 * Find and replace the result of:
 *   (var * flag) + (var * !flag) with var
 *   (-var * (var < 0)) + (var * !(var < 0)) with abs(var)
 * "+" may be ADD, OR or XOR.
 */
static bool TryMergeBoolMulCombineVarAction2Adjust(VarAction2OptimiseState &state, std::vector<DeterministicSpriteGroupAdjust> &adjusts, const int adjust_index)
{
	uint store_var = adjusts[adjust_index].parameter;

	DeterministicSpriteGroupAdjust synth_adjusts[2];
	VarAction2AdjustDescriptor found_adjusts[4] = {};
	uint mul_indices[2] = {};

	auto find_adjusts = [&](int start_index, uint save_index) {
		GetBoolMulSourceAdjusts(adjusts, start_index, store_var, synth_adjusts[save_index], found_adjusts[save_index * 2], found_adjusts[(save_index * 2) + 1], mul_indices + save_index);
	};

	find_adjusts(adjust_index - 1, 0); // A (first, closest)
	if (!found_adjusts[0].IsValid() || !found_adjusts[1].IsValid()) return false;

	/* Find offset of referenced store */
	int store_index = GetVarAction2AdjustOfPreviousTempStoreSource(adjusts.data(), adjust_index - 1, (store_var & 0xFF));
	if (store_index < 0) return false;

	find_adjusts(store_index - 1, 1); // B (second, further)
	if (!found_adjusts[2].IsValid() || !found_adjusts[3].IsValid()) return false;

	bool is_cond_first[2];
	VarAction2AdjustsBooleanInverseResult found = VA2ABIR_NO;
	auto try_find = [&](bool a_first, bool b_first) {
		if (found == VA2ABIR_NO) {
			found = AreVarAction2AdjustsBooleanInverse(found_adjusts[a_first ? 0 : 1], found_adjusts[b_first ? 2 : 3]);
			if (found != VA2ABIR_NO) {
				is_cond_first[0] = a_first;
				is_cond_first[1] = b_first;
			}
		}
	};
	try_find(true, true);
	try_find(true, false);
	try_find(false, true);
	try_find(false, false);

	if (found == VA2ABIR_NO) return false;

	auto try_erase_from = [&](uint start) -> bool {
		for (uint i = start; i < (uint)adjusts.size(); i++) {
			const DeterministicSpriteGroupAdjust &adjust = adjusts[i];
			if (adjust.variable == 0x7E || IsEvalAdjustWithSideEffects(adjust.operation)) return false;
		}
		adjusts.erase(adjusts.begin() + start, adjusts.end());
		return true;
	};
	auto try_to_make_rst_from = [&](uint idx) -> bool {
		const DeterministicSpriteGroupAdjust &src = adjusts[idx];
		if (src.variable == 0x7D) {
			/* Check that variable is still valid */
			for (uint i = idx; i < (uint)adjusts.size(); i++) {
				const DeterministicSpriteGroupAdjust &adjust = adjusts[i];
				if (adjust.variable == 0x7E) return false;
				if (adjust.operation == DSGA_OP_STO) {
					if (adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask < 0x100) {
						/* Temp store */
						if (adjust.and_mask == (src.parameter & 0xFF)) return false;
					} else {
						/* Special register store or unpredictable store, don't use or go past this */
						return false;
					}
				}
			}
		}
		adjusts.push_back(src);
		adjusts.back().operation = DSGA_OP_RST;
		adjusts.back().adjust_flags = DSGAF_NONE;
		return true;
	};

	if (AreVarAction2AdjustsEquivalent(found_adjusts[is_cond_first[0] ? 1 : 0], found_adjusts[is_cond_first[1] ? 3 : 2])) {
		/* replace (var * flag) + (var * !flag) with var */

		if (is_cond_first[0]) {
			/* The cond is the mul variable of the first (closest) mul, the actual value is the prior adjust */
			if (try_erase_from(mul_indices[0] + 1)) return true;
		} else {
			/* The value is the mul variable of the first (closest) mul, the cond is the prior adjust */
			if (try_to_make_rst_from(mul_indices[0])) return true;
		}

		if (!is_cond_first[1]) {
			/* The value is the mul variable of the second (further) mul, the cond is the prior adjust */
			if (try_to_make_rst_from(mul_indices[1])) return true;
		}

		return false;
	}

	auto check_rsub = [&](VarAction2AdjustDescriptor &desc) -> bool {
		int rsub_offset = desc.index;
		if (rsub_offset < 1) return false;
		const DeterministicSpriteGroupAdjust &adj = adjusts[rsub_offset];
		if (adj.operation == DSGA_OP_RSUB && adj.type == DSGA_TYPE_NONE && adj.variable == 0x1A && adj.shift_num == 0 && adj.and_mask == 0) {
			desc.index--;
			return true;
		}
		return false;
	};

	auto check_abs_cond = [&](VarAction2AdjustDescriptor cond, VarAction2AdjustDescriptor &value) -> bool {
		int lt_offset = cond.index;
		if (lt_offset < 1) return false;
		const DeterministicSpriteGroupAdjust &adj = adjusts[lt_offset];
		if (adj.operation == DSGA_OP_SLT && adj.type == DSGA_TYPE_NONE && adj.variable == 0x1A && adj.shift_num == 0 && adj.and_mask == 0) {
			cond.index--;
			return AreVarAction2AdjustsEquivalent(cond, value);
		}
		return false;
	};

	auto append_abs = [&]() {
		adjusts.emplace_back();
		adjusts.back().operation = DSGA_OP_ABS;
		adjusts.back().variable = 0x1A;
		state.inference |= VA2AIF_SIGNED_NON_NEGATIVE;
	};

	if (found == VA2ABIR_XOR_A) {
		/* Try to find an ABS:
		 * A has the extra invert, check cond of B
		 * B is the negative path with the RSUB
		 */
		VarAction2AdjustDescriptor value_b = found_adjusts[is_cond_first[1] ? 3 : 2];
		const VarAction2AdjustDescriptor &cond_b = found_adjusts[is_cond_first[1] ? 2 : 3];

		if (check_rsub(value_b) && check_abs_cond(cond_b, value_b) && AreVarAction2AdjustsEquivalent(found_adjusts[is_cond_first[0] ? 1 : 0], value_b)) {
			/* Found an ABS, use one of the two value parts */

			if (is_cond_first[0]) {
				/* The cond is the mul variable of the A (first, closest) mul, the actual value is the prior adjust */
				if (try_erase_from(mul_indices[0])) {
					append_abs();
					return true;
				}
			} else {
				/* The value is the mul variable of the A (first, closest) mul, the cond is the prior adjust */
				if (try_to_make_rst_from(mul_indices[0])) {
					append_abs();
					return true;
				}
			}
		}
	}
	if (found == VA2ABIR_XOR_B) {
		/* Try to find an ABS:
		 * B has the extra invert, check cond of A
		 * A is the negative path with the RSUB
		 */
		VarAction2AdjustDescriptor value_a = found_adjusts[is_cond_first[0] ? 1 : 0];
		const VarAction2AdjustDescriptor &cond_a = found_adjusts[is_cond_first[0] ? 0 : 1];

		if (check_rsub(value_a) && check_abs_cond(cond_a, value_a) && AreVarAction2AdjustsEquivalent(found_adjusts[is_cond_first[1] ? 3 : 2], value_a)) {
			/* Found an ABS, use one of the two value parts */

			if (is_cond_first[0]) {
				/* The cond is the mul variable of the A (first, closest) mul, the actual value is the prior adjust, -1 to also remove the RSUB */
				if (try_erase_from(mul_indices[0] - 1)) {
					append_abs();
					return true;
				}
			}

			if (!is_cond_first[1]) {
				/* The value is the mul variable of the B (second, further) mul, the cond is the prior adjust */
				if (try_to_make_rst_from(mul_indices[1])) {
					append_abs();
					return true;
				}
			}
		}
	}

	return false;
}

/* Returns the number of adjusts to remove: 0: neither, 1: current, 2: prev and current */
static uint TryMergeVarAction2AdjustConstantOperations(DeterministicSpriteGroupAdjust &prev, DeterministicSpriteGroupAdjust &current)
{
	if (prev.type != DSGA_TYPE_NONE || prev.variable != 0x1A || prev.shift_num != 0) return 0;
	if (current.type != DSGA_TYPE_NONE || current.variable != 0x1A || current.shift_num != 0) return 0;

	switch (current.operation) {
		case DSGA_OP_ADD:
		case DSGA_OP_SUB:
			if (prev.operation == current.operation) {
				prev.and_mask += current.and_mask;
				break;
			}
			if (prev.operation == ((current.operation == DSGA_OP_SUB) ? DSGA_OP_ADD : DSGA_OP_SUB)) {
				prev.and_mask -= current.and_mask;
				break;
			}
			return 0;

		case DSGA_OP_OR:
			if (prev.operation == DSGA_OP_OR) {
				prev.and_mask |= current.and_mask;
				break;
			}
			return 0;

		case DSGA_OP_AND:
			if (prev.operation == DSGA_OP_AND) {
				prev.and_mask &= current.and_mask;
				break;
			}
			return 0;

		case DSGA_OP_XOR:
			if (prev.operation == DSGA_OP_XOR) {
				prev.and_mask ^= current.and_mask;
				break;
			}
			return 0;

		default:
			return 0;
	}

	if (prev.and_mask == 0 && IsEvalAdjustWithZeroRemovable(prev.operation)) {
		/* prev now does nothing, remove it as well */
		return 2;
	}
	return 1;
}

static inline bool IsSimpleContainerSpriteGroup(const SpriteGroup *group) {
	return group != nullptr && (group->type == SGT_RANDOMIZED || group->type == SGT_REAL);
}

struct SimpleContainerSpriteGroupIterator {
	typedef const SpriteGroup * value_type;
	typedef const SpriteGroup * *pointer;
	typedef const SpriteGroup * &reference;
	typedef size_t difference_type;
	typedef std::forward_iterator_tag iterator_category;

	explicit SimpleContainerSpriteGroupIterator(const SpriteGroup * const * current, const SpriteGroup * const * end, const SpriteGroup * const * next)
		: current(current), end(end), next(next)
	{
		this->ValidateIndex();
	};

	bool operator==(const SimpleContainerSpriteGroupIterator &other) const { return this->current == other.current; }
	bool operator!=(const SimpleContainerSpriteGroupIterator &other) const { return !(*this == other); }
	const SpriteGroup *operator*() const { return *this->current; }
	SimpleContainerSpriteGroupIterator &operator++() { ++this->current; this->ValidateIndex(); return *this; }

private:
	const SpriteGroup * const * current;
	const SpriteGroup * const * end;
	const SpriteGroup * const * next;

	void ValidateIndex()
	{
		if (this->current == this->end) this->current = this->next;
	}
};

/* Wrapper to iterate the sprite groups within SGT_RANDOMIZED or SGT_REAL groups */
struct IterateSimpleContainerSpriteGroup {
	std::array<const SpriteGroup * const *, 2> begin_ptr{};
	std::array<const SpriteGroup * const *, 2> end_ptr{};

	IterateSimpleContainerSpriteGroup(const SpriteGroup *sg)
	{
		if (sg == nullptr) return;
		if (sg->type == SGT_RANDOMIZED) {
			const RandomizedSpriteGroup *rsg = (const RandomizedSpriteGroup*)sg;
			this->begin_ptr[0] = rsg->groups.data();
			this->end_ptr[0] = rsg->groups.data() + rsg->groups.size();
		}
		if (sg->type == SGT_REAL) {
			const RealSpriteGroup *rsg = (const RealSpriteGroup*)sg;
			this->begin_ptr[0] = rsg->loaded.data();
			this->end_ptr[0] = rsg->loaded.data() + rsg->loaded.size();
			this->begin_ptr[1] = rsg->loading.data();
			this->end_ptr[1] = rsg->loading.data() + rsg->loading.size();
		}
	}

	SimpleContainerSpriteGroupIterator begin() { return SimpleContainerSpriteGroupIterator(this->begin_ptr[0], this->end_ptr[0], this->begin_ptr[1]); }
	SimpleContainerSpriteGroupIterator end() { return SimpleContainerSpriteGroupIterator(this->end_ptr[1], this->end_ptr[1], this->end_ptr[1]); }
};

void OptimiseVarAction2Adjust(VarAction2OptimiseState &state, const VarAction2AdjustInfo info, DeterministicSpriteGroup *group, DeterministicSpriteGroupAdjust &adjust)
{
	if (unlikely(HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2))) return;

	auto guard = scope_guard([&]() {
		if (!group->adjusts.empty()) {
			const DeterministicSpriteGroupAdjust &adjust = group->adjusts.back();
			if (adjust.variable == 0x7E || IsEvalAdjustWithSideEffects(adjust.operation)) {
				/* save inference state */
				state.inference_backup.adjust_size = (uint)group->adjusts.size();
				state.inference_backup.inference = state.inference;
				state.inference_backup.current_constant = state.current_constant;
			}
		}
	});

	auto try_restore_inference_backup = [&](uint offset) {
		if (state.inference_backup.adjust_size != 0 && state.inference_backup.adjust_size == (uint)group->adjusts.size() - offset) {
			state.inference = state.inference_backup.inference;
			state.current_constant = state.inference_backup.current_constant;
		}
	};

	const VarAction2AdjustInferenceFlags prev_inference = state.inference;
	state.inference = VA2AIF_NONE;

	auto get_sign_bit = [&]() -> uint32_t {
		return (1 << ((info.varsize * 8) - 1));
	};

	auto get_full_mask = [&]() -> uint32_t {
		return UINT_MAX >> ((4 - info.varsize) * 8);
	};

	auto add_inferences_from_mask = [&](uint32_t mask) {
		if (mask == 1) {
			state.inference |= VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
		} else if ((mask & get_sign_bit()) == 0) {
			state.inference |= VA2AIF_SIGNED_NON_NEGATIVE;
		}
	};

	auto replace_with_constant_load = [&](uint32_t constant) {
		group->adjusts.pop_back();
		if ((prev_inference & VA2AIF_HAVE_CONSTANT) && constant == state.current_constant) {
			/* Don't create a new constant load for the same constant as was there previously */
			state.inference = prev_inference;
			return;
		}
		while (!group->adjusts.empty()) {
			const DeterministicSpriteGroupAdjust &prev = group->adjusts.back();
			if (prev.variable != 0x7E && !IsEvalAdjustWithSideEffects(prev.operation)) {
				/* Delete useless operation */
				group->adjusts.pop_back();
			} else {
				break;
			}
		}
		state.inference = VA2AIF_HAVE_CONSTANT;
		add_inferences_from_mask(constant);
		state.current_constant = constant;
		if (constant != 0 || !group->adjusts.empty()) {
			DeterministicSpriteGroupAdjust &replacement = group->adjusts.emplace_back();
			replacement.operation = DSGA_OP_RST;
			replacement.variable = 0x1A;
			replacement.shift_num = 0;
			replacement.type = DSGA_TYPE_NONE;
			replacement.and_mask = constant;
			replacement.add_val = 0;
			replacement.divmod_val = 0;
			state.inference |= VA2AIF_PREV_MASK_ADJUST;
		}
	};

	auto handle_unpredictable_temp_load = [&]() {
		std::bitset<256> bits;
		bits.set();
		for (auto &it : state.temp_stores) {
			bits.set(it.first, false);
		}
		state.GetVarTracking(group)->in |= bits;
	};
	auto reset_store_values = [&]() {
		for (auto &it : state.temp_stores) {
			it.second.inference = VA2AIF_NONE;
			it.second.version++;
		}
		state.default_variable_version++;
		state.special_register_store_mask = 0;
	};
	auto handle_unpredictable_temp_store = [&]() {
		reset_store_values();
	};

	auto try_merge_with_previous = [&]() {
		if (adjust.variable == 0x1A && group->adjusts.size() >= 2) {
			/* Merged this adjust into the previous one */
			uint to_remove = TryMergeVarAction2AdjustConstantOperations(group->adjusts[group->adjusts.size() - 2], adjust);
			if (to_remove > 0) group->adjusts.erase(group->adjusts.end() - to_remove, group->adjusts.end());

			if (to_remove == 1 && group->adjusts.back().and_mask == 0 && IsEvalAdjustWithZeroAlwaysZero(group->adjusts.back().operation)) {
				/* Operation always returns 0, replace it and any useless prior operations */
				replace_with_constant_load(0);
			}
		}
	};

	auto try_inline_procedure = [&]() -> bool {
		if (adjust.operation != DSGA_OP_RST || adjust.type != DSGA_TYPE_NONE || state.var_1C_present) return false;

		const SpriteGroup *subroutine = adjust.subroutine;

		if (subroutine == nullptr || subroutine->type != SGT_DETERMINISTIC || subroutine->feature != group->feature) {
			return false;
		}

		const DeterministicSpriteGroup *dsg = (const DeterministicSpriteGroup*)subroutine;
		if (!(dsg->dsg_flags & DSGF_INLINE_CANDIDATE) || dsg->var_scope != group->var_scope || dsg->size != group->size) return false;

		std::vector<DeterministicSpriteGroupAdjust> *proc = _cur.GetInlinableGroupAdjusts(dsg, false);
		if (proc == nullptr) return false;

		byte shift_num = adjust.shift_num;
		uint32_t and_mask = adjust.and_mask;

		// Initial value state is 0
		replace_with_constant_load(0);

		for (const DeterministicSpriteGroupAdjust &proc_adjust : *proc) {
			group->adjusts.push_back(proc_adjust);
			OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
		}
		if (shift_num != 0) {
			DeterministicSpriteGroupAdjust &adj = group->adjusts.emplace_back();
			adj.operation = DSGA_OP_SHR;
			adj.variable = 0x1A;
			adj.shift_num = 0;
			adj.type = DSGA_TYPE_NONE;
			adj.and_mask = shift_num;
			adj.add_val = 0;
			adj.divmod_val = 0;
			OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
		}
		if (and_mask != 0xFFFFFFFF) {
			DeterministicSpriteGroupAdjust &adj = group->adjusts.emplace_back();
			adj.operation = DSGA_OP_AND;
			adj.variable = 0x1A;
			adj.shift_num = 0;
			adj.type = DSGA_TYPE_NONE;
			adj.and_mask = and_mask;
			adj.add_val = 0;
			adj.divmod_val = 0;
			OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
		}

		group->sg_flags |= SGF_INLINING;

		return true;
	};

	/* Special handling of variable 7B, this uses the parameter as the variable number, and the last value as the variable's parameter.
	 * If the last value is a known constant, it can be substituted immediately. */
	if (adjust.variable == 0x7B) {
		if (prev_inference & VA2AIF_HAVE_CONSTANT) {
			adjust.variable = adjust.parameter;
			adjust.parameter = state.current_constant;
		} else if (adjust.parameter == 0x7D) {
			handle_unpredictable_temp_load();
		} else if (adjust.parameter == 0x1C) {
			/* This is to simplify tracking of variable 1C, the parameter is never used for anything */
			adjust.variable = adjust.parameter;
			adjust.parameter = 0;
		}
	}
	if (adjust.variable == 0x1C && !state.seen_procedure_call) {
		group->dsg_flags |= DSGF_REQUIRES_VAR1C;
	}
	if (adjust.variable == 0x11 || (adjust.variable == 0x7B && adjust.parameter == 0x11)) {
		adjust.variable = 0x1A;
		adjust.parameter = 0;
		adjust.shift_num = 0;
		adjust.and_mask = 0;
	}

	VarAction2AdjustInferenceFlags non_const_var_inference = VA2AIF_NONE;
	int iteration = 32;
	while (adjust.variable == 0x7D && iteration > 0) {
		iteration--;
		non_const_var_inference = VA2AIF_NONE;
		auto iter = state.temp_stores.find(adjust.parameter & 0xFF);
		if (iter == state.temp_stores.end()) {
			/* Read without any previous store */
			state.GetVarTracking(group)->in.set(adjust.parameter & 0xFF, true);
			adjust.parameter |= (state.default_variable_version << 8);
		} else {
			const VarAction2TempStoreInference &store = iter->second;
			if (store.inference & VA2AIF_HAVE_CONSTANT) {
				adjust.variable = 0x1A;
				adjust.parameter = 0;
				adjust.and_mask &= (store.store_constant >> adjust.shift_num);
			} else if ((store.inference & VA2AIF_SINGLE_LOAD) && (store.var_source.variable == 0x7D || IsVariableVeryCheap(store.var_source.variable, info.scope_feature))) {
				if (adjust.type == DSGA_TYPE_NONE && adjust.shift_num == 0 && (adjust.and_mask == 0xFFFFFFFF || ((store.inference & VA2AIF_ONE_OR_ZERO) && (adjust.and_mask & 1)))) {
					adjust.type = store.var_source.type;
					adjust.variable = store.var_source.variable;
					adjust.shift_num = store.var_source.shift_num;
					adjust.parameter = store.var_source.parameter;
					adjust.and_mask = store.var_source.and_mask;
					adjust.add_val = store.var_source.add_val;
					adjust.divmod_val = store.var_source.divmod_val;
					continue;
				} else if (store.var_source.type == DSGA_TYPE_NONE && (adjust.shift_num + store.var_source.shift_num) < 32) {
					adjust.variable = store.var_source.variable;
					adjust.parameter = store.var_source.parameter;
					adjust.and_mask &= store.var_source.and_mask >> adjust.shift_num;
					adjust.shift_num += store.var_source.shift_num;
					continue;
				}
				adjust.parameter |= (store.version << 8);
			} else {
				if (adjust.type == DSGA_TYPE_NONE) {
					non_const_var_inference = store.inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO | VA2AIF_MUL_BOOL);
				}
				if (store.inference & VA2AIF_SINGLE_LOAD) {
					/* Not possible to substitute this here, but it may be possible in the DSE pass */
					state.enable_dse = true;
				}
				adjust.parameter |= (store.version << 8);
			}
		}
		break;
	}

	if (adjust.operation == DSGA_OP_STOP) {
		for (auto &it : state.temp_stores) {
			/* Check if some other variable is marked as a copy of permanent storage */
			if ((it.second.inference & VA2AIF_SINGLE_LOAD) && it.second.var_source.variable == 0x7C) {
				it.second.inference &= ~VA2AIF_SINGLE_LOAD;
			}
		}
	}

	if (IsExpensiveVariable(adjust.variable, info.scope_feature)) state.check_expensive_vars = true;

	auto get_prev_single_load = [&](bool *invert) -> const DeterministicSpriteGroupAdjust* {
		return GetVarAction2PreviousSingleLoadAdjust(group->adjusts, (int)group->adjusts.size() - 2, invert);
	};

	auto get_prev_single_store = [&](bool *invert) -> const DeterministicSpriteGroupAdjust* {
		return GetVarAction2PreviousSingleStoreAdjust(group->adjusts, (int)group->adjusts.size() - 2, invert);
	};

	if ((prev_inference & VA2AIF_SINGLE_LOAD) && adjust.operation == DSGA_OP_RST && adjust.variable != 0x1A && adjust.variable != 0x7D && adjust.variable != 0x7E) {
		/* See if this is a repeated load of a variable (not constant, temp store load or procedure call) */
		const DeterministicSpriteGroupAdjust *prev_load = get_prev_single_load(nullptr);
		if (prev_load != nullptr && MemCmpT<DeterministicSpriteGroupAdjust>(prev_load, &adjust) == 0) {
			group->adjusts.pop_back();
			state.inference = prev_inference;
			return;
		}
	}

	if ((prev_inference & VA2AIF_MUL_BOOL) && (non_const_var_inference & VA2AIF_MUL_BOOL) &&
			(adjust.operation == DSGA_OP_ADD || adjust.operation == DSGA_OP_OR || adjust.operation == DSGA_OP_XOR) &&
			adjust.variable == 0x7D && adjust.type == DSGA_TYPE_NONE && adjust.shift_num == 0 && adjust.and_mask == 0xFFFFFFFF) {
		if (TryMergeBoolMulCombineVarAction2Adjust(state, group->adjusts, (int)(group->adjusts.size() - 1))) {
			OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
			return;
		}
	}

	if (group->adjusts.size() >= 2 && adjust.operation == DSGA_OP_RST && adjust.variable != 0x7B) {
		/* See if any previous adjusts can be removed */
		bool removed = false;
		while (group->adjusts.size() >= 2) {
			const DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
			if (prev.variable != 0x7E && !IsEvalAdjustWithSideEffects(prev.operation)) {
				/* Delete useless operation */
				group->adjusts.erase(group->adjusts.end() - 2);
				removed = true;
			} else {
				break;
			}
		}
		if (removed) {
			try_restore_inference_backup(1);
			OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
			return;
		}
	}

	if (adjust.variable != 0x7E && IsEvalAdjustWithZeroLastValueAlwaysZero(adjust.operation)) {
		adjust.adjust_flags |= DSGAF_SKIP_ON_ZERO;
	}

	if ((prev_inference & VA2AIF_PREV_TERNARY) && adjust.variable == 0x1A && IsEvalAdjustUsableForConstantPropagation(adjust.operation)) {
		/* Propagate constant operation back into previous ternary */
		DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
		prev.and_mask = EvaluateDeterministicSpriteGroupAdjust(group->size, adjust, nullptr, prev.and_mask, UINT_MAX);
		prev.add_val = EvaluateDeterministicSpriteGroupAdjust(group->size, adjust, nullptr, prev.add_val, UINT_MAX);
		group->adjusts.pop_back();
		state.inference = prev_inference;
	} else if ((prev_inference & VA2AIF_HAVE_CONSTANT) && adjust.variable == 0x1A && IsEvalAdjustUsableForConstantPropagation(adjust.operation)) {
		/* Reduce constant operation on previous constant */
		replace_with_constant_load(EvaluateDeterministicSpriteGroupAdjust(group->size, adjust, nullptr, state.current_constant, UINT_MAX));
	} else if ((prev_inference & VA2AIF_HAVE_CONSTANT) && state.current_constant == 0 && (adjust.adjust_flags & DSGAF_SKIP_ON_ZERO)) {
		/* Remove operation which does nothing when applied to 0 */
		group->adjusts.pop_back();
		state.inference = prev_inference;
	} else if ((prev_inference & VA2AIF_HAVE_CONSTANT) && IsEvalAdjustOperationOnConstantEffectiveLoad(adjust.operation, state.current_constant)) {
		/* Convert operation to a load */
		DeterministicSpriteGroupAdjust current = group->adjusts.back();
		group->adjusts.pop_back();
		while (!group->adjusts.empty()) {
			const DeterministicSpriteGroupAdjust &prev = group->adjusts.back();
			if (prev.variable != 0x7E && !IsEvalAdjustWithSideEffects(prev.operation)) {
				/* Delete useless operation */
				group->adjusts.pop_back();
			} else {
				break;
			}
		}
		try_restore_inference_backup(0);
		current.operation = DSGA_OP_RST;
		current.adjust_flags = DSGAF_NONE;
		group->adjusts.push_back(current);
		OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
		return;
	} else if (adjust.variable == 0x7E || adjust.type != DSGA_TYPE_NONE) {
		/* Procedure call or complex adjustment */
		if (adjust.operation == DSGA_OP_STO) handle_unpredictable_temp_store();
		if (adjust.variable == 0x7E) {
			if (try_inline_procedure()) return;

			std::bitset<256> seen_stores;
			bool seen_unpredictable_store = false;
			bool seen_special_store = false;
			uint16_t seen_special_store_mask = 0;
			bool seen_perm_store = false;
			auto handle_proc_stores = y_combinator([&](auto handle_proc_stores, const SpriteGroup *sg) -> void {
				if (sg == nullptr) return;
				if (IsSimpleContainerSpriteGroup(sg)) {
					for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
						handle_proc_stores(group);
					}
				} else if (sg->type == SGT_DETERMINISTIC) {
					const DeterministicSpriteGroup *dsg = (const DeterministicSpriteGroup*)sg;
					for (const DeterministicSpriteGroupAdjust &adjust : dsg->adjusts) {
						if (adjust.variable == 0x7E) {
							handle_proc_stores(adjust.subroutine);
						}
						if (adjust.operation == DSGA_OP_STO) {
							if (adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0) {
								/* Temp store */
								if (adjust.and_mask < 0x100) {
									seen_stores.set(adjust.and_mask, true);
								} else {
									seen_special_store = true;
									if (adjust.and_mask >= 0x100 && adjust.and_mask < 0x110) SetBit(seen_special_store_mask, adjust.and_mask - 0x100);
								}
							} else {
								/* Unpredictable store */
								seen_unpredictable_store = true;
							}
						}
						if (adjust.operation == DSGA_OP_STO_NC) {
							if (adjust.divmod_val < 0x100) {
								seen_stores.set(adjust.divmod_val, true);
							} else {
								seen_special_store = true;
								if (adjust.divmod_val >= 0x100 && adjust.divmod_val < 0x110) SetBit(seen_special_store_mask, adjust.divmod_val - 0x100);
							}
						}
						if (adjust.operation == DSGA_OP_STOP) {
							seen_perm_store = true;
						}
					}
				}
			});

			auto handle_group = y_combinator([&](auto handle_group, const SpriteGroup *sg) -> void {
				if (sg == nullptr) return;
				if (IsSimpleContainerSpriteGroup(sg)) {
					for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
						handle_group(group);
					}
				} else if (sg->type == SGT_DETERMINISTIC) {
					VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(sg, false);
					if (var_tracking != nullptr) {
						std::bitset<256> bits = var_tracking->in;
						for (auto &it : state.temp_stores) {
							bits.set(it.first, false);
						}
						state.GetVarTracking(group)->in |= bits;
					}
					if (!state.seen_procedure_call && ((const DeterministicSpriteGroup*)sg)->dsg_flags & DSGF_REQUIRES_VAR1C) {
						group->dsg_flags |= DSGF_REQUIRES_VAR1C;
					}
					if (((const DeterministicSpriteGroup*)sg)->dsg_flags & DSGF_CB_HANDLER) {
						group->dsg_flags |= DSGF_CB_HANDLER;
					}
					handle_proc_stores(sg);
				}
			});
			handle_group(adjust.subroutine);

			if (seen_unpredictable_store) {
				reset_store_values();
			} else {
				for (auto &it : state.temp_stores) {
					if (seen_stores[it.first]) {
						it.second.inference = VA2AIF_NONE;
						it.second.version++;
					} else {
						/* See DSGA_OP_STO handler */
						if ((it.second.inference & VA2AIF_SINGLE_LOAD) && it.second.var_source.variable == 0x7D && seen_stores[it.second.var_source.parameter & 0xFF]) {
							it.second.inference &= ~VA2AIF_SINGLE_LOAD;
						}
						if (seen_special_store && (it.second.inference & VA2AIF_SINGLE_LOAD) && it.second.var_source.variable != 0x7D) {
							it.second.inference &= ~VA2AIF_SINGLE_LOAD;
						}

						/* See DSGA_OP_STOP handler */
						if (seen_perm_store && (it.second.inference & VA2AIF_SINGLE_LOAD) && it.second.var_source.variable == 0x7C) {
							it.second.inference &= ~VA2AIF_SINGLE_LOAD;
						}
					}
				}
			}
			state.special_register_store_mask &= ~seen_special_store_mask;

			state.seen_procedure_call = true;
		} else if (adjust.operation == DSGA_OP_RST) {
			state.inference = VA2AIF_SINGLE_LOAD;
		}
		if (IsConstantComparisonAdjustType(adjust.type)) {
			if (adjust.operation == DSGA_OP_RST) {
				state.inference |= VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
			} else if (adjust.operation == DSGA_OP_OR || adjust.operation == DSGA_OP_XOR || adjust.operation == DSGA_OP_AND) {
				state.inference |= (prev_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO));
			}
			if (adjust.operation == DSGA_OP_OR && (prev_inference & VA2AIF_ONE_OR_ZERO) && adjust.variable != 0x7E) {
				adjust.adjust_flags |= DSGAF_SKIP_ON_LSB_SET;
			}
			if (adjust.operation == DSGA_OP_MUL && adjust.variable != 0x7E) {
				state.inference |= VA2AIF_MUL_BOOL;
				adjust.adjust_flags |= DSGAF_JUMP_INS_HINT;
				group->dsg_flags |= DSGF_CHECK_INSERT_JUMP;
			}
			if (adjust.operation == DSGA_OP_MUL && (prev_inference & VA2AIF_ONE_OR_ZERO)) {
				state.inference |= VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
			}
		}
		if (adjust.operation == DSGA_OP_RST && adjust.type == DSGA_TYPE_MOD && adjust.divmod_val == 2) {
			/* Non-negative value % 2 implies VA2AIF_ONE_OR_ZERO */
			if ((uint64_t)adjust.and_mask + (uint64_t)adjust.add_val < (uint64_t)get_sign_bit()) {
				state.inference |= VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
			}
		}
	} else {
		if (adjust.and_mask == 0 && IsEvalAdjustWithZeroRemovable(adjust.operation)) {
			/* Delete useless zero operations */
			group->adjusts.pop_back();
			state.inference = prev_inference;
		} else if (adjust.and_mask == 0 && IsEvalAdjustWithZeroAlwaysZero(adjust.operation)) {
			/* Operation always returns 0, replace it and any useless prior operations */
			replace_with_constant_load(0);
		} else if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 1 && IsEvalAdjustWithOneRemovable(adjust.operation)) {
			/* Delete useless operations with a constant of 1 */
			group->adjusts.pop_back();
			state.inference = prev_inference;
		} else {
			if (adjust.variable == 0x7D && adjust.shift_num == 0 && adjust.and_mask == get_full_mask() && IsEvalAdjustOperationCommutative(adjust.operation) && group->adjusts.size() >= 2) {
				DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
				if (group->adjusts.size() >= 3 && prev.operation == DSGA_OP_RST) {
					const DeterministicSpriteGroupAdjust &prev2 = group->adjusts[group->adjusts.size() - 3];
					if (prev2.operation == DSGA_OP_STO && prev2.type == DSGA_TYPE_NONE && prev2.variable == 0x1A &&
							prev2.shift_num == 0 && prev2.and_mask == (adjust.parameter & 0xFF)) {
						/* Convert: store, load var, commutative op on stored --> (dead) store, commutative op var */
						prev.operation = adjust.operation;
						group->adjusts.pop_back();
						state.inference = non_const_var_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO | VA2AIF_MUL_BOOL);
						OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
						return;
					}
				}
			}
			switch (adjust.operation) {
				case DSGA_OP_ADD:
					if (adjust.variable == 0x7D && adjust.shift_num == 0 && adjust.and_mask == 0xFFFFFFFF &&
							(prev_inference & VA2AIF_ONE_OR_ZERO) && (non_const_var_inference & VA2AIF_ONE_OR_ZERO) &&
							((prev_inference & VA2AIF_MUL_BOOL) || (non_const_var_inference & VA2AIF_MUL_BOOL))) {
						/* See if this is a ternary operation where both cases result in bool */
						auto check_ternary_bool = [&]() -> bool {
							int store_index = GetVarAction2AdjustOfPreviousTempStoreSource(group->adjusts.data(), ((int)group->adjusts.size()) - 2, (adjust.parameter & 0xFF));
							if (store_index < 0) return false;

							DeterministicSpriteGroupAdjust synth_adjusts[2];
							VarAction2AdjustDescriptor found_adjusts[4] = {};

							if (prev_inference & VA2AIF_MUL_BOOL) {
								GetBoolMulSourceAdjusts(group->adjusts, ((int)group->adjusts.size()) - 2, adjust.parameter, synth_adjusts[0], found_adjusts[0], found_adjusts[1], nullptr);
							} else if (group->adjusts.size() >= 2) {
								found_adjusts[0] = { group->adjusts.data(), nullptr, ((int)group->adjusts.size()) - 2 };
							}
							if (!found_adjusts[0].IsValid() && !found_adjusts[1].IsValid()) return false;

							if (non_const_var_inference & VA2AIF_MUL_BOOL) {
								GetBoolMulSourceAdjusts(group->adjusts, store_index - 1, adjust.parameter, synth_adjusts[1], found_adjusts[2], found_adjusts[3], nullptr);
							} else if (store_index >= 1) {
								found_adjusts[2] = { group->adjusts.data(), nullptr, store_index - 1 };
							}
							if (!found_adjusts[2].IsValid() && !found_adjusts[3].IsValid()) return false;

							if (AreVarAction2AdjustsBooleanInverse(found_adjusts[0], found_adjusts[2]) != VA2ABIR_NO) return true;
							if (AreVarAction2AdjustsBooleanInverse(found_adjusts[0], found_adjusts[3]) != VA2ABIR_NO) return true;
							if (AreVarAction2AdjustsBooleanInverse(found_adjusts[1], found_adjusts[2]) != VA2ABIR_NO) return true;
							if (AreVarAction2AdjustsBooleanInverse(found_adjusts[1], found_adjusts[3]) != VA2ABIR_NO) return true;
							return false;
						};
						if (check_ternary_bool()) {
							state.inference |= VA2AIF_ONE_OR_ZERO | VA2AIF_SIGNED_NON_NEGATIVE;
						}
					}
					try_merge_with_previous();
					break;
				case DSGA_OP_SUB:
					if (adjust.variable == 0x7D && adjust.shift_num == 0 && adjust.and_mask == 0xFFFFFFFF && group->adjusts.size() >= 2) {
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (group->adjusts.size() >= 3 && prev.operation == DSGA_OP_RST) {
							const DeterministicSpriteGroupAdjust &prev2 = group->adjusts[group->adjusts.size() - 3];
							if (prev2.operation == DSGA_OP_STO && prev2.type == DSGA_TYPE_NONE && prev2.variable == 0x1A &&
									prev2.shift_num == 0 && prev2.and_mask == (adjust.parameter & 0xFF)) {
								/* Convert: store, load var, subtract stored --> (dead) store, reverse subtract var */
								prev.operation = DSGA_OP_RSUB;
								group->adjusts.pop_back();
								state.inference = non_const_var_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO);
								OptimiseVarAction2Adjust(state, info, group, group->adjusts.back());
								return;
							}
						}
					}
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 1 && group->adjusts.size() >= 2) {
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (prev.operation == DSGA_OP_SCMP) {
							state.inference |= VA2AIF_PREV_SCMP_DEC;
						}
					}
					try_merge_with_previous();
					break;
				case DSGA_OP_SMIN:
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 1 && group->adjusts.size() >= 2) {
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (prev.operation == DSGA_OP_SCMP) {
							prev.operation = DSGA_OP_SGE;
							group->adjusts.pop_back();
							state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
							break;
						}
						if (group->adjusts.size() >= 3 && prev.operation == DSGA_OP_XOR && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A &&
								prev.shift_num == 0 && prev.and_mask == 2) {
							DeterministicSpriteGroupAdjust &prev2 = group->adjusts[group->adjusts.size() - 3];
							if (prev2.operation == DSGA_OP_SCMP) {
								prev2.operation = DSGA_OP_SLE;
								group->adjusts.pop_back();
								group->adjusts.pop_back();
								state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
								break;
							}
						}
					}
					if (adjust.and_mask <= 1 && (prev_inference & VA2AIF_SIGNED_NON_NEGATIVE)) state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
					break;
				case DSGA_OP_SMAX:
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 0 && group->adjusts.size() >= 2) {
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (group->adjusts.size() >= 3 && prev.operation == DSGA_OP_SUB && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A &&
								prev.shift_num == 0 && prev.and_mask == 1) {
							DeterministicSpriteGroupAdjust &prev2 = group->adjusts[group->adjusts.size() - 3];
							if (prev2.operation == DSGA_OP_SCMP) {
								prev2.operation = DSGA_OP_SGT;
								group->adjusts.pop_back();
								group->adjusts.pop_back();
								state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
								break;
							}
						}
					}
					break;
				case DSGA_OP_UMIN:
					if (adjust.and_mask == 1) {
						if (prev_inference & VA2AIF_ONE_OR_ZERO) {
							/* Delete useless bool -> bool conversion */
							group->adjusts.pop_back();
							state.inference = prev_inference;
							break;
						} else {
							state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
							if (group->adjusts.size() >= 2) {
								DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
								if (prev.operation == DSGA_OP_RST && prev.type == DSGA_TYPE_NONE) {
									prev.type = DSGA_TYPE_NEQ;
									prev.add_val = 0;
									group->adjusts.pop_back();
									state.inference |= VA2AIF_SINGLE_LOAD;
								}
							}
						}
					}
					break;
				case DSGA_OP_AND:
					if ((prev_inference & VA2AIF_PREV_MASK_ADJUST) && adjust.variable == 0x1A && adjust.shift_num == 0 && group->adjusts.size() >= 2) {
						/* Propagate and into immediately prior variable read */
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						prev.and_mask &= adjust.and_mask;
						add_inferences_from_mask(prev.and_mask);
						state.inference |= VA2AIF_PREV_MASK_ADJUST;
						group->adjusts.pop_back();
						break;
					}
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 1 && group->adjusts.size() >= 2) {
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (prev.operation == DSGA_OP_SCMP || prev.operation == DSGA_OP_UCMP) {
							prev.operation = DSGA_OP_EQ;
							group->adjusts.pop_back();
							state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
							if (group->adjusts.size() >= 2) {
								DeterministicSpriteGroupAdjust &eq_adjust = group->adjusts[group->adjusts.size() - 1];
								DeterministicSpriteGroupAdjust &prev_op = group->adjusts[group->adjusts.size() - 2];
								if (eq_adjust.type == DSGA_TYPE_NONE && eq_adjust.variable == 0x1A &&
										prev_op.type == DSGA_TYPE_NONE && prev_op.operation == DSGA_OP_RST) {
									prev_op.type = DSGA_TYPE_EQ;
									prev_op.add_val = (0xFFFFFFFF >> eq_adjust.shift_num) & eq_adjust.and_mask;
									group->adjusts.pop_back();
									state.inference |= VA2AIF_SINGLE_LOAD;
								}
							}
							break;
						}
						if (prev_inference & VA2AIF_ONE_OR_ZERO) {
							/* Current value is already one or zero, remove this */
							group->adjusts.pop_back();
							state.inference = prev_inference;
							break;
						}
					}
					if (adjust.and_mask <= 1) {
						state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
					} else if ((adjust.and_mask & get_sign_bit()) == 0) {
						state.inference = VA2AIF_SIGNED_NON_NEGATIVE;
					}
					state.inference |= non_const_var_inference;
					if ((state.inference & VA2AIF_ONE_OR_ZERO) && (prev_inference & VA2AIF_ONE_OR_ZERO)) {
						adjust.adjust_flags |= DSGAF_JUMP_INS_HINT;
						group->dsg_flags |= DSGF_CHECK_INSERT_JUMP;
					}
					try_merge_with_previous();
					break;
				case DSGA_OP_OR:
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 1 && (prev_inference & VA2AIF_ONE_OR_ZERO)) {
						replace_with_constant_load(1);
						break;
					}
					if (adjust.and_mask <= 1) state.inference = prev_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO);
					state.inference |= prev_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO) & non_const_var_inference;
					if ((non_const_var_inference & VA2AIF_ONE_OR_ZERO) || (adjust.and_mask <= 1)) {
						adjust.adjust_flags |= DSGAF_SKIP_ON_LSB_SET;
						if (prev_inference & VA2AIF_ONE_OR_ZERO) {
							adjust.adjust_flags |= DSGAF_JUMP_INS_HINT;
							group->dsg_flags |= DSGF_CHECK_INSERT_JUMP;
						}
					}
					try_merge_with_previous();
					break;
				case DSGA_OP_XOR:
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && group->adjusts.size() >= 2) {
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (adjust.and_mask == 1) {
							if (IsEvalAdjustOperationRelationalComparison(prev.operation)) {
								prev.operation = InvertEvalAdjustRelationalComparisonOperation(prev.operation);
								group->adjusts.pop_back();
								state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
								break;
							}
							if (prev.operation == DSGA_OP_UMIN && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == 1) {
								prev.operation = DSGA_OP_TERNARY;
								prev.adjust_flags = DSGAF_NONE;
								prev.and_mask = 0;
								prev.add_val = 1;
								group->adjusts.pop_back();
								state.inference = VA2AIF_PREV_TERNARY;
								break;
							}
							if (prev.operation == DSGA_OP_RST && IsConstantComparisonAdjustType(prev.type)) {
								prev.type = InvertConstantComparisonAdjustType(prev.type);
								group->adjusts.pop_back();
								state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO | VA2AIF_SINGLE_LOAD;
								break;
							}
							if (prev.operation == DSGA_OP_OR && (IsConstantComparisonAdjustType(prev.type) || (prev.type == DSGA_TYPE_NONE && (prev.adjust_flags & DSGAF_SKIP_ON_LSB_SET))) && group->adjusts.size() >= 3) {
								DeterministicSpriteGroupAdjust &prev2 = group->adjusts[group->adjusts.size() - 3];
								bool found = false;
								if (IsEvalAdjustOperationRelationalComparison(prev2.operation)) {
									prev2.operation = InvertEvalAdjustRelationalComparisonOperation(prev2.operation);
									found = true;
								} else if (prev2.operation == DSGA_OP_RST && IsConstantComparisonAdjustType(prev2.type)) {
									prev2.type = InvertConstantComparisonAdjustType(prev2.type);
									found = true;
								}
								if (found) {
									if (prev.type == DSGA_TYPE_NONE) {
										prev.type = DSGA_TYPE_EQ;
										prev.add_val = 0;
									} else {
										prev.type = InvertConstantComparisonAdjustType(prev.type);
									}
									prev.operation = DSGA_OP_AND;
									prev.adjust_flags = DSGAF_SKIP_ON_ZERO;
									group->adjusts.pop_back();
									state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
									break;
								}
							}
						}
						if (prev.operation == DSGA_OP_OR && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == adjust.and_mask) {
							prev.operation = DSGA_OP_AND;
							prev.and_mask = ~prev.and_mask;
							prev.adjust_flags = DSGAF_NONE;
							group->adjusts.pop_back();
							break;
						}
					}
					if (adjust.and_mask <= 1) state.inference = prev_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO);
					state.inference |= prev_inference & (VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO) & non_const_var_inference;
					if (adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask == 1) {
						/* Single load tracking can handle bool inverts */
						state.inference |= (prev_inference & VA2AIF_SINGLE_LOAD);
					}
					if (info.scope_feature == GSF_OBJECTS && group->adjusts.size() >= 2) {
						auto check_slope_vars = [](const DeterministicSpriteGroupAdjust &a, const DeterministicSpriteGroupAdjust &b) -> bool {
							return a.variable == A2VRI_OBJECT_FOUNDATION_SLOPE_CHANGE && a.shift_num == 0 && (a.and_mask & 0x1F) == 0x1F &&
									b.variable == 0x41 && b.shift_num == 8 && b.and_mask == 0x1F;
						};
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (prev.operation == DSGA_OP_RST && prev.type == DSGA_TYPE_NONE &&
								(check_slope_vars(adjust, prev) || check_slope_vars(prev, adjust))) {
							prev.variable = A2VRI_OBJECT_FOUNDATION_SLOPE;
							prev.shift_num = 0;
							prev.and_mask = 0x1F;
							group->adjusts.pop_back();
							state.inference |= VA2AIF_PREV_MASK_ADJUST | VA2AIF_SINGLE_LOAD;
							break;
						}
					}
					try_merge_with_previous();
					break;
				case DSGA_OP_MUL: {
					if ((prev_inference & VA2AIF_ONE_OR_ZERO) && adjust.variable == 0x1A && adjust.shift_num == 0 && group->adjusts.size() >= 2) {
						/* Found a ternary operator */
						adjust.operation = DSGA_OP_TERNARY;
						adjust.adjust_flags = DSGAF_NONE;
						while (group->adjusts.size() > 1) {
							/* Merge with previous if applicable */
							const DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
							if (prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == 1) {
								if (prev.operation == DSGA_OP_XOR) {
									DeterministicSpriteGroupAdjust current = group->adjusts.back();
									group->adjusts.pop_back();
									group->adjusts.pop_back();
									std::swap(current.and_mask, current.add_val);
									group->adjusts.push_back(current);
									continue;
								} else if (prev.operation == DSGA_OP_SMIN || prev.operation == DSGA_OP_UMIN) {
									DeterministicSpriteGroupAdjust current = group->adjusts.back();
									group->adjusts.pop_back();
									group->adjusts.pop_back();
									group->adjusts.push_back(current);
								}
							}
							break;
						}
						if (group->adjusts.size() > 1) {
							/* Remove redundant comparison with 0 if applicable */
							const DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
							if (prev.type == DSGA_TYPE_NONE && prev.operation == DSGA_OP_EQ && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == 0) {
								DeterministicSpriteGroupAdjust current = group->adjusts.back();
								group->adjusts.pop_back();
								group->adjusts.pop_back();
								std::swap(current.and_mask, current.add_val);
								group->adjusts.push_back(current);
							}
						}
						state.inference = VA2AIF_PREV_TERNARY;
						break;
					}
					if ((prev_inference & VA2AIF_PREV_SCMP_DEC) && group->adjusts.size() >= 4 && adjust.variable == 0x7D && adjust.shift_num == 0 && adjust.and_mask == 0xFFFFFFFF) {
						const DeterministicSpriteGroupAdjust &adj1 = group->adjusts[group->adjusts.size() - 4];
						const DeterministicSpriteGroupAdjust &adj2 = group->adjusts[group->adjusts.size() - 3];
						const DeterministicSpriteGroupAdjust &adj3 = group->adjusts[group->adjusts.size() - 2];
						auto is_expected_op = [](const DeterministicSpriteGroupAdjust &adj, DeterministicSpriteGroupAdjustOperation op, uint32_t value) -> bool {
							return adj.operation == op && adj.type == DSGA_TYPE_NONE && adj.variable == 0x1A && adj.shift_num == 0 && adj.and_mask == value;
						};
						if (is_expected_op(adj1, DSGA_OP_STO, (adjust.parameter & 0xFF)) &&
								is_expected_op(adj2, DSGA_OP_SCMP, 0) &&
								is_expected_op(adj3, DSGA_OP_SUB, 1)) {
							group->adjusts.pop_back();
							group->adjusts.pop_back();
							group->adjusts.back().operation = DSGA_OP_ABS;
							state.inference |= VA2AIF_SIGNED_NON_NEGATIVE;
							break;
						}
					}
					uint32_t sign_bit = (1 << ((info.varsize * 8) - 1));
					if ((prev_inference & VA2AIF_PREV_MASK_ADJUST) && (prev_inference & VA2AIF_SIGNED_NON_NEGATIVE) && adjust.variable == 0x1A && adjust.shift_num == 0 && (adjust.and_mask & sign_bit) == 0) {
						/* Determine whether the result will be always non-negative */
						if (((uint64_t)group->adjusts[group->adjusts.size() - 2].and_mask) * ((uint64_t)adjust.and_mask) < ((uint64_t)sign_bit)) {
							state.inference |= VA2AIF_SIGNED_NON_NEGATIVE;
						}
					}
					if ((prev_inference & VA2AIF_ONE_OR_ZERO) || (non_const_var_inference & VA2AIF_ONE_OR_ZERO)) {
						state.inference |= VA2AIF_MUL_BOOL;
					}
					if ((prev_inference & VA2AIF_ONE_OR_ZERO) && (non_const_var_inference & VA2AIF_ONE_OR_ZERO)) {
						state.inference |= VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO;
					}
					if (non_const_var_inference & VA2AIF_ONE_OR_ZERO) {
						adjust.adjust_flags |= DSGAF_JUMP_INS_HINT;
						group->dsg_flags |= DSGF_CHECK_INSERT_JUMP;
					}
					break;
				}
				case DSGA_OP_SCMP:
				case DSGA_OP_UCMP:
					state.inference = VA2AIF_SIGNED_NON_NEGATIVE;
					break;
				case DSGA_OP_STOP:
					state.inference = prev_inference & (~VA2AIF_PREV_MASK);
					break;
				case DSGA_OP_STO:
					state.inference = prev_inference & (~VA2AIF_PREV_MASK);
					if (adjust.variable == 0x1A && adjust.shift_num == 0) {
						state.inference |= VA2AIF_PREV_STORE_TMP;
						if (adjust.and_mask < 0x100) {
							bool invert_store = false;
							const DeterministicSpriteGroupAdjust *prev_store = get_prev_single_store((prev_inference & VA2AIF_ONE_OR_ZERO) ? &invert_store : nullptr);
							if (prev_store != nullptr && prev_store->and_mask == adjust.and_mask) {
								if (invert_store) {
									/* Inverted store of self, don't try to handle this */
									invert_store = false;
									prev_store = nullptr;
								} else {
									/* Duplicate store, don't make any changes */
									break;
								}
							}

							for (auto &it : state.temp_stores) {
								/* Check if some other variable is marked as a copy of the one we are overwriting */
								if ((it.second.inference & VA2AIF_SINGLE_LOAD) && it.second.var_source.variable == 0x7D && (it.second.var_source.parameter & 0xFF) == adjust.and_mask) {
									it.second.inference &= ~VA2AIF_SINGLE_LOAD;
								}
							}
							VarAction2TempStoreInference &store = state.temp_stores[adjust.and_mask];
							if (store.version == 0) {
								/* New store */
								store.version = state.default_variable_version + 1;
							} else {
								/* Updating previous store */
								store.version++;
							}
							store.inference = prev_inference & (~VA2AIF_PREV_MASK);
							store.store_constant = state.current_constant;

							if (prev_store != nullptr) {
								/* This store is a clone of the previous store, or inverted clone of the previous store (bool) */
								store.inference |= VA2AIF_SINGLE_LOAD;
								store.var_source.type = (invert_store ? DSGA_TYPE_EQ : DSGA_TYPE_NONE);
								store.var_source.variable = 0x7D;
								store.var_source.shift_num = 0;
								store.var_source.parameter = prev_store->and_mask | (state.temp_stores[prev_store->and_mask].version << 8);
								store.var_source.and_mask = 0xFFFFFFFF;
								store.var_source.add_val = 0;
								store.var_source.divmod_val = 0;
								break;
							}

							if (prev_inference & VA2AIF_SINGLE_LOAD) {
								bool invert = false;
								const DeterministicSpriteGroupAdjust *prev_load = get_prev_single_load(&invert);
								if (prev_load != nullptr && (!invert || IsConstantComparisonAdjustType(prev_load->type))) {
									if (prev_load->variable == 0x7D && (prev_load->parameter & 0xFF) == adjust.and_mask) {
										/* Store to same variable as previous load, do not mark store as clone of itself */
										break;
									}
									store.inference |= VA2AIF_SINGLE_LOAD;
									store.var_source.type = prev_load->type;
									if (invert) store.var_source.type = InvertConstantComparisonAdjustType(store.var_source.type);
									store.var_source.variable = prev_load->variable;
									store.var_source.shift_num = prev_load->shift_num;
									store.var_source.parameter = prev_load->parameter;
									store.var_source.and_mask = prev_load->and_mask;
									store.var_source.add_val = prev_load->add_val;
									store.var_source.divmod_val = prev_load->divmod_val;
									break;
								}
							}
						} else {
							if (adjust.and_mask >= 0x100 && adjust.and_mask < 0x110) {
								uint idx = adjust.and_mask - 0x100;
								if (prev_inference & VA2AIF_HAVE_CONSTANT) {
									if (HasBit(state.special_register_store_mask, idx) && state.special_register_store_values[idx] == state.current_constant) {
										/* Remove redundant special store of same constant value */
										group->adjusts.pop_back();
										state.inference = prev_inference;
										break;
									}
									SetBit(state.special_register_store_mask, idx);
									state.special_register_store_values[idx] = state.current_constant;
								} else {
									ClrBit(state.special_register_store_mask, idx);
								}
							}

							/* Store to special register, this can change the result of future variable loads for some variables.
							 * Assume all variables except temp storage for now.
							 */
							for (auto &it : state.temp_stores) {
								if (it.second.inference & VA2AIF_SINGLE_LOAD && it.second.var_source.variable != 0x7D) {
									it.second.inference &= ~VA2AIF_SINGLE_LOAD;
								}
							}
						}
					} else {
						handle_unpredictable_temp_store();
					}
					break;
				case DSGA_OP_RST:
					if ((prev_inference & VA2AIF_PREV_STORE_TMP) && adjust.variable == 0x7D && adjust.shift_num == 0 && adjust.and_mask == get_full_mask() && group->adjusts.size() >= 2) {
						const DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (prev.type == DSGA_TYPE_NONE && prev.operation == DSGA_OP_STO && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == (adjust.parameter & 0xFF)) {
							/* Redundant load from temp store after store to temp store */
							group->adjusts.pop_back();
							state.inference = prev_inference;
							break;
						}
					}
					add_inferences_from_mask(adjust.and_mask);
					state.inference |= VA2AIF_PREV_MASK_ADJUST | VA2AIF_SINGLE_LOAD;
					if (adjust.variable == 0x1A || adjust.and_mask == 0) {
						replace_with_constant_load(EvaluateDeterministicSpriteGroupAdjust(group->size, adjust, nullptr, 0, UINT_MAX));
					}
					break;
				case DSGA_OP_SHR:
				case DSGA_OP_SAR:
					if ((adjust.operation == DSGA_OP_SHR || (prev_inference & VA2AIF_SIGNED_NON_NEGATIVE)) &&
							((prev_inference & VA2AIF_PREV_MASK_ADJUST) && adjust.variable == 0x1A && adjust.shift_num == 0 && group->adjusts.size() >= 2)) {
						/* Propagate shift right into immediately prior variable read */
						DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
						if (prev.shift_num + adjust.and_mask < 32) {
							prev.shift_num += adjust.and_mask;
							prev.and_mask >>= adjust.and_mask;
							add_inferences_from_mask(prev.and_mask);
							state.inference |= VA2AIF_PREV_MASK_ADJUST;
							group->adjusts.pop_back();
							break;
						}
					}
					break;
				case DSGA_OP_SDIV:
					if ((prev_inference & VA2AIF_SIGNED_NON_NEGATIVE) && adjust.variable == 0x1A && adjust.shift_num == 0 && HasExactlyOneBit(adjust.and_mask)) {
						uint shift_count = FindFirstBit(adjust.and_mask);
						if (group->adjusts.size() >= 3 && shift_count == 16 && info.varsize == 4 && (info.scope_feature == GSF_TRAINS || info.scope_feature == GSF_ROADVEHICLES || info.scope_feature == GSF_SHIPS)) {
							const DeterministicSpriteGroupAdjust &prev = group->adjusts[group->adjusts.size() - 2];
							DeterministicSpriteGroupAdjust &prev2 = group->adjusts[group->adjusts.size() - 3];
							if (prev.operation == DSGA_OP_MUL && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask <= 0xFFFF &&
									(prev2.operation == DSGA_OP_RST || group->adjusts.size() == 3) && prev2.type == DSGA_TYPE_NONE && prev2.variable == 0xB4 && prev2.shift_num == 0 && prev2.and_mask == 0xFFFF) {
								/* Replace with scaled current speed */
								prev2.variable = A2VRI_VEHICLE_CURRENT_SPEED_SCALED;
								prev2.parameter = prev.and_mask;
								group->adjusts.pop_back();
								group->adjusts.pop_back();
								state.inference = VA2AIF_SIGNED_NON_NEGATIVE;
								break;
							}
						}
						/* Convert to a shift */
						adjust.operation = DSGA_OP_SHR;
						adjust.and_mask = shift_count;
						state.inference = VA2AIF_SIGNED_NON_NEGATIVE;
					}
					break;
				default:
					break;
			}
		}
	}
}

static bool CheckDeterministicSpriteGroupOutputVarBits(const DeterministicSpriteGroup *group, std::bitset<256> bits, std::bitset<256> *store_input_bits, bool quick_exit);

struct CheckDeterministicSpriteGroupOutputVarBitsProcedureHandler {
	std::bitset<256> &bits;             // Needed output bits
	const std::bitset<256> output_bits; // Snapshots of needed output bits at construction

	CheckDeterministicSpriteGroupOutputVarBitsProcedureHandler(std::bitset<256> &bits)
			: bits(bits), output_bits(bits) {}

	/* return true if non-handled leaf node found */
	bool ProcessGroup(const SpriteGroup *sg, std::bitset<256> *input_bits, bool top_level)
	{
		if (sg == nullptr) return true;
		if (IsSimpleContainerSpriteGroup(sg)) {
			bool non_handled = false;
			uint count = 0;
			for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
				count++;
				non_handled |= this->ProcessGroup(group, input_bits, top_level);
			}
			return non_handled || count == 0;
		} else if (sg->type == SGT_DETERMINISTIC) {
			const DeterministicSpriteGroup *sub = static_cast<const DeterministicSpriteGroup *>(sg);

			std::bitset<256> child_input_bits;

			bool is_leaf_node = false;
			if (sub->calculated_result) {
				is_leaf_node = true;
			} else {
				is_leaf_node |= this->ProcessGroup(sub->default_group, &child_input_bits, false);
				for (const auto &range : sub->ranges) {
					is_leaf_node |= this->ProcessGroup(range.group, &child_input_bits, false);
				}
			}

			VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(sub, true);
			std::bitset<256> new_proc_call_out = (is_leaf_node ? this->output_bits : child_input_bits) | var_tracking->proc_call_out;
			if (new_proc_call_out != var_tracking->proc_call_out) {
				std::bitset<256> old_total = var_tracking->out | var_tracking->proc_call_out;
				std::bitset<256> new_total = var_tracking->out | new_proc_call_out;
				var_tracking->proc_call_out = new_proc_call_out;
				if (old_total != new_total) {
					CheckDeterministicSpriteGroupOutputVarBits(sub, new_total, &(var_tracking->proc_call_in), false);
				}
			}
			if (input_bits != nullptr) (*input_bits) |= var_tracking->proc_call_in;
			if (top_level) this->bits |= var_tracking->in;
			return false;
		} else {
			return true;
		}
	}
};

static bool CheckDeterministicSpriteGroupOutputVarBits(const DeterministicSpriteGroup *group, std::bitset<256> bits, std::bitset<256> *store_input_bits, bool quick_exit)
{
	bool dse = false;
	for (int i = (int)group->adjusts.size() - 1; i >= 0; i--) {
		const DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];
		if (adjust.operation == DSGA_OP_STO) {
			if (adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask < 0x100) {
				/* Predictable store */
				if (!bits[adjust.and_mask]) {
					/* Possibly redundant store */
					dse = true;
					if (quick_exit) break;
				}
				bits.set(adjust.and_mask, false);
			}
		}
		if (adjust.operation == DSGA_OP_STO_NC && adjust.divmod_val < 0x100) {
			if (!bits[adjust.divmod_val]) {
				/* Possibly redundant store */
				dse = true;
				if (quick_exit) break;
			}
			bits.set(adjust.divmod_val, false);
		}
		if (adjust.variable == 0x7B && adjust.parameter == 0x7D) {
			/* Unpredictable load */
			bits.set();
		}
		if (adjust.variable == 0x7D) {
			bits.set(adjust.parameter & 0xFF, true);
		}
		if (adjust.variable == 0x7E) {
			/* procedure call */
			CheckDeterministicSpriteGroupOutputVarBitsProcedureHandler proc_handler(bits);
			proc_handler.ProcessGroup(adjust.subroutine, nullptr, true);
		}
	}
	if (store_input_bits != nullptr) *store_input_bits = bits;
	return dse;
}

static bool OptimiseVarAction2DeterministicSpriteGroupExpensiveVarsInner(DeterministicSpriteGroup *group, const GrfSpecFeature scope_feature, VarAction2GroupVariableTracking *var_tracking)
{
	btree::btree_map<uint64_t, uint32_t> seen_expensive_variables;
	std::bitset<256> usable_vars;
	if (var_tracking != nullptr) {
		usable_vars = ~(var_tracking->out | var_tracking->proc_call_out);
	} else {
		usable_vars.set();
	}
	uint16_t target_var = 0;
	uint32_t target_param = 0;
	auto found_target = [&]() -> bool {
		for (auto &iter : seen_expensive_variables) {
			if (iter.second >= 2) {
				target_var = iter.first >> 32;
				target_param = iter.first & 0xFFFFFFFF;
				return true;
			}
		}
		return false;
	};
	auto do_replacements = [&](int start, int end) {
		std::bitset<256> mask(UINT64_MAX);
		std::bitset<256> cur = usable_vars;
		uint8_t bit = 0;
		while (true) {
			uint64_t t = (cur & mask).to_ullong();
			if (t != 0) {
				bit += FindFirstBit(t);
				break;
			}
			cur >>= 64;
			bit += 64;
		}
		int insert_pos = start;
		uint32_t and_mask = 0;
		uint condition_depth = 0;
		bool seen_first = false;
		int last_unused_jump = -1;
		for (int j = end; j >= start; j--) {
			DeterministicSpriteGroupAdjust &adjust = group->adjusts[j];
			if (seen_first && IsEvalAdjustJumpOperation(adjust.operation)) {
				if (condition_depth > 0) {
					/* Do not insert the STO_NC inside a conditional block when it is also needed outside the block */
					condition_depth--;
					insert_pos = j;
				} else {
					last_unused_jump = j;
				}
			}
			if (seen_first && adjust.adjust_flags & DSGAF_END_BLOCK) condition_depth += adjust.jump;
			if (adjust.variable == target_var && adjust.parameter == target_param) {
				and_mask |= adjust.and_mask << adjust.shift_num;
				adjust.variable = 0x7D;
				adjust.parameter = bit;
				insert_pos = j;
				seen_first = true;
			}
		}
		DeterministicSpriteGroupAdjust load = {};
		load.operation = DSGA_OP_STO_NC;
		load.type = DSGA_TYPE_NONE;
		load.variable = target_var;
		load.shift_num = 0;
		load.parameter = target_param;
		load.and_mask = and_mask;
		load.divmod_val = bit;
		if (group->adjusts[insert_pos].adjust_flags & DSGAF_SKIP_ON_ZERO) {
			for (int j = insert_pos + 1; j <= end; j++) {
				if (group->adjusts[j].adjust_flags & DSGAF_SKIP_ON_ZERO) continue;
				if (group->adjusts[j].operation == DSGA_OP_JZ_LV && last_unused_jump == j) {
					/* The variable is never actually read if last_value is 0 at this point */
					load.adjust_flags |= DSGAF_SKIP_ON_ZERO;
				}
				break;
			}
		}
		group->adjusts.insert(group->adjusts.begin() + insert_pos, load);
	};

	int i = (int)group->adjusts.size() - 1;
	int end = i;
	while (i >= 0) {
		const DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];
		if (adjust.operation == DSGA_OP_STO && (adjust.type != DSGA_TYPE_NONE || adjust.variable != 0x1A || adjust.shift_num != 0)) return false;
		if (adjust.variable == 0x7B && adjust.parameter == 0x7D) return false;
		if (adjust.operation == DSGA_OP_STO_NC && adjust.divmod_val < 0x100) {
			usable_vars.set(adjust.divmod_val, false);
		}
		if (adjust.operation == DSGA_OP_STO && adjust.and_mask < 0x100) {
			usable_vars.set(adjust.and_mask, false);
		} else if (adjust.variable == 0x7D) {
			if (adjust.parameter < 0x100) usable_vars.set(adjust.parameter, false);
		} else if (IsExpensiveVariable(adjust.variable, scope_feature)) {
			seen_expensive_variables[(((uint64_t)adjust.variable) << 32) | adjust.parameter]++;
		}
		if (adjust.variable == 0x7E || (adjust.operation == DSGA_OP_STO && adjust.and_mask >= 0x100) || (adjust.operation == DSGA_OP_STO_NC && adjust.divmod_val >= 0x100)) {
			/* Can't cross this barrier, stop here */
			if (usable_vars.none()) return false;
			if (found_target()) {
				do_replacements(i + 1, end);
				return true;
			}
			seen_expensive_variables.clear();
			end = i - 1;
			if (adjust.variable == 0x7E) {
				auto handle_group = y_combinator([&](auto handle_group, const SpriteGroup *sg) -> void {
					if (sg != nullptr && sg->type == SGT_DETERMINISTIC) {
						VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(sg, false);
						if (var_tracking != nullptr) usable_vars &= ~var_tracking->in;
					}
					if (IsSimpleContainerSpriteGroup(sg)) {
						for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
							handle_group(group);
						}
					}
				});
				handle_group(adjust.subroutine);
			}
		}
		i--;
	}
	if (usable_vars.none()) return false;
	if (found_target()) {
		do_replacements(0, end);
		return true;
	}

	return false;
}

static void OptimiseVarAction2DeterministicSpriteGroupExpensiveVars(DeterministicSpriteGroup *group, const GrfSpecFeature scope_feature)
{
	VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(group, false);
	while (OptimiseVarAction2DeterministicSpriteGroupExpensiveVarsInner(group, scope_feature, var_tracking)) {}
}

static void OptimiseVarAction2DeterministicSpriteGroupSimplifyStores(DeterministicSpriteGroup *group)
{
	if (HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_SIMPLIFY_STORES)) return;

	int src_adjust = -1;
	bool is_constant = false;
	for (size_t i = 0; i < group->adjusts.size(); i++) {
		auto acceptable_store = [](const DeterministicSpriteGroupAdjust &adjust) -> bool {
			return adjust.type == DSGA_TYPE_NONE && adjust.operation == DSGA_OP_STO && adjust.variable == 0x1A && adjust.shift_num == 0;
		};

		DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];

		if ((adjust.type == DSGA_TYPE_NONE || IsConstantComparisonAdjustType(adjust.type)) && adjust.operation == DSGA_OP_RST && adjust.variable != 0x7E) {
			src_adjust = (int)i;
			is_constant = (adjust.variable == 0x1A);
			continue;
		}

		if (src_adjust >= 0 && acceptable_store(adjust)) {
			bool ok = false;
			bool more_stores = false;
			size_t j = i;
			while (true) {
				j++;
				if (j == group->adjusts.size()) {
					ok = !group->calculated_result && group->ranges.empty();
					break;
				}
				const DeterministicSpriteGroupAdjust &next = group->adjusts[j];
				if (next.operation == DSGA_OP_RST) {
					ok = (next.variable != 0x7B);
					break;
				}
				if (is_constant && next.operation == DSGA_OP_STO_NC) {
					continue;
				}
				if (is_constant && acceptable_store(next)) {
					more_stores = true;
					continue;
				}
				break;
			}
			if (ok) {
				const DeterministicSpriteGroupAdjust &src = group->adjusts[src_adjust];
				adjust.operation = DSGA_OP_STO_NC;
				adjust.type = src.type;
				adjust.adjust_flags = DSGAF_NONE;
				adjust.divmod_val = adjust.and_mask;
				adjust.add_val = src.add_val;
				adjust.variable = src.variable;
				adjust.parameter = src.parameter;
				adjust.shift_num = src.shift_num;
				adjust.and_mask = src.and_mask;
				if (more_stores) {
					continue;
				}
				group->adjusts.erase(group->adjusts.begin() + src_adjust);
				i--;
			}
		}

		src_adjust = -1;
	}
}

static void OptimiseVarAction2DeterministicSpriteGroupAdjustOrdering(DeterministicSpriteGroup *group, const GrfSpecFeature scope_feature)
{
	if (HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_ADJUST_ORDERING)) return;

	auto acceptable_variable = [](uint16_t variable) -> bool {
		return variable != 0x7E && variable != 0x7B;
	};

	auto get_variable_expense = [&](uint16_t variable) -> int {
		if (variable == 0x1A) return -15;
		if (IsVariableVeryCheap(variable, scope_feature)) return -10;
		if (variable == 0x7D || variable == 0x7C) return -5;
		if (IsExpensiveVariable(variable, scope_feature)) return 10;
		return 0;
	};

	for (size_t i = 0; i + 1 < group->adjusts.size(); i++) {
		DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];

		if (adjust.operation == DSGA_OP_RST && acceptable_variable(adjust.variable)) {
			DeterministicSpriteGroupAdjustOperation operation = group->adjusts[i + 1].operation;
			const size_t start = i;
			size_t end = i;
			if (IsEvalAdjustWithZeroLastValueAlwaysZero(operation) && IsEvalAdjustOperationCommutative(operation)) {
				for (size_t j = start + 1; j < group->adjusts.size(); j++) {
					DeterministicSpriteGroupAdjust &next = group->adjusts[j];
					if (next.operation == operation && acceptable_variable(next.variable) && (next.adjust_flags & DSGAF_SKIP_ON_ZERO)) {
						end = j;
					} else {
						break;
					}
				}
			}
			if (end != start) {
				adjust.operation = operation;
				adjust.adjust_flags |= DSGAF_SKIP_ON_ZERO;

				/* Sort so that the least expensive comes first */
				std::stable_sort(group->adjusts.begin() + start, group->adjusts.begin() + end + 1, [&](const DeterministicSpriteGroupAdjust &a, const DeterministicSpriteGroupAdjust &b) -> bool {
					return get_variable_expense(a.variable) < get_variable_expense(b.variable);
				});

				adjust.operation = DSGA_OP_RST;
				adjust.adjust_flags &= ~(DSGAF_SKIP_ON_ZERO | DSGAF_JUMP_INS_HINT);
			}
		}
	}
}

static bool TryCombineTempStoreLoadWithStoreSourceAdjust(DeterministicSpriteGroupAdjust &target, const DeterministicSpriteGroupAdjust *var_src, bool inverted)
{
	DeterministicSpriteGroupAdjustType var_src_type = var_src->type;
	if (inverted) {
		switch (var_src_type) {
			case DSGA_TYPE_EQ:
				var_src_type = DSGA_TYPE_NEQ;
				break;
			case DSGA_TYPE_NEQ:
				var_src_type = DSGA_TYPE_EQ;
				break;
			default:
				/* Don't try to handle this case */
				return false;
		}
	}
	if (target.type == DSGA_TYPE_NONE && target.shift_num == 0 && (target.and_mask == 0xFFFFFFFF || (IsConstantComparisonAdjustType(var_src_type) && (target.and_mask & 1)))) {
		target.type = var_src_type;
		target.variable = var_src->variable;
		target.shift_num = var_src->shift_num;
		target.parameter = var_src->parameter;
		target.and_mask = var_src->and_mask;
		target.add_val = var_src->add_val;
		target.divmod_val = var_src->divmod_val;
		return true;
	} else if (IsConstantComparisonAdjustType(target.type) && target.shift_num == 0 && (target.and_mask & 1) && target.add_val == 0 &&
			IsConstantComparisonAdjustType(var_src_type)) {
		/* DSGA_TYPE_EQ/NEQ on target are OK if add_val is 0 because this is a boolean invert/convert of the incoming DSGA_TYPE_EQ/NEQ */
		if (target.type == DSGA_TYPE_EQ) {
			target.type = InvertConstantComparisonAdjustType(var_src_type);
		} else {
			target.type = var_src_type;
		}
		target.variable = var_src->variable;
		target.shift_num = var_src->shift_num;
		target.parameter = var_src->parameter;
		target.and_mask = var_src->and_mask;
		target.add_val = var_src->add_val;
		target.divmod_val = var_src->divmod_val;
		return true;
	} else if (var_src_type == DSGA_TYPE_NONE && (target.shift_num + var_src->shift_num) < 32) {
		target.variable = var_src->variable;
		target.parameter = var_src->parameter;
		target.and_mask &= var_src->and_mask >> target.shift_num;
		target.shift_num += var_src->shift_num;
		return true;
	}
	return false;
}

static VarAction2ProcedureAnnotation *OptimiseVarAction2GetFilledProcedureAnnotation(const SpriteGroup *group)
{
	VarAction2ProcedureAnnotation *anno;
	bool is_new;
	std::tie(anno, is_new) = _cur.GetVarAction2ProcedureAnnotation(group);
	if (is_new) {
		auto handle_group_contents = y_combinator([&](auto handle_group_contents, const SpriteGroup *sg) -> void {
			if (sg == nullptr || anno->unskippable) return;
			if (IsSimpleContainerSpriteGroup(sg)) {
				for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
					handle_group_contents(group);
				}

				/* Don't try to skip over procedure calls to randomised groups */
				anno->unskippable = true;
			} else if (sg->type == SGT_DETERMINISTIC) {
				const DeterministicSpriteGroup *dsg = static_cast<const DeterministicSpriteGroup *>(sg);

				for (const DeterministicSpriteGroupAdjust &adjust : dsg->adjusts) {
					/* Don't try to skip over: unpredictable stores, non-constant special stores, or permanent stores */
					if (adjust.operation == DSGA_OP_STO && (adjust.type != DSGA_TYPE_NONE || adjust.variable != 0x1A || adjust.shift_num != 0 || adjust.and_mask >= 0x100)) {
						anno->unskippable = true;
						return;
					}
					if (adjust.operation == DSGA_OP_STO_NC && adjust.divmod_val >= 0x100) {
						if (adjust.divmod_val < 0x110 && adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0) {
							/* Storing a constant */
							anno->special_register_values[adjust.divmod_val - 0x100] = adjust.and_mask;
							SetBit(anno->special_register_mask, adjust.divmod_val - 0x100);
						} else {
							anno->unskippable = true;
						}
						return;
					}
					if (adjust.operation == DSGA_OP_STOP) {
						anno->unskippable = true;
						return;
					}
					if (adjust.variable == 0x7E) {
						handle_group_contents(adjust.subroutine);
					}

					if (adjust.operation == DSGA_OP_STO) anno->stores.set(adjust.and_mask, true);
					if (adjust.operation == DSGA_OP_STO_NC) anno->stores.set(adjust.divmod_val, true);
				}

				if (!dsg->calculated_result) {
					handle_group_contents(dsg->default_group);
					for (const auto &range : dsg->ranges) {
						handle_group_contents(range.group);
					}
				}
			}
		});
		handle_group_contents(group);
	}
	return anno;
}

static uint OptimiseVarAction2InsertSpecialStoreOps(DeterministicSpriteGroup *group, uint offset, uint32_t values[16], uint16_t mask)
{
	uint added = 0;
	for (uint8_t bit : SetBitIterator(mask)) {
		bool skip = false;
		for (size_t i = offset; i < group->adjusts.size(); i++) {
			const DeterministicSpriteGroupAdjust &next = group->adjusts[i];
			if (next.operation == DSGA_OP_STO_NC && next.divmod_val == 0x100u + bit) {
				skip = true;
				break;
			}
			if (next.operation == DSGA_OP_STO && next.variable == 0x1A && next.type == DSGA_TYPE_NONE && next.shift_num == 0 && next.and_mask == 0x100u + bit) {
				skip = true;
				break;
			}
			if (next.variable == 0x7D && next.parameter == 0x100u + bit) break;
			if (next.variable >= 0x40 && next.variable != 0x7D && next.variable != 0x7C) break; // crude whitelist of variables which will never read special registers
		}
		if (skip) continue;
		DeterministicSpriteGroupAdjust store = {};
		store.operation = DSGA_OP_STO_NC;
		store.variable = 0x1A;
		store.type = DSGA_TYPE_NONE;
		store.shift_num = 0;
		store.and_mask = values[bit];
		store.divmod_val = 0x100 + bit;
		group->adjusts.insert(group->adjusts.begin() + offset + added, store);
		added++;
	}
	return added;
}

struct VarAction2ProcedureCallVarReadAnnotation {
	const SpriteGroup *subroutine;
	VarAction2ProcedureAnnotation *anno;
	std::bitset<256> relevant_stores;
	std::bitset<256> last_reads;
	bool unskippable;
};
static std::vector<VarAction2ProcedureCallVarReadAnnotation> _varaction2_proc_call_var_read_annotations;

static void OptimiseVarAction2DeterministicSpriteGroupPopulateLastVarReadAnnotations(DeterministicSpriteGroup *group, VarAction2GroupVariableTracking *var_tracking)
{
	std::bitset<256> bits;
	if (var_tracking != nullptr) bits = (var_tracking->out | var_tracking->proc_call_out);
	bool need_var1C = false;

	for (int i = (int)group->adjusts.size() - 1; i >= 0; i--) {
		DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];

		if (adjust.operation == DSGA_OP_STO) {
			if (adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask < 0x100) {
				/* Predictable store */
				bits.set(adjust.and_mask, false);
			}
		}
		if (adjust.variable == 0x7B && adjust.parameter == 0x7D) {
			/* Unpredictable load */
			bits.set();
		}
		if (adjust.variable == 0x7D && adjust.parameter < 0x100) {
			if (!bits[adjust.parameter]) {
				bits.set(adjust.parameter, true);
				adjust.adjust_flags |= DSGAF_LAST_VAR_READ;
			}
		}
		if (adjust.variable == 0x1C) {
			need_var1C = true;
		}

		if (adjust.variable == 0x7E) {
			/* procedure call */

			VarAction2ProcedureCallVarReadAnnotation &anno = _varaction2_proc_call_var_read_annotations.emplace_back();
			anno.subroutine = adjust.subroutine;
			anno.anno = OptimiseVarAction2GetFilledProcedureAnnotation(adjust.subroutine);
			anno.relevant_stores = anno.anno->stores & bits;
			anno.unskippable = anno.anno->unskippable;
			adjust.jump = (uint)_varaction2_proc_call_var_read_annotations.size() - 1; // index into _varaction2_proc_call_var_read_annotations

			if (need_var1C) {
				anno.unskippable = true;
				need_var1C = false;
			}

			std::bitset<256> orig_bits = bits;

			auto check_randomised_group = y_combinator([&](auto check_randomised_group, const SpriteGroup *sg) -> void {
				if (sg == nullptr) return;
				if (sg->type == SGT_RANDOMIZED) {
					/* Don't try to skip over procedure calls to randomised groups */
					anno.unskippable = true;
				} else if (sg->type == SGT_DETERMINISTIC) {
					const DeterministicSpriteGroup *dsg = static_cast<const DeterministicSpriteGroup *>(sg);
					if (!dsg->calculated_result) {
						if (anno.unskippable) return;
						check_randomised_group(dsg->default_group);
						for (const auto &range : dsg->ranges) {
							if (anno.unskippable) return;
							check_randomised_group(range.group);
						}
					}
				}
			});

			auto handle_group = y_combinator([&](auto handle_group, const SpriteGroup *sg) -> void {
				if (sg == nullptr) return;
				if (IsSimpleContainerSpriteGroup(sg)) {
					for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
						handle_group(group);
					}

					/* Don't try to skip over procedure calls to randomised groups */
					anno.unskippable = true;
				} else if (sg->type == SGT_DETERMINISTIC) {
					const DeterministicSpriteGroup *sub = static_cast<const DeterministicSpriteGroup *>(sg);
					VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(sub, false);
					if (var_tracking != nullptr) {
						bits |= var_tracking->in;
						anno.last_reads |= (var_tracking->in & ~orig_bits);
					}

					if (sub->dsg_flags & DSGF_REQUIRES_VAR1C) need_var1C = true;

					if (!sub->calculated_result && !anno.unskippable) {
						check_randomised_group(sub->default_group);
						for (const auto &range : sub->ranges) {
							if (anno.unskippable) break;
							check_randomised_group(range.group);
						}
					}
				}
			});
			handle_group(anno.subroutine);
		}
	}
}

static void OptimiseVarAction2DeterministicSpriteGroupInsertJumps(DeterministicSpriteGroup *group, VarAction2GroupVariableTracking *var_tracking)
{
	if (HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_INSERT_JUMPS)) return;

	group->dsg_flags &= ~DSGF_CHECK_INSERT_JUMP;

	OptimiseVarAction2DeterministicSpriteGroupPopulateLastVarReadAnnotations(group, var_tracking);

	for (int i = (int)group->adjusts.size() - 1; i >= 1; i--) {
		DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];

		if (adjust.adjust_flags & DSGAF_JUMP_INS_HINT) {
			std::bitset<256> ok_stores;
			uint32_t special_stores[16];
			uint16_t special_stores_mask = 0;
			int j = i - 1;
			int skip_count = 0;
			const DeterministicSpriteGroupAdjustFlags skip_mask = adjust.adjust_flags & (DSGAF_SKIP_ON_ZERO | DSGAF_SKIP_ON_LSB_SET);
			while (j >= 0) {
				DeterministicSpriteGroupAdjust &prev = group->adjusts[j];

				/* Don't try to skip over: unpredictable or unusable special stores, unskippable procedure calls, permanent stores, or another jump */
				if (prev.operation == DSGA_OP_STO && (prev.type != DSGA_TYPE_NONE || prev.variable != 0x1A || prev.shift_num != 0 || prev.and_mask >= 0x100)) break;
				if (prev.operation == DSGA_OP_STO_NC && prev.divmod_val >= 0x100) {
					if (prev.divmod_val < 0x110 && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A && prev.shift_num == 0) {
						/* Storing a constant in a special register */
						if (!HasBit(special_stores_mask, prev.divmod_val - 0x100)) {
							special_stores[prev.divmod_val - 0x100] = prev.and_mask;
							SetBit(special_stores_mask, prev.divmod_val - 0x100);
						}
					} else {
						break;
					}
				}
				if (prev.operation == DSGA_OP_STOP) break;
				if (IsEvalAdjustJumpOperation(prev.operation)) break;
				if (prev.variable == 0x7E) {
					const VarAction2ProcedureCallVarReadAnnotation &anno = _varaction2_proc_call_var_read_annotations[prev.jump];
					if (anno.unskippable) break;
					if ((anno.relevant_stores & ~ok_stores).any()) break;
					ok_stores |= anno.last_reads;

					uint16_t new_stores = anno.anno->special_register_mask & ~special_stores_mask;
					for (uint8_t bit : SetBitIterator(new_stores)) {
						special_stores[bit] = anno.anno->special_register_values[bit];
					}
					special_stores_mask |= new_stores;
				}

				/* Reached a store which can't be skipped over because the value is needed later */
				if (prev.operation == DSGA_OP_STO && !ok_stores[prev.and_mask]) break;
				if (prev.operation == DSGA_OP_STO_NC && prev.divmod_val < 0x100 && !ok_stores[prev.divmod_val]) break;

				if (prev.variable == 0x7D && (prev.adjust_flags & DSGAF_LAST_VAR_READ)) {
					/* The stored value is no longer needed after this, we can skip the corresponding store */
					ok_stores.set(prev.parameter & 0xFF, true);
				}

				/* Avoid creating jumps for skip on zero/LSB set sequences */
				if (prev.adjust_flags & skip_mask) skip_count++;

				j--;
			}
			if (j < i - 1 && (i - j) > (skip_count + 2)) {
				auto mark_end_block = [&](uint index, uint inc) {
					if (group->adjusts[index].variable == 0x7E) {
						/* Procedure call, can't mark this as an end block directly, so insert a NOOP and use that */
						DeterministicSpriteGroupAdjust noop = {};
						noop.operation = DSGA_OP_NOOP;
						noop.variable = 0x1A;
						group->adjusts.insert(group->adjusts.begin() + index + 1, noop);

						/* Fixup offsets */
						if (i > (int)index) i++;
						if (j > (int)index) j++;
						index++;
					}

					DeterministicSpriteGroupAdjust &adj = group->adjusts[index];
					if (adj.adjust_flags & DSGAF_END_BLOCK) {
						adj.jump += inc;
					} else {
						adj.adjust_flags |= DSGAF_END_BLOCK;
						adj.jump = inc;
						if (special_stores_mask) {
							uint added = OptimiseVarAction2InsertSpecialStoreOps(group, index + 1, special_stores, special_stores_mask);

							/* Fixup offsets */
							if (i > (int)index) i += added;
							if (j > (int)index) j += added;
						}
					}
				};

				DeterministicSpriteGroupAdjust current = adjust;
				/* Do not use adjust reference after this point */

				if (current.adjust_flags & DSGAF_END_BLOCK) {
					/* Move the existing end block 1 place back, to avoid it being moved with the jump adjust */
					mark_end_block(i - 1, current.jump);
					current.adjust_flags &= ~DSGAF_END_BLOCK;
					current.jump = 0;
				}
				current.operation = (current.adjust_flags & DSGAF_SKIP_ON_LSB_SET) ? DSGA_OP_JNZ : DSGA_OP_JZ;
				current.adjust_flags &= ~(DSGAF_JUMP_INS_HINT | DSGAF_SKIP_ON_ZERO | DSGAF_SKIP_ON_LSB_SET);
				mark_end_block(i - 1, 1);
				group->adjusts.erase(group->adjusts.begin() + i);
				if (j >= 0 && current.variable == 0x7D && (current.adjust_flags & DSGAF_LAST_VAR_READ)) {
					DeterministicSpriteGroupAdjust &prev = group->adjusts[j];
					if (prev.operation == DSGA_OP_STO_NC && prev.divmod_val == (current.parameter & 0xFF) &&
							TryCombineTempStoreLoadWithStoreSourceAdjust(current, &prev, false)) {
						/* Managed to extract source from immediately prior STO_NC, which can now be removed */
						group->adjusts.erase(group->adjusts.begin() + j);
						j--;
						i--;
					} else if (current.type == DSGA_TYPE_NONE && current.shift_num == 0 && current.and_mask == 0xFFFFFFFF &&
							prev.operation == DSGA_OP_STO && prev.variable == 0x1A && prev.shift_num == 0 && prev.and_mask == (current.parameter & 0xFF)) {
						/* Reading from immediately prior store, which can now be removed */
						current.operation = (current.operation == DSGA_OP_JNZ) ? DSGA_OP_JNZ_LV : DSGA_OP_JZ_LV;
						current.adjust_flags &= ~DSGAF_LAST_VAR_READ;
						current.and_mask = 0;
						current.variable = 0x1A;
						group->adjusts.erase(group->adjusts.begin() + j);
						j--;
						i--;
					}
				}
				group->adjusts.insert(group->adjusts.begin() + (j + 1), current);
				group->dsg_flags |= DSGF_CHECK_INSERT_JUMP;
				i++;
			}
		}
	}

	if (!_varaction2_proc_call_var_read_annotations.empty()) {
		for (DeterministicSpriteGroupAdjust &adjust : group->adjusts) {
			if (adjust.variable == 0x7E) adjust.subroutine = _varaction2_proc_call_var_read_annotations[adjust.jump].subroutine;
		}
		_varaction2_proc_call_var_read_annotations.clear();
	}
}

struct ResolveJumpInnerResult {
	uint end_index;
	uint end_block_remaining;
};

static ResolveJumpInnerResult OptimiseVarAction2DeterministicSpriteResolveJumpsInner(DeterministicSpriteGroup *group, const uint start)
{
	for (uint i = start + 1; i < (uint)group->adjusts.size(); i++) {
		if (IsEvalAdjustJumpOperation(group->adjusts[i].operation)) {
			ResolveJumpInnerResult result = OptimiseVarAction2DeterministicSpriteResolveJumpsInner(group, i);
			i = result.end_index;
			if (result.end_block_remaining > 0) {
				group->adjusts[start].jump = i - start;
				return { i, result.end_block_remaining - 1 };
			}
		} else if (group->adjusts[i].adjust_flags & DSGAF_END_BLOCK) {
			group->adjusts[start].jump = i - start;
			return { i, group->adjusts[i].jump - 1 };
		}
	}

	NOT_REACHED();
}

static void OptimiseVarAction2DeterministicSpriteResolveJumps(DeterministicSpriteGroup *group)
{
	if (HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_INSERT_JUMPS)) return;

	for (uint i = 0; i < (uint)group->adjusts.size(); i++) {
		if (IsEvalAdjustJumpOperation(group->adjusts[i].operation)) {
			ResolveJumpInnerResult result = OptimiseVarAction2DeterministicSpriteResolveJumpsInner(group, i);
			i = result.end_index;
			assert(result.end_block_remaining == 0);
		}
	}
}

static const size_t MAX_PROC_INLINE_ADJUST_COUNT = 8;

static bool IsVariableInlinable(uint16_t variable, GrfSpecFeature feature)
{
	/* Always available global variables */
	if (variable <= 0x03) return true;
	if (variable == 0x06) return true;
	if (variable >= 0x09 && variable <= 0x12) return true;
	if (variable == 0x18) return true;
	if (variable >= 0x1A && variable <= 0x1E) return true;
	if (variable >= 0x20 && variable <= 0x24) return true;

	/* Temp storage, procedure call, GRF param */
	if (variable >= 0x7D && variable <= 0x7F) return true;

	/* Perm storage */
	if (variable == 0x7C) return feature == GSF_AIRPORTS || feature == GSF_INDUSTRIES;

	if (feature == GSF_INDUSTRIES) {
		/* Special case: allow inlining variables 67, 68, even though these are not strictly always available */
		if (variable >= 0x67 && variable <= 0x68) return true;
	}

	return false;
}

static void OptimiseVarAction2CheckInliningCandidate(DeterministicSpriteGroup *group, std::vector<DeterministicSpriteGroupAdjust> &saved_adjusts)
{
	if (HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_PROC_INLINE)) return;
	if (group->adjusts.size() > MAX_PROC_INLINE_ADJUST_COUNT || !group->calculated_result || group->var_scope != VSG_SCOPE_SELF) return;

	for (const DeterministicSpriteGroupAdjust &adjust : group->adjusts) {
		uint variable = adjust.variable;
		if (variable == 0x7B) variable = adjust.parameter;
		if (!IsVariableInlinable(variable, group->feature)) return;
	}

	group->dsg_flags |= DSGF_INLINE_CANDIDATE;
	*(_cur.GetInlinableGroupAdjusts(group, true)) = std::move(saved_adjusts);
}

static void PopulateRegistersUsedByNewGRFSpriteLayout(const NewGRFSpriteLayout &dts, std::bitset<256> &bits)
{
	const TileLayoutRegisters *registers = dts.registers;

	auto process_registers = [&](uint i, bool is_parent) {
		const TileLayoutRegisters *reg = registers + i;
		if (reg->flags & TLF_DODRAW) bits.set(reg->dodraw, true);
		if (reg->flags & TLF_SPRITE) bits.set(reg->sprite, true);
		if (reg->flags & TLF_PALETTE) bits.set(reg->palette, true);
		if (is_parent) {
			if (reg->flags & TLF_BB_XY_OFFSET) {
				bits.set(reg->delta.parent[0], true);
				bits.set(reg->delta.parent[1], true);
			}
			if (reg->flags & TLF_BB_Z_OFFSET) bits.set(reg->delta.parent[2], true);
		} else {
			if (reg->flags & TLF_CHILD_X_OFFSET) bits.set(reg->delta.child[0], true);
			if (reg->flags & TLF_CHILD_Y_OFFSET) bits.set(reg->delta.child[1], true);
		}
	};
	process_registers(0, false);

	uint offset = 0; // offset 0 is the ground sprite
	const DrawTileSeqStruct *element;
	foreach_draw_tile_seq(element, dts.seq) {
		offset++;
		process_registers(offset, element->IsParentSprite());
	}
}

void OptimiseVarAction2DeterministicSpriteGroup(VarAction2OptimiseState &state, const VarAction2AdjustInfo info, DeterministicSpriteGroup *group, std::vector<DeterministicSpriteGroupAdjust> &saved_adjusts)
{
	if (unlikely(HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2))) return;

	bool possible_callback_handler = false;
	for (DeterministicSpriteGroupAdjust &adjust : group->adjusts) {
		if (adjust.variable == 0x7D) adjust.parameter &= 0xFF; // Clear temporary version tags
		if (adjust.variable == 0xC) possible_callback_handler = true;
		if (adjust.operation == DSGA_OP_STOP) possible_callback_handler = true;
	}

	if (!HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_GROUP_PRUNE) && (state.inference & VA2AIF_HAVE_CONSTANT) && !group->calculated_result) {
		/* Result of this sprite group is always the same, discard the unused branches */
		const SpriteGroup *target = group->default_group;
		for (const auto &range : group->ranges) {
			if (range.low <= state.current_constant && state.current_constant <= range.high) {
				target = range.group;
			}
		}
		group->default_group = target;
		group->ranges.clear();
	}
	if (!HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_GROUP_PRUNE) && (state.inference & VA2AIF_ONE_OR_ZERO) && !group->calculated_result && group->ranges.size() == 1) {
		/* See if sprite group uses ranges as a cast to bool, when the result is already bool */
		const DeterministicSpriteGroupRange &r0 = group->ranges[0];
		if (r0.low == 0 && r0.high == 0 && r0.group != nullptr && r0.group->type == SGT_CALLBACK && static_cast<const CallbackResultSpriteGroup*>(r0.group)->result == 0 &&
				group->default_group != nullptr && group->default_group->type == SGT_CALLBACK && static_cast<const CallbackResultSpriteGroup*>(group->default_group)->result == 1) {
			group->calculated_result = true;
			group->ranges.clear();
		} else if (r0.low == 1 && r0.high == 1 && r0.group != nullptr && r0.group->type == SGT_CALLBACK && static_cast<const CallbackResultSpriteGroup*>(r0.group)->result == 1 &&
				group->default_group != nullptr && group->default_group->type == SGT_CALLBACK && static_cast<const CallbackResultSpriteGroup*>(group->default_group)->result == 0) {
			group->calculated_result = true;
			group->ranges.clear();
		}
	}

	std::bitset<256> bits;
	std::bitset<256> pending_bits;
	bool seen_pending = false;
	bool seen_req_var1C = false;
	if (!group->calculated_result) {
		bool is_cb_switch = false;
		if (possible_callback_handler && group->adjusts.size() > 0 && !group->calculated_result &&
				IsFeatureUsableForCBQuickExit(group->feature) && !HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_CB_QUICK_EXIT)) {
			size_t idx = group->adjusts.size() - 1;
			const auto &adjust = group->adjusts[idx];
			if (adjust.variable == 0xC && ((adjust.operation == DSGA_OP_ADD && idx == 0) || adjust.operation == DSGA_OP_RST) &&
					adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
				is_cb_switch = true;
			}
		}

		struct HandleGroupState {
			bool ignore_cb_handler = false;
			bool have_cb_handler = false;
		};
		auto handle_group = y_combinator([&](auto handle_group, const SpriteGroup *sg, HandleGroupState &state) -> void {
			if (sg != nullptr && sg->type == SGT_DETERMINISTIC) {
				VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(sg, false);
				const DeterministicSpriteGroup *dsg = (const DeterministicSpriteGroup*)sg;
				if (dsg->dsg_flags & DSGF_VAR_TRACKING_PENDING) {
					seen_pending = true;
					if (var_tracking != nullptr) pending_bits |= var_tracking->in;
				} else {
					if (var_tracking != nullptr) bits |= var_tracking->in;
				}
				if (dsg->dsg_flags & DSGF_REQUIRES_VAR1C) seen_req_var1C = true;
				if ((dsg->dsg_flags & DSGF_CB_HANDLER) && !state.ignore_cb_handler) {
					group->dsg_flags |= DSGF_CB_HANDLER;
					state.have_cb_handler = true;
				}
				if ((dsg->dsg_flags & DSGF_CB_RESULT) && !state.ignore_cb_handler) {
					group->dsg_flags |= DSGF_CB_RESULT;
					state.have_cb_handler = true;
				}
			}
			if (IsSimpleContainerSpriteGroup(sg)) {
				for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
					handle_group(group, state);
				}
			}
			if (sg != nullptr && sg->type == SGT_TILELAYOUT) {
				const TileLayoutSpriteGroup *tlsg = (const TileLayoutSpriteGroup*)sg;
				if (tlsg->dts.registers != nullptr) {
					PopulateRegistersUsedByNewGRFSpriteLayout(tlsg->dts, bits);
				}
			}
			if (sg != nullptr && sg->type == SGT_INDUSTRY_PRODUCTION) {
				const IndustryProductionSpriteGroup *ipsg = (const IndustryProductionSpriteGroup*)sg;
				if (ipsg->version >= 1) {
					for (int i = 0; i < ipsg->num_input; i++) {
						if (ipsg->subtract_input[i] < 0x100) bits.set(ipsg->subtract_input[i], true);
					}
					for (int i = 0; i < ipsg->num_output; i++) {
						if (ipsg->add_output[i] < 0x100) bits.set(ipsg->add_output[i], true);
					}
					bits.set(ipsg->again, true);
				}
			}
			if (sg != nullptr && sg->type == SGT_CALLBACK) {
				if (!state.ignore_cb_handler && static_cast<const CallbackResultSpriteGroup*>(sg)->result != CALLBACK_FAILED) {
					group->dsg_flags |= DSGF_CB_RESULT;
					state.have_cb_handler = true;
				}
			}
		});

		HandleGroupState default_group_state;
		handle_group(group->default_group, default_group_state);

		HandleGroupState ranges_state;
		for (const auto &range : group->ranges) {
			ranges_state.ignore_cb_handler = is_cb_switch && range.low == 0 && range.high == 0;
			handle_group(range.group, ranges_state);
		}

		if (!default_group_state.have_cb_handler && is_cb_switch) {
			bool found_zero_value = false;
			bool found_non_zero_value = false;
			bool found_random_cb_value = false;
			for (const auto &range : group->ranges) {
				if (range.low == 0) found_zero_value = true;
				if (range.high > 0) found_non_zero_value = true;
				if (range.low <= 1 && range.high >= 1) found_random_cb_value = true;
			}
			if (!found_non_zero_value) {
				/* Group looks at var C but has no branches for non-zero cases, so don't consider it a callback handler.
				 * This pattern is generally only used to implement an "always fail" group.
				 */
				possible_callback_handler = false;
			}
			if (!found_zero_value && !found_random_cb_value) {
				group->ranges.insert(group->ranges.begin(), { group->default_group, 0, 1 });
				extern const CallbackResultSpriteGroup *NewCallbackResultSpriteGroupNoTransform(uint16_t result);
				group->default_group = NewCallbackResultSpriteGroupNoTransform(CALLBACK_FAILED);
			}
		}

		std::bitset<256> in_bits = bits | pending_bits;
		if (in_bits.any()) {
			state.GetVarTracking(group)->out = bits;
			for (auto &it : state.temp_stores) {
				in_bits.set(it.first, false);
			}
			state.GetVarTracking(group)->in |= in_bits;
		}
	} else {
		group->dsg_flags |= DSGF_CB_RESULT;
	}
	if (possible_callback_handler) group->dsg_flags |= DSGF_CB_HANDLER;

	if ((group->dsg_flags & (DSGF_CB_HANDLER | DSGF_CB_RESULT)) == 0 && !HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_CB_QUICK_EXIT)) {
		group->sg_flags |= SGF_SKIP_CB;
	}

	if (!HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_GROUP_PRUNE) && group->ranges.empty() && !group->calculated_result && !seen_req_var1C) {
		/* There is only one option, remove any redundant adjustments when the result will be ignored anyway */
		while (!group->adjusts.empty()) {
			const DeterministicSpriteGroupAdjust &prev = group->adjusts.back();
			if (prev.variable != 0x7E && !IsEvalAdjustWithSideEffects(prev.operation)) {
				/* Delete useless operation */
				group->adjusts.pop_back();
			} else {
				break;
			}
		}
	}

	bool dse_allowed = IsFeatureUsableForDSE(info.feature) && !HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_DSE);
	bool dse_eligible = state.enable_dse;
	if (dse_allowed && !dse_eligible) {
		dse_eligible |= CheckDeterministicSpriteGroupOutputVarBits(group, bits, nullptr, true);
	}
	if (state.seen_procedure_call) {
		/* Be more pessimistic with procedures as the ordering is different.
		 * Later groups can require variables set in earlier procedures instead of the usual
		 * where earlier groups can require variables set in later groups.
		 * DSE on the procedure runs before the groups which use it, so set the procedure
		 * output bits not using values from call site groups before DSE. */
		CheckDeterministicSpriteGroupOutputVarBits(group, bits | pending_bits, nullptr, false);
	}
	bool dse_candidate = (dse_allowed && dse_eligible);
	if (!dse_candidate && (seen_pending || (group->dsg_flags & DSGF_CHECK_INSERT_JUMP))) {
		group->dsg_flags |= DSGF_NO_DSE;
		dse_candidate = true;
	}
	if (dse_candidate) {
		_cur.dead_store_elimination_candidates.push_back(group);
		group->dsg_flags |= DSGF_VAR_TRACKING_PENDING;
	} else {
		OptimiseVarAction2DeterministicSpriteGroupSimplifyStores(group);
		OptimiseVarAction2DeterministicSpriteGroupAdjustOrdering(group, info.scope_feature);
	}

	OptimiseVarAction2CheckInliningCandidate(group, saved_adjusts);

	if (state.check_expensive_vars && !HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_EXPENSIVE_VARS)) {
		if (dse_candidate) {
			group->dsg_flags |= DSGF_CHECK_EXPENSIVE_VARS;
		} else {
			OptimiseVarAction2DeterministicSpriteGroupExpensiveVars(group, info.scope_feature);
		}
	}

	if (!dse_candidate) group->adjusts.shrink_to_fit();
}

static std::bitset<256> HandleVarAction2DeadStoreElimination(DeterministicSpriteGroup *group, VarAction2GroupVariableTracking *var_tracking, bool no_changes)
{
	std::bitset<256> all_bits;
	std::bitset<256> propagate_bits;
	std::vector<uint> substitution_candidates;
	if (var_tracking != nullptr) {
		propagate_bits = var_tracking->out;
		all_bits = propagate_bits | var_tracking->proc_call_out;
	}
	bool need_var1C = false;

	auto abandon_substitution_candidates = [&]() {
		for (uint value : substitution_candidates) {
			all_bits.set(value & 0xFF, true);
			propagate_bits.set(value & 0xFF, true);
		}
		substitution_candidates.clear();
	};
	auto erase_adjust = [&](int index) {
		group->adjusts.erase(group->adjusts.begin() + index);
		for (size_t i = 0; i < substitution_candidates.size();) {
			uint &value = substitution_candidates[i];
			if (value >> 8 == (uint)index) {
				/* Removed the substitution candidate target */
				value = substitution_candidates.back();
				substitution_candidates.pop_back();
				continue;
			}

			if (value >> 8 > (uint)index) {
				/* Adjust the substitution candidate target offset */
				value -= 0x100;
			}

			i++;
		}
	};
	auto try_variable_substitution = [&](DeterministicSpriteGroupAdjust &target, int prev_load_index, uint8_t idx) -> bool {
		assert(target.variable == 0x7D && target.parameter == idx);

		bool inverted = false;
		const DeterministicSpriteGroupAdjust *var_src = GetVarAction2PreviousSingleLoadAdjust(group->adjusts, prev_load_index, &inverted);
		if (var_src != nullptr) {
			if (TryCombineTempStoreLoadWithStoreSourceAdjust(target, var_src, inverted)) return true;
		}
		return false;
	};

	for (int i = (int)group->adjusts.size() - 1; i >= 0;) {
		bool pending_restart = false;
		auto restart = [&]() {
			pending_restart = false;
			i = (int)group->adjusts.size() - 1;
			if (var_tracking != nullptr) {
				propagate_bits = var_tracking->out;
				all_bits = propagate_bits | var_tracking->proc_call_out;
			} else {
				all_bits.reset();
				propagate_bits.reset();
			}
			substitution_candidates.clear();
			need_var1C = false;
		};
		const DeterministicSpriteGroupAdjust &adjust = group->adjusts[i];
		if (adjust.operation == DSGA_OP_STO) {
			if (adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0 && adjust.and_mask < 0x100) {
				uint8_t idx = adjust.and_mask;
				/* Predictable store */

				for (size_t j = 0; j < substitution_candidates.size(); j++) {
					if ((substitution_candidates[j] & 0xFF) == idx) {
						/* Found candidate */

						DeterministicSpriteGroupAdjust &target = group->adjusts[substitution_candidates[j] >> 8];
						bool substituted = try_variable_substitution(target, i - 1, idx);
						if (!substituted) {
							/* Not usable, mark as required so it's not eliminated */
							all_bits.set(idx, true);
							propagate_bits.set(idx, true);
						}
						substitution_candidates[j] = substitution_candidates.back();
						substitution_candidates.pop_back();
						break;
					}
				}

				if (!all_bits[idx] && !no_changes) {
					/* Redundant store */
					erase_adjust(i);
					i--;
					if ((i + 1 < (int)group->adjusts.size() && group->adjusts[i + 1].operation == DSGA_OP_RST && group->adjusts[i + 1].variable != 0x7B) ||
							(i + 1 == (int)group->adjusts.size() && group->ranges.empty() && !group->calculated_result)) {
						/* Now the store is eliminated, the current value has no users */
						while (i >= 0) {
							const DeterministicSpriteGroupAdjust &prev = group->adjusts[i];
							if (prev.variable != 0x7E && !IsEvalAdjustWithSideEffects(prev.operation)) {
								/* Delete useless operation */
								erase_adjust(i);
								i--;
							} else {
								if (i + 1 < (int)group->adjusts.size()) {
									DeterministicSpriteGroupAdjust &next = group->adjusts[i + 1];
									if (prev.operation == DSGA_OP_STO && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A &&
											prev.shift_num == 0 && prev.and_mask < 0x100 &&
											next.operation == DSGA_OP_RST && next.variable == 0x7D &&
											next.parameter == prev.and_mask && next.shift_num == 0 && next.and_mask == 0xFFFFFFFF) {
										if (next.type == DSGA_TYPE_NONE) {
											/* Removing the dead store results in a store/load sequence, remove the load and re-check */
											erase_adjust(i + 1);
											restart();
											break;
										}
										if ((next.type == DSGA_TYPE_EQ || next.type == DSGA_TYPE_NEQ) && next.add_val == 0 && i + 2 < (int)group->adjusts.size()) {
											DeterministicSpriteGroupAdjust &next2 = group->adjusts[i + 2];
											if (next2.operation == DSGA_OP_TERNARY) {
												/* Removing the dead store results in a store, load with bool/invert, ternary sequence, remove the load, adjust ternary and re-check */
												if (next.type == DSGA_TYPE_EQ) {
													std::swap(next2.and_mask, next2.add_val);
												}
												erase_adjust(i + 1);
												restart();
												break;
											}
										}
									}
									if (next.operation == DSGA_OP_RST) {
										/* See if this is a repeated load of a variable (not procedure call) */
										const DeterministicSpriteGroupAdjust *prev_load = GetVarAction2PreviousSingleLoadAdjust(group->adjusts, i, nullptr);
										if (prev_load != nullptr && MemCmpT<DeterministicSpriteGroupAdjust>(prev_load, &next) == 0) {
											if (next.variable == 0x7D) pending_restart = true;
											erase_adjust(i + 1);
											break;
										}
									}
									if (i + 2 < (int)group->adjusts.size() && next.operation == DSGA_OP_RST && next.variable != 0x7E &&
											prev.operation == DSGA_OP_STO && prev.type == DSGA_TYPE_NONE && prev.variable == 0x1A &&
											prev.shift_num == 0 && prev.and_mask < 0x100) {
										const DeterministicSpriteGroupAdjust &next2 = group->adjusts[i + 2];
										if (next2.type == DSGA_TYPE_NONE && next2.variable == 0x7D && next2.shift_num == 0 &&
												next2.and_mask == 0xFFFFFFFF && next2.parameter == prev.and_mask) {
											if (IsEvalAdjustOperationReversable(next2.operation)) {
												/* Convert: store, load var, (anti-)commutative op on stored --> (dead) store, (reversed) (anti-)commutative op var */
												next.operation = ReverseEvalAdjustOperation(next2.operation);
												if (IsEvalAdjustWithZeroLastValueAlwaysZero(next.operation)) {
													next.adjust_flags |= DSGAF_SKIP_ON_ZERO;
												}
												erase_adjust(i + 2);
												restart();
												break;
											}
										}
									}
								}
								break;
							}
						}
					} else {
						while (i >= 0 && i + 1 < (int)group->adjusts.size()) {
							/* See if having removed the store, there is now a useful pair of operations which can be combined */
							DeterministicSpriteGroupAdjust &prev = group->adjusts[i];
							DeterministicSpriteGroupAdjust &next = group->adjusts[i + 1];
							if (next.type == DSGA_TYPE_NONE && next.operation == DSGA_OP_XOR && next.variable == 0x1A && next.shift_num == 0 && next.and_mask == 1) {
								/* XOR: boolean invert */
								if (IsEvalAdjustOperationRelationalComparison(prev.operation)) {
									prev.operation = InvertEvalAdjustRelationalComparisonOperation(prev.operation);
									erase_adjust(i + 1);
									continue;
								} else if (prev.operation == DSGA_OP_RST && IsConstantComparisonAdjustType(prev.type)) {
									prev.type = InvertConstantComparisonAdjustType(prev.type);
									erase_adjust(i + 1);
									continue;
								}
							}
							if (i >= 1 && prev.type == DSGA_TYPE_NONE && IsEvalAdjustOperationRelationalComparison(prev.operation) &&
									prev.variable == 0x1A && prev.shift_num == 0 && next.operation == DSGA_OP_MUL) {
								if (((prev.operation == DSGA_OP_SGT && (prev.and_mask == 0 || prev.and_mask == (uint)-1)) || (prev.operation == DSGA_OP_SGE && (prev.and_mask == 0 || prev.and_mask == 1))) &&
										IsIdenticalValueLoad(GetVarAction2PreviousSingleLoadAdjust(group->adjusts, i - 1, nullptr), &next)) {
									prev.operation = DSGA_OP_SMAX;
									prev.and_mask = 0;
									erase_adjust(i + 1);
									continue;
								}
								if (((prev.operation == DSGA_OP_SLE && (prev.and_mask == 0 || prev.and_mask == (uint)-1)) || (prev.operation == DSGA_OP_SLT && (prev.and_mask == 0 || prev.and_mask == 1))) &&
										IsIdenticalValueLoad(GetVarAction2PreviousSingleLoadAdjust(group->adjusts, i - 1, nullptr), &next)) {
									prev.operation = DSGA_OP_SMIN;
									prev.and_mask = 0;
									erase_adjust(i + 1);
									continue;
								}
							}
							break;
						}
					}
					if (pending_restart) restart();
					continue;
				} else {
					/* Non-redundant store */
					all_bits.set(idx, false);
					propagate_bits.set(idx, false);
				}
			} else {
				/* Unpredictable store */
				abandon_substitution_candidates();
			}
		}
		if (adjust.variable == 0x7B && adjust.parameter == 0x7D) {
			/* Unpredictable load */
			all_bits.set();
			propagate_bits.set();
			abandon_substitution_candidates();
		}
		if (adjust.variable == 0x7D && adjust.parameter < 0x100) {
			if (i > 0 && !all_bits[adjust.parameter] && !no_changes) {
				/* See if this can be made a substitution candidate */
				bool add = true;
				for (size_t j = 0; j < substitution_candidates.size(); j++) {
					if ((substitution_candidates[j] & 0xFF) == adjust.parameter) {
						/* There already is a candidate */
						substitution_candidates[j] = substitution_candidates.back();
						substitution_candidates.pop_back();
						all_bits.set(adjust.parameter, true);
						propagate_bits.set(adjust.parameter, true);
						add = false;
						break;
					}
				}
				if (add) {
					substitution_candidates.push_back(adjust.parameter | (i << 8));
				}
			} else {
				all_bits.set(adjust.parameter, true);
				propagate_bits.set(adjust.parameter, true);
			}
		}
		if (adjust.variable == 0x1C) {
			need_var1C = true;
		}
		if (adjust.variable == 0x7E) {
			/* procedure call */

			VarAction2ProcedureAnnotation *anno = OptimiseVarAction2GetFilledProcedureAnnotation(adjust.subroutine);

			bool may_remove = !need_var1C;
			if (may_remove && anno->unskippable) may_remove = false;
			if (may_remove && (anno->stores & all_bits).any()) may_remove = false;

			if (may_remove) {
				for (size_t j = 0; j < substitution_candidates.size(); j++) {
					if (anno->stores[substitution_candidates[j] & 0xFF]) {
						/* The procedure makes a store which may be used by a later substitution candidate.
						 * The procedure can't be removed, the substitution candidate will be removed below. */
						may_remove = false;
						break;
					}
				}
			}

			if (may_remove) {
				if ((i + 1 < (int)group->adjusts.size() && group->adjusts[i + 1].operation == DSGA_OP_RST && group->adjusts[i + 1].variable != 0x7B) ||
						(i + 1 == (int)group->adjusts.size() && group->ranges.empty() && !group->calculated_result)) {
					/* Procedure is skippable, makes no stores we need, and the return value is also not needed */
					erase_adjust(i);
					if (anno->special_register_mask) {
						OptimiseVarAction2InsertSpecialStoreOps(group, i, anno->special_register_values, anno->special_register_mask);
						restart();
					} else {
						i--;
					}
					continue;
				}
				if (!anno->unskippable && anno->special_register_mask == 0 && IsEvalAdjustWithZeroLastValueAlwaysZero(adjust.operation)) {
					/* No stores made in the procedure are required and there are no special stores or other features which make it unskippable.
					 * Set DSGAF_SKIP_ON_ZERO if appropriate */
					group->adjusts[i].adjust_flags |= DSGAF_SKIP_ON_ZERO;
				}
			}

			need_var1C = false;

			auto handle_group = y_combinator([&](auto handle_group, const SpriteGroup *sg) -> void {
				if (sg == nullptr) return;
				if (IsSimpleContainerSpriteGroup(sg)) {
					for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
						handle_group(group);
					}
				} else if (sg->type == SGT_DETERMINISTIC) {
					const DeterministicSpriteGroup *sub = static_cast<const DeterministicSpriteGroup *>(sg);
					VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(sub, false);
					if (var_tracking != nullptr) {
						all_bits |= var_tracking->in;
						propagate_bits |= var_tracking->in;
					}
					if (sub->dsg_flags & DSGF_REQUIRES_VAR1C) need_var1C = true;
				}
			});
			handle_group(adjust.subroutine);
			if (anno->unskippable || anno->special_register_mask) {
				abandon_substitution_candidates();
			} else {
				/* Flush any substitution candidates which reference stores made in the procedure */
				for (size_t j = 0; j < substitution_candidates.size();) {
					uint8_t idx = substitution_candidates[j] & 0xFF;
					if (anno->stores[idx]) {
						all_bits.set(idx, true);
						propagate_bits.set(idx, true);
						substitution_candidates[j] = substitution_candidates.back();
						substitution_candidates.pop_back();
					} else {
						j++;
					}
				}
			}
		}
		i--;
	}
	abandon_substitution_candidates();
	return propagate_bits;
}

static void PopulateRailStationAdvancedLayoutVariableUsage()
{
	for (uint i = 0; StationClass::IsClassIDValid((StationClassID)i); i++) {
		StationClass *stclass = StationClass::Get((StationClassID)i);

		for (uint j = 0; j < stclass->GetSpecCount(); j++) {
			const StationSpec *statspec = stclass->GetSpec(j);
			if (statspec == nullptr) continue;

			std::bitset<256> bits;
			for (const NewGRFSpriteLayout &dts : statspec->renderdata) {
				if (dts.registers != nullptr) {
					PopulateRegistersUsedByNewGRFSpriteLayout(dts, bits);
				}
			}
			if (bits.any()) {
				/* Simulate a procedure call on each of the root sprite groups which requires the bits used in the tile layouts */
				for (uint k = 0; k < NUM_CARGO + 3; k++) {
					if (statspec->grf_prop.spritegroup[k] != nullptr) {
						std::bitset<256> proc_bits = bits;
						CheckDeterministicSpriteGroupOutputVarBitsProcedureHandler proc_handler(proc_bits);
						proc_handler.ProcessGroup(statspec->grf_prop.spritegroup[k], nullptr, true);
					}
				}
			}
		}
	}
}

void HandleVarAction2OptimisationPasses()
{
	if (unlikely(HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2))) return;

	PopulateRailStationAdvancedLayoutVariableUsage();

	for (DeterministicSpriteGroup *group : _cur.dead_store_elimination_candidates) {
		VarAction2GroupVariableTracking *var_tracking = _cur.GetVarAction2GroupVariableTracking(group, false);
		if (!group->calculated_result) {
			/* Add bits from any groups previously marked with DSGF_VAR_TRACKING_PENDING which should now be correctly updated after DSE */
			auto handle_group = y_combinator([&](auto handle_group, const SpriteGroup *sg) -> void {
				if (sg != nullptr && sg->type == SGT_DETERMINISTIC) {
					VarAction2GroupVariableTracking *targ_var_tracking = _cur.GetVarAction2GroupVariableTracking(sg, false);
					if (targ_var_tracking != nullptr) {
						if (var_tracking == nullptr) var_tracking = _cur.GetVarAction2GroupVariableTracking(group, true);
						var_tracking->out |= targ_var_tracking->in;
					}
				}
				if (IsSimpleContainerSpriteGroup(sg)) {
					for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
						handle_group(group);
					}
				}
			});
			handle_group(group->default_group);
			group->default_group = PruneTargetSpriteGroup(group->default_group);
			for (auto &range : group->ranges) {
				handle_group(range.group);
				range.group = PruneTargetSpriteGroup(range.group);
			}
		}

		/* Always run this even DSGF_NO_DSE is set because the load/store tracking is needed to re-calculate the input bits,
		 * even if no stores are actually eliminated */
		std::bitset<256> in_bits = HandleVarAction2DeadStoreElimination(group, var_tracking, group->dsg_flags & DSGF_NO_DSE);
		if (var_tracking == nullptr && in_bits.any()) {
			var_tracking = _cur.GetVarAction2GroupVariableTracking(group, true);
			var_tracking->in = in_bits;
		} else if (var_tracking != nullptr) {
			var_tracking->in = in_bits;
		}

		const GrfSpecFeature scope_feature = GetGrfSpecFeatureForScope(group->feature, group->var_scope);

		OptimiseVarAction2DeterministicSpriteGroupSimplifyStores(group);
		OptimiseVarAction2DeterministicSpriteGroupAdjustOrdering(group, scope_feature);
		if (group->dsg_flags & DSGF_CHECK_INSERT_JUMP) {
			OptimiseVarAction2DeterministicSpriteGroupInsertJumps(group, var_tracking);
		}
		if (group->dsg_flags & DSGF_CHECK_EXPENSIVE_VARS) {
			OptimiseVarAction2DeterministicSpriteGroupExpensiveVars(group, scope_feature);
		}
		if (group->dsg_flags & DSGF_CHECK_INSERT_JUMP) {
			OptimiseVarAction2DeterministicSpriteResolveJumps(group);
		}

		group->adjusts.shrink_to_fit();
	}
}

const SpriteGroup *PruneTargetSpriteGroup(const SpriteGroup *result)
{
	if (HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2) || HasGrfOptimiserFlag(NGOF_NO_OPT_VARACT2_GROUP_PRUNE)) return result;
	while (result != nullptr) {
		if (result->type == SGT_DETERMINISTIC) {
			const DeterministicSpriteGroup *sg = static_cast<const DeterministicSpriteGroup *>(result);
			if (sg->GroupMayBeBypassed()) {
				/* Deterministic sprite group can be trivially resolved, skip it */
				uint32_t value = (sg->adjusts.size() == 1) ? EvaluateDeterministicSpriteGroupAdjust(sg->size, sg->adjusts[0], nullptr, 0, UINT_MAX) : 0;
				const SpriteGroup *candidate = sg->default_group;
				for (const auto &range : sg->ranges) {
					if (range.low <= value && value <= range.high) {
						candidate = range.group;
						break;
					}
				}

				auto need_var1C = y_combinator([&](auto need_var1C, const SpriteGroup *sg) -> bool {
					if (sg == nullptr) return false;
					if (IsSimpleContainerSpriteGroup(sg)) {
						for (const auto &group : IterateSimpleContainerSpriteGroup(sg)) {
							if (need_var1C(group)) return true;
						}
					} else if (sg->type == SGT_DETERMINISTIC) {
						const DeterministicSpriteGroup *sub = static_cast<const DeterministicSpriteGroup *>(sg);
						if (sub->dsg_flags & DSGF_REQUIRES_VAR1C) return true;
					}
					return false;
				});
				if (need_var1C(candidate)) {
					/* Can't skip this group as the child group requires the result of this group for variable 1C */
					return result;
				}

				result = candidate;
				continue;
			}
		}
		break;
	}
	return result;
}
