/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file fileio_type.h Types for Standard In/Out file operations */

#ifndef FILEIO_TYPE_H
#define FILEIO_TYPE_H

#include "core/enum_type.hpp"
#include <optional>

/** The different abstract types of files that the system knows about. */
enum AbstractFileType : uint8_t {
	FT_NONE,      ///< nothing to do
	FT_SAVEGAME,  ///< old or new savegame
	FT_SCENARIO,  ///< old or new scenario
	FT_HEIGHTMAP, ///< heightmap file
	FT_TOWN_DATA, ///< town data file

	FT_INVALID = 7, ///< Invalid or unknown file type.
	FT_NUMBITS = 3, ///< Number of bits required for storing a #AbstractFileType value.
	FT_MASK = (1 << FT_NUMBITS) - 1, ///< Bitmask for extracting an abstract file type.
};

/** Kinds of files in each #AbstractFileType. */
enum DetailedFileType : uint8_t {
	/* Save game and scenario files. */
	DFT_OLD_GAME_FILE, ///< Old save game or scenario file.
	DFT_GAME_FILE,     ///< Save game or scenario file.

	/* Heightmap files. */
	DFT_HEIGHTMAP_BMP, ///< BMP file.
	DFT_HEIGHTMAP_PNG, ///< PNG file.

	/* Town data files. */
	DFT_TOWN_DATA_JSON,  ///< JSON file.

	/* fios 'files' */
	DFT_FIOS_DRIVE,  ///< A drive (letter) entry.
	DFT_FIOS_PARENT, ///< A parent directory entry.
	DFT_FIOS_DIR,    ///< A directory entry.
	DFT_FIOS_DIRECT, ///< Direct filename.

	DFT_END,         ///< End of this enum. Supports a compile time size check against _fios_colours in fios_gui.cpp

	DFT_INVALID = 255, ///< Unknown or invalid file.
};

/** Operation performed on the file. */
enum SaveLoadOperation : uint8_t {
	SLO_CHECK,   ///< Load file for checking and/or preview.
	SLO_LOAD,    ///< File is being loaded.
	SLO_SAVE,    ///< File is being saved.

	SLO_INVALID, ///< Unknown file operation.
};

/**
 * Construct an enum value for #FiosType as a combination of an abstract and a detailed file type.
 * @param abstract Abstract file type (one of #AbstractFileType).
 * @param detailed Detailed file type (one of #DetailedFileType).
 */
#define MAKE_FIOS_TYPE(abstract, detailed) ((abstract) | ((detailed) << FT_NUMBITS))

/**
 * Elements of a file system that are recognized.
 * Values are a combination of #AbstractFileType and #DetailedFileType.
 * @see GetAbstractFileType GetDetailedFileType
 */
enum FiosType : uint16_t {
	FIOS_TYPE_DRIVE  = MAKE_FIOS_TYPE(FT_NONE, DFT_FIOS_DRIVE),
	FIOS_TYPE_PARENT = MAKE_FIOS_TYPE(FT_NONE, DFT_FIOS_PARENT),
	FIOS_TYPE_DIR    = MAKE_FIOS_TYPE(FT_NONE, DFT_FIOS_DIR),
	FIOS_TYPE_DIRECT = MAKE_FIOS_TYPE(FT_NONE, DFT_FIOS_DIRECT),

	FIOS_TYPE_FILE         = MAKE_FIOS_TYPE(FT_SAVEGAME, DFT_GAME_FILE),
	FIOS_TYPE_OLDFILE      = MAKE_FIOS_TYPE(FT_SAVEGAME, DFT_OLD_GAME_FILE),
	FIOS_TYPE_SCENARIO     = MAKE_FIOS_TYPE(FT_SCENARIO, DFT_GAME_FILE),
	FIOS_TYPE_OLD_SCENARIO = MAKE_FIOS_TYPE(FT_SCENARIO, DFT_OLD_GAME_FILE),
	FIOS_TYPE_PNG          = MAKE_FIOS_TYPE(FT_HEIGHTMAP, DFT_HEIGHTMAP_PNG),
	FIOS_TYPE_BMP          = MAKE_FIOS_TYPE(FT_HEIGHTMAP, DFT_HEIGHTMAP_BMP),
	FIOS_TYPE_JSON         = MAKE_FIOS_TYPE(FT_TOWN_DATA, DFT_TOWN_DATA_JSON),

	FIOS_TYPE_INVALID = MAKE_FIOS_TYPE(FT_INVALID, DFT_INVALID),
};

