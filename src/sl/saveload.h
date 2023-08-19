/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file saveload.h Functions/types related to saving and loading games. */

#ifndef SL_SAVELOAD_H
#define SL_SAVELOAD_H

#include "saveload_types.h"
#include "../fileio_type.h"
#include "../fios.h"
#include "../strings_type.h"
#include "../scope.h"

#include <stdarg.h>
#include <vector>
#include <string>

/** Save or load result codes. */
enum SaveOrLoadResult {
	SL_OK     = 0, ///< completed successfully
	SL_ERROR  = 1, ///< error that was caught before internal structures were modified
	SL_REINIT = 2, ///< error that was caught in the middle of updating game state, need to clear it. (can only happen during load)
};

/** Deals with the type of the savegame, independent of extension */
struct FileToSaveLoad {
	SaveLoadOperation file_op;       ///< File operation to perform.
	DetailedFileType detail_ftype;   ///< Concrete file type (PNG, BMP, old save, etc).
	AbstractFileType abstract_ftype; ///< Abstract type of file (scenario, heightmap, etc).
	std::string name;                ///< Name of the file.
	std::string title;               ///< Internal name of the game.

	void SetMode(FiosType ft);
	void SetMode(SaveLoadOperation fop, AbstractFileType aft, DetailedFileType dft);
	void Set(const FiosItem &item);
};

/** Types of save games. */
enum SavegameType {
	SGT_TTD,    ///< TTD  savegame (can be detected incorrectly)
	SGT_TTDP1,  ///< TTDP savegame ( -//- ) (data at NW border)
	SGT_TTDP2,  ///< TTDP savegame in new format (data at SE border)
	SGT_OTTD,   ///< OTTD savegame
	SGT_TTO,    ///< TTO savegame
	SGT_INVALID = 0xFF, ///< broken savegame (used internally)
};

enum SaveModeFlags : byte {
	SMF_NONE             = 0,
	SMF_NET_SERVER       = 1 << 0, ///< Network server save
	SMF_ZSTD_OK          = 1 << 1, ///< Zstd OK
	SMF_SCENARIO         = 1 << 2, ///< Scenario save
};
DECLARE_ENUM_AS_BIT_SET(SaveModeFlags);

extern FileToSaveLoad _file_to_saveload;

void GenerateDefaultSaveName(char *buf, const char *last);
void SetSaveLoadError(StringID str);
const char *GetSaveLoadErrorString();
SaveOrLoadResult SaveOrLoad(const std::string &filename, SaveLoadOperation fop, DetailedFileType dft, Subdirectory sb, bool threaded = true, SaveModeFlags flags = SMF_NONE);
void WaitTillSaved();
void ProcessAsyncSaveFinish();
void DoExitSave();

void DoAutoOrNetsave(FiosNumberedSaveName &counter, bool threaded, FiosNumberedSaveName *lt_counter = nullptr);

SaveOrLoadResult SaveWithFilter(struct SaveFilter *writer, bool threaded, SaveModeFlags flags);
SaveOrLoadResult LoadWithFilter(struct LoadFilter *reader);
bool IsNetworkServerSave();
bool IsScenarioSave();

typedef void ChunkSaveLoadProc();
typedef void AutolengthProc(void *arg);

void SlUnreachablePlaceholder();

enum ChunkSaveLoadSpecialOp {
	CSLSO_PRE_LOAD,
	CSLSO_PRE_LOADCHECK,
	CSLSO_SHOULD_SAVE_CHUNK,
};
enum ChunkSaveLoadSpecialOpResult {
	CSLSOR_NONE,
	CSLSOR_LOAD_CHUNK_CONSUMED,
	CSLSOR_DONT_SAVE_CHUNK,
};
typedef ChunkSaveLoadSpecialOpResult ChunkSaveLoadSpecialProc(uint32, ChunkSaveLoadSpecialOp);

/** Type of a chunk. */
enum ChunkType {
	CH_RIFF = 0,
	CH_ARRAY = 1,
	CH_SPARSE_ARRAY = 2,
	CH_EXT_HDR      = 15, ///< Extended chunk header

	CH_UPSTREAM_SAVE = 0x80,
};

