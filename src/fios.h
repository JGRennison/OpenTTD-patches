/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file fios.h Declarations for savegames operations */

#ifndef FIOS_H
#define FIOS_H

#include "gfx_type.h"
#include "company_base.h"
#include "newgrf_config.h"
#include "network/core/tcp_content_type.h"
#include <vector>


/** Special values for save-load window for the data parameter of #InvalidateWindowData. */
enum SaveLoadInvalidateWindowData {
	SLIWD_RESCAN_FILES,          ///< Rescan all files (when changed directory, ...)
	SLIWD_SELECTION_CHANGES,     ///< File selection has changed (user click, ...)
	SLIWD_FILTER_CHANGES,        ///< The filename filter has changed (via the editbox)
};

/** Deals with finding savegames */
struct FiosItem {
	FiosType type;
	uint64_t mtime;
	std::string title;
	std::string name;
	bool operator< (const FiosItem &other) const;
};

/** List of file information. */
class FileList : public std::vector<FiosItem> {
public:
	void BuildFileList(AbstractFileType abstract_filetype, SaveLoadOperation fop, bool show_dirs);
	const FiosItem *FindItem(const std::string_view file);
};

enum SortingBits {
	SORT_ASCENDING  = 0,
	SORT_DESCENDING = 1,
	SORT_BY_DATE    = 0,
	SORT_BY_NAME    = 2
};
DECLARE_ENUM_AS_BIT_SET(SortingBits)

/* Variables to display file lists */
extern SortingBits _savegame_sort_order;

void ShowSaveLoadDialog(AbstractFileType abstract_filetype, SaveLoadOperation fop);

void FiosGetSavegameList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetScenarioList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetHeightmapList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetOrderlistList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);

bool FiosBrowseTo(const FiosItem *item);

std::string FiosGetCurrentPath();
std::optional<uint64_t> FiosGetDiskFreeSpace(const std::string &path);
bool FiosDelete(const char *name);
std::string FiosMakeHeightmapName(const char *name);
std::string FiosMakeSavegameName(const char *name);

FiosType FiosGetSavegameListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);
FiosType FiosGetScenarioListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);
FiosType FiosGetHeightmapListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);
FiosType FiosGetOrderlistListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);

void ScanScenarios();
const char *FindScenario(const ContentInfo *ci, bool md5sum);

/**
 * A savegame name automatically numbered.
 */
struct FiosNumberedSaveName {
	FiosNumberedSaveName(const std::string &prefix);
	std::string Filename();
	std::string FilenameUsingMaxSaves(int max_saves);
	std::string FilenameUsingNumber(int num, const char *suffix) const;
	std::string Extension();
	int GetLastNumber() const { return this->number; }
private:
	std::string prefix;
	int number;
};

#endif /* FIOS_H */
