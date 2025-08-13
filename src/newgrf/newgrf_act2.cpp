/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_act2.cpp NewGRF Action 0x02 handler. */

#include "../stdafx.h"
#include <ranges>
#include "../debug.h"
#include "../debug_settings.h"
#include "../newgrf_engine.h"
#include "../newgrf_extension.h"
#include "../newgrf_cargo.h"
#include "../error.h"
#include "../vehicle_base.h"
#include "../road.h"
#include "../core/alloc_func.hpp"
#include "newgrf_bytereader.h"
#include "newgrf_internal.h"
#include "newgrf_optimiser_internal.h"

#include "../table/strings.h"

#include "../safeguards.h"

constexpr uint16_t GROUPID_CALLBACK_FAILED = 0x7FFF; ///< Explicit "failure" result.
constexpr uint16_t GROUPID_CALCULATED_RESULT = 0x7FFE; ///< Return calculated result from VarAction2.

static CalculatedResultSpriteGroup _calculated_result_group;

/**
 * Map the colour modifiers of TTDPatch to those that Open is using.
 * @param grf_sprite Pointer to the structure been modified.
 */
void MapSpriteMappingRecolour(PalSpriteID *grf_sprite)
{
	if (HasBit(grf_sprite->pal, 14)) {
		ClrBit(grf_sprite->pal, 14);
		SetBit(grf_sprite->sprite, SPRITE_MODIFIER_OPAQUE);
	}

	if (HasBit(grf_sprite->sprite, 14)) {
		ClrBit(grf_sprite->sprite, 14);
		SetBit(grf_sprite->sprite, PALETTE_MODIFIER_TRANSPARENT);
	}

	if (HasBit(grf_sprite->sprite, 15)) {
		ClrBit(grf_sprite->sprite, 15);
		SetBit(grf_sprite->sprite, PALETTE_MODIFIER_COLOUR);
	}
}

/**
 * Read a sprite and a palette from the GRF and convert them into a format
 * suitable to OpenTTD.
 * @param buf                 Input stream.
 * @param read_flags          Whether to read TileLayoutFlags.
 * @param invert_action1_flag Set to true, if palette bit 15 means 'not from action 1'.
 * @param use_cur_spritesets  Whether to use currently referenceable action 1 sets.
 * @param feature             GrfSpecFeature to use spritesets from.
 * @param[out] grf_sprite     Read sprite and palette.
 * @param[out] max_sprite_offset  Optionally returns the number of sprites in the spriteset of the sprite. (0 if no spritset)
 * @param[out] max_palette_offset Optionally returns the number of sprites in the spriteset of the palette. (0 if no spritset)
 * @return Read TileLayoutFlags.
 */
TileLayoutFlags ReadSpriteLayoutSprite(ByteReader &buf, bool read_flags, bool invert_action1_flag, bool use_cur_spritesets, int feature, PalSpriteID *grf_sprite, uint16_t *max_sprite_offset, uint16_t *max_palette_offset)
{
	grf_sprite->sprite = buf.ReadWord();
	grf_sprite->pal = buf.ReadWord();
	TileLayoutFlags flags = read_flags ? (TileLayoutFlags)buf.ReadWord() : TLF_NOTHING;

	MapSpriteMappingRecolour(grf_sprite);

	bool custom_sprite = HasBit(grf_sprite->pal, 15) != invert_action1_flag;
	ClrBit(grf_sprite->pal, 15);

	if (custom_sprite) {
		/* Use sprite from Action 1 */
		uint index = GB(grf_sprite->sprite, 0, 14);
		SpriteSetInfo sprite_set_info;
		if (use_cur_spritesets) sprite_set_info = _cur.GetSpriteSetInfo(feature, index);
		if (use_cur_spritesets && (!sprite_set_info.IsValid() || sprite_set_info.GetNumEnts() == 0)) {
			GrfMsg(1, "ReadSpriteLayoutSprite: Spritelayout uses undefined custom spriteset {}", index);
			grf_sprite->sprite = SPR_IMG_QUERY;
			grf_sprite->pal = PAL_NONE;
		} else {
			SpriteID sprite = use_cur_spritesets ? sprite_set_info.GetSprite() : index;
			if (max_sprite_offset != nullptr) *max_sprite_offset = use_cur_spritesets ? sprite_set_info.GetNumEnts() : UINT16_MAX;
			SB(grf_sprite->sprite, 0, SPRITE_WIDTH, sprite);
			SetBit(grf_sprite->sprite, SPRITE_MODIFIER_CUSTOM_SPRITE);
		}
	} else if ((flags & TLF_SPRITE_VAR10) && !(flags & TLF_SPRITE_REG_FLAGS)) {
		GrfMsg(1, "ReadSpriteLayoutSprite: Spritelayout specifies var10 value for non-action-1 sprite");
		DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
		return flags;
	}

	if (flags & TLF_CUSTOM_PALETTE) {
		/* Use palette from Action 1 */
		uint index = GB(grf_sprite->pal, 0, 14);
		SpriteSetInfo sprite_set_info;
		if (use_cur_spritesets) sprite_set_info = _cur.GetSpriteSetInfo(feature, index);
		if (use_cur_spritesets && (!sprite_set_info.IsValid() || sprite_set_info.GetNumEnts() == 0)) {
			GrfMsg(1, "ReadSpriteLayoutSprite: Spritelayout uses undefined custom spriteset {} for 'palette'", index);
			grf_sprite->pal = PAL_NONE;
		} else {
			SpriteID sprite = use_cur_spritesets ? sprite_set_info.GetSprite() : index;
			if (max_palette_offset != nullptr) *max_palette_offset = use_cur_spritesets ? sprite_set_info.GetNumEnts() : UINT16_MAX;
			SB(grf_sprite->pal, 0, SPRITE_WIDTH, sprite);
			SetBit(grf_sprite->pal, SPRITE_MODIFIER_CUSTOM_SPRITE);
		}
	} else if ((flags & TLF_PALETTE_VAR10) && !(flags & TLF_PALETTE_REG_FLAGS)) {
		GrfMsg(1, "ReadSpriteLayoutRegisters: Spritelayout specifies var10 value for non-action-1 palette");
		DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
		return flags;
	}

	return flags;
}

