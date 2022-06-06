/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_spritegroup.cpp Handling of primarily NewGRF action 2. */

#include "stdafx.h"
#include "debug.h"
#include "newgrf_spritegroup.h"
#include "newgrf_profiling.h"
#include "core/pool_func.hpp"
#include "vehicle_type.h"
#include "newgrf_cache_check.h"
#include "string_func.h"
#include "newgrf_extension.h"
#include "newgrf_industrytiles_analysis.h"
#include "scope.h"

#include "safeguards.h"

SpriteGroupPool _spritegroup_pool("SpriteGroup");
INSTANTIATE_POOL_METHODS(SpriteGroup)

TemporaryStorageArray<int32, 0x110> _temp_store;


/**
 * ResolverObject (re)entry point.
 * This cannot be made a call to a virtual function because virtual functions
 * do not like nullptr and checking for nullptr *everywhere* is more cumbersome than
 * this little helper function.
 * @param group the group to resolve for
 * @param object information needed to resolve the group
 * @param top_level true if this is a top-level SpriteGroup, false if used nested in another SpriteGroup.
 * @return the resolved group
 */
/* static */ const SpriteGroup *SpriteGroup::Resolve(const SpriteGroup *group, ResolverObject &object, bool top_level)
{
	if (group == nullptr) return nullptr;

	const GRFFile *grf = object.grffile;
	auto profiler = std::find_if(_newgrf_profilers.begin(), _newgrf_profilers.end(), [&](const NewGRFProfiler &pr) { return pr.grffile == grf; });

	if (profiler == _newgrf_profilers.end() || !profiler->active) {
		if (top_level) _temp_store.ClearChanges();
		return group->Resolve(object);
	} else if (top_level) {
		profiler->BeginResolve(object);
		_temp_store.ClearChanges();
		const SpriteGroup *result = group->Resolve(object);
		profiler->EndResolve(result);
		return result;
	} else {
		profiler->RecursiveResolve();
		return group->Resolve(object);
	}
}

static inline uint32 GetVariable(const ResolverObject &object, ScopeResolver *scope, uint16 variable, uint32 parameter, GetVariableExtra *extra)
{
	uint32 value;
	switch (variable) {
		case 0x0C: return object.callback;
		case 0x10: return object.callback_param1;
		case 0x18: return object.callback_param2;
		case 0x1A: return UINT_MAX;
		case 0x1C: return object.last_value;

		case 0x5F: return (scope->GetRandomBits() << 8) | scope->GetTriggers();

		case 0x7D: return _temp_store.GetValue(parameter);

		case 0x7F:
			if (object.grffile == nullptr) return 0;
			return object.grffile->GetParam(parameter);

		default:
			/* First handle variables common with Action7/9/D */
			if (variable < 0x40 && GetGlobalVariable(variable, &value, object.grffile)) return value;
			/* Not a common variable, so evaluate the feature specific variables */
			return scope->GetVariable(variable, parameter, extra);
	}
}

/**
 * Get a few random bits. Default implementation has no random bits.
 * @return Random bits.
 */
/* virtual */ uint32 ScopeResolver::GetRandomBits() const
{
	return 0;
}

/**
 * Get the triggers. Base class returns \c 0 to prevent trouble.
 * @return The triggers.
 */
/* virtual */ uint32 ScopeResolver::GetTriggers() const
{
	return 0;
}

/**
 * Get a variable value. Default implementation has no available variables.
 * @param variable Variable to read
 * @param parameter Parameter for 60+x variables
 * @param[out] available Set to false, in case the variable does not exist.
 * @return Value
 */
/* virtual */ uint32 ScopeResolver::GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const
{
	DEBUG(grf, 1, "Unhandled scope variable 0x%X", variable);
	extra->available = false;
	return UINT_MAX;
}

/**
 * Store a value into the persistent storage area (PSA). Default implementation does nothing (for newgrf classes without storage).
 * @param reg Position to store into.
 * @param value Value to store.
 */
/* virtual */ void ScopeResolver::StorePSA(uint reg, int32 value) {}

/**
 * Get the real sprites of the grf.
 * @param group Group to get.
 * @return The available sprite group.
 */
/* virtual */ const SpriteGroup *ResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	if (!group->loaded.empty())  return group->loaded[0];
	if (!group->loading.empty()) return group->loading[0];

	return nullptr;
}

/**
 * Get a resolver for the \a scope.
 * @param scope Scope to return.
 * @param relative Additional parameter for #VSG_SCOPE_RELATIVE.
 * @return The resolver for the requested scope.
 */
/* virtual */ ScopeResolver *ResolverObject::GetScope(VarSpriteGroupScope scope, byte relative)
{
	return &this->default_scope;
}

/* Evaluate an adjustment for a variable of the given size.
 * U is the unsigned type and S is the signed type to use. */