#undef MAKE_FIOS_TYPE

/**
 * Extract the abstract file type from a #FiosType.
 * @param fios_type Type to query.
 * @return The Abstract file type of the \a fios_type.
 */
inline AbstractFileType GetAbstractFileType(FiosType fios_type)
{
	return static_cast<AbstractFileType>(static_cast<uint>(fios_type) & FT_MASK);
}

/**
 * Extract the detailed file type from a #FiosType.
 * @param fios_type Type to query.
 * @return The Detailed file type of the \a fios_type.
 */
inline DetailedFileType GetDetailedFileType(FiosType fios_type)
{
	return static_cast<DetailedFileType>(fios_type >> FT_NUMBITS);
}

/**
 * The different kinds of subdirectories OpenTTD uses
 */
enum Subdirectory : uint8_t {
	BASE_DIR,      ///< Base directory for all subdirectories
	SAVE_DIR,      ///< Base directory for all savegames
	AUTOSAVE_DIR,  ///< Subdirectory of save for autosaves
	SCENARIO_DIR,  ///< Base directory for all scenarios
	HEIGHTMAP_DIR, ///< Subdirectory of scenario for heightmaps
	OLD_GM_DIR,    ///< Old subdirectory for the music
	OLD_DATA_DIR,  ///< Old subdirectory for the data.
	BASESET_DIR,   ///< Subdirectory for all base data (base sets, intro game)
	NEWGRF_DIR,    ///< Subdirectory for all NewGRFs
	LANG_DIR,      ///< Subdirectory for all translation files
	AI_DIR,        ///< Subdirectory for all %AI files
	AI_LIBRARY_DIR,///< Subdirectory for all %AI libraries
	GAME_DIR,      ///< Subdirectory for all game scripts
	GAME_LIBRARY_DIR, ///< Subdirectory for all GS libraries
	SCREENSHOT_DIR,   ///< Subdirectory for all screenshots
	SOCIAL_INTEGRATION_DIR, ///< Subdirectory for all social integration plugins
	DOCS_DIR,      ///< Subdirectory for documentation
	NUM_SUBDIRS,   ///< Number of subdirectories
	NO_DIRECTORY,  ///< A path without any base directory
};

/**
 * Types of searchpaths OpenTTD might use
 */
enum Searchpath : uint8_t {
	SP_FIRST_DIR,
	SP_WORKING_DIR = SP_FIRST_DIR, ///< Search in the working directory
#ifdef USE_XDG
	SP_PERSONAL_DIR_XDG,           ///< Search in the personal directory from the XDG specification
#endif
	SP_PERSONAL_DIR,               ///< Search in the personal directory
	SP_SHARED_DIR,                 ///< Search in the shared directory, like 'Shared Files' under Windows
	SP_BINARY_DIR,                 ///< Search in the directory where the binary resides
	SP_INSTALLATION_DIR,           ///< Search in the installation directory
	SP_APPLICATION_BUNDLE_DIR,     ///< Search within the application bundle
	SP_AUTODOWNLOAD_DIR,           ///< Search within the autodownload directory
	SP_AUTODOWNLOAD_PERSONAL_DIR,  ///< Search within the autodownload directory located in the personal directory
	SP_AUTODOWNLOAD_PERSONAL_DIR_XDG, ///< Search within the autodownload directory located in the personal directory (XDG variant)
	NUM_SEARCHPATHS
};

DECLARE_INCREMENT_DECREMENT_OPERATORS(Searchpath)

class FileHandle {
public:
	static std::optional<FileHandle> Open(const char *filename, const char *mode);

	static std::optional<FileHandle> Open(const std::string &filename, const char *mode)
	{
		return FileHandle::Open(filename.c_str(), mode);
	}

	inline int Close()
	{
		if (this->f != nullptr) {
			return fclose(this->f.release());
		} else {
			return 0;
		}
	}

	inline operator FILE *() const
	{
		assert(this->f != nullptr);
		return this->f.get();
	}

private:
	/** Helper to close a FILE * with a \c std::unique_ptr. */
	struct FileDeleter {
		void operator ()(FILE *f)
		{
			if (f != nullptr) fclose(f);
		}
	};

	std::unique_ptr<FILE, FileDeleter> f;

	FileHandle(FILE *f) : f(f) { assert(this->f != nullptr); }
};

/* Ensure has_value() is used consistently. */
template <> constexpr std::optional<FileHandle>::operator bool() const noexcept = delete;

#endif /* FILEIO_TYPE_H */
