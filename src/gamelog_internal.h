/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gamelog_internal.h Declaration shared among gamelog.cpp and saveload/gamelog_sl.cpp */

#ifndef GAMELOG_INTERNAL_H
#define GAMELOG_INTERNAL_H

#include "gamelog.h"
#include "landscape_type.h"

#include <vector>

/** Type of logged change */
enum GamelogChangeType {
	GLCT_MODE,        ///< Scenario editor x Game, different landscape
	GLCT_REVISION,    ///< Changed game revision string
	GLCT_OLDVER,      ///< Loaded from savegame without logged data
	GLCT_SETTING,     ///< Non-networksafe setting value changed
	GLCT_GRFADD,      ///< Removed GRF
	GLCT_GRFREM,      ///< Added GRF
	GLCT_GRFCOMPAT,   ///< Loading compatible GRF
	GLCT_GRFPARAM,    ///< GRF parameter changed
	GLCT_GRFMOVE,     ///< GRF order changed
	GLCT_GRFBUG,      ///< GRF bug triggered
	GLCT_EMERGENCY,   ///< Emergency savegame
	GLCT_END,         ///< So we know how many GLCTs are there
	GLCT_NONE = 0xFF, ///< In savegames, end of list
};


static const uint GAMELOG_REVISION_LENGTH = 15;

/** Contains information about one logged change */
struct LoggedChange {
	GamelogChangeType ct; ///< Type of change logged in this struct
	union {
		struct {
			uint8_t mode;        ///< new game mode - Editor x Game
			LandscapeType landscape; ///< landscape (temperate, arctic, ...)
		} mode;
		struct {
			char *text;          ///< revision string, _openttd_revision
			uint32_t newgrf;     ///< _openttd_newgrf_version
			uint16_t slver;      ///< _sl_version
			uint8_t modified;    ///< _openttd_revision_modified
		} revision;
		struct {
			uint32_t type;       ///< type of savegame, @see SavegameType
			uint32_t version;    ///< major and minor version OR ttdp version
		} oldver;
		GRFIdentifier grfadd;    ///< ID and md5sum of added GRF
		struct {
			uint32_t grfid;      ///< ID of removed GRF
		} grfrem;
		GRFIdentifier grfcompat; ///< ID and new md5sum of changed GRF
		struct {
			uint32_t grfid;      ///< ID of GRF with changed parameters
		} grfparam;
		struct {
			uint32_t grfid;      ///< ID of moved GRF
			int32_t offset;      ///< offset, positive = move down
		} grfmove;
		struct {
			char *name;          ///< name of the setting
			int32_t oldval;      ///< old value
			int32_t newval;      ///< new value
		} setting;
		struct {
			uint64_t data;       ///< additional data
			uint32_t grfid;      ///< ID of problematic GRF
			GRFBug bug;          ///< type of bug, @see enum GRFBugs
		} grfbug;
	};
};


/** Contains information about one logged action that caused at least one logged change */
struct LoggedAction {
	std::vector<LoggedChange> changes; ///< Changes in this action
	GamelogActionType at; ///< Type of action
	uint64_t tick;        ///< Tick when it happened
};

extern std::vector<LoggedAction> _gamelog_actions;

#endif /* GAMELOG_INTERNAL_H */
