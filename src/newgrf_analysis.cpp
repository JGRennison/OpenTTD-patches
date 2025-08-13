/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_analysis.cpp NewGRF analysis. */

#include "stdafx.h"
#include "newgrf_analysis.h"
#include "newgrf_industrytiles_analysis.h"
#include "newgrf_spritegroup.h"

#include "safeguards.h"

static const SpriteGroup *GetSwitchTargetForValue(const DeterministicSpriteGroup *dsg, uint32_t value)
{
	for (const auto &range : dsg->ranges) {
		if (range.low <= value && value <= range.high) {
			return range.group;
		}
	}
	return dsg->default_group;
};

/* Returns true if group already seen before */
bool BaseSpriteChainAnalyser::RegisterSeenDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg)
{
	auto res = this->seen_dsg.insert(dsg);
	if (!res.second) {
		/* Already seen this group */
		return true;
	}
	return false;
}

std::pair<bool, const SpriteGroup *> BaseSpriteChainAnalyser::HandleGroupBypassing(const DeterministicSpriteGroup *dsg)
{
	if (dsg->GroupMayBeBypassed()) {
		/* Not clear why some GRFs do this, perhaps a way of commenting out a branch */
		uint32_t value = (dsg->adjusts.size() == 1) ? EvaluateDeterministicSpriteGroupAdjust(dsg->size, dsg->adjusts[0], nullptr, 0, UINT_MAX) : 0;
		return { true, dsg->GetBypassGroupForValue(value) };
	}
	return { false, nullptr };
}

template <typename T>
void SpriteChainAnalyser<T>::AnalyseGroup(const SpriteGroup *sg)
{
	if (sg == nullptr) return;

	T *self = static_cast<T *>(this); // CRTP pattern
	if (self->IsEarlyExitSet()) return;
	switch (sg->type) {
		case SGT_REAL: {
			const RealSpriteGroup *rsg = static_cast<const RealSpriteGroup *>(sg);
			for (const SpriteGroup *group: rsg->loaded) {
				self->AnalyseGroup(group);
			}
			for (const SpriteGroup *group: rsg->loading) {
				self->AnalyseGroup(group);
			}
			break;
		}

		case SGT_DETERMINISTIC: {
			const DeterministicSpriteGroup *dsg = static_cast<const DeterministicSpriteGroup *>(sg);
			if (this->RegisterSeenDeterministicSpriteGroup(dsg)) break; // Seen this group before
			auto res = this->HandleGroupBypassing(dsg);
			if (res.first) {
				/* Bypass this group */
				self->AnalyseGroup(res.second);
				break;
			}
			self->AnalyseDeterministicSpriteGroup(dsg);
			break;
		}

		case SGT_RANDOMIZED: {
			const RandomizedSpriteGroup *rsg = static_cast<const RandomizedSpriteGroup *>(sg);
			self->AnalyseRandomisedSpriteGroup(rsg);
			break;
		}

		case SGT_CALLBACK: {
			const CallbackResultSpriteGroup *crsg = static_cast<const CallbackResultSpriteGroup *>(sg);
			self->AnalyseCallbackResultSpriteGroup(crsg);
			break;
		}

		default:
			/* Not interested in other sprite group types */
			break;
	}
}
template <typename T>
void SpriteChainAnalyser<T>::DefaultAnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg)
{
	T *self = static_cast<T *>(this); // CRTP pattern
	if (!dsg->IsCalculatedResult()) {
		for (const auto &range : dsg->ranges) {
			self->AnalyseGroup(range.group);
		}
		self->AnalyseGroup(dsg->default_group);
	}
}

template <typename T>
void SpriteChainAnalyser<T>::DefaultAnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *rsg)
{
	T *self = static_cast<T *>(this); // CRTP pattern
	for (const SpriteGroup *group: rsg->groups) {
		self->AnalyseGroup(group);
	}
}

static bool IsSingleVariableLoadSwitch(const DeterministicSpriteGroup *dsg)
{
	return dsg->adjusts.size() == 1 && !dsg->IsCalculatedResult() && (dsg->adjusts[0].operation == DSGA_OP_ADD || dsg->adjusts[0].operation == DSGA_OP_RST) && dsg->adjusts[0].type == DSGA_TYPE_NONE;
}