/**
 * Preprocess the TileLayoutFlags and read register modifiers from the GRF.
 * @param buf        Input stream.
 * @param flags      TileLayoutFlags to process.
 * @param is_parent  Whether the sprite is a parentsprite with a bounding box.
 * @param dts        Sprite layout to insert data into.
 * @param index      Sprite index to process; 0 for ground sprite.
 */
static void ReadSpriteLayoutRegisters(ByteReader &buf, TileLayoutFlags flags, bool is_parent, NewGRFSpriteLayout *dts, uint index)
{
	if (!(flags & TLF_DRAWING_FLAGS)) return;

	if (dts->registers.empty()) dts->AllocateRegisters();
	TileLayoutRegisters &regs = const_cast<TileLayoutRegisters&>(dts->registers[index]);
	regs.flags = flags & TLF_DRAWING_FLAGS;

	if (flags & TLF_DODRAW)  regs.dodraw  = buf.ReadByte();
	if (flags & TLF_SPRITE)  regs.sprite  = buf.ReadByte();
	if (flags & TLF_PALETTE) regs.palette = buf.ReadByte();

	if (is_parent) {
		if (flags & TLF_BB_XY_OFFSET) {
			regs.delta.parent[0] = buf.ReadByte();
			regs.delta.parent[1] = buf.ReadByte();
		}
		if (flags & TLF_BB_Z_OFFSET)    regs.delta.parent[2] = buf.ReadByte();
	} else {
		if (flags & TLF_CHILD_X_OFFSET) regs.delta.child[0]  = buf.ReadByte();
		if (flags & TLF_CHILD_Y_OFFSET) regs.delta.child[1]  = buf.ReadByte();
	}

	if (flags & TLF_SPRITE_VAR10) {
		regs.sprite_var10 = buf.ReadByte();
		if (regs.sprite_var10 > TLR_MAX_VAR10) {
			GrfMsg(1, "ReadSpriteLayoutRegisters: Spritelayout specifies var10 ({}) exceeding the maximal allowed value {}", regs.sprite_var10, TLR_MAX_VAR10);
			DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
			return;
		}
	}

	if (flags & TLF_PALETTE_VAR10) {
		regs.palette_var10 = buf.ReadByte();
		if (regs.palette_var10 > TLR_MAX_VAR10) {
			GrfMsg(1, "ReadSpriteLayoutRegisters: Spritelayout specifies var10 ({}) exceeding the maximal allowed value {}", regs.palette_var10, TLR_MAX_VAR10);
			DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
			return;
		}
	}
}

/**
 * Read a spritelayout from the GRF.
 * @param buf                  Input
 * @param num_building_sprites Number of building sprites to read
 * @param use_cur_spritesets   Whether to use currently referenceable action 1 sets.
 * @param feature              GrfSpecFeature to use spritesets from.
 * @param allow_var10          Whether the spritelayout may specify var10 values for resolving multiple action-1-2-3 chains
 * @param no_z_position        Whether bounding boxes have no Z offset
 * @param dts                  Layout container to output into
 * @return True on error (GRF was disabled).
 */