/** Handlers and description of chunk. */
struct ChunkHandler {
	uint32 id;                          ///< Unique ID (4 letters).
	ChunkSaveLoadProc *save_proc;       ///< Save procedure of the chunk.
	ChunkSaveLoadProc *load_proc;       ///< Load procedure of the chunk.
	ChunkSaveLoadProc *ptrs_proc;       ///< Manipulate pointers in the chunk.
	ChunkSaveLoadProc *load_check_proc; ///< Load procedure for game preview.
	ChunkType type;                     ///< Type of the chunk. @see ChunkType
	ChunkSaveLoadSpecialProc *special_proc = nullptr;
};

template <typename F>
void SlExecWithSlVersion(SaveLoadVersion use_version, F proc)
{
	extern SaveLoadVersion _sl_version;
	SaveLoadVersion old_ver = _sl_version;
	_sl_version = use_version;
	auto guard = scope_guard([&]() {
		_sl_version = old_ver;
	});
	proc();
}

namespace upstream_sl {
	template <uint32 id, typename F>
	ChunkHandler MakeUpstreamChunkHandler()
	{
		extern void SlLoadChunkByID(uint32);
		extern void SlLoadCheckChunkByID(uint32);
		extern void SlFixPointerChunkByID(uint32);

		ChunkHandler ch = {
			id,
			nullptr,
			SlUnreachablePlaceholder,
			[]() {
				SlExecWithSlVersion(F::GetLoadVersion(), []() {
					SlFixPointerChunkByID(id);
				});
			},
			SlUnreachablePlaceholder,
			CH_UPSTREAM_SAVE
		};
		ch.special_proc = [](uint32 chunk_id, ChunkSaveLoadSpecialOp op) -> ChunkSaveLoadSpecialOpResult {
			assert(id == chunk_id);
			switch (op) {
				case CSLSO_PRE_LOAD:
					SlExecWithSlVersion(F::GetLoadVersion(), []() {
						SlLoadChunkByID(id);
					});
					return CSLSOR_LOAD_CHUNK_CONSUMED;
				case CSLSO_PRE_LOADCHECK:
					SlExecWithSlVersion(F::GetLoadVersion(), []() {
						SlLoadCheckChunkByID(id);
					});
					return CSLSOR_LOAD_CHUNK_CONSUMED;
				default:
					return CSLSOR_NONE;
			}
		};
		return ch;
	}
}

using upstream_sl::MakeUpstreamChunkHandler;

struct NullStruct {
	byte null;
};

/** A table of ChunkHandler entries. */
using ChunkHandlerTable = span<const ChunkHandler>;

/** Type of reference (#SLE_REF, #SLE_CONDREF). */
enum SLRefType {
	REF_ORDER            =  0,	///< Load/save a reference to an order.
	REF_VEHICLE          =  1,	///< Load/save a reference to a vehicle.
	REF_STATION          =  2,	///< Load/save a reference to a station.
	REF_TOWN             =  3,	///< Load/save a reference to a town.
	REF_VEHICLE_OLD      =  4,	///< Load/save an old-style reference to a vehicle (for pre-4.4 savegames).
	REF_ROADSTOPS        =  5,	///< Load/save a reference to a bus/truck stop.
	REF_ENGINE_RENEWS    =  6,	///< Load/save a reference to an engine renewal (autoreplace).
	REF_CARGO_PACKET     =  7,	///< Load/save a reference to a cargo packet.
	REF_ORDERLIST        =  8,	///< Load/save a reference to an orderlist.
	REF_STORAGE          =  9,	///< Load/save a reference to a persistent storage.
	REF_LINK_GRAPH       = 10,	///< Load/save a reference to a link graph.
	REF_LINK_GRAPH_JOB   = 11,	///< Load/save a reference to a link graph job.
	REF_TEMPLATE_VEHICLE = 12,	///< Load/save a reference to a template vehicle
};

/** Flags for chunk extended headers */
enum SaveLoadChunkExtHeaderFlags {
	SLCEHF_BIG_RIFF           = 1 << 0,  ///< This block uses a 60-bit RIFF chunk size
};
DECLARE_ENUM_AS_BIT_SET(SaveLoadChunkExtHeaderFlags)

