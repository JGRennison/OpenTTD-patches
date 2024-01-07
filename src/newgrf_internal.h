/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file newgrf_internal.h Internal NewGRF processing definitions.
 */

#ifndef NEWGRF_INTERNAL_H
#define NEWGRF_INTERNAL_H

#include "newgrf.h"
#include "newgrf_spritegroup.h"
#include "spriteloader/spriteloader.hpp"
#include "core/arena_alloc.hpp"

#include "3rdparty/cpp-btree/btree_map.h"
#include <bitset>
#include <vector>

/** Base GRF ID for OpenTTD's base graphics GRFs. */
static const uint32_t OPENTTD_GRAPHICS_BASE_GRF_ID = BSWAP32(0xFF4F5400);

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
struct GrfProcessingState {
private:
	/** Definition of a single Action1 spriteset */
	struct SpriteSet {
		SpriteID sprite;  ///< SpriteID of the first sprite of the set.
		uint num_sprites; ///< Number of sprites in the set.
	};

	/** Currently referenceable spritesets */
	btree::btree_map<uint, SpriteSet> spritesets[GSF_END];

public:
	/* Global state */
	GrfLoadingStage stage;    ///< Current loading stage
	SpriteID spriteid;        ///< First available SpriteID for loading realsprites.

	/* Local state in the file */
	SpriteFile *file;         ///< File of currently processed GRF file.
	GRFFile *grffile;         ///< Currently processed GRF file.
	GRFConfig *grfconfig;     ///< Config of the currently processed GRF file.
	uint32_t nfo_line;        ///< Currently processed pseudo sprite number in the GRF.

	/* Kind of return values when processing certain actions */
	int skip_sprites;         ///< Number of pseudo sprites to skip before processing the next one. (-1 to skip to end of file)

	/* Currently referenceable spritegroups */
	std::vector<const SpriteGroup *> spritegroups;

	/* VarAction2 temporary storage variable tracking */
	btree::btree_map<const SpriteGroup *, VarAction2GroupVariableTracking *> group_temp_store_variable_tracking;
	UniformArenaAllocator<sizeof(VarAction2GroupVariableTracking), 1024> group_temp_store_variable_tracking_storage;
	btree::btree_map<const SpriteGroup *, VarAction2ProcedureAnnotation *> procedure_annotations;
	UniformArenaAllocator<sizeof(VarAction2ProcedureAnnotation), 1024> procedure_annotations_storage;
	btree::btree_map<const DeterministicSpriteGroup *, std::vector<DeterministicSpriteGroupAdjust> *> inlinable_adjust_groups;
	UniformArenaAllocator<sizeof(std::vector<DeterministicSpriteGroupAdjust>), 1024> inlinable_adjust_groups_storage;
	std::vector<DeterministicSpriteGroup *> dead_store_elimination_candidates;

