/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gamelog.cpp Definition of functions used for logging of fundamental changes to the game */

#include "stdafx.h"
#include "sl/saveload.h"
#include "string_func.h"
#include "string_func_extra.h"
#include "settings_type.h"
#include "gamelog_internal.h"
#include "console_func.h"
#include "debug.h"
#include "date_func.h"
#include "rev.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include "safeguards.h"

extern const SaveLoadVersion SAVEGAME_VERSION;  ///< current savegame version

extern SavegameType _savegame_type; ///< type of savegame we are loading

extern uint32_t _ttdp_version;        ///< version of TTDP savegame (if applicable)
extern SaveLoadVersion _sl_version;   ///< the major savegame version identifier
extern uint8_t _sl_minor_version;     ///< the minor savegame version, DO NOT USE!


static GamelogActionType _gamelog_action_type = GLAT_NONE; ///< action to record if anything changes

std::vector<LoggedAction> _gamelog_actions;     ///< logged actions
static LoggedAction *_current_action = nullptr; ///< current action we are logging, nullptr when there is no action active


/**
 * Stores information about new action, but doesn't allocate it
 * Action is allocated only when there is at least one change
 * @param at type of action
 */
void GamelogStartAction(GamelogActionType at)
{
	assert(_gamelog_action_type == GLAT_NONE); // do not allow starting new action without stopping the previous first
	_gamelog_action_type = at;
}

/**
 * Stops logging of any changes
 */
void GamelogStopAction()
{
	assert(_gamelog_action_type != GLAT_NONE); // nobody should try to stop if there is no action in progress

	bool print = _current_action != nullptr;

	_current_action = nullptr;
	_gamelog_action_type = GLAT_NONE;

	if (print) GamelogPrintDebug(5);
}

void GamelogStopAnyAction()
{
	if (_gamelog_action_type != GLAT_NONE) GamelogStopAction();
}

/**
 * Frees the memory allocated by a gamelog
 */
void GamelogFree(std::vector<LoggedAction> &gamelog_actions)
{
	for (LoggedAction &la : gamelog_actions) {
		for (LoggedChange &lc : la.changes) {
			if (lc.ct == GLCT_SETTING) free(lc.setting.name);
			if (lc.ct == GLCT_REVISION) free(lc.revision.text);
			lc.ct = GLCT_NONE;
		}
	}

	gamelog_actions.clear();
}

/**
 * Resets and frees all memory allocated - used before loading or starting a new game
 */
void GamelogReset()
{
	assert(_gamelog_action_type == GLAT_NONE);
	GamelogFree(_gamelog_actions);
	_current_action  = nullptr;
}

/**
 * Prints GRF ID, checksum and filename if found
 * @param buffer The output buffer
 * @param grfid GRF ID
 * @param md5sum array of md5sum to print, if known
 * @param gc GrfConfig, if known
 */
static void PrintGrfInfo(format_target &buffer, uint grfid, const MD5Hash *md5sum, const GRFConfig *gc)
{
	if (md5sum != nullptr) {
		buffer.format("GRF ID {:08X}, checksum {}", std::byteswap(grfid), *md5sum);
	} else {
		buffer.format("GRF ID {:08X}", std::byteswap(grfid));
	}

	if (gc != nullptr) {
		buffer.format(", filename: {} (md5sum matches)", gc->filename);
	} else {
		gc = FindGRFConfig(grfid, FGCM_ANY);
		if (gc != nullptr) {
			buffer.format(", filename: {} (matches GRFID only)", gc->filename);
		} else {
			buffer.append(", unknown GRF");
		}
	}
}


/** Text messages for various logged actions */
static const char * const la_text[] = {
	"new game started",
	"game loaded",
	"GRF config changed",
	"cheat was used",
	"settings changed",
	"GRF bug triggered",
	"emergency savegame",
};

static_assert(lengthof(la_text) == GLAT_END);

/**
 * Information about the presence of a Grf at a certain point during gamelog history
 * Note about missing Grfs:
 * Changes to missing Grfs are not logged including manual removal of the Grf.
 * So if the gamelog tells a Grf is missing we do not know whether it was readded or completely removed
 * at some later point.
 */
