/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file saveload.h Functions/types related to saving and loading games. */

#ifndef SAVELOAD_H
#define SAVELOAD_H

#include "../sl/saveload_common.h"
#include "../fileio_type.h"
#include "../fios.h"
#include "../strings_type.h"
#include "../core/ring_buffer.hpp"
#include <optional>
#include <string>
#include <vector>
#include <list>

extern SaveLoadVersion _sl_version;
extern uint8_t         _sl_minor_version;
extern const SaveLoadVersion SAVEGAME_VERSION;
extern const SaveLoadVersion MAX_LOAD_SAVEGAME_VERSION;

namespace upstream_sl {

typedef void AutolengthProc(void *arg);

/** Type of a chunk. */
enum ChunkType {
	CH_RIFF = 0,
	CH_ARRAY = 1,
	CH_SPARSE_ARRAY = 2,
	CH_TABLE = 3,
	CH_SPARSE_TABLE = 4,

	CH_TYPE_MASK = 0xf, ///< All ChunkType values have to be within this mask.
	CH_READONLY, ///< Chunk is never saved.
};

/** Handlers and description of chunk. */
struct ChunkHandler {
	uint32_t id;                        ///< Unique ID (4 letters).
	ChunkType type;                     ///< Type of the chunk. @see ChunkType

	ChunkHandler(uint32_t id, ChunkType type) : id(id), type(type) {}

	virtual ~ChunkHandler() = default;

	/**
	 * Save the chunk.
	 * Must be overridden, unless Chunk type is CH_READONLY.
	 */
	virtual void Save() const { NOT_REACHED(); }

	/**
	 * Load the chunk.
	 * Must be overridden.
	 */
	virtual void Load() const = 0;

	/**
	 * Fix the pointers.
	 * Pointers are saved using the index of the pointed object.
	 * On load, pointers are filled with indices and need to be fixed to point to the real object.
	 * Must be overridden if the chunk saves any pointer.
	 */
	virtual void FixPointers() const {}

	/**
	 * Load the chunk for game preview.
	 * Default implementation just skips the data.
	 * @param len Number of bytes to skip.
	 */
	virtual void LoadCheck(size_t len = 0) const;
};

/** A reference to ChunkHandler. */
using ChunkHandlerRef = std::reference_wrapper<const ChunkHandler>;

/** A table of ChunkHandler entries. */
using ChunkHandlerTable = std::span<const ChunkHandlerRef>;

/** A table of SaveLoadCompat entries. */
using SaveLoadCompatTable = std::span<const struct SaveLoadCompat>;

/** Handler for saving/loading an object to/from disk. */
class SaveLoadHandler {
public:
	std::optional<std::vector<SaveLoad>> load_description;

	virtual ~SaveLoadHandler() = default;

	/**
	 * Save the object to disk.
	 * @param object The object to store.
	 */
	virtual void Save([[maybe_unused]] void *object) const {}

	/**
	 * Load the object from disk.
	 * @param object The object to load.
	 */
	virtual void Load([[maybe_unused]] void *object) const {}

	/**
	 * Similar to load, but used only to validate savegames.
	 * @param object The object to load.
	 */
	virtual void LoadCheck([[maybe_unused]] void *object) const {}

	/**
	 * A post-load callback to fix #SL_REF integers into pointers.
	 * @param object The object to fix.
	 */
	virtual void FixPointers([[maybe_unused]] void *object) const {}

	/**
	 * Get the description of the fields in the savegame.
	 */
	virtual SaveLoadTable GetDescription() const = 0;

	/**
	 * Get the pre-header description of the fields in the savegame.
	 */
	virtual SaveLoadCompatTable GetCompatDescription() const = 0;

	/**
	 * Get the description for how to load the chunk. Depending on the
	 * savegame version this can either use the headers in the savegame or
	 * fall back to backwards compatibility and uses hard-coded headers.
	 */
	SaveLoadTable GetLoadDescription() const;
};

/**
 * Default handler for saving/loading an object to/from disk.
 *
 * This handles a few common things for handlers, meaning the actual handler
 * needs less code.
 *
 * Usage: class SlMine : public DefaultSaveLoadHandler<SlMine, MyObject> {}
 *
 * @tparam TImpl The class initializing this template.
 * @tparam TObject The class of the object using this SaveLoadHandler.
 */
template <class TImpl, class TObject>
class DefaultSaveLoadHandler : public SaveLoadHandler {
public:
	SaveLoadTable GetDescription() const override { return static_cast<const TImpl *>(this)->description; }
	SaveLoadCompatTable GetCompatDescription() const override { return static_cast<const TImpl *>(this)->compat_description; }