template <typename U, typename S>
static U EvalAdjustT(const DeterministicSpriteGroupAdjust &adjust, ScopeResolver *scope, U last_value, uint32 value)
{
	value >>= adjust.shift_num;
	value  &= adjust.and_mask;

	switch (adjust.type) {
		case DSGA_TYPE_DIV:  value = ((S)value + (S)adjust.add_val) / (S)adjust.divmod_val; break;
		case DSGA_TYPE_MOD:  value = ((S)value + (S)adjust.add_val) % (S)adjust.divmod_val; break;
		case DSGA_TYPE_EQ:   value = (value == adjust.add_val) ? 1 : 0; break;
		case DSGA_TYPE_NEQ:  value = (value != adjust.add_val) ? 1 : 0; break;
		case DSGA_TYPE_NONE: break;
	}

	switch (adjust.operation) {
		case DSGA_OP_ADD:  return last_value + value;
		case DSGA_OP_SUB:  return last_value - value;
		case DSGA_OP_SMIN: return std::min<S>(last_value, value);
		case DSGA_OP_SMAX: return std::max<S>(last_value, value);
		case DSGA_OP_UMIN: return std::min<U>(last_value, value);
		case DSGA_OP_UMAX: return std::max<U>(last_value, value);
		case DSGA_OP_SDIV: return value == 0 ? (S)last_value : (S)last_value / (S)value;
		case DSGA_OP_SMOD: return value == 0 ? (S)last_value : (S)last_value % (S)value;
		case DSGA_OP_UDIV: return value == 0 ? (U)last_value : (U)last_value / (U)value;
		case DSGA_OP_UMOD: return value == 0 ? (U)last_value : (U)last_value % (U)value;
		case DSGA_OP_MUL:  return last_value * value;
		case DSGA_OP_AND:  return last_value & value;
		case DSGA_OP_OR:   return last_value | value;
		case DSGA_OP_XOR:  return last_value ^ value;
		case DSGA_OP_STO:  _temp_store.StoreValue((U)value, (S)last_value); return last_value;
		case DSGA_OP_RST:  return value;
		case DSGA_OP_STOP: scope->StorePSA((U)value, (S)last_value); return last_value;
		case DSGA_OP_ROR:  return ROR<uint32>((U)last_value, (U)value & 0x1F); // mask 'value' to 5 bits, which should behave the same on all architectures.
		case DSGA_OP_SCMP: return ((S)last_value == (S)value) ? 1 : ((S)last_value < (S)value ? 0 : 2);
		case DSGA_OP_UCMP: return ((U)last_value == (U)value) ? 1 : ((U)last_value < (U)value ? 0 : 2);
		case DSGA_OP_SHL:  return (uint32)(U)last_value << ((U)value & 0x1F); // Same behaviour as in ParamSet, mask 'value' to 5 bits, which should behave the same on all architectures.
		case DSGA_OP_SHR:  return (uint32)(U)last_value >> ((U)value & 0x1F);
		case DSGA_OP_SAR:  return (int32)(S)last_value >> ((U)value & 0x1F);
		case DSGA_OP_TERNARY: return (last_value != 0) ? value : adjust.add_val;
		case DSGA_OP_EQ:   return (last_value == value) ? 1 : 0;
		case DSGA_OP_SLT:  return ((S)last_value <  (S)value) ? 1 : 0;
		case DSGA_OP_SGE:  return ((S)last_value >= (S)value) ? 1 : 0;
		case DSGA_OP_SLE:  return ((S)last_value <= (S)value) ? 1 : 0;
		case DSGA_OP_SGT:  return ((S)last_value >  (S)value) ? 1 : 0;
		case DSGA_OP_RSUB: return value - last_value;
		case DSGA_OP_STO_NC: _temp_store.StoreValue(adjust.add_val, (S)value); return last_value;
		default:           return value;
	}
}

uint32 EvaluateDeterministicSpriteGroupAdjust(DeterministicSpriteGroupSize size, const DeterministicSpriteGroupAdjust &adjust, ScopeResolver *scope, uint32 last_value, uint32 value)
{
	switch (size) {
		case DSG_SIZE_BYTE:  return EvalAdjustT<uint8,  int8> (adjust, scope, last_value, value); break;
		case DSG_SIZE_WORD:  return EvalAdjustT<uint16, int16>(adjust, scope, last_value, value); break;
		case DSG_SIZE_DWORD: return EvalAdjustT<uint32, int32>(adjust, scope, last_value, value); break;
		default: NOT_REACHED();
	}
}

static bool RangeHighComparator(const DeterministicSpriteGroupRange& range, uint32 value)
{
	return range.high < value;
}

