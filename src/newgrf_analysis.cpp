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
		AnalyseCallbackOperation cbr_op(ACOM_FIND_CB_RESULT);
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
									AnalyseCallbackOperation cb36_op(ACOM_CB36_PROP);
									range.group->AnalyseCallbacks(cb36_op);
									op.properties_used |= cb36_op.properties_used;
									op.callbacks_used |= cb36_op.callbacks_used;
								}
								break;

							case CBID_VEHICLE_REFIT_CAPACITY:
								found_refit_cap = true;
								if (range.group != nullptr) {
									AnalyseCallbackOperation cb_refit_op(ACOM_CB_REFIT_CAPACITY);
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
										AnalyseCallbackOperation cb36_speed(ACOM_CB36_SPEED);
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
				/* Check for CBID_INDTILE_ANIM_NEXT_FRAME, mark layout subset as animated if found */
				if (op.data.indtile->check_anim_next_frame_cb) {
					for (const auto &range : this->ranges) {
						if (range.low <= CBID_INDTILE_ANIM_NEXT_FRAME && CBID_INDTILE_ANIM_NEXT_FRAME <= range.high) {
							/* Found a CBID_INDTILE_ANIM_NEXT_FRAME */
							*(op.data.indtile->result_mask) &= ~op.data.indtile->check_mask;
						}
					}
				}

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
		if (op.mode == ACOM_INDUSTRY_TILE && adjust.variable == 0x43 && adjust.type == DSGA_TYPE_NONE && this->var_scope == VSG_SCOPE_SELF) {
			const uint32 effective_mask = adjust.and_mask << adjust.shift_num;
			if (effective_mask == 0xFFFF || effective_mask == 0xFF00 || effective_mask == 0x00FF) {
				/* Relative position switch */
				const bool use_x = effective_mask & 0xFF;
				const bool use_y = effective_mask & 0xFF00;
				uint64 default_mask = op.data.indtile->check_mask;
				for (const auto &range : this->ranges) {
					if (range.high - range.low < 32) {
						uint64 new_check_mask = 0;
						for (uint i = range.low; i <= range.high; i++) {
							const uint offset = i << adjust.shift_num;
							const int16 x = offset & 0xFF;
							const int16 y = (offset >> 8) & 0xFF;
							for (uint bit : SetBitIterator<uint, uint64>(op.data.indtile->check_mask)) {
								const TileIndexDiffC &ti = (*(op.data.indtile->layout))[bit].ti;
								if ((!use_x || ti.x == x) && (!use_y || ti.y == y)) {
									SetBit(new_check_mask, bit);
								}
							}
						}
						default_mask &= ~new_check_mask;
						if (range.group != nullptr) {
							AnalyseCallbackOperationIndustryTileData data = *(op.data.indtile);
							data.check_mask = new_check_mask;

							AnalyseCallbackOperation sub_op(ACOM_INDUSTRY_TILE);
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

					AnalyseCallbackOperation sub_op(ACOM_INDUSTRY_TILE);
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
				break;
			}
		}
		if ((op.mode == ACOM_CB_VAR || op.mode == ACOM_CB_REFIT_CAPACITY) && !(adjust.variable == 0xC || adjust.variable == 0x1A || adjust.variable == 0x47 || adjust.variable == 0x7D || adjust.variable == 0x7E)) {
			op.result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;
		}
		if ((op.mode == ACOM_CB_VAR || op.mode == ACOM_CB_REFIT_CAPACITY) && adjust.variable == 0x47) {
			op.result_flags |= ACORF_CB_REFIT_CAP_SEEN_VAR_47;
		}
		if (op.mode != ACOM_CB36_PROP && adjust.variable == 0x7E && adjust.subroutine != nullptr) {
			adjust.subroutine->AnalyseCallbacks(op);
		}
		if (op.mode == ACOM_INDUSTRY_TILE && this->var_scope == VSG_SCOPE_SELF && (adjust.variable == 0x44 || (adjust.variable == 0x61 && adjust.parameter == 0))) {
			*(op.data.indtile->result_mask) &= ~op.data.indtile->check_mask;
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

void CallbackResultSpriteGroup::AnalyseCallbacks(AnalyseCallbackOperation &op) const
{
	if (op.mode == ACOM_FIND_CB_RESULT && this->result != CALLBACK_FAILED) op.result_flags |= ACORF_CB_RESULT_FOUND;
}

void RandomizedSpriteGroup::AnalyseCallbacks(AnalyseCallbackOperation &op) const
{
	op.result_flags |= ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND;

	if ((op.mode == ACOM_CB_VAR || op.mode == ACOM_FIND_RANDOM_TRIGGER) && (this->triggers != 0 || this->cmp_mode == RSG_CMP_ALL)) {
		op.callbacks_used |= SGCU_RANDOM_TRIGGER;
	}

	for (const SpriteGroup *group: this->groups) {
		if (group != nullptr) group->AnalyseCallbacks(op);
	}
}