struct GRFPresence{
	const GRFConfig *gc;  ///< GRFConfig, if known
	bool was_missing;     ///< Grf was missing during some gameload in the past

	GRFPresence(const GRFConfig *gc) : gc(gc), was_missing(false) {}
	GRFPresence() = default;
};
typedef btree::btree_map<uint32_t, GRFPresence> GrfIDMapping;

/**
 * Prints active gamelog
 * @param proc the procedure to draw with
 */
void GamelogPrint(format_target &buffer)
{
	GrfIDMapping grf_names;

	buffer.append("---- gamelog start ----\n");

	for (const LoggedAction &la : _gamelog_actions) {
		assert((uint)la.at < GLAT_END);

		buffer.format("Tick {}: {}\n", la.tick, la_text[(uint)la.at]);

		for (const LoggedChange &lchange : la.changes) {
			const LoggedChange *lc = &lchange;

			switch (lc->ct) {
				default: NOT_REACHED();
				case GLCT_MODE:
					/* Changing landscape, or going from scenario editor to game or back. */
					buffer.format("New game mode: {} landscape: {}",
						(uint)lc->mode.mode, (uint)lc->mode.landscape);
					break;

				case GLCT_REVISION:
					/* The game was loaded in a different version than before. */
					buffer.format("Revision text changed to {}, savegame version {}, ",
						lc->revision.text, lc->revision.slver);

					switch (lc->revision.modified) {
						case 0: buffer.append("not "); break;
						case 1: buffer.append("maybe "); break;
						default: break;
					}

					buffer.format("modified, _openttd_newgrf_version = 0x{:08x}", lc->revision.newgrf);
					break;

				case GLCT_OLDVER:
					/* The game was loaded from before 0.7.0-beta1. */
					buffer.append("Conversion from ");
					switch (lc->oldver.type) {
						default: NOT_REACHED();
						case SGT_OTTD:
							buffer.format("OTTD savegame without gamelog: version {}, {}",
								GB(lc->oldver.version, 8, 16), GB(lc->oldver.version, 0, 8));
							break;

						case SGT_TTO:
							buffer.append("TTO savegame");
							break;

						case SGT_TTD:
							buffer.append("TTD savegame");
							break;

						case SGT_TTDP1:
						case SGT_TTDP2:
							buffer.format("TTDP savegame, {} format",
								lc->oldver.type == SGT_TTDP1 ? "old" : "new");
							if (lc->oldver.version != 0) {
								buffer.format(", TTDP version {}.{}.{}.{}",
									GB(lc->oldver.version, 24, 8), GB(lc->oldver.version, 20, 4),
									GB(lc->oldver.version, 16, 4), GB(lc->oldver.version, 0, 16));
							}
							break;
					}
					break;

				case GLCT_SETTING:
					/* A setting with the SF_NO_NETWORK flag got changed; these settings usually affect NewGRFs, such as road side or wagon speed limits. */
					buffer.format("Setting changed: {} : {} -> {}", lc->setting.name, lc->setting.oldval, lc->setting.newval);
					break;

				case GLCT_GRFADD: {
					/* A NewGRF got added to the game, either at the start of the game (never an issue), or later on when it could be an issue. */
					const GRFConfig *gc = FindGRFConfig(lc->grfadd.grfid, FGCM_EXACT, &lc->grfadd.md5sum);
					buffer.append("Added NewGRF: ");
					PrintGrfInfo(buffer, lc->grfadd.grfid, &lc->grfadd.md5sum, gc);
					auto gm = grf_names.find(lc->grfrem.grfid);
					if (gm != grf_names.end() && !gm->second.was_missing) buffer.append(". Gamelog inconsistency: GrfID was already added!");
					grf_names[lc->grfadd.grfid] = gc;
					break;
				}

				case GLCT_GRFREM: {
					/* A NewGRF got removed from the game, either manually or by it missing when loading the game. */
					auto gm = grf_names.find(lc->grfrem.grfid);
					buffer.append(la.at == GLAT_LOAD ? "Missing NewGRF: " : "Removed NewGRF: ");
					PrintGrfInfo(buffer, lc->grfrem.grfid, nullptr, gm != grf_names.end() ? gm->second.gc : nullptr);
					if (gm == grf_names.end()) {
						buffer.append(". Gamelog inconsistency: GrfID was never added!");
					} else {
						if (la.at == GLAT_LOAD) {
							/* Missing grfs on load are not removed from the configuration */
							gm->second.was_missing = true;
						} else {
							grf_names.erase(gm);
						}
					}
					break;
				}

				case GLCT_GRFCOMPAT: {
					/* Another version of the same NewGRF got loaded. */
					const GRFConfig *gc = FindGRFConfig(lc->grfadd.grfid, FGCM_EXACT, &lc->grfadd.md5sum);
					buffer.append("Compatible NewGRF loaded: ");
					PrintGrfInfo(buffer, lc->grfcompat.grfid, &lc->grfcompat.md5sum, gc);
					if (grf_names.find(lc->grfcompat.grfid) == grf_names.end()) buffer.append(". Gamelog inconsistency: GrfID was never added!");
					grf_names[lc->grfcompat.grfid] = gc;
					break;
				}

				case GLCT_GRFPARAM: {
					/* A parameter of a NewGRF got changed after the game was started. */
					auto gm = grf_names.find(lc->grfrem.grfid);
					buffer.append("GRF parameter changed: ");
					PrintGrfInfo(buffer, lc->grfparam.grfid, nullptr, gm != grf_names.end() ? gm->second.gc : nullptr);
					if (gm == grf_names.end()) buffer.append(". Gamelog inconsistency: GrfID was never added!");
					break;
				}

				case GLCT_GRFMOVE: {
					/* The order of NewGRFs got changed, which might cause some other NewGRFs to behave differently. */
					auto gm = grf_names.find(lc->grfrem.grfid);
					buffer.format("GRF order changed: {:08X} moved {} places {}",
						std::byteswap(lc->grfmove.grfid), abs(lc->grfmove.offset), lc->grfmove.offset >= 0 ? "down" : "up" );
					PrintGrfInfo(buffer, lc->grfmove.grfid, nullptr, gm != grf_names.end() ? gm->second.gc : nullptr);
					if (gm == grf_names.end()) buffer.append(". Gamelog inconsistency: GrfID was never added!");
					break;
				}

				case GLCT_GRFBUG: {
					/* A specific bug in a NewGRF, that could cause wide spread problems, has been noted during the execution of the game. */
					auto gm = grf_names.find(lc->grfrem.grfid);
					assert (lc->grfbug.bug == GRFBug::VehLength);

					buffer.format("Rail vehicle changes length outside a depot: GRF ID {:08X}, internal ID 0x{:X}", std::byteswap(lc->grfbug.grfid), lc->grfbug.data);
					PrintGrfInfo(buffer, lc->grfbug.grfid, nullptr, gm != grf_names.end() ? gm->second.gc : nullptr);
					if (gm == grf_names.end()) buffer.append(". Gamelog inconsistency: GrfID was never added!");
					break;
				}

				case GLCT_EMERGENCY:
					/* At one point the savegame was made during the handling of a game crash.
					 * The generic code already mentioned the emergency savegame, and there is no extra information to log. */
					break;
			}

			buffer.push_back('\n');
		}
	}

	buffer.append("---- gamelog end ----\n");
}