const SpriteGroup *DeterministicSpriteGroup::Resolve(ResolverObject &object) const
{
	uint32 last_value = 0;
	uint32 value = 0;

	ScopeResolver *scope = object.GetScope(this->var_scope);

	for (const auto &adjust : this->adjusts) {
		if ((adjust.adjust_flags & DSGAF_SKIP_ON_ZERO) && (last_value == 0)) continue;
		if ((adjust.adjust_flags & DSGAF_SKIP_ON_LSB_SET) && (last_value & 1) != 0) continue;

		/* Try to get the variable. We shall assume it is available, unless told otherwise. */
		GetVariableExtra extra(adjust.and_mask << adjust.shift_num);
		if (adjust.variable == 0x7E) {
			const SpriteGroup *subgroup = SpriteGroup::Resolve(adjust.subroutine, object, false);
			if (subgroup == nullptr) {
				value = CALLBACK_FAILED;
			} else {
				value = subgroup->GetCallbackResult();
			}

			/* Note: 'last_value' and 'reseed' are shared between the main chain and the procedure */
		} else if (adjust.variable == 0x7B) {
			_sprite_group_resolve_check_veh_check = false;
			value = GetVariable(object, scope, adjust.parameter, last_value, &extra);
		} else {
			value = GetVariable(object, scope, adjust.variable, adjust.parameter, &extra);
		}

		if (!extra.available) {
			/* Unsupported variable: skip further processing and return either
			 * the group from the first range or the default group. */
			return SpriteGroup::Resolve(this->error_group, object, false);
		}

		switch (this->size) {
			case DSG_SIZE_BYTE:  value = EvalAdjustT<uint8,  int8> (adjust, scope, last_value, value); break;
			case DSG_SIZE_WORD:  value = EvalAdjustT<uint16, int16>(adjust, scope, last_value, value); break;
			case DSG_SIZE_DWORD: value = EvalAdjustT<uint32, int32>(adjust, scope, last_value, value); break;
			default: NOT_REACHED();
		}
		last_value = value;
	}

	object.last_value = last_value;

	if (this->calculated_result) {
		/* nvar == 0 is a special case -- we turn our value into a callback result */
		if (value != CALLBACK_FAILED) value = GB(value, 0, 15);
		static CallbackResultSpriteGroup nvarzero(0);
		nvarzero.result = value;
		return &nvarzero;
	}

	if (this->ranges.size() > 4) {
		const auto &lower = std::lower_bound(this->ranges.begin(), this->ranges.end(), value, RangeHighComparator);
		if (lower != this->ranges.end() && lower->low <= value) {
			assert(lower->low <= value && value <= lower->high);
			return SpriteGroup::Resolve(lower->group, object, false);
		}
	} else {
		for (const auto &range : this->ranges) {
			if (range.low <= value && value <= range.high) {
				return SpriteGroup::Resolve(range.group, object, false);
			}
		}
	}

	return SpriteGroup::Resolve(this->default_group, object, false);
}

