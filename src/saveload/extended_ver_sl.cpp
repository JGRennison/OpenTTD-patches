/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file extended_ver_sl.cpp Functions related to handling save/load extended version info.
 *
 * Known extended features are stored in _sl_xv_feature_versions, features which are currently enabled/in use and their versions are stored in the savegame.
 * On load, the list of features and their versions are loaded from the savegame. If the savegame contains a feature which is either unknown, or has too high a version,
 * loading can be either aborted, or the feature can be ignored if the the feature flags in the savegame indicate that it can be ignored. The savegame may also list any additional
 * chunk IDs which are associated with an extended feature, these can be discarded if the feature is discarded.
 * This information is stored in the SLXI chunk, the contents of which has the following format:
 *
 * uint32                               chunk version
 * uint32                               chunk flags
 * uint32                               number of sub chunks/features
 *     For each of N sub chunk/feature:
 *     uint32                           feature flags (SlxiSubChunkFlags)
 *     uint16                           feature version
 *     SLE_STR                          feature name
 *     uint32*                          extra data length [only present iff feature flags & XSCF_EXTRA_DATA_PRESENT]
 *         N bytes                      extra data
 *     uint32*                          chunk ID list count [only present iff feature flags & XSCF_CHUNK_ID_LIST_PRESENT]
 *         N x uint32                   chunk ID list
 */

#include "../stdafx.h"
#include "../debug.h"
#include "saveload.h"
#include "extended_ver_sl.h"

#include <vector>

#include "../safeguards.h"

uint16 _sl_xv_feature_versions[XSLFI_SIZE];                 ///< array of all known feature types and their current versions
bool _sl_is_ext_version;                                    ///< is this an extended savegame version, with more info in the SLXI chunk?
bool _sl_is_faked_ext;                                      ///< is this a faked extended savegame version, with no SLXI chunk?
std::vector<uint32> _sl_xv_discardable_chunk_ids;           ///< list of chunks IDs which we can discard if no chunk loader exists

static const uint32 _sl_xv_slxi_chunk_version = 0;          ///< current version os SLXI chunk

const SlxiSubChunkInfo _sl_xv_sub_chunk_infos[] = {
	{ XSLFI_TRACE_RESTRICT,         XSCF_NULL,                6,   6, "tracerestrict",             NULL, NULL, "TRRM,TRRP" },
	{ XSLFI_NULL, XSCF_NULL, 0, 0, NULL, NULL, NULL, NULL },// This is the end marker
};

/**
 * Extended save/load feature test
 *
 * First performs a tradional check on the provided @p savegame_version against @p savegame_version_from and @p savegame_version_to.
 * Then, if the feature set in the constructor is not XSLFI_NULL, also check than the feature version is inclusively bounded by @p min_version and @p max_version,
 * and return the combination of the two tests using the operator defined in the constructor.
 * Otherwise just returns the result of the savegame version test
 */
bool SlXvFeatureTest::IsFeaturePresent(uint16 savegame_version, uint16 savegame_version_from, uint16 savegame_version_to) const
{
	bool savegame_version_ok = savegame_version >= savegame_version_from && savegame_version <= savegame_version_to;

	if (this->feature == XSLFI_NULL) return savegame_version_ok;

	bool feature_ok = SlXvIsFeaturePresent(this->feature, this->min_version, this->max_version);

	switch (op) {
		case XSLFTO_OR:
			return savegame_version_ok || feature_ok;

		case XSLFTO_AND:
			return savegame_version_ok && feature_ok;

		default:
			NOT_REACHED();
			return false;
	}
}

/**
 * Returns true if @p feature is present and has a version inclusively bounded by @p min_version and @p max_version
 */
bool SlXvIsFeaturePresent(SlXvFeatureIndex feature, uint16 min_version, uint16 max_version)
{
	assert(feature < XSLFI_SIZE);
	return _sl_xv_feature_versions[feature] >= min_version && _sl_xv_feature_versions[feature] <= max_version;
}

/**
 * Returns true if @p feature is present and has a version inclusively bounded by @p min_version and @p max_version
 */
const char *SlXvGetFeatureName(SlXvFeatureIndex feature)
{
	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		if (info->index == feature) {
			return info->name;
		}
	}
	return "(unknown feature)";
}

/**
 * Resets all extended feature versions to 0
 */
void SlXvResetState()
{
	_sl_is_ext_version = false;
	_sl_is_faked_ext = false;
	_sl_xv_discardable_chunk_ids.clear();
	memset(_sl_xv_feature_versions, 0, sizeof(_sl_xv_feature_versions));
}

