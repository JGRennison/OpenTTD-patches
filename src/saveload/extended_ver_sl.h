/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file extended_ver_sl.h Functions/types related to handling save/load extended version info. */

#ifndef EXTENDED_VER_SL_H
#define EXTENDED_VER_SL_H

#include "../core/bitmath_func.hpp"

#include <vector>

/**
 * List of extended features, each feature has its own (16 bit) version
 */
enum ExtendedSaveLoadFeatureIndex {
	XSLFI_NULL                          = 0,      ///< Unused value, to indicate that no extended feature test is in use

	XSLFI_SIZE,                                   ///< Total count of features, including null feature
};

extern uint16 _sl_xv_feature_versions[XSLFI_SIZE];

/**
 * Operator to use when combining traditional savegame number test with an extended feature version test
 */
enum ExtendedSaveLoadFeatureTestOperator {
	XSLFTO_OR                           = 0,      ///< Test if traditional savegame version is in bounds OR extended feature is in version bounds
	XSLFTO_AND                                    ///< Test if traditional savegame version is in bounds AND extended feature is in version bounds
};

/**
 * Structure to describe an extended feature version test, and how it combines with a traditional savegame version test
 */
struct ExtendedSaveLoadFeatureTest {
	private:
	uint64 value;

	public:
	ExtendedSaveLoadFeatureTest()
			: value(0) { }

	ExtendedSaveLoadFeatureTest(ExtendedSaveLoadFeatureTestOperator op, ExtendedSaveLoadFeatureIndex feature, uint16 min_version = 1, uint16 max_version = 0xFFFF)
	{
		this->value = 0;
		SB(this->value, 0, 16, feature);
		SB(this->value, 16, 16, min_version);
		SB(this->value, 32, 16, max_version);
		SB(this->value, 48, 16, op);
	}

	bool IsFeaturePresent(uint16 savegame_version, uint16 savegame_version_from, uint16 savegame_version_to) const;
};

bool SlXvIsFeaturePresent(ExtendedSaveLoadFeatureIndex feature, uint16 min_version = 1, uint16 max_version = 0xFFFF);

/**
 * Returns true if @p feature is missing (i.e. has a version of 0)
 */
inline bool SlXvIsFeatureMissing(ExtendedSaveLoadFeatureIndex feature)
{
	return !SlXvIsFeaturePresent(feature);
}

void SlXvResetState();

void SlXvSetCurrentState();

void SlXvCheckSpecialSavegameVersions();

bool SlXvIsChunkDiscardable(uint32 id);

#endif /* EXTENDED_VER_SL_H */