/** Print the gamelog data to the console. */
void GamelogPrintConsole()
{
	format_buffer buffer;
	GamelogPrint(buffer);
	ProcessLineByLine(buffer, [&](std::string_view line) {
		IConsolePrint(CC_WARNING, std::string{line});
	});
}

/**
 * Prints gamelog to debug output.
 * @param level debug level we need to print stuff
 */
void GamelogPrintDebug(int level)
{
	if (level != 0 && GetDebugLevel(DebugLevelID::gamelog) < level) return;

	format_buffer buffer;
	GamelogPrint(buffer);
	ProcessLineByLine(buffer, [&](std::string_view line) {
		debug_print(DebugLevelID::gamelog, level, line);
	});
}


/**
 * Allocates new LoggedChange and new LoggedAction if needed.
 * If there is no action active, nullptr is returned.
 * @param ct type of change
 * @return new LoggedChange, or nullptr if there is no action active
 */
static LoggedChange *GamelogChange(GamelogChangeType ct)
{
	if (_current_action == nullptr) {
		if (_gamelog_action_type == GLAT_NONE) return nullptr;

		_current_action  = &_gamelog_actions.emplace_back();

		_current_action->at      = _gamelog_action_type;
		_current_action->tick    = _tick_counter;
	}

	_current_action->changes.push_back({});
	LoggedChange *lc = &_current_action->changes.back();
	lc->ct = ct;

	return lc;
}


