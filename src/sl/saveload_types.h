/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file saveload_common.h Common functions/types for saving and loading games. */

#ifndef SL_SAVELOAD_TYPES_H
#define SL_SAVELOAD_TYPES_H

#include "saveload_common.h"
#include "extended_ver_sl.h"

/**
 * VarTypes is the general bitmasked magic type that tells us
 * certain characteristics about the variable it refers to. For example
 * SLE_FILE_* gives the size(type) as it would be in the savegame and
 * SLE_VAR_* the size(type) as it is in memory during runtime. These are
 * the first 8 bits (0-3 SLE_FILE, 4-7 SLE_VAR).
 * Bits 8-15 are reserved for various flags as explained below
 */
enum VarTypes {
	/* 4 bits allocated a maximum of 16 types for NumberType */
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

	/* End of values storable in save games */
	SLE_FILE_TABLE_END = 12,

	SLE_FILE_VEHORDERID = 12,

	SLE_FILE_TYPE_MASK = 0xF, ///< Mask to get the file-type (and not any flags).
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
	SLE_VAR_NAME  = 14 << 4, ///< old custom name to be converted to a std::string
	SLE_VAR_CNAME = 15 << 4, ///< old custom name to be converted to a char pointer
	/* 0 more possible memory-primitives */

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
	SLE_CNAME        = SLE_FILE_STRINGID | SLE_VAR_CNAME,
	SLE_VEHORDERID   = SLE_FILE_VEHORDERID  | SLE_VAR_U16,

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
enum SaveLoadTypes {
	SL_VAR = 0,          ///< Save/load a variable.
	SL_REF,              ///< Save/load a reference.
	SL_ARR,              ///< Save/load a fixed-size array of #SL_VAR elements.
	SL_STR,              ///< Save/load a string.
	SL_REFLIST,          ///< Save/load a list of #SL_REF elements.
	SL_RING,             ///< Save/load a ring of #SL_VAR elements.
	SL_VEC,              ///< Save/load a vector of #SL_REF elements.
	SL_STDSTR,           ///< Save/load a std::string.
	SL_PTRRING,          ///< Save/load a ring of #SL_REF elements.
	SL_VARVEC,           ///< Save/load a primitive type vector.

	SL_STRUCT,           ///< Save/load a struct.
	SL_STRUCTLIST,       ///< Save/load a list of structs.

	/* non-normal save-load types */
	SL_WRITEBYTE,
	SL_VEH_INCLUDE,
	SL_INCLUDE,
};

typedef uint8_t SaveLoadType; ///< Save/load type. @see SaveLoadTypes

using SaveLoadStructHandlerFactory = std::unique_ptr<class SaveLoadStructHandler> (*)();
using SaveLoadIncludeFunctor = void (*)(std::vector<struct SaveLoad> &);

/** SaveLoad type struct. Do NOT use this directly but use the SLE_ macros defined just below! */
struct SaveLoad {
	bool global;         ///< should we load a global variable or a non-global one
	SaveLoadType cmd;    ///< the action to take with the saved/loaded type, All types need different action
	VarType conv;        ///< type of the variable to be saved, int
	uint16_t length;     ///< (conditional) length of the variable (eg. arrays) (max array size is 65536 elements)
	SaveLoadVersion version_from; ///< save/load the variable starting from this savegame version
	SaveLoadVersion version_to;   ///< save/load the variable until this savegame version
	uint16_t label_tag;  ///< for labelling purposes

	union {
		/* NOTE: This element either denotes the address of the variable for a global
		 * variable, or the offset within a struct which is then bound to a variable
		 * during runtime. Decision on which one to use is controlled by the function
		 * that is called to save it. address: global=true, offset: global=false */
		void *address;                                       ///< address of variable OR offset of variable in the struct (max offset is 65536)
		SaveLoadStructHandlerFactory struct_handler_factory; ///< factory function pointer for SaveLoadStructHandler
		SaveLoadIncludeFunctor include_functor;              ///< include functor for SL_INCLUDE
	};