void DeterministicSpriteGroup::AnalyseCallbacks(AnalyseCallbackOperation &op) const
{
	auto res = op.seen.insert(this);
	if (!res.second) {
		/* Already seen this group */
		return;
	}

	if (op.mode == ACOM_INDUSTRY_TILE && op.data.indtile->anim_state_at_offset) return;

	auto check_1A_range = [&]() -> bool {
		if (this->GroupMayBeBypassed()) {
			/* Not clear why some GRFs do this, perhaps a way of commenting out a branch */
			uint32 value = (this->adjusts.size() == 1) ? EvaluateDeterministicSpriteGroupAdjust(this->size, this->adjusts[0], nullptr, 0, UINT_MAX) : 0;
			for (const auto &range : this->ranges) {
				if (range.low <= value && value <= range.high) {
					if (range.group != nullptr) range.group->AnalyseCallbacks(op);
					return true;
				}
			}
			if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
			return true;
		}
		return false;
	};

	if (op.mode == ACOM_FIND_CB_RESULT) {
		if (this->calculated_result) {
			op.result_flags |= ACORF_CB_RESULT_FOUND;
			return;
		} else if (!(op.result_flags & ACORF_CB_RESULT_FOUND)) {
			if (check_1A_range()) return;
			auto check_var_filter = [&](uint8 var, uint value) -> bool {
				if (this->adjusts.size() == 1 && this->adjusts[0].variable == var && (this->adjusts[0].operation == DSGA_OP_ADD || this->adjusts[0].operation == DSGA_OP_RST)) {
					const auto &adjust = this->adjusts[0];
					if (adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
						for (const auto &range : this->ranges) {
							if (range.low == range.high && range.low == value) {
								if (range.group != nullptr) range.group->AnalyseCallbacks(op);
								return true;
							}
						}
						if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
						return true;
					}
				}
				return false;
			};
			if (check_var_filter(0xC, op.data.cb_result.callback)) return;
			if (op.data.cb_result.check_var_10 && check_var_filter(0x10, op.data.cb_result.var_10_value)) return;
			for (const auto &range : this->ranges) {
				if (range.group != nullptr) range.group->AnalyseCallbacks(op);
			}
			if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
		}
		return;
	}

	if (check_1A_range()) return;

	if ((op.mode == ACOM_CB_VAR || op.mode == ACOM_CB_REFIT_CAPACITY) && this->var_scope != VSG_SCOPE_SELF) {
		op.result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;
	}

	auto find_cb_result = [&](const SpriteGroup *group, AnalyseCallbackOperation::FindCBResultData data) -> bool {
		if (group == nullptr) return false;
		AnalyseCallbackOperation cbr_op;
		cbr_op.mode = ACOM_FIND_CB_RESULT;
		cbr_op.data.cb_result = data;
		group->AnalyseCallbacks(cbr_op);
		return (cbr_op.result_flags & ACORF_CB_RESULT_FOUND);
	};

	if (this->adjusts.size() == 1 && !this->calculated_result && (this->adjusts[0].operation == DSGA_OP_ADD || this->adjusts[0].operation == DSGA_OP_RST)) {
		const auto &adjust = this->adjusts[0];
		if (op.mode == ACOM_CB_VAR && adjust.variable == 0xC) {
			if (adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
				bool found_refit_cap = false;
				for (const auto &range : this->ranges) {
					if (range.low == range.high) {
						switch (range.low) {
							case CBID_VEHICLE_32DAY_CALLBACK:
								op.callbacks_used |= SGCU_VEHICLE_32DAY_CALLBACK;
								break;

							case CBID_VEHICLE_REFIT_COST:
								op.callbacks_used |= SGCU_VEHICLE_REFIT_COST;
								break;

							case CBID_RANDOM_TRIGGER:
								op.callbacks_used |= SGCU_RANDOM_TRIGGER;
								break;

							case CBID_VEHICLE_MODIFY_PROPERTY:
								if (range.group != nullptr) {
									AnalyseCallbackOperation cb36_op;
									cb36_op.mode = ACOM_CB36_PROP;
									range.group->AnalyseCallbacks(cb36_op);
									op.properties_used |= cb36_op.properties_used;
									op.callbacks_used |= cb36_op.callbacks_used;
								}
								break;

							case CBID_VEHICLE_REFIT_CAPACITY:
								found_refit_cap = true;
								if (range.group != nullptr) {
									AnalyseCallbackOperation cb_refit_op;
									cb_refit_op.mode = ACOM_CB_REFIT_CAPACITY;
									range.group->AnalyseCallbacks(cb_refit_op);
									op.result_flags |= (cb_refit_op.result_flags & (ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND | ACORF_CB_REFIT_CAP_SEEN_VAR_47));
								}
								break;
						}
					} else {
						if (range.group != nullptr) range.group->AnalyseCallbacks(op);
					}
				}
				if (this->default_group != nullptr) {
					AnalyseCallbackOperationResultFlags prev_result = op.result_flags;
					this->default_group->AnalyseCallbacks(op);
					if (found_refit_cap) {
						const AnalyseCallbackOperationResultFlags save_mask = ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND | ACORF_CB_REFIT_CAP_SEEN_VAR_47;
						op.result_flags &= ~save_mask;
						op.result_flags |= (prev_result & save_mask);
					}
				}
				return;
			}
		}
		if (op.mode == ACOM_CB36_PROP && adjust.variable == 0x10) {
			if (adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
				for (const auto &range : this->ranges) {
					if (range.low == range.high) {
						if (range.low < 64) {
							if (find_cb_result(range.group, { CBID_VEHICLE_MODIFY_PROPERTY, true, (uint8)range.low })) {
								SetBit(op.properties_used, range.low);
								if (range.low == 0x9) {
									/* Speed */
									if (range.group != nullptr) {
										AnalyseCallbackOperation cb36_speed;
										cb36_speed.mode = ACOM_CB36_SPEED;
										range.group->AnalyseCallbacks(cb36_speed);
										op.callbacks_used |= cb36_speed.callbacks_used;
									}
								}
							}
						}
					} else {
						if (range.group != nullptr) range.group->AnalyseCallbacks(op);
					}
				}
				if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
				return;
			}
		}
		if (op.mode == ACOM_CB36_PROP && adjust.variable == 0xC) {
			if (adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
				for (const auto &range : this->ranges) {
					if (range.low <= CBID_VEHICLE_MODIFY_PROPERTY && CBID_VEHICLE_MODIFY_PROPERTY <= range.high) {
						if (range.group != nullptr) range.group->AnalyseCallbacks(op);
						return;
					}
				}
				if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
				return;
			}
		}
		if (op.mode == ACOM_CB36_SPEED && adjust.variable == 0x4A) {
			op.callbacks_used |= SGCU_CB36_SPEED_RAILTYPE;
			return;
		}
		if (op.mode == ACOM_INDUSTRY_TILE && adjust.variable == 0xC) {
			if (adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
				/* Callback switch, skip to the default/graphics chain */
				for (const auto &range : this->ranges) {
					if (range.low == 0) {
						if (range.group != nullptr) range.group->AnalyseCallbacks(op);
						return;
					}
				}
				if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
				return;
			}
		}
		if (op.mode == ACOM_INDUSTRY_TILE && adjust.variable == 0x44 && this->var_scope == VSG_SCOPE_PARENT) {
			if (adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
				/* Layout index switch */
				for (const auto &range : this->ranges) {
					if (range.low <= op.data.indtile->layout_index && op.data.indtile->layout_index <= range.high) {
						if (range.group != nullptr) range.group->AnalyseCallbacks(op);
						return;
					}
				}
				if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
				return;
			}
		}
		if (op.mode == ACOM_INDUSTRY_TILE && adjust.variable == 0x43 && this->var_scope == VSG_SCOPE_SELF) {
			if (adjust.shift_num == 0 && adjust.and_mask == 0xFFFF && adjust.type == DSGA_TYPE_NONE) {
				/* Relative position switch */
				uint64 default_mask = op.data.indtile->check_mask;
				for (const auto &range : this->ranges) {
					if (range.high - range.low < 32) {
						uint64 new_check_mask = 0;
						for (uint i = range.low; i <= range.high; i++) {
							int16 x = i & 0xFF;
							int16 y = (i >> 8) & 0xFF;
							for (uint bit : SetBitIterator<uint, uint64>(op.data.indtile->check_mask)) {
								const TileIndexDiffC &ti = (*(op.data.indtile->layout))[bit].ti;
								if (ti.x == x && ti.y == y) {
									SetBit(new_check_mask, bit);
								}
							}
						}
						default_mask &= ~new_check_mask;
						if (range.group != nullptr) {
							AnalyseCallbackOperationIndustryTileData data = *(op.data.indtile);
							data.check_mask = new_check_mask;

							AnalyseCallbackOperation sub_op;
							sub_op.mode = ACOM_INDUSTRY_TILE;
							sub_op.data.indtile = &data;
							range.group->AnalyseCallbacks(sub_op);

							if (data.anim_state_at_offset) {
								op.data.indtile->anim_state_at_offset = true;
								return;
							}
						}
					} else {
						if (range.group != nullptr) range.group->AnalyseCallbacks(op);
					}
				}
				if (this->default_group != nullptr) {
					AnalyseCallbackOperationIndustryTileData data = *(op.data.indtile);
					data.check_mask = default_mask;

					AnalyseCallbackOperation sub_op;
					sub_op.mode = ACOM_INDUSTRY_TILE;
					sub_op.data.indtile = &data;

					this->default_group->AnalyseCallbacks(sub_op);
				}
				return;
			}
		}
	}
	for (const auto &adjust : this->adjusts) {
		if (op.mode == ACOM_CB_VAR && adjust.variable == 0xC) {
			op.callbacks_used |= SGCU_ALL;
		}
		if (op.mode == ACOM_CB36_PROP && adjust.variable == 0x10) {
			if (find_cb_result(this, { CBID_VEHICLE_MODIFY_PROPERTY, false, 0 })) {
				op.properties_used |= UINT64_MAX;
			}
		}
		if ((op.mode == ACOM_CB_VAR || op.mode == ACOM_CB_REFIT_CAPACITY) && !(adjust.variable == 0xC || adjust.variable == 0x1A || adjust.variable == 0x47 || adjust.variable == 0x7D || adjust.variable == 0x7E)) {
			op.result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;
		}
		if ((op.mode == ACOM_CB_VAR || op.mode == ACOM_CB_REFIT_CAPACITY) && adjust.variable == 0x47) {
			op.result_flags |= ACORF_CB_REFIT_CAP_SEEN_VAR_47;
		}
		if (adjust.variable == 0x7E && adjust.subroutine != nullptr) {
			adjust.subroutine->AnalyseCallbacks(op);
		}
		if (op.mode == ACOM_INDUSTRY_TILE && this->var_scope == VSG_SCOPE_SELF && (adjust.variable == 0x44 || (adjust.variable == 0x61 && adjust.parameter == 0))) {
			*(op.data.indtile->result_mask) &= ~op.data.indtile->check_mask;
			return;
		}
		if (op.mode == ACOM_INDUSTRY_TILE && ((this->var_scope == VSG_SCOPE_SELF && adjust.variable == 0x61) || (this->var_scope == VSG_SCOPE_PARENT && adjust.variable == 0x63))) {
			op.data.indtile->anim_state_at_offset = true;
			return;
		}
	}
	if (!this->calculated_result) {
		for (const auto &range : this->ranges) {
			if (range.group != nullptr) range.group->AnalyseCallbacks(op);
		}
		if (this->default_group != nullptr) this->default_group->AnalyseCallbacks(op);
	}
}