static bool IsSingleVariableLoadAdjustOfSpecificVariable(const DeterministicSpriteGroupAdjust &adjust, uint8_t var, uint32_t min_mask)
{
	return adjust.variable == var && adjust.shift_num == 0 && (adjust.and_mask & min_mask) == min_mask;
}

static bool IsTrivialSwitchOfSpecificVariable(const DeterministicSpriteGroup *dsg, uint8_t var, uint32_t min_mask)
{
	return IsSingleVariableLoadSwitch(dsg) && IsSingleVariableLoadAdjustOfSpecificVariable(dsg->adjusts[0], var, min_mask);
}

/* Find CB result */

void FindCBResultAnalyser::AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg)
{
	if (dsg->IsCalculatedResult()) {
		this->found = true;
		return;
	}

	auto check_var_filter = [&](uint8_t var, uint value) -> bool {
		if (IsTrivialSwitchOfSpecificVariable(dsg, var, 0xFF)) {
			this->AnalyseGroup(GetSwitchTargetForValue(dsg, value));
			return true;
		}
		return false;
	};
	if (check_var_filter(0xC, this->callback)) return;
	if (this->check_var_10 && check_var_filter(0x10, this->var_10_value)) return;
	for (const auto &range : dsg->ranges) {
		this->AnalyseGroup(range.group);
	}
	this->AnalyseGroup(dsg->default_group);
}

void FindCBResultAnalyser::AnalyseCallbackResultSpriteGroup(const CallbackResultSpriteGroup *crsg)
{
	if (crsg->result != CALLBACK_FAILED) this->found = true;
}

/* Find random triggers */

void FindRandomTriggerAnalyser::AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg)
{
	/* Only follow CBID_RANDOM_TRIGGER in callback switches */
	if (IsTrivialSwitchOfSpecificVariable(dsg, 0xC, 0xFF)) {
		this->AnalyseGroup(GetSwitchTargetForValue(dsg, CBID_RANDOM_TRIGGER));
		return;
	}

	this->DefaultAnalyseDeterministicSpriteGroup(dsg);
}

void FindRandomTriggerAnalyser::AnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *rsg)
{
	if (rsg->triggers != 0 || rsg->cmp_mode == RSG_CMP_ALL) {
		this->found_trigger = true;
		return;
	}

	this->DefaultAnalyseRandomisedSpriteGroup(rsg);
}

/* Industry tile analysis */