	virtual void Save([[maybe_unused]] TObject *object) const {}
	void Save(void *object) const override { this->Save(static_cast<TObject *>(object)); }

	virtual void Load([[maybe_unused]] TObject *object) const {}
	void Load(void *object) const override { this->Load(static_cast<TObject *>(object)); }

	virtual void LoadCheck([[maybe_unused]] TObject *object) const {}
	void LoadCheck(void *object) const override { this->LoadCheck(static_cast<TObject *>(object)); }

	virtual void FixPointers([[maybe_unused]] TObject *object) const {}
	void FixPointers(void *object) const override { this->FixPointers(static_cast<TObject *>(object)); }
};

/** Type of reference (#SLE_REF, #SLE_CONDREF). */
enum SLRefType {
	REF_ORDER          =  0, ///< Load/save a reference to an order.
	REF_VEHICLE        =  1, ///< Load/save a reference to a vehicle.
	REF_STATION        =  2, ///< Load/save a reference to a station.
	REF_TOWN           =  3, ///< Load/save a reference to a town.
	REF_VEHICLE_OLD    =  4, ///< Load/save an old-style reference to a vehicle (for pre-4.4 savegames).
	REF_ROADSTOPS      =  5, ///< Load/save a reference to a bus/truck stop.
	REF_ENGINE_RENEWS  =  6, ///< Load/save a reference to an engine renewal (autoreplace).
	REF_CARGO_PACKET   =  7, ///< Load/save a reference to a cargo packet.
	REF_ORDERLIST      =  8, ///< Load/save a reference to an orderlist.
	REF_STORAGE        =  9, ///< Load/save a reference to a persistent storage.
	REF_LINK_GRAPH     = 10, ///< Load/save a reference to a link graph.
	REF_LINK_GRAPH_JOB = 11, ///< Load/save a reference to a link graph job.
};

/**
 * VarTypes is the general bitmasked magic type that tells us
 * certain characteristics about the variable it refers to. For example
 * SLE_FILE_* gives the size(type) as it would be in the savegame and
 * SLE_VAR_* the size(type) as it is in memory during runtime. These are
 * the first 8 bits (0-3 SLE_FILE, 4-7 SLE_VAR).
 * Bits 8-15 are reserved for various flags as explained below
 */
enum VarTypes {
	/* 4 bits allocated a maximum of 16 types for NumberType.
	 * NOTE: the SLE_FILE_NNN values are stored in the savegame! */
	SLE_FILE_END      =  0, ///< Used to mark end-of-header in tables.
	SLE_FILE_I8       =  1,
	SLE_FILE_U8       =  2,
	SLE_FILE_I16      =  3,
	SLE_FILE_U16      =  4,
	SLE_FILE_I32      =  5,
	SLE_FILE_U32      =  6,
	SLE_FILE_I64      =  7,
	SLE_FILE_U64      =  8,
	SLE_FILE_STRINGID =  9, ///< StringID offset into strings-array
	SLE_FILE_STRING   = 10,
	SLE_FILE_STRUCT   = 11,
	/* 4 more possible file-primitives */

	SLE_FILE_TYPE_MASK = 0xf, ///< Mask to get the file-type (and not any flags).
	SLE_FILE_HAS_LENGTH_FIELD = 1 << 4, ///< Bit stored in savegame to indicate field has a length field for each entry.

	/* 4 bits allocated a maximum of 16 types for NumberType */
	SLE_VAR_BL    =  0 << 4,
	SLE_VAR_I8    =  1 << 4,
	SLE_VAR_U8    =  2 << 4,
	SLE_VAR_I16   =  3 << 4,
	SLE_VAR_U16   =  4 << 4,
	SLE_VAR_I32   =  5 << 4,
	SLE_VAR_U32   =  6 << 4,
	SLE_VAR_I64   =  7 << 4,
	SLE_VAR_U64   =  8 << 4,
	SLE_VAR_NULL  =  9 << 4, ///< useful to write zeros in savegame.
	SLE_VAR_STRB  = 10 << 4, ///< string (with pre-allocated buffer)
	SLE_VAR_STR   = 12 << 4, ///< string pointer
	SLE_VAR_STRQ  = 13 << 4, ///< string pointer enclosed in quotes
	SLE_VAR_NAME  = 14 << 4, ///< old custom name to be converted to a char pointer
	/* 1 more possible memory-primitives */