bool DeterministicSpriteGroup::GroupMayBeBypassed() const
{
	if (this->calculated_result) return false;
	if (this->adjusts.size() == 0) return true;
	if ((this->adjusts.size() == 1 && this->adjusts[0].variable == 0x1A && (this->adjusts[0].operation == DSGA_OP_ADD || this->adjusts[0].operation == DSGA_OP_RST))) return true;
	return false;
}

void CallbackResultSpriteGroup::AnalyseCallbacks(AnalyseCallbackOperation &op) const
{
	if (op.mode == ACOM_FIND_CB_RESULT) op.result_flags |= ACORF_CB_RESULT_FOUND;
}

const SpriteGroup *RandomizedSpriteGroup::Resolve(ResolverObject &object) const
{
	ScopeResolver *scope = object.GetScope(this->var_scope, this->count);
	if (object.callback == CBID_RANDOM_TRIGGER) {
		/* Handle triggers */
		byte match = this->triggers & object.waiting_triggers;
		bool res = (this->cmp_mode == RSG_CMP_ANY) ? (match != 0) : (match == this->triggers);

		if (res) {
			object.used_triggers |= match;
			object.reseed[this->var_scope] |= (this->groups.size() - 1) << this->lowest_randbit;
		}
	}

	uint32 mask = ((uint)this->groups.size() - 1) << this->lowest_randbit;
	byte index = (scope->GetRandomBits() & mask) >> this->lowest_randbit;

	return SpriteGroup::Resolve(this->groups[index], object, false);
}

void RandomizedSpriteGroup::AnalyseCallbacks(AnalyseCallbackOperation &op) const
{
	op.result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;

	if (op.mode == ACOM_CB_VAR) op.callbacks_used |= SGCU_RANDOM_TRIGGER;

	for (const SpriteGroup *group: this->groups) {
		if (group != nullptr) group->AnalyseCallbacks(op);
	}
}