/**
 * Storage of simple variables, references (pointers), and arrays.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 * @note In general, it is better to use one of the SLE_* macros below.
 */
#define SLE_GENERAL_X(cmd, base, variable, type, length, from, to, extver) SaveLoad {false, cmd, type, length, from, to, (void*)cpp_offsetof(base, variable), cpp_sizeof(base, variable), extver}
#define SLE_GENERAL(cmd, base, variable, type, length, from, to) SLE_GENERAL_X(cmd, base, variable, type, length, from, to, SlXvFeatureTest())

/**
 * Storage of a variable in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDVAR_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_VAR, base, variable, type, 0, from, to, extver)
#define SLE_CONDVAR(base, variable, type, from, to) SLE_CONDVAR_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a reference in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Type of the reference, a value from #SLRefType.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDREF_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_REF, base, variable, type, 0, from, to, extver)
#define SLE_CONDREF(base, variable, type, from, to) SLE_CONDREF_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a fixed-size array of #SL_VAR elements in some savegame versions.
 * @param base     Name of the class or struct containing the array.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the array.
 * @param to       Last savegame version that has the array.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDARR_X(base, variable, type, length, from, to, extver) SLE_GENERAL_X(SL_ARR, base, variable, type, length, from, to, extver)
#define SLE_CONDARR(base, variable, type, length, from, to) SLE_CONDARR_X(base, variable, type, length, from, to, SlXvFeatureTest())

/**
 * Storage of a string in some savegame versions.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDSTR_X(base, variable, type, length, from, to, extver) SLE_GENERAL_X(SL_STR, base, variable, type, length, from, to, extver)
#define SLE_CONDSTR(base, variable, type, length, from, to) SLE_CONDSTR_X(base, variable, type, length, from, to, SlXvFeatureTest())

/**
 * Storage of a \c std::string in some savegame versions.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDSSTR_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_STDSTR, base, variable, type, 0, from, to, extver)
#define SLE_CONDSSTR(base, variable, type, from, to) SLE_GENERAL(SL_STDSTR, base, variable, type, 0, from, to)

/**
 * Storage of a list of #SL_REF elements in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDREFLIST_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_REFLIST, base, variable, type, 0, from, to, extver)
#define SLE_CONDREFLIST(base, variable, type, from, to) SLE_CONDREFLIST_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a ring in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDPTRRING_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_PTRRING, base, variable, type, 0, from, to, extver)
#define SLE_CONDPTRRING(base, variable, type, from, to) SLE_CONDPTRRING_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a vector in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDVEC_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_VEC, base, variable, type, 0, from, to, extver)
#define SLE_CONDVEC(base, variable, type, from, to) SLE_CONDVEC_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a variable vector in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDVARVEC_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_VARVEC, base, variable, type, 0, from, to, extver)
#define SLE_CONDVARVEC(base, variable, type, from, to) SLE_CONDVARVEC_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a ring of #SL_VAR elements in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLE_CONDRING_X(base, variable, type, from, to, extver) SLE_GENERAL_X(SL_RING, base, variable, type, 0, from, to, extver)
#define SLE_CONDRING(base, variable, type, from, to) SLE_CONDRING_X(base, variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a variable in every version of a savegame.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_VAR(base, variable, type) SLE_CONDVAR(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a reference in every version of a savegame.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Type of the reference, a value from #SLRefType.
 */
#define SLE_REF(base, variable, type) SLE_CONDREF(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of fixed-size array of #SL_VAR elements in every version of a savegame.
 * @param base     Name of the class or struct containing the array.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 */
#define SLE_ARR(base, variable, type, length) SLE_CONDARR(base, variable, type, length, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a string in every savegame version.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 */
#define SLE_STR(base, variable, type, length) SLE_CONDSTR(base, variable, type, length, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a \c std::string in every savegame version.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_SSTR(base, variable, type) SLE_CONDSSTR(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a list of #SL_REF elements in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_REFLIST(base, variable, type) SLE_CONDREFLIST(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a ring in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_PTRRING(base, variable, type) SLE_CONDPTRRING(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a vector in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_VEC(base, variable, type) SLE_CONDVEC(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a variable vector in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_VARVEC(base, variable, type) SLE_CONDVARVEC(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Empty space in every savegame version.
 * @param length Length of the empty space.
 */
