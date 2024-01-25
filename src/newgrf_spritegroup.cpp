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
#include "scope.h"
#include "debug_settings.h"
#include "newgrf_engine.h"
#include <bit>

#include "safeguards.h"

SpriteGroupPool _spritegroup_pool("SpriteGroup");
INSTANTIATE_POOL_METHODS(SpriteGroup)

TemporaryStorageArray<int32_t, 0x110> _temp_store;

std::map<const DeterministicSpriteGroup *, DeterministicSpriteGroupShadowCopy> _deterministic_sg_shadows;
std::map<const RandomizedSpriteGroup *, RandomizedSpriteGroupShadowCopy> _randomized_sg_shadows;
bool _grfs_loaded_with_sg_shadow_enable = false;

GrfSpecFeature GetGrfSpecFeatureForParentScope(GrfSpecFeature feature)
{
	switch (feature) {
		case GSF_STATIONS:
		case GSF_BRIDGES:
		case GSF_HOUSES:
		case GSF_INDUSTRIES:
		case GSF_OBJECTS:
		case GSF_ROADSTOPS:
			return GSF_FAKE_TOWNS;

		case GSF_INDUSTRYTILES:
			return GSF_INDUSTRIES;

		default:
			return feature;
	}
}

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

static inline uint32_t GetVariable(const ResolverObject &object, ScopeResolver *scope, uint16_t variable, uint32_t parameter, GetVariableExtra *extra)
{
	uint32_t value;
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
/* virtual */ uint32_t ScopeResolver::GetRandomBits() const
{
	return 0;
}

/**
 * Get the triggers. Base class returns \c 0 to prevent trouble.
 * @return The triggers.
 */
/* virtual */ uint32_t ScopeResolver::GetTriggers() const
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
/* virtual */ uint32_t ScopeResolver::GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra *extra) const
{
	DEBUG(grf, 1, "Unhandled scope variable 0x%X", variable);
	extra->available = false;
	return UINT_MAX;
}

/**
 * Store a value into the persistent storage area (PSA). Default implementation does nothing (for newgrf classes without storage).
 */
/* virtual */ void ScopeResolver::StorePSA(uint reg, int32_t value) {}

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
 * @return The resolver for the requested scope.
 */
/* virtual */ ScopeResolver *ResolverObject::GetScope(VarSpriteGroupScope scope, VarSpriteGroupScopeOffset relative)
{
	return &this->default_scope;
}

/* Evaluate an adjustment for a variable of the given size.
 * U is the unsigned type and S is the signed type to use. */
template <typename U, typename S>
static U EvalAdjustT(const DeterministicSpriteGroupAdjust &adjust, ScopeResolver *scope, U last_value, uint32_t value, const DeterministicSpriteGroupAdjust **adjust_iter = nullptr)
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

	auto handle_jump = [&](bool jump, U jump_return_value) -> U {
		if (jump && adjust_iter != nullptr) {
			/* Jump */
			(*adjust_iter) += adjust.jump;
			return jump_return_value;
		} else {
			/* Don't jump */
			return last_value;
		}
	};

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
		case DSGA_OP_ROR:  return std::rotr<uint32_t>((U)last_value, (U)value & 0x1F); // mask 'value' to 5 bits, which should behave the same on all architectures.
		case DSGA_OP_SCMP: return ((S)last_value == (S)value) ? 1 : ((S)last_value < (S)value ? 0 : 2);
		case DSGA_OP_UCMP: return ((U)last_value == (U)value) ? 1 : ((U)last_value < (U)value ? 0 : 2);
		case DSGA_OP_SHL:  return (uint32_t)(U)last_value << ((U)value & 0x1F); // Same behaviour as in ParamSet, mask 'value' to 5 bits, which should behave the same on all architectures.
		case DSGA_OP_SHR:  return (uint32_t)(U)last_value >> ((U)value & 0x1F);
		case DSGA_OP_SAR:  return (int32_t)(S)last_value >> ((U)value & 0x1F);
		case DSGA_OP_TERNARY: return (last_value != 0) ? value : adjust.add_val;
		case DSGA_OP_EQ:   return (last_value == value) ? 1 : 0;
		case DSGA_OP_SLT:  return ((S)last_value <  (S)value) ? 1 : 0;
		case DSGA_OP_SGE:  return ((S)last_value >= (S)value) ? 1 : 0;
		case DSGA_OP_SLE:  return ((S)last_value <= (S)value) ? 1 : 0;
		case DSGA_OP_SGT:  return ((S)last_value >  (S)value) ? 1 : 0;
		case DSGA_OP_RSUB: return value - last_value;
		case DSGA_OP_STO_NC: _temp_store.StoreValue(adjust.divmod_val, (S)value); return last_value;
		case DSGA_OP_ABS:  return ((S)last_value < 0) ? -((S)last_value) : (S)last_value;
		case DSGA_OP_JZ:     return handle_jump(value == 0, value);
		case DSGA_OP_JNZ:    return handle_jump(value != 0, value);
		case DSGA_OP_JZ_LV:  return handle_jump(last_value == 0, last_value);
		case DSGA_OP_JNZ_LV: return handle_jump(last_value != 0, last_value);
		case DSGA_OP_NOOP: return last_value;
		default:           return value;
	}
}

