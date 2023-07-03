/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gamelog_sl.cpp Code handling saving and loading of gamelog data */

#include "../stdafx.h"
#include "../gamelog_internal.h"
#include "../fios.h"
#include "../load_check.h"
#include "../string_func.h"

#include "saveload.h"

#include "../safeguards.h"

static const SaveLoad _glog_action_desc[] = {
	SLE_CONDVAR_X(LoggedAction, tick,       SLE_FILE_U16 | SLE_VAR_U64,  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_U64_TICK_COUNTER, 0, 0)),
	SLE_CONDVAR_X(LoggedAction, tick,       SLE_UINT64,                  SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_U64_TICK_COUNTER)),
};

static const SaveLoad _glog_mode_desc[] = {
	SLE_VAR(LoggedChange, mode.mode,         SLE_UINT8),
	SLE_VAR(LoggedChange, mode.landscape,    SLE_UINT8),
};

static char old_revision_text[GAMELOG_REVISION_LENGTH];

static const SaveLoad _glog_revision_desc[] = {
	SLEG_CONDARR_X(old_revision_text,        SLE_UINT8, GAMELOG_REVISION_LENGTH, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTENDED_GAMELOG, 0, 0)),
	SLE_CONDSTR_X(LoggedChange, revision.text, SLE_STR,                       0, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_EXTENDED_GAMELOG)),
	SLE_VAR(LoggedChange, revision.newgrf,   SLE_UINT32),
	SLE_VAR(LoggedChange, revision.slver,    SLE_UINT16),
	SLE_VAR(LoggedChange, revision.modified, SLE_UINT8),
};

static const SaveLoad _glog_oldver_desc[] = {
	SLE_VAR(LoggedChange, oldver.type,       SLE_UINT32),
	SLE_VAR(LoggedChange, oldver.version,    SLE_UINT32),
};

static const SaveLoad _glog_setting_desc[] = {
	SLE_STR(LoggedChange, setting.name,      SLE_STR,    128),
	SLE_VAR(LoggedChange, setting.oldval,    SLE_INT32),
	SLE_VAR(LoggedChange, setting.newval,    SLE_INT32),
};

static const SaveLoad _glog_grfadd_desc[] = {
	SLE_VAR(LoggedChange, grfadd.grfid,      SLE_UINT32    ),
	SLE_ARR(LoggedChange, grfadd.md5sum,     SLE_UINT8,  16),
};

static const SaveLoad _glog_grfrem_desc[] = {
	SLE_VAR(LoggedChange, grfrem.grfid,      SLE_UINT32),
};

static const SaveLoad _glog_grfcompat_desc[] = {
	SLE_VAR(LoggedChange, grfcompat.grfid,   SLE_UINT32    ),
	SLE_ARR(LoggedChange, grfcompat.md5sum,  SLE_UINT8,  16),
};

static const SaveLoad _glog_grfparam_desc[] = {
	SLE_VAR(LoggedChange, grfparam.grfid,    SLE_UINT32),
};

static const SaveLoad _glog_grfmove_desc[] = {
	SLE_VAR(LoggedChange, grfmove.grfid,     SLE_UINT32),
	SLE_VAR(LoggedChange, grfmove.offset,    SLE_INT32),
};

static const SaveLoad _glog_grfbug_desc[] = {
	SLE_VAR(LoggedChange, grfbug.data,       SLE_UINT64),
	SLE_VAR(LoggedChange, grfbug.grfid,      SLE_UINT32),
	SLE_VAR(LoggedChange, grfbug.bug,        SLE_UINT8),
};

static const SaveLoad _glog_emergency_desc[] = {
	SLE_CONDNULL(0, SL_MIN_VERSION, SL_MIN_VERSION), // Just an empty list, to keep the rest of the code easier.
};

static const SaveLoadTable _glog_desc[] = {
	_glog_mode_desc,
	_glog_revision_desc,
	_glog_oldver_desc,
	_glog_setting_desc,
	_glog_grfadd_desc,
	_glog_grfrem_desc,
	_glog_grfcompat_desc,
	_glog_grfparam_desc,
	_glog_grfmove_desc,
	_glog_grfbug_desc,
	_glog_emergency_desc,
};

static_assert(lengthof(_glog_desc) == GLCT_END);

static void Load_GLOG_common(std::vector<LoggedAction> &gamelog_actions)
{
	assert(gamelog_actions.empty());

	byte type;
	while ((type = SlReadByte()) != GLAT_NONE) {
		if (type >= GLAT_END) SlErrorCorrupt("Invalid gamelog action type");
		GamelogActionType at = (GamelogActionType)type;

		LoggedAction &la = gamelog_actions.emplace_back();

		la.at = at;

		SlObject(&la, _glog_action_desc); // has to be saved after 'DATE'!

		while ((type = SlReadByte()) != GLCT_NONE) {
			if (type >= GLCT_END) SlErrorCorrupt("Invalid gamelog change type");
			GamelogChangeType ct = (GamelogChangeType)type;

			la.changes.push_back({});
			LoggedChange *lc = &la.changes.back();

			lc->ct = ct;
			SlObject(lc, _glog_desc[ct]);

			if (ct == GLCT_REVISION && SlXvIsFeatureMissing(XSLFI_EXTENDED_GAMELOG)) {
				lc->revision.text = stredup(old_revision_text, lastof(old_revision_text));
			}
		}
	}
}

static void Save_GLOG()
{
	SlAutolength([](void *) {
		for (LoggedAction &la : _gamelog_actions) {
			SlWriteByte(la.at);
			SlObject(&la, _glog_action_desc);

			for (LoggedChange &lc : la.changes) {
				SlWriteByte(lc.ct);
				assert((uint)lc.ct < GLCT_END);
				SlObject(&lc, _glog_desc[lc.ct]);
			}
			SlWriteByte(GLCT_NONE);
		}
		SlWriteByte(GLAT_NONE);
	}, nullptr);
}

static void Load_GLOG()
{
	Load_GLOG_common(_gamelog_actions);
}

static void Check_GLOG()
{
	Load_GLOG_common(_load_check_data.gamelog_actions);
}

static const ChunkHandler gamelog_chunk_handlers[] = {
	{ 'GLOG', Save_GLOG, Load_GLOG, nullptr, Check_GLOG, CH_RIFF }
};

extern const ChunkHandlerTable _gamelog_chunk_handlers(gamelog_chunk_handlers);