bool ReadSpriteLayout(ByteReader &buf, uint num_building_sprites, bool use_cur_spritesets, uint8_t feature, bool allow_var10, bool no_z_position, NewGRFSpriteLayout *dts)
{
	bool has_flags = HasBit(num_building_sprites, 6);
	ClrBit(num_building_sprites, 6);
	TileLayoutFlags valid_flags = TLF_KNOWN_FLAGS;
	if (!allow_var10) valid_flags &= ~TLF_VAR10_FLAGS;
	dts->Allocate(num_building_sprites); // allocate before reading groundsprite flags

	TempBufferT<uint16_t, 16> max_sprite_offset(num_building_sprites + 1, 0);
	TempBufferT<uint16_t, 16> max_palette_offset(num_building_sprites + 1, 0);

	/* Groundsprite */
	TileLayoutFlags flags = ReadSpriteLayoutSprite(buf, has_flags, false, use_cur_spritesets, feature, &dts->ground, max_sprite_offset, max_palette_offset);
	if (_cur.skip_sprites < 0) return true;

	if (flags & ~(valid_flags & ~TLF_NON_GROUND_FLAGS)) {
		GrfMsg(1, "ReadSpriteLayout: Spritelayout uses invalid flag 0x{:X} for ground sprite", flags & ~(valid_flags & ~TLF_NON_GROUND_FLAGS));
		DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
		return true;
	}

	ReadSpriteLayoutRegisters(buf, flags, false, dts, 0);
	if (_cur.skip_sprites < 0) return true;

	for (uint i = 0; i < num_building_sprites; i++) {
		DrawTileSeqStruct *seq = const_cast<DrawTileSeqStruct*>(&dts->seq[i]);

		flags = ReadSpriteLayoutSprite(buf, has_flags, false, use_cur_spritesets, feature, &seq->image, max_sprite_offset + i + 1, max_palette_offset + i + 1);
		if (_cur.skip_sprites < 0) return true;

		if (flags & ~valid_flags) {
			GrfMsg(1, "ReadSpriteLayout: Spritelayout uses unknown flag 0x{:X}", flags & ~valid_flags);
			DisableGrf(STR_NEWGRF_ERROR_INVALID_SPRITE_LAYOUT);
			return true;
		}

		seq->delta_x = buf.ReadByte();
		seq->delta_y = buf.ReadByte();

		if (!no_z_position) seq->delta_z = buf.ReadByte();

		if (seq->IsParentSprite()) {
			seq->size_x = buf.ReadByte();
			seq->size_y = buf.ReadByte();
			seq->size_z = buf.ReadByte();
		}

		ReadSpriteLayoutRegisters(buf, flags, seq->IsParentSprite(), dts, i + 1);
		if (_cur.skip_sprites < 0) return true;
	}

	/* Check if the number of sprites per spriteset is consistent */
	bool is_consistent = true;
	dts->consistent_max_offset = 0;
	for (uint i = 0; i < num_building_sprites + 1; i++) {
		if (max_sprite_offset[i] > 0) {
			if (dts->consistent_max_offset == 0) {
				dts->consistent_max_offset = max_sprite_offset[i];
			} else if (dts->consistent_max_offset != max_sprite_offset[i]) {
				is_consistent = false;
				break;
			}
		}
		if (max_palette_offset[i] > 0) {
			if (dts->consistent_max_offset == 0) {
				dts->consistent_max_offset = max_palette_offset[i];
			} else if (dts->consistent_max_offset != max_palette_offset[i]) {
				is_consistent = false;
				break;
			}
		}
	}

	/* When the Action1 sets are unknown, everything should be 0 (no spriteset usage) or UINT16_MAX (some spriteset usage) */
	assert(use_cur_spritesets || (is_consistent && (dts->consistent_max_offset == 0 || dts->consistent_max_offset == UINT16_MAX)));

	if (!is_consistent || !dts->registers.empty()) {
		dts->consistent_max_offset = 0;
		if (dts->registers.empty()) dts->AllocateRegisters();

		for (uint i = 0; i < num_building_sprites + 1; i++) {
			TileLayoutRegisters &regs = const_cast<TileLayoutRegisters&>(dts->registers[i]);
			regs.max_sprite_offset = max_sprite_offset[i];
			regs.max_palette_offset = max_palette_offset[i];
		}
	}

	return false;
}

static robin_hood::unordered_map<uint16_t, const CallbackResultSpriteGroup *> _callback_result_cache;

void ResetCallbacks(bool final)
{
	_callback_result_cache.clear();
	if (final) {
		auto tmp = std::move(_callback_result_cache);
	}
}

const CallbackResultSpriteGroup *NewCallbackResultSpriteGroupNoTransform(uint16_t result)
{
	const CallbackResultSpriteGroup *&ptr = _callback_result_cache[result];
	if (ptr == nullptr) {
		assert(CallbackResultSpriteGroup::CanAllocateItem());
		ptr = new CallbackResultSpriteGroup(result);
	}
	return ptr;
}

static const CallbackResultSpriteGroup *NewCallbackResultSpriteGroup(uint16_t groupid)
{
	uint16_t result = CallbackResultSpriteGroup::TransformResultValue(groupid, _cur.grffile->grf_version >= 8);
	return NewCallbackResultSpriteGroupNoTransform(result);
}

static const SpriteGroup *GetGroupFromGroupIDNoCBResult(uint16_t setid, uint8_t type, uint16_t groupid)
{
	if (groupid == GROUPID_CALLBACK_FAILED) return nullptr;

	if ((size_t)groupid >= _cur.spritegroups.size() || _cur.spritegroups[groupid] == nullptr) {
		GrfMsg(1, "GetGroupFromGroupID(0x{:02X}:0x{:02X}): Groupid 0x{:04X} does not exist, leaving empty", setid, type, groupid);
		return nullptr;
	}

	const SpriteGroup *result = _cur.spritegroups[groupid];
	if (likely(!HasBit(_misc_debug_flags, MDF_NEWGRF_SG_SAVE_RAW))) result = PruneTargetSpriteGroup(result);
	return result;
}

/* Helper function to either create a callback or link to a previously
 * defined spritegroup. */
static const SpriteGroup *GetGroupFromGroupID(uint16_t setid, uint8_t type, uint16_t groupid)
{
	if (HasBit(groupid, 15)) {
		return NewCallbackResultSpriteGroup(groupid);
	}

	return GetGroupFromGroupIDNoCBResult(setid, type, groupid);
}

/**
 * Helper function to either create a callback or a result sprite group.
 * @param feature GrfSpecFeature to define spritegroup for.
 * @param setid SetID of the currently being parsed Action2. (only for debug output)
 * @param type Type of the currently being parsed Action2. (only for debug output)
 * @param spriteid Raw value from the GRF for the new spritegroup; describes either the return value or the referenced spritegroup.
 * @return Created spritegroup.
 */