#define SLE_NULL(length) SLE_CONDNULL(length, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Empty space in some savegame versions.
 * @param length Length of the empty space.
 * @param from   First savegame version that has the empty space.
 * @param to     Last savegame version that has the empty space.
 * @param extver SlXvFeatureTest to test (along with from and to) which savegames have empty space
 */
#define SLE_CONDNULL_X(length, from, to, extver) SLE_CONDARR_X(NullStruct, null, SLE_FILE_U8 | SLE_VAR_NULL, length, from, to, extver)
#define SLE_CONDNULL(length, from, to) SLE_CONDNULL_X(length, from, to, SlXvFeatureTest())

/** Translate values ingame to different values in the savegame and vv. */
#define SLE_WRITEBYTE(base, variable) SLE_GENERAL(SL_WRITEBYTE, base, variable, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION)

#define SLE_VEH_INCLUDE() {false, SL_VEH_INCLUDE, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION, nullptr, 0, SlXvFeatureTest()}
#define SLE_ST_INCLUDE() {false, SL_ST_INCLUDE, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION, nullptr, 0, SlXvFeatureTest()}

/**
 * Storage of global simple variables, references (pointers), and arrays.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 * @note In general, it is better to use one of the SLEG_* macros below.
 */
#define SLEG_GENERAL_X(cmd, variable, type, length, from, to, extver) SaveLoad {true, cmd, type, length, from, to, (void*)&variable, sizeof(variable), extver}
#define SLEG_GENERAL(cmd, variable, type, length, from, to) SLEG_GENERAL_X(cmd, variable, type, length, from, to, SlXvFeatureTest())

/**
 * Storage of a global variable in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDVAR_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_VAR, variable, type, 0, from, to, extver)
#define SLEG_CONDVAR(variable, type, from, to) SLEG_CONDVAR_X(variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a global reference in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDREF_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_REF, variable, type, 0, from, to, extver)
#define SLEG_CONDREF(variable, type, from, to) SLEG_CONDREF_X(variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a global fixed-size array of #SL_VAR elements in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the array.
 * @param to       Last savegame version that has the array.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDARR_X(variable, type, length, from, to, extver) SLEG_GENERAL_X(SL_ARR, variable, type, length, from, to, extver)
#define SLEG_CONDARR(variable, type, length, from, to) SLEG_CONDARR_X(variable, type, length, from, to, SlXvFeatureTest())

/**
 * Storage of a global string in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDSTR_X(variable, type, length, from, to, extver) SLEG_GENERAL_X(SL_STR, variable, type, length, from, to, extver)
#define SLEG_CONDSTR(variable, type, length, from, to) SLEG_CONDSTR_X(variable, type, length, from, to, SlXvFeatureTest())

/**
 * Storage of a global \c std::string in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLEG_CONDSSTR_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_STDSTR, variable, type, 0, from, to, extver)
#define SLEG_CONDSSTR(variable, type, from, to) SLEG_GENERAL(SL_STDSTR, variable, type, 0, from, to)

/**
 * Storage of a global reference list in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDREFLIST_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_REFLIST, variable, type, 0, from, to, extver)
#define SLEG_CONDREFLIST(variable, type, from, to) SLEG_CONDREFLIST_X(variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a global ring in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDPTRRING_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_PTRRING, variable, type, 0, from, to, extver)
#define SLEG_CONDPTRRING(variable, type, from, to) SLEG_CONDPTRRING_X(variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a global vector in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDVEC_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_VEC, variable, type, 0, from, to, extver)
#define SLEG_CONDVEC(variable, type, from, to) SLEG_CONDVEC_X(variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a variable vector in some savegame versions.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 * @param extver   SlXvFeatureTest to test (along with from and to) which savegames have the field
 */
#define SLEG_CONDVARVEC_X(variable, type, from, to, extver) SLEG_GENERAL_X(SL_VARVEC, variable, type, 0, from, to, extver)
#define SLEG_CONDVARVEC(variable, type, from, to) SLEG_CONDVARVEC_X(variable, type, from, to, SlXvFeatureTest())