/**
 * Logs a emergency savegame
 */
void GamelogEmergency()
{
	/* Terminate any active action */
	if (_gamelog_action_type != GLAT_NONE) GamelogStopAction();
	GamelogStartAction(GLAT_EMERGENCY);
	GamelogChange(GLCT_EMERGENCY);
	GamelogStopAction();
}

/**
 * Finds out if current game is a loaded emergency savegame.
 */
bool GamelogTestEmergency()
{
	for (LoggedAction &la : _gamelog_actions) {
		for (LoggedChange &lc : la.changes) {
			if (lc.ct == GLCT_EMERGENCY) return true;
		}
	}

	return false;
}

/**
 * Logs a change in game revision
 */
void GamelogRevision()
{
	assert(_gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_LOAD);

	LoggedChange *lc = GamelogChange(GLCT_REVISION);
	if (lc == nullptr) return;

	lc->revision.text = stredup(_openttd_revision);
	lc->revision.slver = SAVEGAME_VERSION;
	lc->revision.modified = _openttd_revision_modified;
	lc->revision.newgrf = _openttd_newgrf_version;
}

/**
 * Logs a change in game mode (scenario editor or game)
 */
void GamelogMode()
{
	assert(_gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_CHEAT);

	LoggedChange *lc = GamelogChange(GLCT_MODE);
	if (lc == nullptr) return;

	lc->mode.mode      = _game_mode;
	lc->mode.landscape = _settings_game.game_creation.landscape;
}

/**
 * Logs loading from savegame without gamelog
 */
void GamelogOldver()
{
	assert(_gamelog_action_type == GLAT_LOAD);

	LoggedChange *lc = GamelogChange(GLCT_OLDVER);
	if (lc == nullptr) return;

	lc->oldver.type = _savegame_type;
	lc->oldver.version = (_savegame_type == SGT_OTTD ? ((uint32_t)_sl_version << 8 | _sl_minor_version) : _ttdp_version);
}

/**
 * Logs change in game settings. Only non-networksafe settings are logged
 * @param name setting name
 * @param oldval old setting value
 * @param newval new setting value
 */
void GamelogSetting(const char *name, int32_t oldval, int32_t newval)
{
	assert(_gamelog_action_type == GLAT_SETTING);

	LoggedChange *lc = GamelogChange(GLCT_SETTING);
	if (lc == nullptr) return;

	lc->setting.name = stredup(name);
	lc->setting.oldval = oldval;
	lc->setting.newval = newval;
}


/**
 * Finds out if current revision is different than last revision stored in the savegame.
 * Appends GLCT_REVISION when the revision string changed
 */
void GamelogTestRevision()
{
	const LoggedChange *rev = nullptr;

	for (LoggedAction &la : _gamelog_actions) {
		for (LoggedChange &lc : la.changes) {
			if (lc.ct == GLCT_REVISION) rev = &lc;
		}
	}

	if (rev == nullptr || strcmp(rev->revision.text, _openttd_revision) != 0 ||
			rev->revision.modified != _openttd_revision_modified ||
			rev->revision.newgrf != _openttd_newgrf_version) {
		GamelogRevision();
	}
}

/**
 * Finds last stored game mode or landscape.
 * Any change is logged
 */
void GamelogTestMode()
{
	const LoggedChange *mode = nullptr;

	for (LoggedAction &la : _gamelog_actions) {
		for (LoggedChange &lc : la.changes) {
			if (lc.ct == GLCT_MODE) mode = &lc;
		}
	}

	if (mode == nullptr || mode->mode.mode != _game_mode || mode->mode.landscape != _settings_game.game_creation.landscape) GamelogMode();
}


/**
 * Logs triggered GRF bug.
 * @param grfid ID of problematic GRF
 * @param bug type of bug, @see enum GRFBugs
 * @param data additional data
 */