static const SpriteGroup *CreateGroupFromGroupID(uint8_t feature, uint16_t setid, uint8_t type, uint16_t spriteid)
{
	if (HasBit(spriteid, 15)) {
		return NewCallbackResultSpriteGroup(spriteid);
	}

	const SpriteSetInfo sprite_set_info = _cur.GetSpriteSetInfo(feature, spriteid);

	if (!sprite_set_info.IsValid()) {
		GrfMsg(1, "CreateGroupFromGroupID(0x{:02X}:0x{:02X}): Sprite set {} invalid", setid, type, spriteid);
		return nullptr;
	}

	SpriteID spriteset_start = sprite_set_info.GetSprite();
	uint num_sprites = sprite_set_info.GetNumEnts();

	/* Ensure that the sprites are loeded */
	assert(spriteset_start + num_sprites <= _cur.spriteid);

	assert(ResultSpriteGroup::CanAllocateItem());
	return new ResultSpriteGroup(spriteset_start, num_sprites);
}

static void ProcessDeterministicSpriteGroupRanges(const std::vector<DeterministicSpriteGroupRange> &ranges, std::vector<DeterministicSpriteGroupRange> &ranges_out, const SpriteGroup *default_group)
{
	/* Sort ranges ascending. When ranges overlap, this may required clamping or splitting them */
	std::vector<uint32_t> bounds;
	bounds.reserve(ranges.size());
	for (uint i = 0; i < ranges.size(); i++) {
		bounds.push_back(ranges[i].low);
		if (ranges[i].high != UINT32_MAX) bounds.push_back(ranges[i].high + 1);
	}
	std::sort(bounds.begin(), bounds.end());
	bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());

	std::vector<const SpriteGroup *> target;
	target.reserve(bounds.size());
	for (uint j = 0; j < bounds.size(); ++j) {
		uint32_t v = bounds[j];
		const SpriteGroup *t = default_group;
		for (uint i = 0; i < ranges.size(); i++) {
			if (ranges[i].low <= v && v <= ranges[i].high) {
				t = ranges[i].group;
				break;
			}
		}
		target.push_back(t);
	}
	assert(target.size() == bounds.size());

	for (uint j = 0; j < bounds.size(); ) {
		if (target[j] != default_group) {
			DeterministicSpriteGroupRange &r = ranges_out.emplace_back();
			r.group = target[j];
			r.low = bounds[j];
			while (j < bounds.size() && target[j] == r.group) {
				j++;
			}
			r.high = j < bounds.size() ? bounds[j] - 1 : UINT32_MAX;
		} else {
			j++;
		}
	}
}

static VarSpriteGroupScopeOffset ParseRelativeScopeByte(uint8_t relative)
{
	VarSpriteGroupScopeOffset var_scope_count = (GB(relative, 6, 2) << 8);
	if ((relative & 0xF) == 0) {
		SetBit(var_scope_count, 15);
	} else {
		var_scope_count |= (relative & 0xF);
	}
	return var_scope_count;
}