/**
 * Resets all extended feature versions to their currently enabled versions, i.e. versions suitable for saving
 */
void SlXvSetCurrentState()
{
	SlXvResetState();
	_sl_is_ext_version = true;

	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		_sl_xv_feature_versions[info->index] = info->save_version;
	}
}

/**
 * Check for "special" savegame versions (i.e. known patchpacks) and set correct savegame version, settings, etc.
 */
void SlXvCheckSpecialSavegameVersions()
{
	// Checks for special savegame versions go here
	extern uint16 _sl_version;

	if (_sl_version == 2000) {
		DEBUG(sl, 1, "Loading a trace restrict patch savegame version %d as version 194", _sl_version);
		_sl_version = 194;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_TRACE_RESTRICT] = 1;
	}
	if (_sl_version == 2001) {
		DEBUG(sl, 1, "Loading a trace restrict patch savegame version %d as version 195", _sl_version);
		_sl_version = 195;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_TRACE_RESTRICT] = 6;
	}
}

/**
 * Return true if this chunk has been marked as discardable
 */
bool SlXvIsChunkDiscardable(uint32 id)
{
	for(size_t i = 0; i < _sl_xv_discardable_chunk_ids.size(); i++) {
		if (_sl_xv_discardable_chunk_ids[i] == id) {
			return true;
		}
	}
	return false;
}

/**
 * Writes a chunk ID list string to the savegame, returns the number of chunks written
 * In dry run mode, only returns the number of chunk which would have been written
 */
static uint32 WriteChunkIdList(const char *chunk_list, bool dry_run)
{
	unsigned int chunk_count = 0;  // number of chunks output
	unsigned int id_offset = 0;    // how far are we into the ID
	for (; *chunk_list != 0; chunk_list++) {
		if (id_offset == 4) {
			assert(*chunk_list == ',');
			id_offset = 0;
		} else {
			if (!dry_run) {
				SlWriteByte(*chunk_list);
			}
			if (id_offset == 3) {
				chunk_count++;
			}
			id_offset++;
		}
	}
	assert(id_offset == 4);
	return chunk_count;
}

static void Save_SLXI()
{
	SlXvSetCurrentState();

	static const SaveLoad _xlsi_sub_chunk_desc[] = {
		SLE_STR(SlxiSubChunkInfo, name,           SLE_STR, 0),
		SLE_END()
	};

	// calculate lengths
	uint32 item_count = 0;
	uint32 length = 12;
	std::vector<uint32> extra_data_lengths;
	std::vector<uint32> chunk_counts;
	extra_data_lengths.resize(XSLFI_SIZE);
	chunk_counts.resize(XSLFI_SIZE);
	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		if (_sl_xv_feature_versions[info->index] > 0) {
			item_count++;
			length += 6;
			length += SlCalcObjLength(info, _xlsi_sub_chunk_desc);
			if (info->save_proc) {
				uint32 extra_data_length = info->save_proc(info, true);
				if (extra_data_length) {
					extra_data_lengths[info->index] = extra_data_length;
					length += 4 + extra_data_length;
				}
			}
			if (info->chunk_list) {
				uint32 chunk_count = WriteChunkIdList(info->chunk_list, true);
				if (chunk_count) {
					chunk_counts[info->index] = chunk_count;
					length += 4 * (1 + chunk_count);
				}
			}
		}
	}

	// write header
	SlSetLength(length);
	SlWriteUint32(_sl_xv_slxi_chunk_version);               // chunk version
	SlWriteUint32(0);                                       // flags
	SlWriteUint32(item_count);                              // item count

	// write data
	info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		uint16 save_version = _sl_xv_feature_versions[info->index];
		if (save_version > 0) {
			SlxiSubChunkFlags flags = info->flags;
			assert(!(flags & (XSCF_EXTRA_DATA_PRESENT | XSCF_CHUNK_ID_LIST_PRESENT)));
			uint32 extra_data_length = extra_data_lengths[info->index];
			uint32 chunk_count = chunk_counts[info->index];
			if (extra_data_length > 0) flags |= XSCF_EXTRA_DATA_PRESENT;
			if (chunk_count > 0) flags |= XSCF_CHUNK_ID_LIST_PRESENT;
			SlWriteUint32(flags);
			SlWriteUint16(save_version);
			SlObject(const_cast<SlxiSubChunkInfo *>(info), _xlsi_sub_chunk_desc);

			if (extra_data_length > 0) {
				SlWriteUint32(extra_data_length);
				size_t written = SlGetBytesWritten();
				info->save_proc(info, false);
				assert(SlGetBytesWritten() == written + extra_data_length);
			}
			if (chunk_count > 0) {
				SlWriteUint32(chunk_count);
				size_t written = SlGetBytesWritten();
				WriteChunkIdList(info->chunk_list, false);
				assert(SlGetBytesWritten() == written + (chunk_count * 4));
			}
		}
	}
}