/**
 * Storage of a global variable in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_VAR(variable, type) SLEG_CONDVAR(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global reference in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_REF(variable, type) SLEG_CONDREF(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global fixed-size array of #SL_VAR elements in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_ARR(variable, type) SLEG_CONDARR(variable, type, lengthof(variable), SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global string in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_STR(variable, type) SLEG_CONDSTR(variable, type, sizeof(variable), SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global \c std::string in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_SSTR(variable, type) SLEG_CONDSSTR(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global reference list in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_REFLIST(variable, type) SLEG_CONDREFLIST(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global ring in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_PTRRING(variable, type) SLEG_CONDPTRRING(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global vector in every savegame version.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_VEC(variable, type) SLEG_CONDVEC(variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Empty global space in some savegame versions.
 * @param length Length of the empty space.
 * @param from   First savegame version that has the empty space.
 * @param to     Last savegame version that has the empty space.
 * @param extver SlXvFeatureTest to test (along with from and to) which savegames have empty space
 */
#define SLEG_CONDNULL(length, from, to) {true, SL_ARR, SLE_FILE_U8 | SLE_VAR_NULL, length, from, to, (void*)nullptr, SlXvFeatureTest()}

/**
 * Checks whether the savegame is below \a major.\a minor.
 * @param major Major number of the version to check against.
 * @param minor Minor number of the version to check against. If \a minor is 0 or not specified, only the major number is checked.
 * @return Savegame version is earlier than the specified version.
 */
static inline bool IsSavegameVersionBefore(SaveLoadVersion major, byte minor = 0)
{
	extern SaveLoadVersion _sl_version;
	extern byte            _sl_minor_version;
	return _sl_version < major || (minor > 0 && _sl_version == major && _sl_minor_version < minor);
}

/**
 * Checks whether the savegame is below or at \a major. This should be used to repair data from existing
 * savegames which is no longer corrupted in new savegames, but for which otherwise no savegame
 * bump is required.
 * @param major Major number of the version to check against.
 * @return Savegame version is at most the specified version.
 */
static inline bool IsSavegameVersionUntil(SaveLoadVersion major)
{
	extern SaveLoadVersion _sl_version;
	return _sl_version <= major;
}

/**
 * Checks if some version from/to combination falls within the range of the
 * active savegame version.
 * @param version_from Inclusive savegame version lower bound.
 * @param version_to   Exclusive savegame version upper bound. SL_MAX_VERSION if no upper bound.
 * @return Active savegame version falls within the given range.
 */
static inline bool SlIsObjectCurrentlyValid(SaveLoadVersion version_from, SaveLoadVersion version_to, SlXvFeatureTest ext_feature_test)
{
	extern const SaveLoadVersion SAVEGAME_VERSION;
	if (!ext_feature_test.IsFeaturePresent(_sl_xv_feature_static_versions, SAVEGAME_VERSION, version_from, version_to)) return false;

	return true;
}

/**
 * Get the NumberType of a setting. This describes the integer type
 * as it is represented in memory
 * @param type VarType holding information about the variable-type
 * @return the SLE_VAR_* part of a variable-type description
 */
static inline VarType GetVarMemType(VarType type)
{
	return type & 0xF0; // GB(type, 4, 4) << 4;
}

/**
 * Get the FileType of a setting. This describes the integer type
 * as it is represented in a savegame/file
 * @param type VarType holding information about the file-type
 * @return the SLE_FILE_* part of a variable-type description
 */
static inline VarType GetVarFileType(VarType type)
{
	return type & 0xF; // GB(type, 0, 4);
}

/**
 * Check if the given saveload type is a numeric type.
 * @param conv the type to check
 * @return True if it's a numeric type.
 */
static inline bool IsNumericType(VarType conv)
{
	return GetVarMemType(conv) <= SLE_VAR_U64;
}

/**
 * Get the address of the variable. Which one to pick depends on the object
 * pointer. If it is nullptr we are dealing with global variables so the address
 * is taken. If non-null only the offset is stored in the union and we need
 * to add this to the address of the object
 */
