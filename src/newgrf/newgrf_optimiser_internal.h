/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file newgrf_optimiser_internal.h Internal NewGRF optimiser processing definitions.
 */

#ifndef NEWGRF_OPTIMISER_INTERNAL_H
#define NEWGRF_OPTIMISER_INTERNAL_H

#include "../newgrf.h"
#include "../newgrf_spritegroup.h"
#include "../spriteloader/spriteloader.hpp"
#include "../core/arena_alloc.hpp"

#include "../3rdparty/robin_hood/robin_hood.h"
#include <bitset>
#include <vector>

struct VarAction2GroupVariableTracking {
	std::bitset<256> in;
	std::bitset<256> out;
	std::bitset<256> proc_call_out;
	std::bitset<256> proc_call_in;
};

struct VarAction2ProcedureAnnotation {
	std::bitset<256> stores;
	uint32_t special_register_values[16];
	uint16_t special_register_mask = 0;
	bool unskippable = false;
};

/** Temporary data during loading of GRFs */
struct GrfProcessingOptimiserState {
	/* VarAction2 temporary storage variable tracking */
	robin_hood::unordered_flat_map<const SpriteGroup *, VarAction2GroupVariableTracking *> group_temp_store_variable_tracking;
	BumpAllocContainer<VarAction2GroupVariableTracking, 1024> group_temp_store_variable_tracking_storage;
	robin_hood::unordered_flat_map<const SpriteGroup *, VarAction2ProcedureAnnotation *> procedure_annotations;
	BumpAllocContainer<VarAction2ProcedureAnnotation, 1024> procedure_annotations_storage;
	robin_hood::unordered_map<const DeterministicSpriteGroup *, std::vector<DeterministicSpriteGroupAdjust>> inlinable_adjust_groups;
	std::vector<DeterministicSpriteGroup *> dead_store_elimination_candidates;

	VarAction2GroupVariableTracking *GetVarAction2GroupVariableTracking(const SpriteGroup *group, bool make_new)
	{
		if (make_new) {
			VarAction2GroupVariableTracking *&ptr = this->group_temp_store_variable_tracking[group];
			if (ptr == nullptr) ptr = this->group_temp_store_variable_tracking_storage.New();
			return ptr;
		} else {
			auto iter = this->group_temp_store_variable_tracking.find(group);
			if (iter != this->group_temp_store_variable_tracking.end()) return iter->second;
			return nullptr;
		}
	}

	std::pair<VarAction2ProcedureAnnotation *, bool> GetVarAction2ProcedureAnnotation(const SpriteGroup *group)
	{
		VarAction2ProcedureAnnotation *&ptr = this->procedure_annotations[group];
		if (ptr == nullptr) {
			ptr = this->procedure_annotations_storage.New();
			return std::make_pair(ptr, true);
		} else {
			return std::make_pair(ptr, false);
		}
	}

	std::vector<DeterministicSpriteGroupAdjust> *GetInlinableGroupAdjusts(const DeterministicSpriteGroup *group, bool make_new)
	{
		if (make_new) {
			return &this->inlinable_adjust_groups[group];
		} else {
			auto iter = this->inlinable_adjust_groups.find(group);
			if (iter != this->inlinable_adjust_groups.end()) return &iter->second;
			return nullptr;
		}
	}

	/** Clear temporary data before processing the next file in the current loading stage */
	void ClearDataForNextFile()
	{
		this->group_temp_store_variable_tracking.clear();
		this->group_temp_store_variable_tracking_storage.clear();
		this->procedure_annotations.clear();
		this->procedure_annotations_storage.clear();
		this->inlinable_adjust_groups.clear();
		this->dead_store_elimination_candidates.clear();
	}
};

extern GrfProcessingOptimiserState _cur_grf_optimise_state;

enum VarAction2AdjustInferenceFlags : uint16_t {
	VA2AIF_NONE                  = 0x00,

	VA2AIF_SIGNED_NON_NEGATIVE   = 0x01,
	VA2AIF_ONE_OR_ZERO           = 0x02,
	VA2AIF_PREV_TERNARY          = 0x04,
	VA2AIF_PREV_MASK_ADJUST      = 0x08,
	VA2AIF_PREV_STORE_TMP        = 0x10,
	VA2AIF_HAVE_CONSTANT         = 0x20,
	VA2AIF_SINGLE_LOAD           = 0x40,
	VA2AIF_MUL_BOOL              = 0x80,
	VA2AIF_PREV_SCMP_DEC         = 0x100,

	VA2AIF_PREV_MASK             = VA2AIF_PREV_TERNARY | VA2AIF_PREV_MASK_ADJUST | VA2AIF_PREV_STORE_TMP | VA2AIF_PREV_SCMP_DEC,
	VA2AIF_STORE_SAVE_MASK       = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO | VA2AIF_HAVE_CONSTANT | VA2AIF_MUL_BOOL,
};
DECLARE_ENUM_AS_BIT_SET(VarAction2AdjustInferenceFlags)

