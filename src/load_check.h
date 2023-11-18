/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file load_check.h Load check data. */

#ifndef LOAD_CHECK_H
#define LOAD_CHECK_H

#include "company_base.h"
#include "date_type.h"
#include "gamelog_internal.h"
#include "newgrf_config.h"
#include "strings_type.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include <memory>
#include <string>
#include <vector>

using CompanyPropertiesMap = btree::btree_map<uint, std::unique_ptr<CompanyProperties>>;

/**
 * Container for loading in mode SL_LOAD_CHECK.
 */
struct LoadCheckData {
	bool checkable;     ///< True if the savegame could be checked by SL_LOAD_CHECK. (Old savegames are not checkable.)
	StringID error;     ///< Error message from loading. INVALID_STRING_ID if no error.
	std::string error_msg; ///< Data to pass to SetDParamStr when displaying #error.

	uint32 map_size_x, map_size_y;
	Date current_date;

	GameSettings settings;

	CompanyPropertiesMap companies;               ///< Company information.

	GRFConfig *grfconfig;                         ///< NewGrf configuration from save.
	bool want_grf_compatibility = true;
	GRFListCompatibility grf_compatibility;       ///< Summary state of NewGrfs, whether missing files or only compatible found.

	std::vector<LoggedAction> gamelog_actions;    ///< Gamelog actions

	bool want_debug_data = false;
	std::string debug_log_data;
	std::string debug_config_data;

	bool sl_is_ext_version = false;
	std::string version_name;

	LoadCheckData() : grfconfig(nullptr),
			grf_compatibility(GLC_NOT_FOUND)
	{
		this->Clear();
	}

	/**
	 * Don't leak memory at program exit
	 */
	~LoadCheckData()
	{
		this->Clear();
	}

	/**
	 * Check whether loading the game resulted in errors.
	 * @return true if errors were encountered.
	 */
	bool HasErrors()
	{
		return this->checkable && this->error != INVALID_STRING_ID;
	}

	/**
	 * Check whether the game uses any NewGrfs.
	 * @return true if NewGrfs are used.
	 */
	bool HasNewGrfs()
	{
		return this->checkable && this->error == INVALID_STRING_ID && this->grfconfig != nullptr;
	}

	void Clear();
};

extern LoadCheckData _load_check_data;

#endif /* LOAD_CHECK_H */