const SpriteGroup *RealSpriteGroup::Resolve(ResolverObject &object) const
{
	return object.ResolveReal(this);
}

/**
 * Process registers and the construction stage into the sprite layout.
 * The passed construction stage might get reset to zero, if it gets incorporated into the layout
 * during the preprocessing.
 * @param[in,out] stage Construction stage (0-3), or nullptr if not applicable.
 * @return sprite layout to draw.
 */
const DrawTileSprites *TileLayoutSpriteGroup::ProcessRegisters(uint8 *stage) const
{
	if (!this->dts.NeedsPreprocessing()) {
		if (stage != nullptr && this->dts.consistent_max_offset > 0) *stage = GetConstructionStageOffset(*stage, this->dts.consistent_max_offset);
		return &this->dts;
	}

	static DrawTileSprites result;
	uint8 actual_stage = stage != nullptr ? *stage : 0;
	this->dts.PrepareLayout(0, 0, 0, actual_stage, false);
	this->dts.ProcessRegisters(0, 0, false);
	result.seq = this->dts.GetLayout(&result.ground);

	/* Stage has been processed by PrepareLayout(), set it to zero. */
	if (stage != nullptr) *stage = 0;

	return &result;
}

static const char *_dsg_op_names[] {
	"ADD",
	"SUB",
	"SMIN",
	"SMAX",
	"UMIN",
	"UMAX",
	"SDIV",
	"SMOD",
	"UDIV",
	"UMOD",
	"MUL",
	"AND",
	"OR",
	"XOR",
	"STO",
	"RST",
	"STOP",
	"ROR",
	"SCMP",
	"UCMP",
	"SHL",
	"SHR",
	"SAR",
};
static_assert(lengthof(_dsg_op_names) == DSGA_OP_END);

static const char *_dsg_op_special_names[] {
	"TERNARY",
	"EQ",
	"SLT",
	"SGE",
	"SLE",
	"SGT",
	"RSUB",
	"STO_NC",
};
static_assert(lengthof(_dsg_op_special_names) == DSGA_OP_SPECIAL_END - DSGA_OP_TERNARY);

static const char *_sg_scope_names[] {
	"SELF",
	"PARENT",
	"RELATIVE",
};
static_assert(lengthof(_sg_scope_names) == VSG_END);

static const char *_sg_size_names[] {
	"BYTE",
	"WORD",
	"DWORD",
};

static char *GetAdjustOperationName(char *str, const char *last, DeterministicSpriteGroupAdjustOperation operation)
{
	if (operation < DSGA_OP_END) return strecat(str, _dsg_op_names[operation], last);
	if (operation >= DSGA_OP_TERNARY && operation < DSGA_OP_SPECIAL_END) return strecat(str, _dsg_op_special_names[operation - DSGA_OP_TERNARY], last);
	return str + seprintf(str, last, "\?\?\?(0x%X)", operation);
}