	VarAction2GroupVariableTracking *GetVarAction2GroupVariableTracking(const SpriteGroup *group, bool make_new)
	{
		if (make_new) {
			VarAction2GroupVariableTracking *&ptr = this->group_temp_store_variable_tracking[group];
			if (!ptr) ptr = new (this->group_temp_store_variable_tracking_storage.Allocate()) VarAction2GroupVariableTracking();
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
		if (!ptr) {
			ptr = new (this->procedure_annotations_storage.Allocate()) VarAction2ProcedureAnnotation();
			return std::make_pair(ptr, true);
		} else {
			return std::make_pair(ptr, false);
		}
	}

	std::vector<DeterministicSpriteGroupAdjust> *GetInlinableGroupAdjusts(const DeterministicSpriteGroup *group, bool make_new)
	{
		if (make_new) {
			std::vector<DeterministicSpriteGroupAdjust> *&ptr = this->inlinable_adjust_groups[group];
			if (!ptr) ptr = new (this->inlinable_adjust_groups_storage.Allocate()) std::vector<DeterministicSpriteGroupAdjust>();
			return ptr;
		} else {
			auto iter = this->inlinable_adjust_groups.find(group);
			if (iter != this->inlinable_adjust_groups.end()) return iter->second;
			return nullptr;
		}
	}

	/** Clear temporary data before processing the next file in the current loading stage */
	void ClearDataForNextFile()
	{
		this->nfo_line = 0;
		this->skip_sprites = 0;

		for (uint i = 0; i < GSF_END; i++) {
			this->spritesets[i].clear();
		}

		this->spritegroups.clear();

		this->group_temp_store_variable_tracking.clear();
		this->group_temp_store_variable_tracking_storage.EmptyArena();
		this->procedure_annotations.clear();
		this->procedure_annotations_storage.EmptyArena();
		for (auto iter : this->inlinable_adjust_groups) {
			iter.second->~vector<DeterministicSpriteGroupAdjust>();
		}
		this->inlinable_adjust_groups.clear();
		this->inlinable_adjust_groups_storage.EmptyArena();
		this->dead_store_elimination_candidates.clear();
	}

	/**
	 * Records new spritesets.
	 * @param feature GrfSpecFeature the set is defined for.
	 * @param first_sprite SpriteID of the first sprite in the set.
	 * @param first_set First spriteset to define.
	 * @param numsets Number of sets to define.
	 * @param numents Number of sprites per set to define.
	 */
	void AddSpriteSets(byte feature, SpriteID first_sprite, uint first_set, uint numsets, uint numents)
	{
		assert(feature < GSF_END);
		for (uint i = 0; i < numsets; i++) {
			SpriteSet &set = this->spritesets[feature][first_set + i];
			set.sprite = first_sprite + i * numents;
			set.num_sprites = numents;
		}
	}

	/**
	 * Check whether there are any valid spritesets for a feature.
	 * @param feature GrfSpecFeature to check.
	 * @return true if there are any valid sets.
	 * @note Spritesets with zero sprites are valid to allow callback-failures.
	 */
	bool HasValidSpriteSets(byte feature) const
	{
		assert(feature < GSF_END);
		return !this->spritesets[feature].empty();
	}

	/**
	 * Check whether a specific set is defined.
	 * @param feature GrfSpecFeature to check.
	 * @param set Set to check.
	 * @return true if the set is valid.
	 * @note Spritesets with zero sprites are valid to allow callback-failures.
	 */
	bool IsValidSpriteSet(byte feature, uint set) const
	{
		assert(feature < GSF_END);
		return this->spritesets[feature].find(set) != this->spritesets[feature].end();
	}

	/**
	 * Returns the first sprite of a spriteset.
	 * @param feature GrfSpecFeature to query.
	 * @param set Set to query.
	 * @return First sprite of the set.
	 */
	SpriteID GetSprite(byte feature, uint set) const
	{
		assert(IsValidSpriteSet(feature, set));
		return this->spritesets[feature].find(set)->second.sprite;
	}

	/**
	 * Returns the number of sprites in a spriteset
	 * @param feature GrfSpecFeature to query.
	 * @param set Set to query.
	 * @return Number of sprites in the set.
	 */
	uint GetNumEnts(byte feature, uint set) const
	{
		assert(IsValidSpriteSet(feature, set));
		return this->spritesets[feature].find(set)->second.num_sprites;
	}
};

extern GrfProcessingState _cur;

enum VarAction2AdjustInferenceFlags {
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
};
DECLARE_ENUM_AS_BIT_SET(VarAction2AdjustInferenceFlags)

struct VarAction2TempStoreInferenceVarSource {
	DeterministicSpriteGroupAdjustType type;
	uint16_t variable;
	byte shift_num;
	uint32_t parameter;
	uint32_t and_mask;
	uint32_t add_val;
	uint32_t divmod_val;
};

struct VarAction2TempStoreInference {
	VarAction2AdjustInferenceFlags inference = VA2AIF_NONE;
	uint32_t store_constant = 0;
	VarAction2TempStoreInferenceVarSource var_source;
	uint version = 0;
};

struct VarAction2InferenceBackup {
	VarAction2AdjustInferenceFlags inference = VA2AIF_NONE;
	uint32_t current_constant = 0;
	uint adjust_size = 0;
};

struct VarAction2OptimiseState {
	VarAction2AdjustInferenceFlags inference = VA2AIF_NONE;
	uint32_t current_constant = 0;
	btree::btree_map<uint8_t, VarAction2TempStoreInference> temp_stores;
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
			this->var_tracking = _cur.GetVarAction2GroupVariableTracking(group, true);
		}
		return this->var_tracking;
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
	byte varsize;
};

const SpriteGroup *PruneTargetSpriteGroup(const SpriteGroup *result);
void OptimiseVarAction2Adjust(VarAction2OptimiseState &state, const VarAction2AdjustInfo info, DeterministicSpriteGroup *group, DeterministicSpriteGroupAdjust &adjust);
void OptimiseVarAction2DeterministicSpriteGroup(VarAction2OptimiseState &state, const VarAction2AdjustInfo info, DeterministicSpriteGroup *group, std::vector<DeterministicSpriteGroupAdjust> &saved_adjusts);
void HandleVarAction2OptimisationPasses();

#endif /* NEWGRF_INTERNAL_H */