static void Load_SLXI()
{
	if (_sl_is_faked_ext || !_sl_is_ext_version) {
		SlErrorCorrupt("SXLI chunk is unexpectedly present");
	}

	SlXvResetState();
	_sl_is_ext_version = true;

	uint32 version = SlReadUint32();
	if (version > _sl_xv_slxi_chunk_version) SlErrorCorruptFmt("SLXI chunk: version: %u is too new (expected max: %u)", version, _sl_xv_slxi_chunk_version);

	uint32 chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("SLXI chunk: unknown chunk header flags: 0x%X", chunk_flags);

	char name_buffer[256];
	const SaveLoadGlobVarList xlsi_sub_chunk_name_desc[] = {
		SLEG_STR(name_buffer, SLE_STRB),
		SLEG_END()
	};

	uint32 item_count = SlReadUint32();
	for (uint32 i = 0; i < item_count; i++) {
		SlxiSubChunkFlags flags = static_cast<SlxiSubChunkFlags>(SlReadUint32());
		uint16 version = SlReadUint16();
		SlGlobList(xlsi_sub_chunk_name_desc);

		// linearly scan through feature list until found name match
		bool found = false;
		const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
		for (; info->index != XSLFI_NULL; ++info) {
			if (strcmp(name_buffer, info->name) == 0) {
				found = true;
				break;
			}
		}

		bool discard_chunks = false;
		if (found) {
			if (version > info->max_version) {
				if (flags & XSCF_IGNORABLE_VERSION) {
					// version too large but carry on regardless
					discard_chunks = true;
					if (flags & XSCF_EXTRA_DATA_PRESENT) {
						SlSkipBytes(SlReadUint32()); // skip extra data field
					}
					DEBUG(sl, 1, "SLXI chunk: too large version for feature: '%s', version: %d, max version: %d, ignoring", name_buffer, version, info->max_version);
				} else {
					SlErrorCorruptFmt("SLXI chunk: too large version for feature: '%s', version: %d, max version: %d", name_buffer, version, info->max_version);
				}
			} else {
				// success path :)

				_sl_xv_feature_versions[info->index] = version;
				if (flags & XSCF_EXTRA_DATA_PRESENT) {
					uint32 extra_data_size = SlReadUint32();
					if (extra_data_size) {
						if (info->load_proc) {
							size_t read = SlGetBytesRead();
							info->load_proc(info, extra_data_size);
							if (SlGetBytesRead() != read + extra_data_size) {
								SlErrorCorruptFmt("SLXI chunk: feature: %s, version: %d, extra data length mismatch", name_buffer, version);
							}
						} else {
							SlErrorCorruptFmt("SLXI chunk: feature: %s, version: %d, unexpectedly includes extra data", name_buffer, version);
						}
					}
				}

				DEBUG(sl, 1, "SLXI chunk: found known feature: '%s', version: %d, max version: %d", name_buffer, version, info->max_version);
			}
		} else {
			if (flags & XSCF_IGNORABLE_UNKNOWN) {
				// not found but carry on regardless
				discard_chunks = true;
				if (flags & XSCF_EXTRA_DATA_PRESENT) {
					SlSkipBytes(SlReadUint32()); // skip extra data field
				}
				DEBUG(sl, 1, "SLXI chunk: unknown feature: '%s', version: %d, ignoring", name_buffer, version);
			} else {
				SlErrorCorruptFmt("SLXI chunk: unknown feature: %s, version: %d", name_buffer, version);
			}
		}

		// at this point the extra data field should have been consumed
		// handle chunk ID list field
		if (flags & XSCF_CHUNK_ID_LIST_PRESENT) {
			uint32 chunk_count = SlReadUint32();
			for (uint32 j = 0; j < chunk_count; j++) {
				uint32 chunk_id = SlReadUint32();
				if (discard_chunks) {
					_sl_xv_discardable_chunk_ids.push_back(chunk_id);
					DEBUG(sl, 2, "SLXI chunk: unknown feature: '%s', discarding chunk: %c%c%c%c", name_buffer, chunk_id >> 24, chunk_id >> 16, chunk_id >> 8, chunk_id);
				}
			}
		}
	}
}

extern const ChunkHandler _version_ext_chunk_handlers[] = {
	{ 'SLXI', Save_SLXI, Load_SLXI, NULL, Load_SLXI, CH_RIFF | CH_LAST},
};