	/* Shortcut values */
	SLE_VAR_CHAR = SLE_VAR_I8,

	/* Default combinations of variables. As savegames change, so can variables
	 * and thus it is possible that the saved value and internal size do not
	 * match and you need to specify custom combo. The defaults are listed here */
	SLE_BOOL         = SLE_FILE_I8  | SLE_VAR_BL,
	SLE_INT8         = SLE_FILE_I8  | SLE_VAR_I8,
	SLE_UINT8        = SLE_FILE_U8  | SLE_VAR_U8,
	SLE_INT16        = SLE_FILE_I16 | SLE_VAR_I16,
	SLE_UINT16       = SLE_FILE_U16 | SLE_VAR_U16,
	SLE_INT32        = SLE_FILE_I32 | SLE_VAR_I32,
	SLE_UINT32       = SLE_FILE_U32 | SLE_VAR_U32,
	SLE_INT64        = SLE_FILE_I64 | SLE_VAR_I64,
	SLE_UINT64       = SLE_FILE_U64 | SLE_VAR_U64,
	SLE_CHAR         = SLE_FILE_I8  | SLE_VAR_CHAR,
	SLE_STRINGID     = SLE_FILE_STRINGID | SLE_VAR_U32,
	SLE_STRINGBUF    = SLE_FILE_STRING   | SLE_VAR_STRB,
	SLE_STRING       = SLE_FILE_STRING   | SLE_VAR_STR,
	SLE_STRINGQUOTE  = SLE_FILE_STRING   | SLE_VAR_STRQ,
	SLE_NAME         = SLE_FILE_STRINGID | SLE_VAR_NAME,

	/* Shortcut values */
	SLE_UINT  = SLE_UINT32,
	SLE_INT   = SLE_INT32,
	SLE_STRB  = SLE_STRINGBUF,
	SLE_STR   = SLE_STRING,
	SLE_STRQ  = SLE_STRINGQUOTE,

	/* 8 bits allocated for a maximum of 8 flags
	 * Flags directing saving/loading of a variable */
	SLF_ALLOW_CONTROL   = 1 << 8, ///< Allow control codes in the strings.
	SLF_ALLOW_NEWLINE   = 1 << 9, ///< Allow new lines in the strings.
};

typedef uint32_t VarType;

/** Type of data saved. */
enum SaveLoadType : uint8_t {
	SL_VAR         =  0, ///< Save/load a variable.
	SL_REF         =  1, ///< Save/load a reference.
	SL_STRUCT      =  2, ///< Save/load a struct.

	SL_STR         =  3, ///< Save/load a string.
	SL_STDSTR      =  4, ///< Save/load a \c std::string.

	SL_ARR         =  5, ///< Save/load a fixed-size array of #SL_VAR elements.
	SL_RING        =  6, ///< Save/load a ring of #SL_VAR elements.
	SL_VECTOR      =  7, ///< Save/load a vector of #SL_VAR elements.
	SL_REFLIST     =  8, ///< Save/load a list of #SL_REF elements.
	SL_STRUCTLIST  =  9, ///< Save/load a list of structs.

	SL_SAVEBYTE    = 10, ///< Save (but not load) a byte.
	SL_NULL        = 11, ///< Save null-bytes and load to nowhere.

