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
	SL_VAR         =  0, ///< Save/load a variable.
	SL_REF         =  1, ///< Save/load a reference.
	SL_ARR         =  2, ///< Save/load a fixed-size array of #SL_VAR elements.
	SL_STR         =  3, ///< Save/load a string.
	SL_REFLIST     =  4, ///< Save/load a list of #SL_REF elements.
	SL_RING        =  5, ///< Save/load a ring of #SL_VAR elements.
	SL_VEC         =  6, ///< Save/load a vector of #SL_REF elements.
	SL_STDSTR      =  7, ///< Save/load a std::string.

	/* non-normal save-load types */
	SL_WRITEBYTE   =  8,
	SL_VEH_INCLUDE =  9,
	SL_ST_INCLUDE  = 10,

	SL_PTRRING     = 13, ///< Save/load a ring of #SL_REF elements.
	SL_VARVEC      = 14, ///< Save/load a primitive type vector.
};

typedef uint8_t SaveLoadType; ///< Save/load type. @see SaveLoadTypes

/** SaveLoad type struct. Do NOT use this directly but use the SLE_ macros defined just below! */
struct SaveLoad {
	bool global;         ///< should we load a global variable or a non-global one
	SaveLoadType cmd;    ///< the action to take with the saved/loaded type, All types need different action
	VarType conv;        ///< type of the variable to be saved, int
	uint16_t length;     ///< (conditional) length of the variable (eg. arrays) (max array size is 65536 elements)
	SaveLoadVersion version_from; ///< save/load the variable starting from this savegame version
	SaveLoadVersion version_to;   ///< save/load the variable until this savegame version
	/* NOTE: This element either denotes the address of the variable for a global
	 * variable, or the offset within a struct which is then bound to a variable
	 * during runtime. Decision on which one to use is controlled by the function
	 * that is called to save it. address: global=true, offset: global=false */
	void *address;       ///< address of variable OR offset of variable in the struct (max offset is 65536)
	size_t size;         ///< the sizeof size.
	SlXvFeatureTest ext_feature_test;  ///< extended feature test
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

#endif /* SL_SAVELOAD_TYPES_H */
