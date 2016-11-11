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
enum SlXvFeatureIndex {
	XSLFI_NULL                          = 0,      ///< Unused value, to indicate that no extended feature test is in use
	XSLFI_DEPARTURE_BOARDS,                       ///< Departure boards patch, in ticks mode
	XSLFI_TIMETABLES_START_TICKS,                 ///< Timetable start time is in ticks, instead of days (from departure boards patch)

	XSLFI_SIZE,                                   ///< Total count of features, including null feature
};

extern uint16 _sl_xv_feature_versions[XSLFI_SIZE];

/**
 * Operator to use when combining traditional savegame number test with an extended feature version test
 */
enum SlXvFeatureTestOperator {
	XSLFTO_OR                           = 0,      ///< Test if traditional savegame version is in bounds OR extended feature is in version bounds
	XSLFTO_AND                                    ///< Test if traditional savegame version is in bounds AND extended feature is in version bounds
};

/**
 * Structure to describe an extended feature version test, and how it combines with a traditional savegame version test
 */
struct SlXvFeatureTest {
	private:
	uint16 min_version;
	uint16 max_version;
	SlXvFeatureIndex feature;
	SlXvFeatureTestOperator op;

	public:
	SlXvFeatureTest()
			: min_version(0), max_version(0), feature(XSLFI_NULL), op(XSLFTO_OR) { }

	SlXvFeatureTest(SlXvFeatureTestOperator op_, SlXvFeatureIndex feature_, uint16 min_version_ = 1, uint16 max_version_ = 0xFFFF)
			: min_version(min_version_), max_version(max_version_), feature(feature_), op(op_) { }

	bool IsFeaturePresent(uint16 savegame_version, uint16 savegame_version_from, uint16 savegame_version_to) const;
};

bool SlXvIsFeaturePresent(SlXvFeatureIndex feature, uint16 min_version = 1, uint16 max_version = 0xFFFF);

/**
 * Returns true if @p feature is missing (i.e. has a version of 0)
 */
inline bool SlXvIsFeatureMissing(SlXvFeatureIndex feature)
{
	return !SlXvIsFeaturePresent(feature);
}

const char *SlXvGetFeatureName(SlXvFeatureIndex feature);

/**
 * sub chunk flags, this is saved as-is
 * (XSCF_EXTRA_DATA_PRESENT and XSCF_CHUNK_ID_LIST_PRESENT must only be set by the save code, and read by the load code)
 */
enum SlxiSubChunkFlags {
	XSCF_NULL                     = 0,       ///< zero value
	XSCF_IGNORABLE_UNKNOWN        = 1 << 0,  ///< the loader is free to ignore this without aborting the load if it doesn't know what it is at all
	XSCF_IGNORABLE_VERSION        = 1 << 1,  ///< the loader is free to ignore this without aborting the load if the version is greater than the maximum that can be loaded
	XSCF_EXTRA_DATA_PRESENT       = 1 << 2,  ///< extra data field is present, extra data in some sub-chunk/feature specific format, not used for anything yet
	XSCF_CHUNK_ID_LIST_PRESENT    = 1 << 3,  ///< chunk ID list field is present, list of chunks which this sub-chunk/feature adds to the save game, this can be used to discard the chunks if the feature is unknown

	XSCF_IGNORABLE_ALL            = XSCF_IGNORABLE_UNKNOWN | XSCF_IGNORABLE_VERSION, ///< all "ignorable" flags
};
DECLARE_ENUM_AS_BIT_SET(SlxiSubChunkFlags)

struct SlxiSubChunkInfo;

typedef uint32 SlxiSubChunkSaveProc(const SlxiSubChunkInfo *info, bool dry_run);  ///< sub chunk save procedure type, must return length and write no data when dry_run is true
typedef void SlxiSubChunkLoadProc(const SlxiSubChunkInfo *info, uint32 length);   ///< sub chunk load procedure, must consume length bytes

/** Handlers and description of chunk. */
struct SlxiSubChunkInfo {
	SlXvFeatureIndex index;                       ///< feature index, this is saved
	SlxiSubChunkFlags flags;                      ///< flags, this is saved
	uint16 save_version;                          ///< version to save
	uint16 max_version;                           ///< maximum version to accept on load
	const char *name;                             ///< feature name, this *IS* saved, so must be globally unique
	SlxiSubChunkSaveProc *save_proc;              ///< save procedure of the sub chunk, this may be NULL in which case no extra chunk data is saved
	SlxiSubChunkLoadProc *load_proc;              ///< load procedure of the sub chunk, this may be NULL in which case the extra chunk data must be missing or of 0 length
	const char *chunk_list;                       ///< this is a list of chunks that this feature uses, which should be written to the savegame, this must be a comma-seperated list of 4-character IDs, with no spaces, or NULL
};

void SlXvResetState();

void SlXvSetCurrentState();

void SlXvCheckSpecialSavegameVersions();

bool SlXvIsChunkDiscardable(uint32 id);

#endif /* EXTENDED_VER_SL_H */