	SL_REFRING,          ///< Save/load a ring of #SL_REF elements.
	SL_REFVEC,           ///< Save/load a vector of #SL_REF elements.
};

typedef void *SaveLoadAddrProc(void *base, size_t extra);

/** SaveLoad type struct. Do NOT use this directly but use the SLE_ macros defined just below! */
struct SaveLoad {
	std::string name;    ///< Name of this field (optional, used for tables).
	SaveLoadType cmd;    ///< The action to take with the saved/loaded type, All types need different action.
	VarType conv;        ///< Type of the variable to be saved; this field combines both FileVarType and MemVarType.
	uint16_t length;     ///< (Conditional) length of the variable (eg. arrays) (max array size is 65536 elements).
	SaveLoadVersion version_from;   ///< Save/load the variable starting from this savegame version.
	SaveLoadVersion version_to;     ///< Save/load the variable before this savegame version.
	size_t size;                    ///< The sizeof size.
	SaveLoadAddrProc *address_proc; ///< Callback proc the get the actual variable address in memory.
	size_t extra_data;              ///< Extra data for the callback proc.
	std::shared_ptr<SaveLoadHandler> handler; ///< Custom handler for Save/Load procs.
};

/**
 * SaveLoad information for backwards compatibility.
 *
 * At SLV_SETTINGS_NAME a new method of keeping track of fields in a savegame
 * was added, where the order of fields is no longer important. For older
 * savegames we still need to know the correct order. This struct is the glue
 * to make that happen.
 */
struct SaveLoadCompat {
	std::string name;             ///< Name of the field.
	VarTypes null_type;           ///< The type associated with the NULL field; defaults to SLE_FILE_U8 to just count bytes.
	uint16_t null_length;         ///< Length of the NULL field.
	SaveLoadVersion version_from; ///< Save/load the variable starting from this savegame version.
	SaveLoadVersion version_to;   ///< Save/load the variable before this savegame version.
};

/**
 * Get the NumberType of a setting. This describes the integer type
 * as it is represented in memory
 * @param type VarType holding information about the variable-type
 * @return the SLE_VAR_* part of a variable-type description
 */
inline constexpr VarType GetVarMemType(VarType type)
{
	return type & 0xF0; // GB(type, 4, 4) << 4;
}

/**
 * Get the FileType of a setting. This describes the integer type
 * as it is represented in a savegame/file
 * @param type VarType holding information about the file-type
 * @return the SLE_FILE_* part of a variable-type description
 */
inline constexpr VarType GetVarFileType(VarType type)
{
	return type & 0xF; // GB(type, 0, 4);
}

/**
 * Check if the given saveload type is a numeric type.
 * @param conv the type to check
 * @return True if it's a numeric type.
 */
inline constexpr bool IsNumericType(VarType conv)
{
	return GetVarMemType(conv) <= SLE_VAR_U64;
}

/**
 * Return expect size in bytes of a VarType
 * @param type VarType to get size of.
 * @return size of type in bytes.
 */
inline constexpr size_t SlVarSize(VarType type)
{
	switch (GetVarMemType(type)) {
		case SLE_VAR_BL: return sizeof(bool);
		case SLE_VAR_I8: return sizeof(int8_t);
		case SLE_VAR_U8: return sizeof(uint8_t);
		case SLE_VAR_I16: return sizeof(int16_t);
		case SLE_VAR_U16: return sizeof(uint16_t);
		case SLE_VAR_I32: return sizeof(int32_t);
		case SLE_VAR_U32: return sizeof(uint32_t);
		case SLE_VAR_I64: return sizeof(int64_t);
		case SLE_VAR_U64: return sizeof(uint64_t);
		case SLE_VAR_NULL: return sizeof(void *);
		case SLE_VAR_STR: return sizeof(std::string);
		case SLE_VAR_STRQ: return sizeof(std::string);
		case SLE_VAR_NAME: return sizeof(std::string);
		default: NOT_REACHED();
	}
}

/**
 * Check if a saveload cmd/type/length entry matches the size of the variable.
 * @param cmd SaveLoadType of entry.
 * @param type VarType of entry.
 * @param length Array length of entry.
 * @param size Actual size of variable.
 * @return true iff the sizes match.
 */
inline constexpr bool SlCheckVarSize(SaveLoadType cmd, VarType type, size_t length, size_t size)
{
	switch (cmd) {
		case SL_VAR: return SlVarSize(type) == size;
		case SL_REF: return sizeof(void *) == size;
		case SL_STR: return sizeof(void *) == size;
		case SL_STDSTR: return SlVarSize(type) == size;
		case SL_ARR: return SlVarSize(type) * length <= size; // Partial load of array is permitted.
		case SL_RING: return sizeof(ring_buffer<void *>) == size;
		case SL_VECTOR: return sizeof(std::vector<void *>) == size;
		case SL_REFLIST: return sizeof(std::list<void *>) == size;
		case SL_REFRING: return sizeof(ring_buffer<void *>) == size;
		case SL_REFVEC: return sizeof(std::vector<void *>) == size;
		case SL_SAVEBYTE: return true;
		default: NOT_REACHED();
	}
}

/**
 * Storage of simple variables, references (pointers), and arrays.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param name     Field name for table chunks.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extra    Extra data to pass to the address callback function.
 * @note In general, it is better to use one of the SLE_* macros below.
 */
#define SLE_GENERAL_NAME(cmd, name, base, variable, type, length, from, to, extra) \
	SaveLoad {name, cmd, type, length, from, to, cpp_sizeof(base, variable), [] (void *b, size_t) -> void * { \
		static_assert(SlCheckVarSize(cmd, type, length, sizeof(static_cast<base *>(b)->variable))); \
		assert(b != nullptr); \
		return const_cast<void *>(static_cast<const void *>(std::addressof(static_cast<base *>(b)->variable))); \
	}, extra, nullptr}

/**
 * Storage of simple variables, references (pointers), and arrays with a custom name.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extra    Extra data to pass to the address callback function.
 * @note In general, it is better to use one of the SLE_* macros below.
 */
#define SLE_GENERAL(cmd, base, variable, type, length, from, to, extra) SLE_GENERAL_NAME(cmd, #variable, base, variable, type, length, from, to, extra)

/**
 * Storage of a variable in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLE_CONDVAR(base, variable, type, from, to) SLE_GENERAL(SL_VAR, base, variable, type, 0, from, to, 0)

/**
 * Storage of a variable in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param name     Field name for table chunks.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLE_CONDVARNAME(base, variable, name, type, from, to) SLE_GENERAL_NAME(SL_VAR, name, base, variable, type, 0, from, to, 0)

/**
 * Storage of a reference in some savegame versions.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Type of the reference, a value from #SLRefType.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLE_CONDREF(base, variable, type, from, to) SLE_GENERAL(SL_REF, base, variable, type, 0, from, to, 0)

/**
 * Storage of a fixed-size array of #SL_VAR elements in some savegame versions.
 * @param base     Name of the class or struct containing the array.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the array.
 * @param to       Last savegame version that has the array.
 */
#define SLE_CONDARR(base, variable, type, length, from, to) SLE_GENERAL(SL_ARR, base, variable, type, length, from, to, 0)

/**
 * Storage of a string in some savegame versions.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLE_CONDSTR(base, variable, type, length, from, to) SLE_GENERAL(SL_STR, base, variable, type, length, from, to, 0)

/**
 * Storage of a \c std::string in some savegame versions.
 * @param base     Name of the class or struct containing the string.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLE_CONDSSTR(base, variable, type, from, to) SLE_GENERAL(SL_STDSTR, base, variable, type, 0, from, to, 0)

/**
 * Storage of a list of #SL_REF elements in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLE_CONDREFLIST(base, variable, type, from, to) SLE_GENERAL(SL_REFLIST, base, variable, type, 0, from, to, 0)

/**
 * Storage of a ring of #SL_REF elements in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLE_CONDREFRING(base, variable, type, from, to) SLE_GENERAL(SL_REFRING, base, variable, type, 0, from, to, 0)

/**
 * Storage of a vector of #SL_REF elements in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLE_CONDREFVEC(base, variable, type, from, to) SLE_GENERAL(SL_REFVEC, base, variable, type, 0, from, to, 0)

/**
 * Storage of a ring of #SL_VAR elements in some savegame versions.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLE_CONDRING(base, variable, type, from, to) SLE_GENERAL(SL_RING, base, variable, type, 0, from, to, 0)

/**
 * Storage of a variable in every version of a savegame.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_VAR(base, variable, type) SLE_CONDVAR(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)
#define SLE_VAR2(base, name, variable, type) SLE_CONDVARNAME(base, variable, name, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a variable in every version of a savegame.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param name     Field name for table chunks.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_VARNAME(base, variable, name, type) SLE_CONDVARNAME(base, variable, name, type, SL_MIN_VERSION, SL_MAX_VERSION)

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
 * Storage of a ring of #SL_REF elements in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_REFRING(base, variable, type) SLE_CONDREFRING(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a vector of #SL_REF elements in every savegame version.
 * @param base     Name of the class or struct containing the list.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLE_REFVEC(base, variable, type) SLE_CONDREFVEC(base, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Only write byte during saving; never read it during loading.
 * When using SLE_SAVEBYTE you will have to read this byte before the table
 * this is in is read. This also means SLE_SAVEBYTE can only be used at the
 * top of a chunk.
 * This is intended to be used to indicate what type of entry this is in a
 * list of entries.
 * @param base     Name of the class or struct containing the variable.
 * @param variable Name of the variable in the class or struct referenced by \a base.
 */
#define SLE_SAVEBYTE(base, variable) SLE_GENERAL(SL_SAVEBYTE, base, variable, 0, 0, SL_MIN_VERSION, SL_MAX_VERSION, 0)

/**
 * Storage of global simple variables, references (pointers), and arrays.
 * @param name     The name of the field.
 * @param cmd      Load/save type. @see SaveLoadType
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 * @param extra    Extra data to pass to the address callback function.
 * @note In general, it is better to use one of the SLEG_* macros below.
 */
#define SLEG_GENERAL(name, cmd, variable, type, length, from, to, extra) \
	SaveLoad {name, cmd, type, length, from, to, sizeof(variable), [] (void *, size_t) -> void * { \
		static_assert(SlCheckVarSize(cmd, type, length, sizeof(variable))); \
		return static_cast<void *>(std::addressof(variable)); }, extra, nullptr}

/**
 * Storage of a global variable in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLEG_CONDVAR(name, variable, type, from, to) SLEG_GENERAL(name, SL_VAR, variable, type, 0, from, to, 0)

/**
 * Storage of a global reference in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the field.
 * @param to       Last savegame version that has the field.
 */
#define SLEG_CONDREF(name, variable, type, from, to) SLEG_GENERAL(name, SL_REF, variable, type, 0, from, to, 0)

/**
 * Storage of a global fixed-size array of #SL_VAR elements in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the array.
 * @param from     First savegame version that has the array.
 * @param to       Last savegame version that has the array.
 */
#define SLEG_CONDARR(name, variable, type, length, from, to) SLEG_GENERAL(name, SL_ARR, variable, type, length, from, to, 0)

/**
 * Storage of a global string in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param length   Number of elements in the string (only used for fixed size buffers).
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLEG_CONDSTR(name, variable, type, length, from, to) SLEG_GENERAL(name, SL_STR, variable, type, length, from, to, 0)

/**
 * Storage of a global \c std::string in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the string.
 * @param to       Last savegame version that has the string.
 */
#define SLEG_CONDSSTR(name, variable, type, from, to) SLEG_GENERAL(name, SL_STDSTR, variable, type, 0, from, to, 0)

/**
 * Storage of a structs in some savegame versions.
 * @param name     The name of the field.
 * @param handler  SaveLoadHandler for the structs.
 * @param from     First savegame version that has the struct.
 * @param to       Last savegame version that has the struct.
 */
#define SLEG_CONDSTRUCT(name, handler, from, to) SaveLoad {name, SL_STRUCT, 0, 0, from, to, 0, nullptr, 0, std::make_shared<handler>()}

/**
 * Storage of a global reference list in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLEG_CONDREFLIST(name, variable, type, from, to) SLEG_GENERAL(name, SL_REFLIST, variable, type, 0, from, to, 0)

/**
 * Storage of a global reference ring in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLEG_CONDREFRING(name, variable, type, from, to) SLEG_GENERAL(name, SL_REFRING, variable, type, 0, from, to, 0)

/**
 * Storage of a global reference vector in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLEG_CONDREFVEC(name, variable, type, from, to) SLEG_GENERAL(name, SL_REFVEC, variable, type, 0, from, to, 0)

/**
 * Storage of a global vector of #SL_VAR elements in some savegame versions.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLEG_CONDVECTOR(name, variable, type, from, to) SLEG_GENERAL(name, SL_VECTOR, variable, type, 0, from, to, 0)

/**
 * Storage of a list of structs in some savegame versions.
 * @param name     The name of the field.
 * @param handler  SaveLoadHandler for the list of structs.
 * @param from     First savegame version that has the list.
 * @param to       Last savegame version that has the list.
 */
#define SLEG_CONDSTRUCTLIST(name, handler, from, to) SaveLoad {name, SL_STRUCTLIST, 0, 0, from, to, 0, nullptr, 0, std::make_shared<handler>()}

/**
 * Storage of a global variable in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_VAR(name, variable, type) SLEG_CONDVAR(name, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global reference in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_REF(name, variable, type) SLEG_CONDREF(name, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global fixed-size array of #SL_VAR elements in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_ARR(name, variable, type) SLEG_CONDARR(name, variable, type, lengthof(variable), SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global string in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_STR(name, variable, type) SLEG_CONDSTR(name, variable, type, sizeof(variable), SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global \c std::string in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_SSTR(name, variable, type) SLEG_CONDSSTR(name, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a structs in every savegame version.
 * @param name     The name of the field.
 * @param handler SaveLoadHandler for the structs.
 */
#define SLEG_STRUCT(name, handler) SLEG_CONDSTRUCT(name, handler, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global reference list in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_REFLIST(name, variable, type) SLEG_CONDREFLIST(name, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global reference ring in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_REFRING(name, variable, type) SLEG_CONDREFRING(name, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a global vector of #SL_VAR elements in every savegame version.
 * @param name     The name of the field.
 * @param variable Name of the global variable.
 * @param type     Storage of the data in memory and in the savegame.
 */
#define SLEG_VECTOR(name, variable, type) SLEG_CONDVECTOR(name, variable, type, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Storage of a list of structs in every savegame version.
 * @param name    The name of the field.
 * @param handler SaveLoadHandler for the list of structs.
 */
#define SLEG_STRUCTLIST(name, handler) SLEG_CONDSTRUCTLIST(name, handler, SL_MIN_VERSION, SL_MAX_VERSION)

/**
 * Field name where the real SaveLoad can be located.
 * @param name The name of the field.
 */
#define SLC_VAR(name) {name, SLE_FILE_U8, 0, SL_MIN_VERSION, SL_MAX_VERSION}

/**
 * Empty space in every savegame version.
 * @param length Length of the empty space in bytes.
 * @param from   First savegame version that has the empty space.
 * @param to     Last savegame version that has the empty space.
 */
#define SLC_NULL(length, from, to) {{}, SLE_FILE_U8, length, from, to}

/**
 * Empty space in every savegame version that was filled with a string.
 * @param length Number of strings in the empty space.
 * @param from   First savegame version that has the empty space.
 * @param to     Last savegame version that has the empty space.
 */
#define SLC_NULL_STR(length, from, to) {{}, SLE_FILE_STRING, length, from, to}

/** End marker of compat variables save or load. */
#define SLC_END() {{}, 0, 0, SL_MIN_VERSION, SL_MIN_VERSION}

/**
 * Checks whether the savegame is below \a major.\a minor.
 * @param major Major number of the version to check against.
 * @param minor Minor number of the version to check against. If \a minor is 0 or not specified, only the major number is checked.
 * @return Savegame version is earlier than the specified version.
 */
inline bool IsSavegameVersionBefore(SaveLoadVersion major, uint8_t minor = 0)
{
	return _sl_version < major || (minor > 0 && _sl_version == major && _sl_minor_version < minor);
}

/**
 * Checks whether the savegame is below or at \a major. This should be used to repair data from existing
 * savegames which is no longer corrupted in new savegames, but for which otherwise no savegame
 * bump is required.
 * @param major Major number of the version to check against.
 * @return Savegame version is at most the specified version.
 */
inline bool IsSavegameVersionBeforeOrAt(SaveLoadVersion major)
{
	return _sl_version <= major;
}

/**
 * Get the address of the variable. Null-variables don't have an address,
 * everything else has a callback function that returns the address based
 * on the saveload data and the current object for non-globals.
 */
inline void *GetVariableAddress(const void *object, const SaveLoad &sld)
{
	/* Entry is a null-variable, mostly used to read old savegames etc. */
	if (GetVarMemType(sld.conv) == SLE_VAR_NULL) {
		assert(sld.address_proc == nullptr);
		return nullptr;
	}

	/* Everything else should be a non-null pointer. */
	assert(sld.address_proc != nullptr);
	return sld.address_proc(const_cast<void *>(object), sld.extra_data);
}

int64_t ReadValue(const void *ptr, VarType conv);
void WriteValue(void *ptr, VarType conv, int64_t val);

void SlSetArrayIndex(uint index);
int SlIterateArray();

void SlSetStructListLength(size_t length);
size_t SlGetStructListLength(size_t limit);

void SlAutolength(AutolengthProc *proc, void *arg);
size_t SlGetFieldLength();
void SlSetLength(size_t length);
size_t SlCalcObjMemberLength(const void *object, const SaveLoad &sld);
size_t SlCalcObjLength(const void *object, const SaveLoadTable &slt);

void SlGlobList(const SaveLoadTable &slt);
void SlCopy(void *object, size_t length, VarType conv);
std::vector<SaveLoad> SlTableHeader(const SaveLoadTable &slt);
std::vector<SaveLoad> SlCompatTableHeader(const SaveLoadTable &slt, const SaveLoadCompatTable &slct);
void SlObject(void *object, const SaveLoadTable &slt);

}

#endif /* SAVELOAD_H */
