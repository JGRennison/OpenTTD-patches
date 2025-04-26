/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gamelog.h Functions to be called to log fundamental changes to the game */

#ifndef GAMELOG_H
#define GAMELOG_H

#include "newgrf_config.h"
#include <vector>

struct LoggedAction;

/** The actions we log. */
enum GamelogActionType : uint8_t {
	GLAT_START,        ///< Game created
	GLAT_LOAD,         ///< Game loaded
	GLAT_GRF,          ///< GRF changed
	GLAT_CHEAT,        ///< Cheat was used
	GLAT_SETTING,      ///< Setting changed
	GLAT_GRFBUG,       ///< GRF bug was triggered
	GLAT_EMERGENCY,    ///< Emergency savegame
	GLAT_END,          ///< So we know how many GLATs are there
	GLAT_NONE  = 0xFF, ///< No logging active; in savegames, end of list
};

void GamelogStartAction(GamelogActionType at);
void GamelogStopAction();
void GamelogStopAnyAction();

void GamelogFree(std::vector<LoggedAction> &gamelog_actions);
void GamelogReset();

void GamelogPrint(struct format_target &buffer);

void GamelogPrintDebug(int level);
void GamelogPrintConsole();

void GamelogEmergency();
bool GamelogTestEmergency();

void GamelogRevision();
void GamelogMode();
void GamelogOldver();
void GamelogSetting(const char *name, int32_t oldval, int32_t newval);

void GamelogGRFUpdate(const GRFConfigList &oldg, const GRFConfigList &newg);
void GamelogGRFAddList(const GRFConfigList &newg);
void GamelogGRFRemove(uint32_t grfid);
void GamelogGRFAdd(const GRFConfig &newg);
void GamelogGRFCompatible(const GRFIdentifier &newg);

void GamelogTestRevision();
void GamelogTestMode();

bool GamelogGRFBugReverse(uint32_t grfid, uint16_t internal_id);

void GamelogInfo(const std::vector<LoggedAction> &gamelog_actions, uint32_t *last_ottd_rev, uint8_t *ever_modified, bool *removed_newgrfs);
const char *GamelogGetLastRevision(const std::vector<LoggedAction> &gamelog_actions);

#endif /* GAMELOG_H */