uint32_t EvaluateDeterministicSpriteGroupAdjust(DeterministicSpriteGroupSize size, const DeterministicSpriteGroupAdjust &adjust, ScopeResolver *scope, uint32_t last_value, uint32_t value)
{
	switch (size) {
		case DSG_SIZE_BYTE:  return EvalAdjustT<uint8_t,  int8_t> (adjust, scope, last_value, value); break;
		case DSG_SIZE_WORD:  return EvalAdjustT<uint16_t, int16_t>(adjust, scope, last_value, value); break;
		case DSG_SIZE_DWORD: return EvalAdjustT<uint32_t, int32_t>(adjust, scope, last_value, value); break;
		default: NOT_REACHED();
	}
}

static bool RangeHighComparator(const DeterministicSpriteGroupRange &range, uint32_t value)
{
	return range.high < value;
}

const SpriteGroup *DeterministicSpriteGroup::Resolve(ResolverObject &object) const
{
	if ((this->sg_flags & SGF_SKIP_CB) != 0 && object.callback > 1) {
		static CallbackResultSpriteGroup cbfail(CALLBACK_FAILED);
		return &cbfail;
	}

	uint32_t last_value = 0;
	uint32_t value = 0;

	ScopeResolver *scope = object.GetScope(this->var_scope, this->var_scope_count);

	const DeterministicSpriteGroupAdjust *end = this->adjusts.data() + this->adjusts.size();
	for (const DeterministicSpriteGroupAdjust *iter = this->adjusts.data(); iter != end; ++iter) {
		const DeterministicSpriteGroupAdjust &adjust = *iter;

		if ((adjust.adjust_flags & DSGAF_SKIP_ON_ZERO) && (last_value == 0)) continue;
		if ((adjust.adjust_flags & DSGAF_SKIP_ON_LSB_SET) && (last_value & 1) != 0) continue;

		/* Try to get the variable. We shall assume it is available, unless told otherwise. */
		GetVariableExtra extra(adjust.and_mask << adjust.shift_num);
		if (adjust.variable == 0x7E) {
			const Vehicle *relative_scope_vehicle = nullptr;
			VarSpriteGroupScopeOffset relative_scope_cached_count = 0;
			if (this->var_scope == VSG_SCOPE_RELATIVE) {
				/* Save relative scope vehicle in case it will be changed during the procedure */
				VehicleResolverObject *veh_object = dynamic_cast<VehicleResolverObject *>(&object);
				if (veh_object != nullptr) {
					relative_scope_vehicle = veh_object->relative_scope.v;
					relative_scope_cached_count = veh_object->cached_relative_count;
				}
			}

			const SpriteGroup *subgroup = SpriteGroup::Resolve(adjust.subroutine, object, false);
			if (subgroup == nullptr) {
				value = CALLBACK_FAILED;
			} else {
				value = subgroup->GetCallbackResult();
			}

			if (relative_scope_vehicle != nullptr) {
				/* Reset relative scope vehicle in case it was changed during the procedure */
				VehicleResolverObject *veh_object = static_cast<VehicleResolverObject *>(&object);
				veh_object->relative_scope.v = relative_scope_vehicle;
				veh_object->cached_relative_count = relative_scope_cached_count;
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
			case DSG_SIZE_BYTE:  value = EvalAdjustT<uint8_t,  int8_t> (adjust, scope, last_value, value, &iter); break;
			case DSG_SIZE_WORD:  value = EvalAdjustT<uint16_t, int16_t>(adjust, scope, last_value, value, &iter); break;
			case DSG_SIZE_DWORD: value = EvalAdjustT<uint32_t, int32_t>(adjust, scope, last_value, value, &iter); break;
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

bool DeterministicSpriteGroup::GroupMayBeBypassed() const
{
	if (this->calculated_result) return false;
	if (this->adjusts.size() == 0) return true;
	if ((this->adjusts.size() == 1 && this->adjusts[0].variable == 0x1A && (this->adjusts[0].operation == DSGA_OP_ADD || this->adjusts[0].operation == DSGA_OP_RST))) return true;
	return false;
}

const SpriteGroup *RandomizedSpriteGroup::Resolve(ResolverObject &object) const
{
	ScopeResolver *scope = object.GetScope(this->var_scope, this->var_scope_count);
	if (object.callback == CBID_RANDOM_TRIGGER) {
		/* Handle triggers */
		byte match = this->triggers & object.waiting_triggers;
		bool res = (this->cmp_mode == RSG_CMP_ANY) ? (match != 0) : (match == this->triggers);

		if (res) {
			object.used_triggers |= match;
			object.reseed[this->var_scope] |= (this->groups.size() - 1) << this->lowest_randbit;
		}
	}

	uint32_t mask = ((uint)this->groups.size() - 1) << this->lowest_randbit;
	byte index = (scope->GetRandomBits() & mask) >> this->lowest_randbit;

	return SpriteGroup::Resolve(this->groups[index], object, false);
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
const DrawTileSprites *TileLayoutSpriteGroup::ProcessRegisters(uint8_t *stage) const
{
	if (!this->dts.NeedsPreprocessing()) {
		if (stage != nullptr && this->dts.consistent_max_offset > 0) *stage = GetConstructionStageOffset(*stage, this->dts.consistent_max_offset);
		return &this->dts;
	}

	static DrawTileSprites result;
	uint8_t actual_stage = stage != nullptr ? *stage : 0;
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
	"ABS",
	"JZ",
	"JNZ",
	"JZ_LV",
	"JNZ_LV",
	"NOOP",
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

static const char *_sg_relative_scope_modes[] {
	"BACKWARD_SELF",
	"FORWARD_SELF",
	"BACKWARD_ENGINE",
	"BACKWARD_SAMEID",
};
static_assert(lengthof(_sg_relative_scope_modes) == VSGSRM_END);

static char *GetAdjustOperationName(char *str, const char *last, DeterministicSpriteGroupAdjustOperation operation)
{
	if (operation < DSGA_OP_END) return strecat(str, _dsg_op_names[operation], last);
	if (operation >= DSGA_OP_TERNARY && operation < DSGA_OP_SPECIAL_END) return strecat(str, _dsg_op_special_names[operation - DSGA_OP_TERNARY], last);
	return str + seprintf(str, last, "\?\?\?(0x%X)", operation);
}

char *SpriteGroupDumper::DumpSpriteGroupAdjust(char *p, const char *last, const DeterministicSpriteGroupAdjust &adjust, const char *padding, uint32_t &highlight_tag, uint &conditional_indent)
{
	if (adjust.variable == 0x7D) {
		/* Temp storage load */
		highlight_tag = (1 << 16) | (adjust.parameter & 0xFFFF);
	}
	if (adjust.variable == 0x7C) {
		/* Perm storage load */
		highlight_tag = (2 << 16) | (adjust.parameter & 0xFFFF);
	}

	p += seprintf(p, last, "%s", padding);
	for (uint i = 0; i < conditional_indent; i++) {
		p += seprintf(p, last, "> ");
	}

	auto append_flags = [&]() {
		if (adjust.adjust_flags & DSGAF_SKIP_ON_ZERO) {
			p += seprintf(p, last, ", skip on zero");
		}
		if (adjust.adjust_flags & DSGAF_SKIP_ON_LSB_SET) {
			p += seprintf(p, last, ", skip on LSB set");
		}
		if (adjust.adjust_flags & DSGAF_LAST_VAR_READ && this->more_details) {
			p += seprintf(p, last, ", last var read");
		}
		if (adjust.adjust_flags & DSGAF_JUMP_INS_HINT && this->more_details) {
			p += seprintf(p, last, ", jump ins hint");
		}
		if (adjust.adjust_flags & DSGAF_END_BLOCK) {
			p += seprintf(p, last, ", end block (%u)", adjust.jump);
		}
	};

	auto append_extended_var = [&](int var_id) {
		const char *name = GetExtendedVariableNameById(var_id);
		if (name != nullptr) {
			p += seprintf(p, last, " (%s)", name);
		}
	};

	if (IsEvalAdjustJumpOperation(adjust.operation)) {
		conditional_indent++;
	}
	if (adjust.adjust_flags & DSGAF_END_BLOCK) {
		conditional_indent -= adjust.jump;
	}

	if (adjust.operation == DSGA_OP_TERNARY) {
		p += seprintf(p, last, "TERNARY: true: %X, false: %X", adjust.and_mask, adjust.add_val);
		append_flags();
		return p;
	}
	if (adjust.operation == DSGA_OP_ABS) {
		p += seprintf(p, last, "ABS");
		append_flags();
		return p;
	}
	if (adjust.operation == DSGA_OP_NOOP) {
		p += seprintf(p, last, "NOOP");
		append_flags();
		return p;
	}
	if (adjust.operation == DSGA_OP_JZ_LV || adjust.operation == DSGA_OP_JNZ_LV) {
		p = GetAdjustOperationName(p, last, adjust.operation);
		p += seprintf(p, last, " +%u", adjust.jump);
		append_flags();
		return p;
	}
	if (adjust.operation == DSGA_OP_STO && adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0) {
		/* Temp storage store */
		highlight_tag = (1 << 16) | (adjust.and_mask & 0xFFFF);
	}
	if (adjust.operation == DSGA_OP_STOP && adjust.type == DSGA_TYPE_NONE && adjust.variable == 0x1A && adjust.shift_num == 0) {
		/* Perm storage store */
		highlight_tag = (2 << 16) | (adjust.and_mask & 0xFFFF);
	}
	p += seprintf(p, last, "var: %X", adjust.variable);
	if (adjust.variable >= 0x100) {
		append_extended_var(adjust.variable);
	}
	if (adjust.variable == 0x7B && adjust.parameter >= 0x100) {
		p += seprintf(p, last, " (parameter: %X", adjust.parameter);
		append_extended_var(adjust.parameter);
		p += seprintf(p, last, ")");
	} else if ((adjust.variable >= 0x60 && adjust.variable <= 0x7F && adjust.variable != 0x7E) || adjust.parameter != 0) {
		p += seprintf(p, last, " (parameter: %X)", adjust.parameter);
	}
	p += seprintf(p, last, ", shift: %X, and: %X", adjust.shift_num, adjust.and_mask);
	switch (adjust.type) {
		case DSGA_TYPE_DIV: p += seprintf(p, last, ", add: %X, div: %X", adjust.add_val, adjust.divmod_val); break;
		case DSGA_TYPE_MOD: p += seprintf(p, last, ", add: %X, mod: %X", adjust.add_val, adjust.divmod_val); break;
		case DSGA_TYPE_EQ:  p += seprintf(p, last, ", eq: %X", adjust.add_val); break;
		case DSGA_TYPE_NEQ: p += seprintf(p, last, ", neq: %X", adjust.add_val); break;
		case DSGA_TYPE_NONE: break;
	}
	if (adjust.operation == DSGA_OP_STO_NC) {
		p += seprintf(p, last, ", store to: %X", adjust.divmod_val);
		highlight_tag = (1 << 16) | adjust.divmod_val;
	}
	p += seprintf(p, last, ", op: ");
	p = GetAdjustOperationName(p, last, adjust.operation);
	if (IsEvalAdjustJumpOperation(adjust.operation)) {
		p += seprintf(p, last, " +%u", adjust.jump);
	}
	append_flags();
	return p;
}

void SpriteGroupDumper::DumpSpriteGroup(const SpriteGroup *sg, const char *padding, uint flags)
{
	uint32_t highlight_tag = 0;
	auto print = [&]() {
		this->print_fn(sg, DSGPO_PRINT, highlight_tag, this->buffer);
		highlight_tag = 0;
	};

	if (sg == nullptr) {
		seprintf(this->buffer, lastof(this->buffer), "%sNULL GROUP", padding);
		print();
		return;
	}

	if (sg->nfo_line != 0) this->print_fn(sg, DSGPO_NFO_LINE, sg->nfo_line, nullptr);

	bool start_emitted = false;
	auto emit_start = [&]() {
		this->print_fn(sg, DSGPO_START, 0, nullptr);
		start_emitted = true;
	};
	auto guard = scope_guard([&]() {
		if (start_emitted) {
			this->print_fn(sg, DSGPO_END, 0, padding);
		}
	});

	char extra_info[64] = "";
	if (sg->sg_flags & SGF_ACTION6) strecat(extra_info, " (action 6 modified)", lastof(extra_info));
	if (sg->sg_flags & SGF_SKIP_CB) strecat(extra_info, " (skip CB)", lastof(extra_info));
	if (this->more_details) {
		if (sg->sg_flags & SGF_INLINING) strecat(extra_info, " (inlining)", lastof(extra_info));
	}

	char scope_buffer[64] = "";
	auto get_scope_name = [&](VarSpriteGroupScope var_scope, VarSpriteGroupScopeOffset var_scope_count) -> const char * {
		if (var_scope == VSG_SCOPE_RELATIVE) {
			char *b = scope_buffer;
			b += seprintf(b, lastof(scope_buffer), "%s[%s, ", _sg_scope_names[var_scope], _sg_relative_scope_modes[GB(var_scope_count, 8, 2)]);
			byte offset = GB(var_scope_count, 0, 8);
			if (HasBit(var_scope_count, 15)) {
				b += seprintf(b, lastof(scope_buffer), "var 0x100]");
			} else {
				b += seprintf(b, lastof(scope_buffer), "%u]", offset);
			}
			return scope_buffer;
		} else {
			return _sg_scope_names[var_scope];
		}
	};

	switch (sg->type) {
		case SGT_REAL: {
			const RealSpriteGroup *rsg = (const RealSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%sReal (loaded: %u, loading: %u)%s [%u]",
					padding, (uint)rsg->loaded.size(), (uint)rsg->loading.size(), extra_info, sg->nfo_line);
			print();
			emit_start();
			std::string sub_padding(padding);
			sub_padding += "    ";
			for (size_t i = 0; i < rsg->loaded.size(); i++) {
				seprintf(this->buffer, lastof(this->buffer), "%s  Loaded %u", padding, (uint)i);
				print();
				this->DumpSpriteGroup(rsg->loaded[i], sub_padding.c_str(), 0);
			}
			for (size_t i = 0; i < rsg->loading.size(); i++) {
				seprintf(this->buffer, lastof(this->buffer), "%s  Loading %u", padding, (uint)i);
				print();
				this->DumpSpriteGroup(rsg->loading[i], sub_padding.c_str(), 0);
			}
			break;
		}
		case SGT_DETERMINISTIC: {
			const DeterministicSpriteGroup *dsg = (const DeterministicSpriteGroup*)sg;

			const SpriteGroup *default_group = dsg->default_group;
			const std::vector<DeterministicSpriteGroupAdjust> *adjusts = &(dsg->adjusts);
			const std::vector<DeterministicSpriteGroupRange> *ranges = &(dsg->ranges);
			bool calculated_result = dsg->calculated_result;

			if (this->use_shadows) {
				auto iter = _deterministic_sg_shadows.find(dsg);
				if (iter != _deterministic_sg_shadows.end()) {
					default_group = iter->second.default_group;
					adjusts = &(iter->second.adjusts);
					ranges = &(iter->second.ranges);
					calculated_result = iter->second.calculated_result;
				}
			}

			bool is_callback_group = false;
			if (adjusts->size() == 1 && !calculated_result) {
				const DeterministicSpriteGroupAdjust &adjust = (*adjusts)[0];
				if (adjust.variable == 0xC && (adjust.operation == DSGA_OP_ADD || adjust.operation == DSGA_OP_RST)
						&& adjust.shift_num == 0 && (adjust.and_mask & 0xFF) == 0xFF && adjust.type == DSGA_TYPE_NONE) {
					is_callback_group = true;
					if (*padding == 0 && !calculated_result && ranges->size() > 0) {
						const DeterministicSpriteGroupRange &first_range = (*ranges)[0];
						if (first_range.low == 0 && first_range.high == 0 && first_range.group != nullptr) {
							this->top_graphics_group = first_range.group;
						}
					}
				}
			}

			if (*padding == 0 && !calculated_result && default_group != nullptr) {
				this->top_default_group = default_group;
			}
			if (dsg == this->top_default_group && !((flags & SGDF_DEFAULT) && strlen(padding) == 2)) {
				seprintf(this->buffer, lastof(this->buffer), "%sTOP LEVEL DEFAULT GROUP: Deterministic (%s, %s), [%u]",
						padding, get_scope_name(dsg->var_scope, dsg->var_scope_count), _sg_size_names[dsg->size], dsg->nfo_line);
				print();
				return;
			}
			if (dsg == this->top_graphics_group && !((flags & SGDF_RANGE) && strlen(padding) == 2)) {
				seprintf(this->buffer, lastof(this->buffer), "%sTOP LEVEL GRAPHICS GROUP: Deterministic (%s, %s), [%u]",
						padding, get_scope_name(dsg->var_scope, dsg->var_scope_count), _sg_size_names[dsg->size], dsg->nfo_line);
				print();
				return;
			}
			auto res = this->seen_dsgs.insert(dsg);
			if (!res.second) {
				seprintf(this->buffer, lastof(this->buffer), "%sGROUP SEEN ABOVE: Deterministic (%s, %s), [%u]",
						padding, get_scope_name(dsg->var_scope, dsg->var_scope_count), _sg_size_names[dsg->size], dsg->nfo_line);
				print();
				return;
			}
			char *p = this->buffer;
			p += seprintf(p, lastof(this->buffer), "%sDeterministic (%s, %s)%s [%u]",
					padding, get_scope_name(dsg->var_scope, dsg->var_scope_count), _sg_size_names[dsg->size], extra_info, dsg->nfo_line);
			if (this->more_details) {
				if (dsg->dsg_flags & DSGF_NO_DSE) p += seprintf(p, lastof(this->buffer), ", NO_DSE");
				if (dsg->dsg_flags & DSGF_VAR_TRACKING_PENDING) p += seprintf(p, lastof(this->buffer), ", VAR_PENDING");
				if (dsg->dsg_flags & DSGF_REQUIRES_VAR1C) p += seprintf(p, lastof(this->buffer), ", REQ_1C");
				if (dsg->dsg_flags & DSGF_CHECK_EXPENSIVE_VARS) p += seprintf(p, lastof(this->buffer), ", CHECK_EXP_VAR");
				if (dsg->dsg_flags & DSGF_CHECK_INSERT_JUMP) p += seprintf(p, lastof(this->buffer), ", CHECK_INS_JMP");
				if (dsg->dsg_flags & DSGF_CB_RESULT) p += seprintf(p, lastof(this->buffer), ", CB_RESULT");
				if (dsg->dsg_flags & DSGF_CB_HANDLER) p += seprintf(p, lastof(this->buffer), ", CB_HANDLER");
				if (dsg->dsg_flags & DSGF_INLINE_CANDIDATE) p += seprintf(p, lastof(this->buffer), ", INLINE_CANDIDATE");
			}
			print();
			emit_start();
			std::string sub_padding(padding);
			sub_padding += "  ";
			uint conditional_indent = 0;
			for (const auto &adjust : (*adjusts)) {
				this->DumpSpriteGroupAdjust(this->buffer, lastof(this->buffer), adjust, sub_padding.c_str(), highlight_tag, conditional_indent);
				print();
				if (adjust.variable == 0x7E && adjust.subroutine != nullptr) {
					std::string subroutine_padding(sub_padding);
					for (uint i = 0; i < conditional_indent; i++) {
						subroutine_padding += "> ";
					}
					subroutine_padding += "   | ";
					this->DumpSpriteGroup(adjust.subroutine, subroutine_padding.c_str(), 0);
				}
			}
			if (calculated_result) {
				seprintf(this->buffer, lastof(this->buffer), "%scalculated_result", padding);
				print();
			} else {
				std::string subgroup_padding(padding);
				subgroup_padding += "  ";
				for (const auto &range : (*ranges)) {
					char *p = this->buffer;
					p += seprintf(p, lastof(this->buffer), "%srange: %X -> %X", padding, range.low, range.high);
					if (range.low == range.high && is_callback_group) {
						const char *cb_name = GetNewGRFCallbackName((CallbackID)range.low);
						if (cb_name != nullptr) {
							p += seprintf(p, lastof(this->buffer), " (%s)", cb_name);
						}
					}
					if (this->more_details && range.group == dsg->error_group) {
						p += seprintf(p, lastof(this->buffer), " (error_group)");
					}
					print();
					this->DumpSpriteGroup(range.group, subgroup_padding.c_str(), SGDF_RANGE);
				}
				if (default_group != nullptr) {
					char *p = this->buffer;
					p += seprintf(p, lastof(this->buffer), "%sdefault", padding);
					if (this->more_details && default_group == dsg->error_group) {
						p += seprintf(p, lastof(this->buffer), " (error_group)");
					}
					print();
					this->DumpSpriteGroup(default_group, subgroup_padding.c_str(), SGDF_DEFAULT);
				}
			}
			break;
		}
		case SGT_RANDOMIZED: {
			const RandomizedSpriteGroup *rsg = (const RandomizedSpriteGroup*)sg;

			const std::vector<const SpriteGroup *> *groups = &(rsg->groups);

			if (this->use_shadows) {
				auto iter = _randomized_sg_shadows.find(rsg);
				if (iter != _randomized_sg_shadows.end()) {
					groups = &(iter->second.groups);
				}
			}

			seprintf(this->buffer, lastof(this->buffer), "%sRandom (%s, %s, triggers: %X, lowest_randbit: %X, groups: %u)%s [%u]",
					padding, get_scope_name(rsg->var_scope, rsg->var_scope_count), rsg->cmp_mode == RSG_CMP_ANY ? "ANY" : "ALL",
					rsg->triggers, rsg->lowest_randbit, (uint)rsg->groups.size(), extra_info, rsg->nfo_line);
			print();
			emit_start();
			std::string sub_padding(padding);
			sub_padding += "  ";
			std::string sub_padding_indent(sub_padding);
			sub_padding_indent += "  ";
			auto end = groups->end();
			for (auto iter = groups->begin(); iter != end;) {
				uint count = 1;
				const SpriteGroup *group = *iter;
				while (true) {
					++iter;
					if (iter == end) break;
					if (*iter != group) break;
					count++;
				}
				if (count > 1) {
					seprintf(this->buffer, lastof(this->buffer), "%s%u x:", sub_padding.c_str(), count);
					print();
					this->DumpSpriteGroup(group, sub_padding_indent.c_str(), 0);
				} else {
					this->DumpSpriteGroup(group, sub_padding.c_str(), 0);
				}
			}
			break;
		}
		case SGT_CALLBACK:
			seprintf(this->buffer, lastof(this->buffer), "%sCallback Result: %X", padding, ((const CallbackResultSpriteGroup *) sg)->result);
			print();
			break;
		case SGT_RESULT:
			seprintf(this->buffer, lastof(this->buffer), "%sSprite Result: SpriteID: %u, num: %u",
					padding, ((const ResultSpriteGroup *) sg)->sprite, ((const ResultSpriteGroup *) sg)->num_sprites);
			print();
			break;
		case SGT_TILELAYOUT: {
			const TileLayoutSpriteGroup *tlsg = (const TileLayoutSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%sTile Layout%s [%u]", padding, extra_info, sg->nfo_line);
			print();
			emit_start();

			const TileLayoutRegisters *registers = tlsg->dts.registers;
			auto print_reg_info = [&](char *b, uint i, bool is_parent) {
				if (registers == nullptr) {
					print();
					return;
				}
				const TileLayoutRegisters *reg = registers + i;
				if (reg->flags == 0) {
					print();
					return;
				}
				seprintf(b, lastof(this->buffer), ", register flags: %X", reg->flags);
				print();
				auto log_reg = [&](TileLayoutFlags flag, const char *name, uint8_t flag_reg) {
					if (reg->flags & flag) {
						highlight_tag = (1 << 16) | flag_reg;
						seprintf(this->buffer, lastof(this->buffer), "%s    %s reg: %X", padding, name, flag_reg);
						print();
					}
				};
				log_reg(TLF_DODRAW, "TLF_DODRAW", reg->dodraw);
				log_reg(TLF_SPRITE, "TLF_SPRITE", reg->sprite);
				log_reg(TLF_PALETTE, "TLF_PALETTE", reg->palette);
				if (is_parent) {
					log_reg(TLF_BB_XY_OFFSET, "TLF_BB_XY_OFFSET x", reg->delta.parent[0]);
					log_reg(TLF_BB_XY_OFFSET, "TLF_BB_XY_OFFSET y", reg->delta.parent[1]);
					log_reg(TLF_BB_Z_OFFSET, "TLF_BB_Z_OFFSET", reg->delta.parent[2]);
				} else {
					log_reg(TLF_CHILD_X_OFFSET, "TLF_CHILD_X_OFFSET", reg->delta.child[0]);
					log_reg(TLF_CHILD_Y_OFFSET, "TLF_CHILD_Y_OFFSET", reg->delta.child[1]);
				}
				if (reg->flags & TLF_SPRITE_VAR10) {
					seprintf(this->buffer, lastof(this->buffer), "%s    TLF_SPRITE_VAR10 value: %X", padding, reg->sprite_var10);
					print();
				}
				if (reg->flags & TLF_PALETTE_VAR10) {
					seprintf(this->buffer, lastof(this->buffer), "%s    TLF_PALETTE_VAR10 value: %X", padding, reg->palette_var10);
					print();
				}
			};

			char *b = this->buffer + seprintf(this->buffer, lastof(this->buffer), "%s  ground: (%X, %X)",
					padding, tlsg->dts.ground.sprite, tlsg->dts.ground.pal);
			print_reg_info(b, 0, false);

			uint offset = 0; // offset 0 is the ground sprite
			const DrawTileSeqStruct *element;
			foreach_draw_tile_seq(element, tlsg->dts.seq) {
				offset++;
				char *b = this->buffer;
				if (element->IsParentSprite()) {
					b += seprintf(this->buffer, lastof(this->buffer), "%s  section: %X, image: (%X, %X), d: (%d, %d, %d), s: (%d, %d, %d)",
							padding, offset, element->image.sprite, element->image.pal,
							element->delta_x, element->delta_y, element->delta_z,
							element->size_x, element->size_y, element->size_z);
				} else {
					b += seprintf(this->buffer, lastof(this->buffer), "%s  section: %X, image: (%X, %X), d: (%d, %d)",
							padding, offset, element->image.sprite, element->image.pal,
							element->delta_x, element->delta_y);
				}
				print_reg_info(b, offset, element->IsParentSprite());
			}
			break;
		}
		case SGT_INDUSTRY_PRODUCTION: {
			const IndustryProductionSpriteGroup *ipsg = (const IndustryProductionSpriteGroup*)sg;
			seprintf(this->buffer, lastof(this->buffer), "%sIndustry Production (version %X) [%u]", padding, ipsg->version, ipsg->nfo_line);
			print();
			emit_start();
			auto log_io = [&](const char *prefix, int i, int quantity, CargoID cargo) {
				if (ipsg->version >= 1) highlight_tag = (1 << 16) | quantity;
				if (ipsg->version >= 2) {
					seprintf(this->buffer, lastof(this->buffer), "%s  %s %X: reg %X, cargo ID: %X", padding, prefix, i, quantity, cargo);
					print();
				} else {
					const char *type = (ipsg->version >= 1) ? "reg" : "value";
					seprintf(this->buffer, lastof(this->buffer), "%s  %s %X: %s %X", padding, prefix, i, type, quantity);
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
			seprintf(this->buffer, lastof(this->buffer), "%s  Again: %s %X", padding, (ipsg->version >= 1) ? "reg" : "value", ipsg->again);
			print();
			break;
		}
	}
}