	SlXvFeatureTest ext_feature_test;  ///< extended feature test
	SaveLoadStructHandler *struct_handler = nullptr;
};

inline constexpr SaveLoad SLTAG(uint16_t label_tag, SaveLoad save_load)
{
	save_load.label_tag = label_tag;
	return save_load;
}

enum SaveLoadTags {
	SLTAG_DEFAULT,
	SLTAG_TABLE_UNKNOWN,
	SLTAG_CUSTOM_START,
	SLTAG_CUSTOM_0 = SLTAG_CUSTOM_START,
	SLTAG_CUSTOM_1,
	SLTAG_CUSTOM_2,
};

enum NamedSaveLoadFlags : uint8_t {
	NSLF_NONE                     = 0,
	NSLF_TABLE_ONLY               = 1 << 0,
};
DECLARE_ENUM_AS_BIT_SET(NamedSaveLoadFlags)

/** Named SaveLoad type struct, for use in tables */
struct NamedSaveLoad {
	const char *name;             ///< the name (for use in table chunks)
	SaveLoad save_load;           ///< SaveLoad type struct
	NamedSaveLoadFlags nsl_flags; ///< Flags
};

inline constexpr NamedSaveLoad NSL(const char *name, SaveLoad save_load)
{
	return { name, save_load, NSLF_NONE };
}

inline constexpr NamedSaveLoad NSLT(const char *name, SaveLoad save_load)
{
	return { name, save_load, NSLF_TABLE_ONLY };
}

template<typename T, auto... ARGS>
inline constexpr SaveLoadStructHandlerFactory MakeSaveLoadStructHandlerFactory()
{
	SaveLoadStructHandlerFactory factory = []() -> std::unique_ptr<class SaveLoadStructHandler> {
		return std::make_unique<T>(ARGS...);
	};
	return factory;
}

inline constexpr NamedSaveLoad NSL_STRUCT_COMMON(const char *name, NamedSaveLoadFlags nsl_flags, SaveLoadStructHandlerFactory factory, SaveLoadVersion from, SaveLoadVersion to, SlXvFeatureTest extver)
{
	return { name, SaveLoad { true, SL_STRUCT, SLE_FILE_STRUCT, 0, from, to, SLTAG_DEFAULT, { .struct_handler_factory = factory }, extver }, nsl_flags };
}

inline constexpr NamedSaveLoad NSL_STRUCT(const char *name, SaveLoadStructHandlerFactory factory, SaveLoadVersion from = SL_MIN_VERSION, SaveLoadVersion to = SL_MAX_VERSION, SlXvFeatureTest extver = {})
{
	return NSL_STRUCT_COMMON(name, NSLF_NONE, factory, from, to, extver);
}

template <typename T, typename... Args>
inline constexpr NamedSaveLoad NSL_STRUCT(const char *name, Args&&... args)
{
	return NSL_STRUCT(name, MakeSaveLoadStructHandlerFactory<T>(), std::forward<Args>(args)...);
}

inline constexpr NamedSaveLoad NSLT_STRUCT(const char *name, SaveLoadStructHandlerFactory factory, SaveLoadVersion from = SL_MIN_VERSION, SaveLoadVersion to = SL_MAX_VERSION, SlXvFeatureTest extver = {})
{
	return NSL_STRUCT_COMMON(name, NSLF_TABLE_ONLY, factory, from, to, extver);
}

template <typename T, typename... Args>
inline constexpr NamedSaveLoad NSLT_STRUCT(const char *name, Args&&... args)
{
	return NSLT_STRUCT(name, MakeSaveLoadStructHandlerFactory<T>(), std::forward<Args>(args)...);
}

inline constexpr NamedSaveLoad NSLT_STRUCTLIST(const char *name, SaveLoadStructHandlerFactory factory, SaveLoadVersion from = SL_MIN_VERSION, SaveLoadVersion to = SL_MAX_VERSION, SlXvFeatureTest extver = {})
{
	return { name, SaveLoad { true, SL_STRUCTLIST, SLE_FILE_STRUCT, 0, from, to, SLTAG_DEFAULT, { .struct_handler_factory = factory }, extver }, NSLF_TABLE_ONLY };
}

template <typename T>
inline constexpr NamedSaveLoad NSLT_STRUCTLIST(const char *name, SaveLoadVersion from = SL_MIN_VERSION, SaveLoadVersion to = SL_MAX_VERSION, SlXvFeatureTest extver = {})
{
	return NSLT_STRUCTLIST(name, MakeSaveLoadStructHandlerFactory<T>(), from, to, extver);
}

inline constexpr NamedSaveLoad NSLTAG(uint16_t label_tag, NamedSaveLoad nsl)
{
	nsl.save_load.label_tag = label_tag;
	return nsl;
}

struct SaveLoadTableData : public std::vector<SaveLoad> {
	std::vector<std::unique_ptr<class SaveLoadStructHandler>> struct_handlers;
};

/** Handler for saving/loading a SL_STRUCT/SL_STRUCTLIST. */
class SaveLoadStructHandler {
public:
	SaveLoadTableData table_data;

	virtual ~SaveLoadStructHandler() = default;

	/**
	 * Get the (static) description of the fields in the savegame.
	 */
	virtual NamedSaveLoadTable GetDescription() const = 0;

	/**
	 * Get the (current) description of the fields in the savegame.
	 */
	SaveLoadTable GetLoadDescription() const { return this->table_data; }

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
	 * Called immediately after table_data is populated during header load.
	 */
	virtual void LoadedTableDescription() {};

	/**
	 * Called immediately after table_data is populated during header save.
	 */
	virtual void SavedTableDescription() {};
};


template <class TImpl, class TObject>
class TypedSaveLoadStructHandler : public SaveLoadStructHandler {
public:
	virtual void Save([[maybe_unused]] TObject *object) const {}
	void Save(void *object) const override { static_cast<const TImpl *>(this)->Save(static_cast<TObject *>(object)); }

	virtual void Load([[maybe_unused]] TObject *object) const {}
	void Load(void *object) const override { static_cast<const TImpl *>(this)->Load(static_cast<TObject *>(object)); }

	virtual void LoadCheck([[maybe_unused]] TObject *object) const {}
	void LoadCheck(void *object) const override { static_cast<const TImpl *>(this)->LoadCheck(static_cast<TObject *>(object)); }

	virtual void FixPointers([[maybe_unused]] TObject *object) const {}
	void FixPointers(void *object) const override { static_cast<const TImpl *>(this)->FixPointers(static_cast<TObject *>(object)); }
};

class HeaderOnlySaveLoadStructHandler : public SaveLoadStructHandler {
public:
	void Save(void *object) const override { NOT_REACHED(); }
	void Load(void *object) const override { NOT_REACHED(); }
	void LoadCheck(void *object) const override { NOT_REACHED(); }
	void FixPointers(void *object) const override { NOT_REACHED(); }
};

#endif /* SL_SAVELOAD_TYPES_H */