static inline void *GetVariableAddress(const void *object, const SaveLoad &sld)
{
	/* Entry is a global address. */
	if (sld.global) return sld.address;

#ifdef _DEBUG
	/* Entry is a null-variable, mostly used to read old savegames etc. */
	if (GetVarMemType(sld.conv) == SLE_VAR_NULL) {
		assert(sld.address == nullptr);
		return nullptr;
	}

	/* Everything else should be a non-null pointer. */
	assert(object != nullptr);
#endif
	return const_cast<byte *>((const byte *)object + (ptrdiff_t)sld.address);
}

int64 ReadValue(const void *ptr, VarType conv);
void WriteValue(void *ptr, VarType conv, int64 val);

void SlSetArrayIndex(uint index);
int SlIterateArray();

void SlAutolength(AutolengthProc *proc, void *arg);
size_t SlGetFieldLength();
void SlSetLength(size_t length);
size_t SlCalcObjMemberLength(const void *object, const SaveLoad &sld);
size_t SlCalcObjLength(const void *object, const SaveLoadTable &slt);

/**
 * Run proc, saving result in the autolength temp buffer
 * @param proc The callback procedure that is called
 * @return Span of the saved data, in the autolength temp buffer
 */
template <typename F>
span<byte> SlSaveToTempBuffer(F proc)
{
	extern uint8 SlSaveToTempBufferSetup();
	extern std::pair<byte *, size_t> SlSaveToTempBufferRestore(uint8 state);

	uint8 state = SlSaveToTempBufferSetup();
	proc();
	auto result = SlSaveToTempBufferRestore(state);
	return span<byte>(result.first, result.second);
}

/**
 * Run proc, saving result to a std::vector
 * This is implemented in terms of SlSaveToTempBuffer
 * @param proc The callback procedure that is called
 * @return a vector containing the saved data
 */
template <typename F>
std::vector<uint8> SlSaveToVector(F proc)
{
	span<byte> result = SlSaveToTempBuffer(proc);
	return std::vector<uint8>(result.begin(), result.end());
}

struct SlConditionallySaveState {
	size_t current_len;
	uint8 need_length;
	bool nested;
};

/**
 * Run proc, saving as normal if proc returns true, otherwise the saved data is discarded
 * @param proc The callback procedure that is called, this should return true to save, false to discard
 * @return Whether the callback procedure returned true to save
 */
template <typename F>
bool SlConditionallySave(F proc)
{
	extern SlConditionallySaveState SlConditionallySaveSetup();
	extern void SlConditionallySaveCompletion(const SlConditionallySaveState &state, bool save);

	SlConditionallySaveState state = SlConditionallySaveSetup();
	bool save = proc();
	SlConditionallySaveCompletion(state, save);
	return save;
}

struct SlLoadFromBufferState {
	size_t old_obj_len;
	byte *old_bufp;
	byte *old_bufe;
};

/**
 * Run proc, loading exactly length bytes from the contents of buffer
 * @param proc The callback procedure that is called
 */
template <typename F>
void SlLoadFromBuffer(const byte *buffer, size_t length, F proc)
{
	extern SlLoadFromBufferState SlLoadFromBufferSetup(const byte *buffer, size_t length);
	extern void SlLoadFromBufferRestore(const SlLoadFromBufferState &state, const byte *buffer, size_t length);

	SlLoadFromBufferState state = SlLoadFromBufferSetup(buffer, length);
	proc();
	SlLoadFromBufferRestore(state, buffer, length);
}

void SlGlobList(const SaveLoadTable &slt);
void SlArray(void *array, size_t length, VarType conv);
void SlObject(void *object, const SaveLoadTable &slt);
bool SlObjectMember(void *object, const SaveLoad &sld);

std::vector<SaveLoad> SlFilterObject(const SaveLoadTable &slt);
void SlObjectSaveFiltered(void *object, const SaveLoadTable &slt);
void SlObjectLoadFiltered(void *object, const SaveLoadTable &slt);
void SlObjectPtrOrNullFiltered(void *object, const SaveLoadTable &slt);

void NORETURN CDECL SlErrorFmt(StringID string, const char *msg, ...) WARN_FORMAT(2, 3);

bool SaveloadCrashWithMissingNewGRFs();

void SlResetVENC();
void SlProcessVENC();

void SlResetTNNC();

extern std::string _savegame_format;
extern bool _do_autosave;

#endif /* SL_SAVELOAD_H */
