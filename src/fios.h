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
#include "order_type.h"
#include <vector>


/** Special values for save-load window for the data parameter of #InvalidateWindowData. */
enum SaveLoadInvalidateWindowData : uint8_t {
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
	const FiosItem *FindItem(std::string_view file);
};

enum SortingBits : uint8_t {
	SORT_ASCENDING  = 0,
	SORT_DESCENDING = 1,
	SORT_BY_DATE    = 0,
	SORT_BY_NAME    = 2
};
DECLARE_ENUM_AS_BIT_SET(SortingBits)

/* Variables to display file lists */
extern SortingBits _savegame_sort_order;

struct FiosOrderListInfo {
	const Vehicle * const veh;
	const VehicleOrderID order_insert_index;
	const bool reverse;

	FiosOrderListInfo(const Vehicle *veh, VehicleOrderID order_insert_index = INVALID_VEH_ORDER_ID, bool reverse = false)
			: veh(veh), order_insert_index(order_insert_index), reverse(reverse) {}
};

void ShowSaveLoadDialog(AbstractFileType abstract_filetype, SaveLoadOperation fop, std::optional<FiosOrderListInfo> order_list_info = std::nullopt);

void FiosGetSavegameList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetScenarioList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetHeightmapList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetOrderlistList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);
void FiosGetTownDataList(SaveLoadOperation fop, bool show_dirs, FileList &file_list);

bool FiosBrowseTo(const FiosItem *item);

std::string FiosGetCurrentPath();
std::optional<uint64_t> FiosGetDiskFreeSpace(const std::string &path);
bool FiosDelete(const char *name, AbstractFileType file_type);
std::string FiosMakeHeightmapName(const char *name);
std::string FiosMakeSavegameName(const char *name);
std::string FiosMakeOrderListName(const char *name);

FiosType FiosGetSavegameListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);
FiosType FiosGetScenarioListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);
FiosType FiosGetHeightmapListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);
FiosType FiosGetOrderlistListCallback(SaveLoadOperation fop, const std::string &file, const char *ext, char *title, const char *last);

void ScanScenarios();
std::optional<std::string_view> FindScenario(const ContentInfo &ci, bool md5sum);

/**
 * A savegame name automatically numbered.
 */
struct FiosNumberedSaveName {
	FiosNumberedSaveName(std::string_view prefix);
	std::string Filename();
	std::string FilenameUsingMaxSaves(uint max_saves);
	void FilenameUsingNumber(struct format_target &buffer, int num, const char *suffix) const;
	std::string Extension();
	uint GetLastNumber() const { return this->number; }
	std::string_view GetSavePath() const { return this->save_path; }
private:
	std::string prefix;
	std::string save_path;
	uint number;
};

#endif /* FIOS_H */
