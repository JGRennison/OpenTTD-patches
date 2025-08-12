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

#include "../newgrf.h"
#include "../spriteloader/spriteloader.hpp"

#include "../3rdparty/cpp-btree/btree_map.h"
#include <vector>

/** Base GRF ID for OpenTTD's base graphics GRFs. */
static const uint32_t OPENTTD_GRAPHICS_BASE_GRF_ID = std::byteswap<uint32_t>(0xFF4F5400);

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

	/** Clear temporary data before processing the next file in the current loading stage */
	void ClearDataForNextFile();

	/**
	 * Records new spritesets.
	 * @param feature GrfSpecFeature the set is defined for.
	 * @param first_sprite SpriteID of the first sprite in the set.
	 * @param first_set First spriteset to define.
	 * @param numsets Number of sets to define.
	 * @param numents Number of sprites per set to define.
	 */
	void AddSpriteSets(uint8_t feature, SpriteID first_sprite, uint first_set, uint numsets, uint numents)
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
	bool HasValidSpriteSets(uint8_t feature) const
	{
		assert(feature < GSF_END);
		return !this->spritesets[feature].empty();
	}

	struct SpriteSetInfo {
	private:
		SpriteSet info;

	public:
		SpriteSetInfo() : info({ 0, UINT_MAX }) {}
		SpriteSetInfo(SpriteSet info) : info(info) {}

		/**
		 * Check whether this set is defined.
		 * @return true if the set is valid.
		 * @note Spritesets with zero sprites are valid to allow callback-failures.
		 */
		bool IsValid() const { return this->info.num_sprites != UINT_MAX; }

		/**
		 * Returns the first sprite of this spriteset.
		 * @return First sprite of the set.
		 */
		SpriteID GetSprite() const
		{
			assert(this->IsValid());
			return this->info.sprite;
		}

		/**
		 * Returns the number of sprites in this spriteset
		 * @return Number of sprites in the set.
		 */
		uint GetNumEnts() const
		{
			assert(this->IsValid());
			return this->info.num_sprites;
		}
	};

	/**
	 * Get information for a specific set is defined.
	 * @param feature GrfSpecFeature to check.
	 * @param set Set to check.
	 * @return Sprite set information.
	 * @note Spritesets with zero sprites are valid to allow callback-failures.
	 */
	SpriteSetInfo GetSpriteSetInfo(uint8_t feature, uint set) const
	{
		assert(feature < GSF_END);
		auto iter = this->spritesets[feature].find(set);
		return iter != this->spritesets[feature].end() ? SpriteSetInfo(iter->second) : SpriteSetInfo();
	}
};

using SpriteSetInfo = GrfProcessingState::SpriteSetInfo;

extern GrfProcessingState _cur;

#endif /* NEWGRF_INTERNAL_H */