static void GamelogGRFBug(uint32_t grfid, GRFBug bug, uint64_t data)
{
	assert(_gamelog_action_type == GLAT_GRFBUG);

	LoggedChange *lc = GamelogChange(GLCT_GRFBUG);
	if (lc == nullptr) return;

	lc->grfbug.data  = data;
	lc->grfbug.grfid = grfid;
	lc->grfbug.bug   = bug;
}

/**
 * Logs GRF bug - rail vehicle has different length after reversing.
 * Ensures this is logged only once for each GRF and engine type
 * This check takes some time, but it is called pretty seldom, so it
 * doesn't matter that much (ideally it shouldn't be called at all).
 * @param grfid the broken NewGRF
 * @param internal_id the internal ID of whatever's broken in the NewGRF
 * @return true iff a unique record was done
 */
bool GamelogGRFBugReverse(uint32_t grfid, uint16_t internal_id)
{
	for (LoggedAction &la : _gamelog_actions) {
		for (LoggedChange &lc : la.changes) {
			if (lc.ct == GLCT_GRFBUG && lc.grfbug.grfid == grfid &&
					lc.grfbug.bug == GRFBug::VehLength && lc.grfbug.data == internal_id) {
				return false;
			}
		}
	}

	GamelogStartAction(GLAT_GRFBUG);
	GamelogGRFBug(grfid, GRFBug::VehLength, internal_id);
	GamelogStopAction();

	return true;
}


/**
 * Decides if GRF should be logged
 * @param g grf to determine
 * @return true iff GRF is not static and is loaded
 */
static inline bool IsLoggableGrfConfig(const GRFConfig &g)
{
	return !g.flags.Test(GRFConfigFlag::Static) && g.status != GCS_NOT_FOUND;
}

/**
 * Logs removal of a GRF
 * @param grfid ID of removed GRF
 */
void GamelogGRFRemove(uint32_t grfid)
{
	assert(_gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFREM);
	if (lc == nullptr) return;

	lc->grfrem.grfid = grfid;
}

/**
 * Logs adding of a GRF
 * @param newg added GRF
 */
void GamelogGRFAdd(const GRFConfig &newg)
{
	assert(_gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_GRF);

	if (!IsLoggableGrfConfig(newg)) return;

	LoggedChange *lc = GamelogChange(GLCT_GRFADD);
	if (lc == nullptr) return;

	lc->grfadd = newg.ident;
}

/**
 * Logs loading compatible GRF
 * (the same ID, but different MD5 hash)
 * @param newg new (updated) GRF
 */
void GamelogGRFCompatible(const GRFIdentifier &newg)
{
	assert(_gamelog_action_type == GLAT_LOAD || _gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFCOMPAT);
	if (lc == nullptr) return;

	lc->grfcompat = newg;
}

/**
 * Logs changing GRF order
 * @param grfid GRF that is moved
 * @param offset how far it is moved, positive = moved down
 */
static void GamelogGRFMove(uint32_t grfid, int32_t offset)
{
	assert(_gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFMOVE);
	if (lc == nullptr) return;

	lc->grfmove.grfid  = grfid;
	lc->grfmove.offset = offset;
}

/**
 * Logs change in GRF parameters.
 * Details about parameters changed are not stored
 * @param grfid ID of GRF to store
 */
static void GamelogGRFParameters(uint32_t grfid)
{
	assert(_gamelog_action_type == GLAT_GRF);

	LoggedChange *lc = GamelogChange(GLCT_GRFPARAM);
	if (lc == nullptr) return;

	lc->grfparam.grfid = grfid;
}

/**
 * Logs adding of list of GRFs.
 * Useful when old savegame is loaded or when new game is started
 * @param newg the GRFConfigList.
 */
void GamelogGRFAddList(const GRFConfigList &newg)
{
	assert(_gamelog_action_type == GLAT_START || _gamelog_action_type == GLAT_LOAD);

	for (const auto &gc : newg) {
		GamelogGRFAdd(*gc);
	}
}

/**
 * Generates GRFList
 * @param grfc the GRFConfigList.
 */
static std::vector<const GRFConfig *> GenerateGRFList(const GRFConfigList &grfc)
{
	std::vector<const GRFConfig *> list;
	for (const auto &g : grfc) {
		if (IsLoggableGrfConfig(*g)) list.push_back(g.get());
	}

	return list;
}