void IndustryTileDataAnalyser::AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg)
{
	if (IsSingleVariableLoadSwitch(dsg)) {
		const auto &adjust = dsg->adjusts[0];

		if (IsSingleVariableLoadAdjustOfSpecificVariable(adjust, 0xC, 0xFF)) {
			/* Check for CBID_INDTILE_ANIM_NEXT_FRAME, mark layout subset as animated if found */
			if (this->cfg.check_anim_next_frame_cb) {
				for (const auto &range : dsg->ranges) {
					if (range.low <= CBID_INDTILE_ANIM_NEXT_FRAME && CBID_INDTILE_ANIM_NEXT_FRAME <= range.high) {
						/* Found a CBID_INDTILE_ANIM_NEXT_FRAME */
						*(this->cfg.result_mask) &= ~this->check_mask;
					}
				}
			}

			/* Callback switch, skip to the default/graphics chain */
			for (const auto &range : dsg->ranges) {
				if (range.low == 0) {
					this->AnalyseGroup(range.group);
					return;
				}
			}
			this->AnalyseGroup(dsg->default_group);
			return;
		}
		if (IsSingleVariableLoadAdjustOfSpecificVariable(adjust, 0x44, 0xFF) && dsg->var_scope == VSG_SCOPE_PARENT) {
			/* Layout index switch */
			this->AnalyseGroup(GetSwitchTargetForValue(dsg, this->cfg.layout_index));
			return;
		}
		if (adjust.variable == 0x43 && dsg->var_scope == VSG_SCOPE_SELF) {
			const uint32_t effective_mask = adjust.and_mask << adjust.shift_num;
			if (effective_mask == 0xFFFF || effective_mask == 0xFF00 || effective_mask == 0x00FF) {
				/* Relative position switch */
				const bool use_x = effective_mask & 0xFF;
				const bool use_y = effective_mask & 0xFF00;
				uint64_t default_mask = this->check_mask;
				for (const auto &range : dsg->ranges) {
					if (range.high - range.low < 32) {
						uint64_t new_check_mask = 0;
						for (uint i = range.low; i <= range.high; i++) {
							const uint offset = i << adjust.shift_num;
							const int16_t x = offset & 0xFF;
							const int16_t y = (offset >> 8) & 0xFF;
							for (uint bit : SetBitIterator<uint, uint64_t>(this->check_mask)) {
								const TileIndexDiffC &ti = (*(this->cfg.layout))[bit].ti;
								if ((!use_x || ti.x == x) && (!use_y || ti.y == y)) {
									SetBit(new_check_mask, bit);
								}
							}
						}
						default_mask &= ~new_check_mask;
						if (range.group != nullptr) {
							IndustryTileDataAnalyser sub_analyser(this->cfg, new_check_mask);
							sub_analyser.AnalyseGroup(range.group);

							if (sub_analyser.anim_state_at_offset) {
								this->anim_state_at_offset = true;
								return;
							}
						}
					} else {
						this->AnalyseGroup(range.group);
					}
				}
				if (dsg->default_group != nullptr) {
					IndustryTileDataAnalyser sub_analyser(this->cfg, default_mask);
					sub_analyser.AnalyseGroup(dsg->default_group);

					if (sub_analyser.anim_state_at_offset) {
						this->anim_state_at_offset = true;
						return;
					}
				}
				return;
			}
		}
	}

	for (const auto &adjust : dsg->adjusts) {
		if (adjust.variable == 0x7E) this->AnalyseGroup(adjust.subroutine);
		if (dsg->var_scope == VSG_SCOPE_SELF && (adjust.variable == 0x44 || (adjust.variable == 0x61 && adjust.parameter == 0))) {
			*(this->cfg.result_mask) &= ~this->check_mask;
		}
		if ((dsg->var_scope == VSG_SCOPE_SELF && adjust.variable == 0x61) || (dsg->var_scope == VSG_SCOPE_PARENT && adjust.variable == 0x63)) {
			this->anim_state_at_offset = true;
			return;
		}
	}

	this->DefaultAnalyseDeterministicSpriteGroup(dsg);
}

/* Callback operation analysis */