struct VarAction2TempStoreInferenceVarSource {
	uint16_t variable;
	DeterministicSpriteGroupAdjustType type;
	uint8_t shift_num;
	uint32_t parameter;
	uint32_t and_mask;
	uint32_t add_val;
	uint32_t divmod_val;
};

struct VarAction2TempStoreInference {
	VarAction2AdjustInferenceFlags inference = VA2AIF_NONE;
	const uint8_t var_index;
	uint32_t store_constant = 0;
	VarAction2TempStoreInferenceVarSource var_source;
	uint version = 0;

	VarAction2TempStoreInference(uint8_t var_index) : var_index(var_index) {}
};

struct VarAction2InferenceBackup {
	VarAction2AdjustInferenceFlags inference = VA2AIF_NONE;
	uint32_t current_constant = 0;
	uint adjust_size = 0;
};

struct VarAction2OptimiseState {
	struct TempStoreState {
	private:
		std::vector<VarAction2TempStoreInference> storage;
		std::vector<std::pair<uint8_t, uint8_t>> storage_index;

	public:
		VarAction2TempStoreInference *begin() { return this->storage.data(); }
		VarAction2TempStoreInference *end() { return this->storage.data() + this->storage.size(); }

		VarAction2TempStoreInference *find(uint8_t var)
		{
			for (const auto &it : this->storage_index) {
				if (it.first == var) return &this->storage[it.second];
			}
			return nullptr;
		}

		VarAction2TempStoreInference &operator[](uint8_t var)
		{
			VarAction2TempStoreInference *ptr = this->find(var);
			if (ptr != nullptr) return *ptr;

			this->storage_index.emplace_back(var, static_cast<uint8_t>(this->storage.size()));
			return this->storage.emplace_back(var);
		}

		void clear()
		{
			this->storage.clear();
			this->storage_index.clear();
		}
	};

	static TempStoreState temp_store_cache;

	VarAction2AdjustInferenceFlags inference = VA2AIF_NONE;
	uint32_t current_constant = 0;
	TempStoreState temp_stores;
	VarAction2InferenceBackup inference_backup;
	VarAction2GroupVariableTracking *var_tracking = nullptr;
	bool seen_procedure_call = false;
	bool var_1C_present = false;
	bool check_expensive_vars = false;
	bool enable_dse = false;
	uint default_variable_version = 0;
	uint32_t special_register_store_values[16];
	uint16_t special_register_store_mask = 0;

	inline VarAction2GroupVariableTracking *GetVarTracking(DeterministicSpriteGroup *group)
	{
		if (this->var_tracking == nullptr) {
			this->var_tracking = _cur_grf_optimise_state.GetVarAction2GroupVariableTracking(group, true);
		}
		return this->var_tracking;
	}

	VarAction2OptimiseState()
	{
		this->temp_stores = std::move(temp_store_cache);
		this->temp_stores.clear();
	}

	~VarAction2OptimiseState()
	{
		temp_store_cache = std::move(this->temp_stores);
	}

	static void ReleaseCaches()
	{
		TempStoreState tmp = std::move(temp_store_cache);
	}
};

inline void OptimiseVarAction2PreCheckAdjust(VarAction2OptimiseState &state, const DeterministicSpriteGroupAdjust &adjust)
{
	uint16_t variable = adjust.variable;
	if (variable == 0x7B) variable = adjust.parameter;
	if (variable == 0x1C) state.var_1C_present = true;
}

struct VarAction2AdjustInfo {
	GrfSpecFeature feature;
	GrfSpecFeature scope_feature;
	uint8_t varsize;
};

struct DeterministicSpriteGroupShadowCopy {
	std::vector<DeterministicSpriteGroupAdjust> adjusts;
	std::vector<DeterministicSpriteGroupRange> ranges;
	const SpriteGroup *default_group;
	bool calculated_result;
};

struct RandomizedSpriteGroupShadowCopy {
	std::vector<const SpriteGroup *> groups;
};

extern robin_hood::unordered_node_map<const DeterministicSpriteGroup *, DeterministicSpriteGroupShadowCopy> _deterministic_sg_shadows;
extern robin_hood::unordered_flat_map<const RandomizedSpriteGroup *, RandomizedSpriteGroupShadowCopy> _randomized_sg_shadows;

const SpriteGroup *PruneTargetSpriteGroup(const SpriteGroup *result);
void OptimiseVarAction2Adjust(VarAction2OptimiseState &state, const VarAction2AdjustInfo info, DeterministicSpriteGroup *group, DeterministicSpriteGroupAdjust &adjust);
void OptimiseVarAction2DeterministicSpriteGroup(VarAction2OptimiseState &state, const VarAction2AdjustInfo info, DeterministicSpriteGroup *group, std::vector<DeterministicSpriteGroupAdjust> &saved_adjusts);
void HandleVarAction2OptimisationPasses();
void ReleaseVarAction2OptimisationCaches();

#endif /* NEWGRF_OPTIMISER_INTERNAL_H */