void SpriteGroupDumper::DumpSpriteGroup(const SpriteGroup *sg, int padding, uint flags)
{
	uint32 highlight_tag = 0;
	auto print = [&]() {
		this->print_fn(sg, DSGPO_PRINT, highlight_tag, this->buffer);
		highlight_tag = 0;
	};

	if (sg == nullptr) {
		seprintf(this->buffer, lastof(this->buffer), "%*sNULL GROUP", padding, "");
		print();
		return;
	}

	bool start_emitted = false;
	auto emit_start = [&]() {
		this->print_fn(sg, DSGPO_START, 0, nullptr);
		start_emitted = true;
	};
	auto guard = scope_guard([&]() {
		if (start_emitted) {
			this->print_fn(sg, DSGPO_END, padding, nullptr);
		}
	});

	switch (sg->type) {
		case SGT_REAL: {
			const RealSpriteGroup *rsg = (const RealSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%*sReal (loaded: %u, loading: %u) [%u]",
					padding, "", (uint)rsg->loaded.size(), (uint)rsg->loading.size(), sg->nfo_line);
			print();
			emit_start();
			for (size_t i = 0; i < rsg->loaded.size(); i++) {
				seprintf(this->buffer, lastof(this->buffer), "%*sLoaded %u", padding + 2, "", (uint)i);
				print();
				this->DumpSpriteGroup(rsg->loaded[i], padding + 4, 0);
			}
			for (size_t i = 0; i < rsg->loading.size(); i++) {
				seprintf(this->buffer, lastof(this->buffer), "%*sLoading %u", padding + 2, "", (uint)i);
				print();
				this->DumpSpriteGroup(rsg->loading[i], padding + 4, 0);
			}
			break;
		}
		case SGT_DETERMINISTIC: {
			const DeterministicSpriteGroup *dsg = (const DeterministicSpriteGroup*)sg;
			if (padding == 0 && !dsg->calculated_result && dsg->default_group != nullptr) {
				this->top_default_group = dsg->default_group;
			}
			if (dsg == this->top_default_group && !(padding == 4 && (flags & SGDF_DEFAULT))) {
				seprintf(this->buffer, lastof(this->buffer), "%*sTOP LEVEL DEFAULT GROUP: Deterministic (%s, %s), [%u]",
						padding, "", _sg_scope_names[dsg->var_scope], _sg_size_names[dsg->size], dsg->nfo_line);
				print();
				return;
			}
			auto res = this->seen_dsgs.insert(dsg);
			if (!res.second) {
				seprintf(this->buffer, lastof(this->buffer), "%*sGROUP SEEN ABOVE: Deterministic (%s, %s), [%u]",
						padding, "", _sg_scope_names[dsg->var_scope], _sg_size_names[dsg->size], dsg->nfo_line);
				print();
				return;
			}
			seprintf(this->buffer, lastof(this->buffer), "%*sDeterministic (%s, %s), [%u]",
					padding, "", _sg_scope_names[dsg->var_scope], _sg_size_names[dsg->size], dsg->nfo_line);
			print();
			emit_start();
			padding += 2;
			for (const auto &adjust : dsg->adjusts) {
				char *p = this->buffer;
				if (adjust.variable == 0x7D) {
					/* Temp storage load */
					highlight_tag = (1 << 16) | (adjust.parameter & 0xFFFF);
				}

				auto append_flags = [&]() {
					if (adjust.adjust_flags & DSGAF_SKIP_ON_ZERO) {
						p += seprintf(p, lastof(this->buffer), ", skip on zero");
					}
					if (adjust.adjust_flags & DSGAF_SKIP_ON_LSB_SET) {
						p += seprintf(p, lastof(this->buffer), ", skip on LSB set");
					}
				};

				if (adjust.operation == DSGA_OP_TERNARY) {
					p += seprintf(p, lastof(this->buffer), "%*sTERNARY: true: %X, false: %X", padding, "", adjust.and_mask, adjust.add_val);
					append_flags();
					print();
					continue;
				}
				if (adjust.operation == DSGA_OP_STO && adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0) {
					/* Temp storage store */
					highlight_tag = (1 << 16) | (adjust.and_mask & 0xFFFF);
				}
				p += seprintf(p, lastof(this->buffer), "%*svar: %X", padding, "", adjust.variable);
				if (adjust.variable == A2VRI_VEHICLE_CURRENT_SPEED_SCALED) {
					p += seprintf(p, lastof(this->buffer), " (current_speed_scaled)");
				} else if (adjust.variable >= 0x100) {
					extern const GRFVariableMapDefinition _grf_action2_remappable_variables[];
					for (const GRFVariableMapDefinition *info = _grf_action2_remappable_variables; info->name != nullptr; info++) {
						if (adjust.variable == info->id) {
							p += seprintf(p, lastof(this->buffer), " (%s)", info->name);
							break;
						}
					}
				}
				if ((adjust.variable >= 0x60 && adjust.variable <= 0x7F && adjust.variable != 0x7E) || adjust.parameter != 0) p += seprintf(p, lastof(this->buffer), " (parameter: %X)", adjust.parameter);
				p += seprintf(p, lastof(this->buffer), ", shift: %X, and: %X", adjust.shift_num, adjust.and_mask);
				switch (adjust.type) {
					case DSGA_TYPE_DIV: p += seprintf(p, lastof(this->buffer), ", add: %X, div: %X", adjust.add_val, adjust.divmod_val); break;
					case DSGA_TYPE_MOD: p += seprintf(p, lastof(this->buffer), ", add: %X, mod: %X", adjust.add_val, adjust.divmod_val); break;
					case DSGA_TYPE_EQ:  p += seprintf(p, lastof(this->buffer), ", eq: %X", adjust.add_val); break;
					case DSGA_TYPE_NEQ: p += seprintf(p, lastof(this->buffer), ", neq: %X", adjust.add_val); break;
					case DSGA_TYPE_NONE: break;
				}
				if (adjust.operation == DSGA_OP_STO_NC) {
					p += seprintf(p, lastof(this->buffer), ", store to: %X", adjust.add_val);
					highlight_tag = (1 << 16) | adjust.add_val;
				}
				p += seprintf(p, lastof(this->buffer), ", op: ");
				p = GetAdjustOperationName(p, lastof(this->buffer), adjust.operation);
				append_flags();
				print();
				if (adjust.variable == 0x7E && adjust.subroutine != nullptr) {
					this->DumpSpriteGroup(adjust.subroutine, padding + 5, 0);
				}
			}
			if (dsg->calculated_result) {
				seprintf(this->buffer, lastof(this->buffer), "%*scalculated_result", padding, "");
				print();
			} else {
				for (const auto &range : dsg->ranges) {
					seprintf(this->buffer, lastof(this->buffer), "%*srange: %X -> %X", padding, "", range.low, range.high);
					print();
					this->DumpSpriteGroup(range.group, padding + 2, 0);
				}
				if (dsg->default_group != nullptr) {
					seprintf(this->buffer, lastof(this->buffer), "%*sdefault", padding, "");
					print();
					this->DumpSpriteGroup(dsg->default_group, padding + 2, SGDF_DEFAULT);
				}
			}
			break;
		}
		case SGT_RANDOMIZED: {
			const RandomizedSpriteGroup *rsg = (const RandomizedSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%*sRandom (%s, %s, triggers: %X, count: %X, lowest_randbit: %X, groups: %u) [%u]",
					padding, "", _sg_scope_names[rsg->var_scope], rsg->cmp_mode == RSG_CMP_ANY ? "ANY" : "ALL",
					rsg->triggers, rsg->count, rsg->lowest_randbit, (uint)rsg->groups.size(), rsg->nfo_line);
			print();
			emit_start();
			for (const auto &group : rsg->groups) {
				this->DumpSpriteGroup(group, padding + 2, 0);
			}
			break;
		}
		case SGT_CALLBACK:
			seprintf(this->buffer, lastof(this->buffer), "%*sCallback Result: %X", padding, "", ((const CallbackResultSpriteGroup *) sg)->result);
			print();
			break;
		case SGT_RESULT:
			seprintf(this->buffer, lastof(this->buffer), "%*sSprite Result: SpriteID: %u, num: %u",
					padding, "", ((const ResultSpriteGroup *) sg)->sprite, ((const ResultSpriteGroup *) sg)->num_sprites);
			print();
			break;
		case SGT_TILELAYOUT: {
			const TileLayoutSpriteGroup *tlsg = (const TileLayoutSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%*sTile Layout [%u]", padding, "", sg->nfo_line);
			print();
			emit_start();
			padding += 2;
			if (tlsg->dts.registers != nullptr) {
				const TileLayoutRegisters *registers = tlsg->dts.registers;
				size_t count = 1; // 1 for the ground sprite
				const DrawTileSeqStruct *element;
				foreach_draw_tile_seq(element, tlsg->dts.seq) count++;
				for (size_t i = 0; i < count; i ++) {
					const TileLayoutRegisters *reg = registers + i;
					seprintf(this->buffer, lastof(this->buffer), "%*ssection: %X, register flags: %X", padding, "", (uint)i, reg->flags);
					print();
					auto log_reg = [&](TileLayoutFlags flag, const char *name, uint8 flag_reg) {
						if (reg->flags & flag) {
							highlight_tag = (1 << 16) | flag_reg;
							seprintf(this->buffer, lastof(this->buffer), "%*s%s reg: %X", padding + 2, "", name, flag_reg);
							print();
						}
					};
					log_reg(TLF_DODRAW, "TLF_DODRAW", reg->dodraw);
					log_reg(TLF_SPRITE, "TLF_SPRITE", reg->sprite);
					log_reg(TLF_PALETTE, "TLF_PALETTE", reg->palette);
					log_reg(TLF_BB_XY_OFFSET, "TLF_BB_XY_OFFSET x", reg->delta.parent[0]);
					log_reg(TLF_BB_XY_OFFSET, "TLF_BB_XY_OFFSET y", reg->delta.parent[1]);
					log_reg(TLF_BB_Z_OFFSET, "TLF_BB_Z_OFFSET", reg->delta.parent[2]);
					log_reg(TLF_CHILD_X_OFFSET, "TLF_CHILD_X_OFFSET", reg->delta.child[0]);
					log_reg(TLF_CHILD_Y_OFFSET, "TLF_CHILD_Y_OFFSET", reg->delta.child[1]);
					if (reg->flags & TLF_SPRITE_VAR10) {
						seprintf(this->buffer, lastof(this->buffer), "%*sTLF_SPRITE_VAR10 value: %X", padding + 2, "", reg->sprite_var10);
						print();
					}
					if (reg->flags & TLF_PALETTE_VAR10) {
						seprintf(this->buffer, lastof(this->buffer), "%*sTLF_PALETTE_VAR10 value: %X", padding + 2, "", reg->palette_var10);
						print();
					}
				}
			}
			break;
		}
		case SGT_INDUSTRY_PRODUCTION: {
			const IndustryProductionSpriteGroup *ipsg = (const IndustryProductionSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%*sIndustry Production (version %X) [%u]", padding, "", ipsg->version, ipsg->nfo_line);
			print();
			emit_start();
			auto log_io = [&](const char *prefix, int i, int quantity, CargoID cargo) {
				if (ipsg->version >= 1) highlight_tag = (1 << 16) | quantity;
				if (ipsg->version >= 2) {
					seprintf(this->buffer, lastof(this->buffer), "%*s%s %X: reg %X, cargo ID: %X", padding + 2, "", prefix, i, quantity, cargo);
					print();
				} else {
					const char *type = (ipsg->version >= 1) ? "reg" : "value";
					seprintf(this->buffer, lastof(this->buffer), "%*s%s %X: %s %X", padding + 2, "", prefix, i, type, quantity);
					print();
				}
			};
			for (int i = 0; i < ipsg->num_input; i++) {
				log_io("Subtract input", i, ipsg->subtract_input[i], ipsg->cargo_input[i]);
			}
			for (int i = 0; i < ipsg->num_output; i++) {
				log_io("Add input", i, ipsg->add_output[i], ipsg->cargo_output[i]);
			}
			if (ipsg->version >= 1) highlight_tag = (1 << 16) | ipsg->again;
			seprintf(this->buffer, lastof(this->buffer), "%*sAgain: %s %X", padding + 2, "", (ipsg->version >= 1) ? "reg" : "value", ipsg->again);
			print();
			break;
		}
	}
}

void DumpSpriteGroup(const SpriteGroup *sg, DumpSpriteGroupPrinter print)
{
	SpriteGroupDumper dumper(std::move(print));
	dumper.DumpSpriteGroup(sg, 0, 0);
}