/**
 * Compares two NewGRF lists and logs any change
 * @param oldc original GRF list
 * @param newc new GRF list
 */
void GamelogGRFUpdate(const GRFConfigList &oldc, const GRFConfigList &newc)
{
	std::vector<const GRFConfig *> ol = GenerateGRFList(oldc);
	std::vector<const GRFConfig *> nl = GenerateGRFList(newc);

	size_t o = 0;
	size_t n = 0;

	while (o < ol.size() && n < nl.size()) {
		const GRFConfig *og = ol[o];
		const GRFConfig *ng = nl[n];

		if (og->ident.grfid != ng->ident.grfid) {
			size_t oi, ni;
			for (oi = 0; oi < ol.size(); oi++) {
				if (ol[oi]->ident.grfid == nl[n]->ident.grfid) break;
			}
			if (oi < o) {
				/* GRF was moved, this change has been logged already */
				n++;
				continue;
			}
			if (oi == ol.size()) {
				/* GRF couldn't be found in the OLD list, GRF was ADDED */
				GamelogGRFAdd(*nl[n++]);
				continue;
			}
			for (ni = 0; ni < nl.size(); ni++) {
				if (nl[ni]->ident.grfid == ol[o]->ident.grfid) break;
			}
			if (ni < n) {
				/* GRF was moved, this change has been logged already */
				o++;
				continue;
			}
			if (ni == nl.size()) {
				/* GRF couldn't be found in the NEW list, GRF was REMOVED */
				GamelogGRFRemove(ol[o++]->ident.grfid);
				continue;
			}

			/* o < oi < ol->n
			 * n < ni < nl->n */
			assert(ni > n && ni < nl.size());
			assert(oi > o && oi < ol.size());

			ni -= n; // number of GRFs it was moved downwards
			oi -= o; // number of GRFs it was moved upwards

			if (ni >= oi) { // prefer the one that is moved further
				/* GRF was moved down */
				GamelogGRFMove(ol[o++]->ident.grfid, (int)ni);
			} else {
				GamelogGRFMove(nl[n++]->ident.grfid, -(int)oi);
			}
		} else {
			if (og->ident.md5sum != ng->ident.md5sum) {
				/* md5sum changed, probably loading 'compatible' GRF */
				GamelogGRFCompatible(nl[n]->ident);
			}

			if (og->param != ng->param) {
				GamelogGRFParameters(ol[o]->ident.grfid);
			}

			o++;
			n++;
		}
	}

	while (o < ol.size()) GamelogGRFRemove(ol[o++]->ident.grfid); // remaining GRFs were removed ...
	while (n < nl.size()) GamelogGRFAdd(*nl[n++]);                // ... or added
}

/**
 * Get some basic information from the given gamelog.
 * @param gamelog_action Pointer to the gamelog to extract information from.
 * @param gamelog_actions Number of actions in the given gamelog.
 * @param[out] last_ottd_rev OpenTTD NewGRF version from the binary that saved the savegame last.
 * @param[out] ever_modified Max value of 'modified' from all binaries that ever saved this savegame.
 * @param[out] removed_newgrfs Set to true if any NewGRFs have been removed.
 */
void GamelogInfo(const std::vector<LoggedAction> &gamelog_actions, uint32_t *last_ottd_rev, uint8_t *ever_modified, bool *removed_newgrfs)
{
	for (const LoggedAction &la : gamelog_actions) {
		for (const LoggedChange &lc : la.changes) {
			switch (lc.ct) {
				default: break;

				case GLCT_REVISION:
					*last_ottd_rev = lc.revision.newgrf;
					*ever_modified = std::max(*ever_modified, lc.revision.modified);
					break;

				case GLCT_GRFREM:
					*removed_newgrfs = true;
					break;
			}
		}
	}
}

const char *GamelogGetLastRevision(const std::vector<LoggedAction> &gamelog_actions)
{
	for (size_t i = gamelog_actions.size(); i > 0; i--) {
		const LoggedAction &la = gamelog_actions[i - 1];
		for (const LoggedChange &lc : la.changes) {
			switch (lc.ct) {
				case GLCT_REVISION:
					return lc.revision.text;
					break;

				default:
					break;
			}
		}
	}
	return nullptr;
}