void CallbackOperationAnalyser::AnalyseDeterministicSpriteGroup(const DeterministicSpriteGroup *dsg)
{
	if ((this->mode == ACOM_CB_VAR || this->mode == ACOM_CB_REFIT_CAPACITY) && dsg->var_scope != VSG_SCOPE_SELF) {
		this->result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;
	}

	if (IsSingleVariableLoadSwitch(dsg)) {
		const auto &adjust = dsg->adjusts[0];
		if (this->mode == ACOM_CB_VAR && IsSingleVariableLoadAdjustOfSpecificVariable(adjust, 0xC, 0xFF)) {
			bool found_refit_cap = false;
			const AnalyseCallbackOperationResultFlags prev_result = this->result_flags;
			AnalyseCallbackOperationResultFlags refit_result_flags = ACORF_NONE;
			const AnalyseCallbackOperationResultFlags refit_result_mask = ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND | ACORF_CB_REFIT_CAP_SEEN_VAR_47;
			for (const auto &range : dsg->ranges) {
				if (range.low == range.high) {
					switch (range.low) {
						case CBID_VEHICLE_32DAY_CALLBACK:
							this->callbacks_used |= SGCU_VEHICLE_32DAY_CALLBACK;
							break;

						case CBID_VEHICLE_REFIT_COST:
							this->callbacks_used |= SGCU_VEHICLE_REFIT_COST;
							break;

						case CBID_RANDOM_TRIGGER:
							this->callbacks_used |= SGCU_RANDOM_TRIGGER;
							break;

						case CBID_VEHICLE_MODIFY_PROPERTY:
							if (range.group != nullptr) {
								CallbackOperationAnalyser cb36_op(ACOM_CB36_PROP);
								cb36_op.AnalyseGroup(range.group);
								this->cb36_properties_used |= cb36_op.cb36_properties_used;
								this->callbacks_used |= cb36_op.callbacks_used;
							}
							break;

						case CBID_VEHICLE_REFIT_CAPACITY:
							found_refit_cap = true;
							if (range.group != nullptr) {
								CallbackOperationAnalyser cb_refit_op(ACOM_CB_REFIT_CAPACITY);
								cb_refit_op.AnalyseGroup(range.group);
								refit_result_flags = (cb_refit_op.result_flags & refit_result_mask);
							}
							break;
					}
				} else {
					this->AnalyseGroup(range.group);
				}
			}
			this->AnalyseGroup(dsg->default_group);
			if (found_refit_cap) {
				/* Found a refit callback, so ignore flags in refit_result_mask from all other child groups */
				this->result_flags = (prev_result & refit_result_mask) | (this->result_flags & ~refit_result_mask) | refit_result_flags;
			}
			return;
		}
		if (this->mode == ACOM_CB36_PROP && IsSingleVariableLoadAdjustOfSpecificVariable(adjust, 0x10, 0xFF)) {
			for (const auto &range : dsg->ranges) {
				if (range.low == range.high) {
					if (range.low < 64) {
						if (FindCBResultAnalyser::Execute(range.group, CBID_VEHICLE_MODIFY_PROPERTY, true, (uint8_t)range.low)) {
							SetBit(this->cb36_properties_used, range.low);
							if (range.low == 0x9) {
								/* Speed */
								if (range.group != nullptr) {
									CallbackOperationAnalyser cb36_speed(ACOM_CB36_SPEED);
									cb36_speed.AnalyseGroup(range.group);
									this->callbacks_used |= cb36_speed.callbacks_used;
								}
							}
						}
					}
				} else {
					this->AnalyseGroup(range.group);
				}
			}
			this->AnalyseGroup(dsg->default_group);
			return;
		}
		if (this->mode == ACOM_CB36_PROP && IsSingleVariableLoadAdjustOfSpecificVariable(adjust, 0xC, 0xFF)) {
			this->AnalyseGroup(GetSwitchTargetForValue(dsg, CBID_VEHICLE_MODIFY_PROPERTY));
			return;
		}
		if (this->mode == ACOM_CB_REFIT_CAPACITY && IsSingleVariableLoadAdjustOfSpecificVariable(adjust, 0xC, 0xFF)) {
			this->AnalyseGroup(GetSwitchTargetForValue(dsg, CBID_VEHICLE_REFIT_CAPACITY));
			return;
		}
	}
	for (const auto &adjust : dsg->adjusts) {
		if (this->mode == ACOM_CB_VAR && adjust.variable == 0xC) {
			this->callbacks_used |= SGCU_ALL;
		}
		if (this->mode == ACOM_CB36_PROP && adjust.variable == 0x10) {
			if (FindCBResultAnalyser::Execute(dsg, CBID_VEHICLE_MODIFY_PROPERTY, false, 0)) {
				this->cb36_properties_used |= UINT64_MAX;
				break;
			}
		}
		if ((this->mode == ACOM_CB_VAR || this->mode == ACOM_CB_REFIT_CAPACITY) && !(adjust.variable == 0xC || adjust.variable == 0x1A || adjust.variable == 0x47 || adjust.variable == 0x7D || adjust.variable == 0x7E)) {
			this->result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;
		}
		if ((this->mode == ACOM_CB_VAR || this->mode == ACOM_CB_REFIT_CAPACITY) && adjust.variable == 0x47) {
			this->result_flags |= ACORF_CB_REFIT_CAP_SEEN_VAR_47;
		}
		if (this->mode != ACOM_CB36_PROP && adjust.variable == 0x7E) {
			this->AnalyseGroup(adjust.subroutine);
		}
		if (this->mode == ACOM_CB36_SPEED && adjust.variable == 0x4A) {
			this->callbacks_used |= SGCU_CB36_SPEED_RAILTYPE;
			return;
		}
	}

	this->DefaultAnalyseDeterministicSpriteGroup(dsg);
}

void CallbackOperationAnalyser::AnalyseRandomisedSpriteGroup(const RandomizedSpriteGroup *rsg)
{
	this->result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;

	if (this->mode == ACOM_CB_VAR && (rsg->triggers != 0 || rsg->cmp_mode == RSG_CMP_ALL)) {
		this->callbacks_used |= SGCU_RANDOM_TRIGGER;
	}

	this->DefaultAnalyseRandomisedSpriteGroup(rsg);
}