/* Action 0x02 */
static void NewSpriteGroup(ByteReader &buf)
{
	/* <02> <feature> <set-id> <type/num-entries> <feature-specific-data...>
	 *
	 * B feature       see action 1
	 * B set-id        ID of this particular definition
	 *                 This is an extended byte if feature "more_action2_ids" is tested for
	 * B type/num-entries
	 *                 if 80 or greater, this is a randomized or variational
	 *                 list definition, see below
	 *                 otherwise it specifies a number of entries, the exact
	 *                 meaning depends on the feature
	 * V feature-specific-data (huge mess, don't even look it up --pasky) */
	const SpriteGroup *act_group = nullptr;

	GrfSpecFeatureRef feature_ref = ReadFeature(buf.ReadByte());
	GrfSpecFeature feature = feature_ref.id;
	if (feature >= GSF_END) {
		GrfMsg(1, "NewSpriteGroup: Unsupported feature {}, skipping", GetFeatureString(feature_ref));
		return;
	}

	uint16_t setid  = HasBit(_cur.grffile->observed_feature_tests, GFTOF_MORE_ACTION2_IDS) ? buf.ReadExtendedByte() : buf.ReadByte();
	uint8_t type    = buf.ReadByte();

	/* Sprite Groups are created here but they are allocated from a pool, so
	 * we do not need to delete anything if there is an exception from the
	 * ByteReader. */

	/* Decoded sprite type */
	enum SpriteType {
		STYPE_NORMAL,
		STYPE_DETERMINISTIC,
		STYPE_DETERMINISTIC_RELATIVE,
		STYPE_DETERMINISTIC_RELATIVE_2,
		STYPE_RANDOMIZED,
		STYPE_CB_FAILURE,
	};
	SpriteType stype = STYPE_NORMAL;
	switch (type) {
		/* Deterministic Sprite Group */
		case 0x81: // Self scope, byte
		case 0x82: // Parent scope, byte
		case 0x85: // Self scope, word
		case 0x86: // Parent scope, word
		case 0x89: // Self scope, dword
		case 0x8A: // Parent scope, dword
			stype = STYPE_DETERMINISTIC;
			break;

		/* Randomized Sprite Group */
		case 0x80: // Self scope
		case 0x83: // Parent scope
		case 0x84: // Relative scope
			stype = STYPE_RANDOMIZED;
			break;

		/* Extension type */
		case 0x87:
			if (HasBit(_cur.grffile->observed_feature_tests, GFTOF_MORE_VARACTION2_TYPES)) {
				uint8_t subtype = buf.ReadByte();
				switch (subtype) {
					case 0:
						stype = STYPE_CB_FAILURE;
						break;

					case 1:
						stype = STYPE_DETERMINISTIC_RELATIVE;
						break;

					case 2:
						stype = STYPE_DETERMINISTIC_RELATIVE_2;
						break;

					default:
						GrfMsg(1, "NewSpriteGroup: Unknown 0x87 extension subtype {:02X} for feature {}, handling as CB failure", subtype, GetFeatureString(feature));
						stype = STYPE_CB_FAILURE;
						break;
				}
			}
			break;

		default:
			break;
	}

	switch (stype) {
		/* Deterministic Sprite Group */
		case STYPE_DETERMINISTIC:
		case STYPE_DETERMINISTIC_RELATIVE:
		case STYPE_DETERMINISTIC_RELATIVE_2:
		{
			VarSpriteGroupScopeOffset var_scope_count = 0;
			if (stype == STYPE_DETERMINISTIC_RELATIVE) {
				var_scope_count = ParseRelativeScopeByte(buf.ReadByte());
			} else if (stype == STYPE_DETERMINISTIC_RELATIVE_2) {
				uint8_t mode = buf.ReadByte();
				uint8_t offset = buf.ReadByte();
				bool invalid = false;
				if ((mode & 0x7F) >= VSGSRM_END) {
					invalid = true;
				}
				if (HasBit(mode, 7)) {
					/* Use variable 0x100 */
					if (offset != 0) invalid = true;
				}
				if (invalid) {
					GrfMsg(1, "NewSpriteGroup: Unknown 0x87 extension subtype 2 relative mode: {:02X} {:02X} for feature {}, handling as CB failure", mode, offset, GetFeatureString(feature));
					act_group = NewCallbackResultSpriteGroupNoTransform(CALLBACK_FAILED);
					break;
				}
				var_scope_count = (mode << 8) | offset;
			}

			uint8_t varadjust;
			uint8_t varsize;

			bool first_adjust = true;

			assert(DeterministicSpriteGroup::CanAllocateItem());
			DeterministicSpriteGroup *group = new DeterministicSpriteGroup();
			group->nfo_line = _cur.nfo_line;
			group->feature = feature;
			if (_action6_override_active) group->sg_flags |= SGF_ACTION6;
			act_group = group;

			if (stype == STYPE_DETERMINISTIC_RELATIVE || stype == STYPE_DETERMINISTIC_RELATIVE_2) {
				group->var_scope = (feature <= GSF_AIRCRAFT) ? VSG_SCOPE_RELATIVE : VSG_SCOPE_SELF;
				group->var_scope_count = var_scope_count;

				group->size = DSG_SIZE_DWORD;
				varsize = 4;
			} else {
				group->var_scope = HasBit(type, 1) ? VSG_SCOPE_PARENT : VSG_SCOPE_SELF;

				switch (GB(type, 2, 2)) {
					default: NOT_REACHED();
					case 0: group->size = DSG_SIZE_BYTE;  varsize = 1; break;
					case 1: group->size = DSG_SIZE_WORD;  varsize = 2; break;
					case 2: group->size = DSG_SIZE_DWORD; varsize = 4; break;
				}
			}

			const VarAction2AdjustInfo info = { feature, GetGrfSpecFeatureForScope(feature, group->var_scope), varsize };

			DeterministicSpriteGroupShadowCopy *shadow = nullptr;
			if (unlikely(HasBit(_misc_debug_flags, MDF_NEWGRF_SG_SAVE_RAW))) {
				shadow = &(_deterministic_sg_shadows[group]);
			}
			static std::vector<DeterministicSpriteGroupAdjust> current_adjusts;
			current_adjusts.clear();

			VarAction2OptimiseState va2_opt_state;
			/* The initial value is always the constant 0 */
			va2_opt_state.inference = VA2AIF_SIGNED_NON_NEGATIVE | VA2AIF_ONE_OR_ZERO | VA2AIF_HAVE_CONSTANT;
			va2_opt_state.current_constant = 0;

			/* Loop through the var adjusts. Unfortunately we don't know how many we have
			 * from the outset, so we shall have to keep reallocing. */
			do {
				DeterministicSpriteGroupAdjust &adjust = current_adjusts.emplace_back();

				/* The first var adjust doesn't have an operation specified, so we set it to add. */
				adjust.operation = first_adjust ? DSGA_OP_ADD : (DeterministicSpriteGroupAdjustOperation)buf.ReadByte();
				first_adjust = false;
				if (adjust.operation > DSGA_OP_END) adjust.operation = DSGA_OP_END;
				adjust.variable  = buf.ReadByte();
				if (adjust.variable == 0x7E) {
					/* Link subroutine group */
					adjust.subroutine = GetGroupFromGroupIDNoCBResult(setid, type, HasBit(_cur.grffile->observed_feature_tests, GFTOF_MORE_ACTION2_IDS) ? buf.ReadExtendedByte() : buf.ReadByte());
				} else {
					adjust.parameter = IsInsideMM(adjust.variable, 0x60, 0x80) ? buf.ReadByte() : 0;
				}

				varadjust = buf.ReadByte();
				adjust.shift_num = GB(varadjust, 0, 5);
				adjust.type      = (DeterministicSpriteGroupAdjustType)GB(varadjust, 6, 2);
				adjust.and_mask  = buf.ReadVarSize(varsize);

				if (adjust.variable == 0x11) {
					for (const GRFVariableMapEntry &remap : _cur.grffile->grf_variable_remaps) {
						if (remap.feature == info.scope_feature && remap.input_shift == adjust.shift_num && remap.input_mask == adjust.and_mask) {
							adjust.variable = remap.id;
							adjust.shift_num = remap.output_shift;
							adjust.and_mask = remap.output_mask;
							adjust.parameter = remap.output_param;
							break;
						}
					}
				} else if (adjust.variable == 0x7B && adjust.parameter == 0x11) {
					for (const GRFVariableMapEntry &remap : _cur.grffile->grf_variable_remaps) {
						if (remap.feature == info.scope_feature && remap.input_shift == adjust.shift_num && remap.input_mask == adjust.and_mask) {
							adjust.parameter = remap.id;
							adjust.shift_num = remap.output_shift;
							adjust.and_mask = remap.output_mask;
							break;
						}
					}
				}

				if (info.scope_feature == GSF_ROADSTOPS && HasBit(_cur.grffile->observed_feature_tests, GFTOF_ROAD_STOPS)) {
					if (adjust.variable == 0x68) adjust.variable = A2VRI_ROADSTOP_INFO_NEARBY_TILES_EXT;
					if (adjust.variable == 0x7B && adjust.parameter == 0x68) adjust.parameter = A2VRI_ROADSTOP_INFO_NEARBY_TILES_EXT;
				}

				if (adjust.type != DSGA_TYPE_NONE) {
					adjust.add_val    = buf.ReadVarSize(varsize);
					adjust.divmod_val = buf.ReadVarSize(varsize);
					if (adjust.divmod_val == 0) adjust.divmod_val = 1; // Ensure that divide by zero cannot occur
				} else {
					adjust.add_val    = 0;
					adjust.divmod_val = 0;
				}
				if (unlikely(shadow != nullptr)) {
					shadow->adjusts.push_back(adjust);
					/* Pruning was turned off so that the unpruned target could be saved in the shadow, prune now */
					if (adjust.subroutine != nullptr) adjust.subroutine = PruneTargetSpriteGroup(adjust.subroutine);
				}

				OptimiseVarAction2PreCheckAdjust(va2_opt_state, adjust);

				/* Continue reading var adjusts while bit 5 is set. */
			} while (HasBit(varadjust, 5));

			/* shrink_to_fit will be called later */
			group->adjusts.reserve(current_adjusts.size());

			for (const DeterministicSpriteGroupAdjust &adjust : current_adjusts) {
				group->adjusts.push_back(adjust);
				OptimiseVarAction2Adjust(va2_opt_state, info, group, group->adjusts.back());
			}

			auto get_result_group = [&](uint16_t group_id) -> const SpriteGroup * {
				if (group_id == GROUPID_CALCULATED_RESULT) {
					return &_calculated_result_group;
				} else {
					return GetGroupFromGroupID(setid, type, group_id);
				}
			};

			std::vector<DeterministicSpriteGroupRange> ranges;
			ranges.resize(buf.ReadByte());
			for (auto &range : ranges) {
				range.group = get_result_group(buf.ReadWord());
				range.low   = buf.ReadVarSize(varsize);
				range.high  = buf.ReadVarSize(varsize);
			}

			group->default_group = get_result_group(buf.ReadWord());

			if (unlikely(shadow != nullptr)) {
				shadow->calculated_result = ranges.size() == 0;
				ProcessDeterministicSpriteGroupRanges(ranges, shadow->ranges, group->default_group);
				shadow->default_group = group->default_group;

				/* Pruning was turned off so that the unpruned targets could be saved in the shadow ranges, prune now */
				for (DeterministicSpriteGroupRange &range : ranges) {
					range.group = PruneTargetSpriteGroup(range.group);
				}
				group->default_group = PruneTargetSpriteGroup(group->default_group);
			}

			group->error_group = ranges.empty() ? group->default_group : ranges[0].group;
			/* nvar == 0 is a special case -- we turn our value into a callback result */
			if (ranges.empty()) group->dsg_flags |= DSGF_CALCULATED_RESULT;

			ProcessDeterministicSpriteGroupRanges(ranges, group->ranges, group->default_group);

			OptimiseVarAction2DeterministicSpriteGroup(va2_opt_state, info, group, current_adjusts);
			current_adjusts.clear();
			break;
		}

		/* Randomized Sprite Group */
		case STYPE_RANDOMIZED:
		{
			assert(RandomizedSpriteGroup::CanAllocateItem());
			RandomizedSpriteGroup *group = new RandomizedSpriteGroup();
			group->nfo_line = _cur.nfo_line;
			if (_action6_override_active) group->sg_flags |= SGF_ACTION6;
			act_group = group;
			group->var_scope = HasBit(type, 1) ? VSG_SCOPE_PARENT : VSG_SCOPE_SELF;

			if (HasBit(type, 2)) {
				if (feature <= GSF_AIRCRAFT) group->var_scope = VSG_SCOPE_RELATIVE;
				group->var_scope_count = ParseRelativeScopeByte(buf.ReadByte());
			}

			uint8_t triggers = buf.ReadByte();
			group->triggers       = GB(triggers, 0, 7);
			group->cmp_mode       = HasBit(triggers, 7) ? RSG_CMP_ALL : RSG_CMP_ANY;
			group->lowest_randbit = buf.ReadByte();

			uint8_t num_groups = buf.ReadByte();
			if (!HasExactlyOneBit(num_groups)) {
				GrfMsg(1, "NewSpriteGroup: Random Action 2 nrand should be power of 2");
			}

			group->groups.reserve(num_groups);
			for (uint i = 0; i < num_groups; i++) {
				group->groups.push_back(GetGroupFromGroupID(setid, type, buf.ReadWord()));
			}

			if (unlikely(HasBit(_misc_debug_flags, MDF_NEWGRF_SG_SAVE_RAW))) {
				RandomizedSpriteGroupShadowCopy *shadow = &(_randomized_sg_shadows[group]);
				shadow->groups = group->groups;

				/* Pruning was turned off so that the unpruned targets could be saved in the shadow groups, prune now */
				for (const SpriteGroup *&group : group->groups) {
					group = PruneTargetSpriteGroup(group);
				}
			}

			break;
		}

		case STYPE_CB_FAILURE:
			act_group = NewCallbackResultSpriteGroupNoTransform(CALLBACK_FAILED);
			break;

		/* Neither a variable or randomized sprite group... must be a real group */
		case STYPE_NORMAL:
		{
			switch (feature) {
				case GSF_TRAINS:
				case GSF_ROADVEHICLES:
				case GSF_SHIPS:
				case GSF_AIRCRAFT:
				case GSF_STATIONS:
				case GSF_CANALS:
				case GSF_CARGOES:
				case GSF_AIRPORTS:
				case GSF_RAILTYPES:
				case GSF_ROADTYPES:
				case GSF_TRAMTYPES:
				case GSF_BADGES:
				case GSF_SIGNALS:
				case GSF_NEWLANDSCAPE:
				{
					uint8_t num_loaded  = type;
					uint8_t num_loading = buf.ReadByte();

					if (!_cur.HasValidSpriteSets(feature)) {
						GrfMsg(0, "NewSpriteGroup: No sprite set to work on! Skipping");
						return;
					}

					if (num_loaded + num_loading == 0) {
						GrfMsg(1, "NewSpriteGroup: no result, skipping invalid RealSpriteGroup");
						break;
					}

					GrfMsg(6, "NewSpriteGroup: New SpriteGroup 0x{:02X}, {} loaded, {} loading",
							setid, num_loaded, num_loading);

					if (num_loaded + num_loading == 0) {
						GrfMsg(1, "NewSpriteGroup: no result, skipping invalid RealSpriteGroup");
						break;
					}

					if (num_loaded + num_loading == 1) {
						/* Avoid creating 'Real' sprite group if only one option. */
						uint16_t spriteid = buf.ReadWord();
						act_group = CreateGroupFromGroupID(feature, setid, type, spriteid);
						GrfMsg(8, "NewSpriteGroup: one result, skipping RealSpriteGroup = subset {}", spriteid);
						break;
					}

					std::vector<uint16_t> loaded;
					std::vector<uint16_t> loading;

					loaded.reserve(num_loaded);
					for (uint i = 0; i < num_loaded; i++) {
						loaded.push_back(buf.ReadWord());
						GrfMsg(8, "NewSpriteGroup: + rg->loaded[{}]  = subset {}", i, loaded[i]);
					}

					loading.reserve(num_loading);
					for (uint i = 0; i < num_loading; i++) {
						loading.push_back(buf.ReadWord());
						GrfMsg(8, "NewSpriteGroup: + rg->loading[{}] = subset {}", i, loading[i]);
					}

					bool loaded_same = !loaded.empty() && std::adjacent_find(loaded.begin(),  loaded.end(),  std::not_equal_to<>()) == loaded.end();
					bool loading_same = !loading.empty() && std::adjacent_find(loading.begin(), loading.end(), std::not_equal_to<>()) == loading.end();
					if (loaded_same && loading_same && loaded[0] == loading[0]) {
						/* Both lists only contain the same value, so don't create 'Real' sprite group */
						act_group = CreateGroupFromGroupID(feature, setid, type, loaded[0]);
						GrfMsg(8, "NewSpriteGroup: same result, skipping RealSpriteGroup = subset {}", loaded[0]);
						break;
					}

					assert(RealSpriteGroup::CanAllocateItem());
					RealSpriteGroup *group = new RealSpriteGroup();
					group->nfo_line = _cur.nfo_line;
					if (_action6_override_active) group->sg_flags |= SGF_ACTION6;
					act_group = group;

					if (loaded_same && loaded.size() > 1) loaded.resize(1);
					group->loaded.reserve(loaded.size());
					for (uint16_t spriteid : loaded) {
						const SpriteGroup *t = CreateGroupFromGroupID(feature, setid, type, spriteid);
						group->loaded.push_back(t);
					}

					if (loading_same && loading.size() > 1) loading.resize(1);
					group->loading.reserve(loading.size());
					for (uint16_t spriteid : loading) {
						const SpriteGroup *t = CreateGroupFromGroupID(feature, setid, type, spriteid);
						group->loading.push_back(t);
					}

					break;
				}

				case GSF_HOUSES:
				case GSF_AIRPORTTILES:
				case GSF_OBJECTS:
				case GSF_INDUSTRYTILES:
				case GSF_ROADSTOPS: {
					uint8_t num_building_sprites = std::max((uint8_t)1, type);

					assert(TileLayoutSpriteGroup::CanAllocateItem());
					TileLayoutSpriteGroup *group = new TileLayoutSpriteGroup();
					group->nfo_line = _cur.nfo_line;
					if (_action6_override_active) group->sg_flags |= SGF_ACTION6;
					act_group = group;

					/* On error, bail out immediately. Temporary GRF data was already freed */
					if (ReadSpriteLayout(buf, num_building_sprites, true, feature, false, type == 0, &group->dts)) return;
					break;
				}

				case GSF_INDUSTRIES: {
					if (type > 2) {
						GrfMsg(1, "NewSpriteGroup: Unsupported industry production version {}, skipping", type);
						break;
					}

					assert(IndustryProductionSpriteGroup::CanAllocateItem());
					IndustryProductionSpriteGroup *group = new IndustryProductionSpriteGroup();
					group->nfo_line = _cur.nfo_line;
					if (_action6_override_active) group->sg_flags |= SGF_ACTION6;
					act_group = group;
					group->version = type;
					if (type == 0) {
						group->num_input = INDUSTRY_ORIGINAL_NUM_INPUTS;
						for (uint i = 0; i < INDUSTRY_ORIGINAL_NUM_INPUTS; i++) {
							group->subtract_input[i] = (int16_t)buf.ReadWord(); // signed
						}
						group->num_output = INDUSTRY_ORIGINAL_NUM_OUTPUTS;
						for (uint i = 0; i < INDUSTRY_ORIGINAL_NUM_OUTPUTS; i++) {
							group->add_output[i] = buf.ReadWord(); // unsigned
						}
						group->again = buf.ReadByte();
					} else if (type == 1) {
						group->num_input = INDUSTRY_ORIGINAL_NUM_INPUTS;
						for (uint i = 0; i < INDUSTRY_ORIGINAL_NUM_INPUTS; i++) {
							group->subtract_input[i] = buf.ReadByte();
						}
						group->num_output = INDUSTRY_ORIGINAL_NUM_OUTPUTS;
						for (uint i = 0; i < INDUSTRY_ORIGINAL_NUM_OUTPUTS; i++) {
							group->add_output[i] = buf.ReadByte();
						}
						group->again = buf.ReadByte();
					} else if (type == 2) {
						group->num_input = buf.ReadByte();
						if (group->num_input > std::size(group->subtract_input)) {
							GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
							error->data = "too many inputs (max 16)";
							return;
						}
						for (uint i = 0; i < group->num_input; i++) {
							uint8_t rawcargo = buf.ReadByte();
							CargoType cargo = GetCargoTranslation(rawcargo, _cur.grffile);
							if (!IsValidCargoType(cargo)) {
								/* The mapped cargo is invalid. This is permitted at this point,
								 * as long as the result is not used. Mark it invalid so this
								 * can be tested later. */
								group->version = 0xFF;
							} else if (auto v = group->cargo_input | std::views::take(i); std::ranges::find(v, cargo) != v.end()) {
								GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
								error->data = "duplicate input cargo";
								return;
							}
							group->cargo_input[i] = cargo;
							group->subtract_input[i] = buf.ReadByte();
						}
						group->num_output = buf.ReadByte();
						if (group->num_output > std::size(group->add_output)) {
							GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
							error->data = "too many outputs (max 16)";
							return;
						}
						for (uint i = 0; i < group->num_output; i++) {
							uint8_t rawcargo = buf.ReadByte();
							CargoType cargo = GetCargoTranslation(rawcargo, _cur.grffile);
							if (!IsValidCargoType(cargo)) {
								/* Mark this result as invalid to use */
								group->version = 0xFF;
							} else if (auto v = group->cargo_output | std::views::take(i); std::ranges::find(v, cargo) != v.end()) {
								GRFError *error = DisableGrf(STR_NEWGRF_ERROR_INDPROD_CALLBACK);
								error->data = "duplicate output cargo";
								return;
							}
							group->cargo_output[i] = cargo;
							group->add_output[i] = buf.ReadByte();
						}
						group->again = buf.ReadByte();
					} else {
						NOT_REACHED();
					}
					break;
				}

				case GSF_FAKE_TOWNS:
					act_group = NewCallbackResultSpriteGroupNoTransform(CALLBACK_FAILED);
					break;

				/* Loading of Tile Layout and Production Callback groups would happen here */
				default: GrfMsg(1, "NewSpriteGroup: Unsupported feature {}, skipping", GetFeatureString(feature));
			}
		}
	}

	if ((size_t)setid >= _cur.spritegroups.size()) _cur.spritegroups.resize(setid + 1);
	_cur.spritegroups[setid] = act_group;
}

template <> void GrfActionHandler<0x02>::FileScan(ByteReader &) { }
template <> void GrfActionHandler<0x02>::SafetyScan(ByteReader &) { }
template <> void GrfActionHandler<0x02>::LabelScan(ByteReader &) { }
template <> void GrfActionHandler<0x02>::Init(ByteReader &) { }
template <> void GrfActionHandler<0x02>::Reserve(ByteReader &) { }
template <> void GrfActionHandler<0x02>::Activation(ByteReader &buf) { NewSpriteGroup(buf); }
