/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file afterload.cpp Code updating data after game load */

#include "../stdafx.h"
#include "../void_map.h"
#include "../signs_base.h"
#include "../depot_base.h"
#include "../tunnel_base.h"
#include "../fios.h"
#include "../gamelog_internal.h"
#include "../network/network.h"
#include "../network/network_func.h"
#include "../gfxinit.h"
#include "../viewport_func.h"
#include "../viewport_kdtree.h"
#include "../industry.h"
#include "../clear_map.h"
#include "../vehicle_func.h"
#include "../string_func.h"
#include "../date_func.h"
#include "../roadveh.h"
#include "../train.h"
#include "../station_base.h"
#include "../waypoint_base.h"
#include "../roadstop_base.h"
#include "../tunnelbridge.h"
#include "../tunnelbridge_map.h"
#include "../pathfinder/yapf/yapf_cache.h"
#include "../elrail_func.h"
#include "../signs_func.h"
#include "../aircraft.h"
#include "../object_map.h"
#include "../object_base.h"
#include "../tree_map.h"
#include "../company_func.h"
#include "../road_cmd.h"
#include "../ai/ai.hpp"
#include "../script/script_gui.h"
#include "../game/game.hpp"
#include "../town.h"
#include "../economy_base.h"
#include "../animated_tile_func.h"
#include "../subsidy_base.h"
#include "../subsidy_func.h"
#include "../newgrf.h"
#include "../newgrf_station.h"
#include "../engine_func.h"
#include "../rail_gui.h"
#include "../road_gui.h"
#include "../core/backup_type.hpp"
#include "../core/mem_func.hpp"
#include "../smallmap_gui.h"
#include "../news_func.h"
#include "../order_backup.h"
#include "../error.h"
#include "../disaster_vehicle.h"
#include "../ship.h"
#include "../tracerestrict.h"
#include "../tunnel_map.h"
#include "../bridge_signal_map.h"
#include "../water.h"
#include "../settings_func.h"
#include "../animated_tile.h"
#include "../company_func.h"
#include "../infrastructure_func.h"
#include "../event_logs.h"
#include "../newgrf_object.h"
#include "../newgrf_industrytiles.h"
#include "../timer/timer.h"
#include "../timer/timer_game_tick.h"
#include "../pathfinder/water_regions.h"


#include "../sl/saveload_internal.h"

#include <signal.h>
#include <algorithm>

#include "../safeguards.h"

extern bool IndividualRoadVehicleController(RoadVehicle *v, const RoadVehicle *prev);

/**
 * Makes a tile canal or water depending on the surroundings.
 *
 * Must only be used for converting old savegames. Use WaterClass now.
 *
 * This as for example docks and shipdepots do not store
 * whether the tile used to be canal or 'normal' water.
 * @param t the tile to change.
 * @param include_invalid_water_class Also consider WATER_CLASS_INVALID, i.e. industry tiles on land
 */
void SetWaterClassDependingOnSurroundings(TileIndex t, bool include_invalid_water_class)
{
	/* If the slope is not flat, we always assume 'land' (if allowed). Also for one-corner-raised-shores.
	 * Note: Wrt. autosloping under industry tiles this is the most fool-proof behaviour. */
	if (!IsTileFlat(t)) {
		if (include_invalid_water_class) {
			SetWaterClass(t, WATER_CLASS_INVALID);
			return;
		} else {
			SlErrorCorrupt("Invalid water class for dry tile");
		}
	}

	/* Mark tile dirty in all cases */
	MarkTileDirtyByTile(t);

	if (TileX(t) == 0 || TileY(t) == 0 || TileX(t) == MapMaxX() - 1 || TileY(t) == MapMaxY() - 1) {
		/* tiles at map borders are always WATER_CLASS_SEA */
		SetWaterClass(t, WATER_CLASS_SEA);
		return;
	}

	bool has_water = false;
	bool has_canal = false;
	bool has_river = false;

	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		TileIndex neighbour = TileAddByDiagDir(t, dir);
		switch (GetTileType(neighbour)) {
			case MP_WATER:
				/* clear water and shipdepots have already a WaterClass associated */
				if (IsCoast(neighbour)) {
					has_water = true;
				} else if (!IsLock(neighbour)) {
					switch (GetWaterClass(neighbour)) {
						case WATER_CLASS_SEA:   has_water = true; break;
						case WATER_CLASS_CANAL: has_canal = true; break;
						case WATER_CLASS_RIVER: has_river = true; break;
						default: SlErrorCorrupt("Invalid water class for tile");
					}
				}
				break;

			case MP_RAILWAY:
				/* Shore or flooded halftile */
				has_water |= (GetRailGroundType(neighbour) == RAIL_GROUND_WATER);
				break;

			case MP_TREES:
				/* trees on shore */
				has_water |= (GB(_m[neighbour].m2, 4, 2) == TREE_GROUND_SHORE);
				break;

			default: break;
		}
	}

	if (!has_water && !has_canal && !has_river && include_invalid_water_class) {
		SetWaterClass(t, WATER_CLASS_INVALID);
		return;
	}

	if (has_river && !has_canal) {
		SetWaterClass(t, WATER_CLASS_RIVER);
	} else if (has_canal || !has_water) {
		SetWaterClass(t, WATER_CLASS_CANAL);
	} else {
		SetWaterClass(t, WATER_CLASS_SEA);
	}
}

static void ConvertTownOwner()
{
	for (TileIndex tile = 0; tile != MapSize(); tile++) {
		switch (GetTileType(tile)) {
			case MP_ROAD:
				if (GB(_m[tile].m5, 4, 2) == ROAD_TILE_CROSSING && HasBit(_m[tile].m3, 7)) {
					_m[tile].m3 = OWNER_TOWN;
				}
				[[fallthrough]];

			case MP_TUNNELBRIDGE:
				if (_m[tile].m1 & 0x80) SetTileOwner(tile, OWNER_TOWN);
				break;

			default: break;
		}
	}
}

/* since savegame version 4.1, exclusive transport rights are stored at towns */
static void UpdateExclusiveRights()
{
	for (Town *t : Town::Iterate()) {
		t->exclusivity = INVALID_COMPANY;
	}

	/* FIXME old exclusive rights status is not being imported (stored in s->blocked_months_obsolete)
	 *   could be implemented this way:
	 * 1.) Go through all stations
	 *     Build an array town_blocked[ town_id ][ company_id ]
	 *     that stores if at least one station in that town is blocked for a company
	 * 2.) Go through that array, if you find a town that is not blocked for
	 *     one company, but for all others, then give it exclusivity.
	 */
}

static const byte convert_currency[] = {
	 0,  1, 12,  8,  3,
	10, 14, 19,  4,  5,
	 9, 11, 13,  6, 17,
	16, 22, 21,  7, 15,
	18,  2, 20,
};

/* since savegame version 4.2 the currencies are arranged differently */
static void UpdateCurrencies()
{
	_settings_game.locale.currency = convert_currency[_settings_game.locale.currency];
}

/* Up to revision 1413 the invisible tiles at the southern border have not been
 * MP_VOID, even though they should have. This is fixed by this function
 */
static void UpdateVoidTiles()
{
	for (uint x = 0; x < MapSizeX(); x++) MakeVoid(TileXY(x, MapMaxY()));
	for (uint y = 0; y < MapSizeY(); y++) MakeVoid(TileXY(MapMaxX(), y));
}

static inline RailType UpdateRailType(RailType rt, RailType min)
{
	return rt >= min ? (RailType)(rt + 1): rt;
}

/**
 * Update the viewport coordinates of all signs.
 */
void UpdateAllVirtCoords()
{
	if (IsHeadless()) return;
	UpdateAllStationVirtCoords();
	UpdateAllSignVirtCoords();
	UpdateAllTownVirtCoords();
	UpdateAllTextEffectVirtCoords();
	RebuildViewportKdtree();
}

void ClearAllCachedNames()
{
	ClearAllStationCachedNames();
	ClearAllTownCachedNames();
	ClearAllIndustryCachedNames();
}

/**
 * Initialization of the windows and several kinds of caches.
 * This is not done directly in AfterLoadGame because these
 * functions require that all saveload conversions have been
 * done. As people tend to add savegame conversion stuff after
 * the initialization of the windows and caches quite some bugs
 * had been made.
 * Moving this out of there is both cleaner and less bug-prone.
 */
static void InitializeWindowsAndCaches()
{
	SetupTimeSettings();

	/* Initialize windows */
	ResetWindowSystem();
	SetupColoursAndInitialWindow();

	/* Update coordinates of the signs. */
	ClearAllCachedNames();
	UpdateAllVirtCoords();
	ResetViewportAfterLoadGame();

	for (Company *c : Company::Iterate()) {
		/* For each company, verify (while loading a scenario) that the inauguration date is the current year and set it
		 * accordingly if it is not the case.  No need to set it on companies that are not been used already,
		 * thus the MIN_YEAR (which is really nothing more than Zero, initialized value) test */
		if (_file_to_saveload.abstract_ftype == FT_SCENARIO && c->inaugurated_year != CalTime::MIN_YEAR) {
			c->inaugurated_year = CalTime::CurYear();
			c->display_inaugurated_period = EconTime::Detail::WallClockYearToDisplay(EconTime::CurYear());
			c->age_years = 0;
		}
	}

	/* Count number of objects per type */
	for (Object *o : Object::Iterate()) {
		Object::IncTypeCount(o->type);
	}

	/* Identify owners of persistent storage arrays */
	for (Industry *i : Industry::Iterate()) {
		if (i->psa != nullptr) {
			i->psa->feature = GSF_INDUSTRIES;
			i->psa->tile = i->location.tile;
		}
	}
	for (Station *s : Station::Iterate()) {
		if (s->airport.psa != nullptr) {
			s->airport.psa->feature = GSF_AIRPORTS;
			s->airport.psa->tile = s->airport.tile;
		}
	}
	for (Town *t : Town::Iterate()) {
		for (auto &it : t->psa_list) {
			it->feature = GSF_FAKE_TOWNS;
			it->tile = t->xy;
		}
	}
	for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
		rv->CargoChanged();
	}

	RecomputePrices();

	GroupStatistics::UpdateAfterLoad();

	RebuildSubsidisedSourceAndDestinationCache();

	/* Towns have a noise controlled number of airports system
	 * So each airport's noise value must be added to the town->noise_reached value
	 * Reset each town's noise_reached value to '0' before. */
	UpdateAirportsNoise();

	CheckTrainsLengths();
	ShowNewGRFError();

	/* Rebuild the smallmap list of owners. */
	BuildOwnerLegend();
}

#ifdef WITH_SIGACTION
static struct sigaction _prev_segfault;
static struct sigaction _prev_abort;
static struct sigaction _prev_fpe;

static void CDECL HandleSavegameLoadCrash(int signum, siginfo_t *si, void *context);
#else
typedef void (CDECL *SignalHandlerPointer)(int);
static SignalHandlerPointer _prev_segfault = nullptr;
static SignalHandlerPointer _prev_abort    = nullptr;
static SignalHandlerPointer _prev_fpe      = nullptr;

static void CDECL HandleSavegameLoadCrash(int signum);
#endif

/**
 * Replaces signal handlers of SIGSEGV and SIGABRT
 * and stores pointers to original handlers in memory.
 */
static void SetSignalHandlers()
{
#ifdef WITH_SIGACTION
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = HandleSavegameLoadCrash;
	sigaction(SIGSEGV, &sa, &_prev_segfault);
	sigaction(SIGABRT, &sa, &_prev_abort);
	sigaction(SIGFPE,  &sa, &_prev_fpe);
#else
	_prev_segfault = signal(SIGSEGV, HandleSavegameLoadCrash);
	_prev_abort    = signal(SIGABRT, HandleSavegameLoadCrash);
	_prev_fpe      = signal(SIGFPE,  HandleSavegameLoadCrash);
#endif
}

/**
 * Resets signal handlers back to original handlers.
 */
static void ResetSignalHandlers()
{
#ifdef WITH_SIGACTION
	sigaction(SIGSEGV, &_prev_segfault, nullptr);
	sigaction(SIGABRT, &_prev_abort, nullptr);
	sigaction(SIGFPE,  &_prev_fpe, nullptr);
#else
	signal(SIGSEGV, _prev_segfault);
	signal(SIGABRT, _prev_abort);
	signal(SIGFPE,  _prev_fpe);
#endif
}

/**
 * Try to find the overridden GRF identifier of the given GRF.
 * @param c the GRF to get the 'previous' version of.
 * @return the GRF identifier or \a c if none could be found.
 */
static const GRFIdentifier *GetOverriddenIdentifier(const GRFConfig *c)
{
	const LoggedAction &la = _gamelog_actions.back();
	if (la.at != GLAT_LOAD) return &c->ident;

	for (const LoggedChange &lc : la.changes) {
		if (lc.ct == GLCT_GRFCOMPAT && lc.grfcompat.grfid == c->ident.grfid) return &lc.grfcompat;
	}

	return &c->ident;
}

/** Was the saveload crash because of missing NewGRFs? */
static bool _saveload_crash_with_missing_newgrfs = false;

/**
 * Did loading the savegame cause a crash? If so,
 * were NewGRFs missing?
 * @return when the saveload crashed due to missing NewGRFs.
 */
bool SaveloadCrashWithMissingNewGRFs()
{
	return _saveload_crash_with_missing_newgrfs;
}

/**
 * Signal handler used to give a user a more useful report for crashes during
 * the savegame loading process; especially when there's problems with the
 * NewGRFs that are required by the savegame.
 * @param signum received signal
 */
#ifdef WITH_SIGACTION
static void CDECL HandleSavegameLoadCrash(int signum, siginfo_t *si, void *context)
#else
static void CDECL HandleSavegameLoadCrash(int signum)
#endif
{
	ResetSignalHandlers();

	char buffer[8192];
	char *p = buffer;
	p += seprintf(p, lastof(buffer), "Loading your savegame caused OpenTTD to crash.\n");

	for (const GRFConfig *c = _grfconfig; !_saveload_crash_with_missing_newgrfs && c != nullptr; c = c->next) {
		_saveload_crash_with_missing_newgrfs = HasBit(c->flags, GCF_COMPATIBLE) || c->status == GCS_NOT_FOUND;
	}

	if (_saveload_crash_with_missing_newgrfs) {
		p += seprintf(p, lastof(buffer),
			"This is most likely caused by a missing NewGRF or a NewGRF that\n"
			"has been loaded as replacement for a missing NewGRF. OpenTTD\n"
			"cannot easily determine whether a replacement NewGRF is of a newer\n"
			"or older version.\n"
			"It will load a NewGRF with the same GRF ID as the missing NewGRF.\n"
			"This means that if the author makes incompatible NewGRFs with the\n"
			"same GRF ID, OpenTTD cannot magically do the right thing. In most\n"
			"cases, OpenTTD will load the savegame and not crash, but this is an\n"
			"exception.\n"
			"Please load the savegame with the appropriate NewGRFs installed.\n"
			"The missing/compatible NewGRFs are:\n");

		for (const GRFConfig *c = _grfconfig; c != nullptr; c = c->next) {
			if (HasBit(c->flags, GCF_COMPATIBLE)) {
				const GRFIdentifier *replaced = GetOverriddenIdentifier(c);
				char original_md5[40];
				char replaced_md5[40];
				md5sumToString(original_md5, lastof(original_md5), c->original_md5sum);
				md5sumToString(replaced_md5, lastof(replaced_md5), replaced->md5sum);
				p += seprintf(p, lastof(buffer), "NewGRF %08X (checksum %s) not found.\n  Loaded NewGRF \"%s\" (checksum %s) with same GRF ID instead.\n", BSWAP32(c->ident.grfid), original_md5, c->filename.c_str(), replaced_md5);
			}
			if (c->status == GCS_NOT_FOUND) {
				char buf[40];
				md5sumToString(buf, lastof(buf), c->ident.md5sum);
				p += seprintf(p, lastof(buffer), "NewGRF %08X (%s) not found; checksum %s.\n", BSWAP32(c->ident.grfid), c->filename.c_str(), buf);
			}
		}
	} else {
		p += seprintf(p, lastof(buffer),
			"This is probably caused by a corruption in the savegame.\n"
			"Please file a bug report and attach this savegame.\n");
	}

	ShowInfoI(buffer);

#ifdef WITH_SIGACTION
	struct sigaction call;
#else
	SignalHandlerPointer call = nullptr;
#endif
	switch (signum) {
		case SIGSEGV: call = _prev_segfault; break;
		case SIGABRT: call = _prev_abort; break;
		case SIGFPE:  call = _prev_fpe; break;
		default: NOT_REACHED();
	}
#ifdef WITH_SIGACTION
	if (call.sa_flags & SA_SIGINFO) {
		if (call.sa_sigaction != nullptr) call.sa_sigaction(signum, si, context);
	} else {
		if (call.sa_handler != nullptr) call.sa_handler(signum);
	}
#else
	if (call != nullptr) call(signum);
#endif

}

/**
 * Tries to change owner of this rail tile to a valid owner. In very old versions it could happen that
 * a rail track had an invalid owner. When conversion isn't possible, track is removed.
 * @param t tile to update
 */
static void FixOwnerOfRailTrack(TileIndex t)
{
	assert(!Company::IsValidID(GetTileOwner(t)) && (IsLevelCrossingTile(t) || IsPlainRailTile(t)));

	/* remove leftover rail piece from crossing (from very old savegames) */
	Train *v = nullptr;
	for (Train *w : Train::Iterate()) {
		if (w->tile == t) {
			v = w;
			break;
		}
	}

	if (v != nullptr) {
		/* when there is a train on crossing (it could happen in TTD), set owner of crossing to train owner */
		SetTileOwner(t, v->owner);
		return;
	}

	/* try to find any connected rail */
	for (DiagDirection dd = DIAGDIR_BEGIN; dd < DIAGDIR_END; dd++) {
		TileIndex tt = t + TileOffsByDiagDir(dd);
		if (GetTileTrackStatus(t, TRANSPORT_RAIL, 0, dd) != 0 &&
				GetTileTrackStatus(tt, TRANSPORT_RAIL, 0, ReverseDiagDir(dd)) != 0 &&
				Company::IsValidID(GetTileOwner(tt))) {
			SetTileOwner(t, GetTileOwner(tt));
			return;
		}
	}

	if (IsLevelCrossingTile(t)) {
		/* else change the crossing to normal road (road vehicles won't care) */
		Owner road = GetRoadOwner(t, RTT_ROAD);
		Owner tram = GetRoadOwner(t, RTT_TRAM);
		RoadBits bits = GetCrossingRoadBits(t);
		bool hasroad = HasBit(_me[t].m7, 6);
		bool hastram = HasBit(_me[t].m7, 7);

		/* MakeRoadNormal */
		SetTileType(t, MP_ROAD);
		SetTileOwner(t, road);
		_m[t].m3 = (hasroad ? bits : 0);
		_m[t].m5 = (hastram ? bits : 0) | ROAD_TILE_NORMAL << 6;
		SB(_me[t].m6, 2, 4, 0);
		SetRoadOwner(t, RTT_TRAM, tram);
		return;
	}

	/* if it's not a crossing, make it clean land */
	MakeClear(t, CLEAR_GRASS, 0);
}

/**
 * Fixes inclination of a vehicle. Older OpenTTD versions didn't update the bits correctly.
 * @param v vehicle
 * @param dir vehicle's direction, or # INVALID_DIR if it can be ignored
 * @return inclination bits to set
 */
static uint FixVehicleInclination(Vehicle *v, Direction dir)
{
	/* Compute place where this vehicle entered the tile */
	int entry_x = v->x_pos;
	int entry_y = v->y_pos;
	switch (dir) {
		case DIR_NE: entry_x |= TILE_UNIT_MASK; break;
		case DIR_NW: entry_y |= TILE_UNIT_MASK; break;
		case DIR_SW: entry_x &= ~TILE_UNIT_MASK; break;
		case DIR_SE: entry_y &= ~TILE_UNIT_MASK; break;
		case INVALID_DIR: break;
		default: NOT_REACHED();
	}
	byte entry_z = GetSlopePixelZ(entry_x, entry_y, true);

	/* Compute middle of the tile. */
	int middle_x = (v->x_pos & ~TILE_UNIT_MASK) + TILE_SIZE / 2;
	int middle_y = (v->y_pos & ~TILE_UNIT_MASK) + TILE_SIZE / 2;
	byte middle_z = GetSlopePixelZ(middle_x, middle_y, true);

	/* middle_z == entry_z, no height change. */
	if (middle_z == entry_z) return 0;

	/* middle_z < entry_z, we are going downwards. */
	if (middle_z < entry_z) return 1U << GVF_GOINGDOWN_BIT;

	/* middle_z > entry_z, we are going upwards. */
	return 1U << GVF_GOINGUP_BIT;
}

/**
 * Check whether the ground vehicles are at the correct Z-coordinate. When they
 * are not, this will cause all kinds of problems later on as the vehicle might
 * not get onto bridges and so on.
 */
static void CheckGroundVehiclesAtCorrectZ()
{
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->IsGroundVehicle()) {
			/*
			 * Either the vehicle is not actually on the given tile, i.e. it is
			 * in the wormhole of a bridge or a tunnel, or the Z-coordinate must
			 * be the same as when it would be recalculated right now.
			 */
			assert(v->tile != TileVirtXY(v->x_pos, v->y_pos) || v->z_pos == GetSlopePixelZ(v->x_pos, v->y_pos, true));
		}
	}
}

/**
 * Checks for the possibility that a bridge may be on this tile
 * These are in fact all the tile types on which a bridge can be found
 * @param t The tile to analyze
 * @return True if a bridge might have been present prior to savegame 194.
 */
static inline bool MayHaveBridgeAbove(TileIndex t)
{
	return IsTileType(t, MP_CLEAR) || IsTileType(t, MP_RAILWAY) || IsTileType(t, MP_ROAD) ||
			IsTileType(t, MP_WATER) || IsTileType(t, MP_TUNNELBRIDGE) || IsTileType(t, MP_OBJECT);
}

TileIndex GetOtherTunnelBridgeEndOld(TileIndex tile)
{
	DiagDirection dir = GetTunnelBridgeDirection(tile);
	TileIndexDiff delta = TileOffsByDiagDir(dir);
	int z = GetTileZ(tile);

	dir = ReverseDiagDir(dir);
	do {
		tile += delta;
	} while (
		!IsTunnelTile(tile) ||
		GetTunnelBridgeDirection(tile) != dir ||
		GetTileZ(tile) != z
	);

	return tile;
}


/**
 * Start the scripts.
 */
static void StartScripts()
{
	/* Script debug window requires AIs to be started before trying to start GameScript. */

	/* Start the AIs. */
	for (const Company *c : Company::Iterate()) {
		if (Company::IsValidAiID(c->index)) AI::StartNew(c->index);
	}

	/* Start the GameScript. */
	Game::StartNew();

	ShowScriptDebugWindowIfScriptError();
}

template <typename F>
void IterateVehicleAndOrderListOrders(F func)
{
	for (Order *order : Order::Iterate()) {
		func(order);
	}
	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		func(&(v->current_order));
	}
}

/**
 * Perform a (large) amount of savegame conversion *magic* in order to
 * load older savegames and to fill the caches for various purposes.
 * @return True iff conversion went without a problem.
 */
bool AfterLoadGame()
{
	SetSignalHandlers();

	TileIndex map_size = MapSize();

	/* Only new games can use wallclock units. */
	if (SlXvIsFeatureMissing(XSLFI_VARIABLE_DAY_LENGTH, 5) && IsSavegameVersionBefore(SLV_ECONOMY_MODE_TIMEKEEPING_UNITS)) {
		_settings_game.economy.timekeeping_units = TKU_CALENDAR;
	}
	UpdateEffectiveDayLengthFactor();

	SetupTickRate();

	extern TileIndex _cur_tileloop_tile; // From landscape.cpp.
	/* The LFSR used in RunTileLoop iteration cannot have a zeroed state, make it non-zeroed. */
	if (_cur_tileloop_tile == 0) _cur_tileloop_tile = 1;

	extern TileIndex _aux_tileloop_tile;
	if (_aux_tileloop_tile == 0) _aux_tileloop_tile = 1;

	if (IsSavegameVersionBefore(SLV_98)) GamelogOldver();

	GamelogTestRevision();
	GamelogTestMode();

	RebuildTownKdtree();
	RebuildStationKdtree();
	UpdateCachedSnowLine();
	UpdateCachedSnowLineBounds();

	_viewport_sign_kdtree_valid = false;

	if (IsSavegameVersionBefore(SLV_98)) GamelogGRFAddList(_grfconfig);

	if (IsSavegameVersionBefore(SLV_119)) {
		_pause_mode = (_pause_mode == 2) ? PM_PAUSED_NORMAL : PM_UNPAUSED;
	} else if (_network_dedicated && (_pause_mode & PM_PAUSED_ERROR) != 0) {
		DEBUG(net, 0, "The loading savegame was paused due to an error state");
		DEBUG(net, 0, "  This savegame cannot be used for multiplayer");
		/* Restore the signals */
		ResetSignalHandlers();
		return false;
	} else if (!_networking || _network_server) {
		/* If we are in singleplayer mode, i.e. not networking, and loading the
		 * savegame or we are loading the savegame as network server we do
		 * not want to be bothered by being paused because of the automatic
		 * reason of a network server, e.g. joining clients or too few
		 * active clients. Note that resetting these values for a network
		 * client are very bad because then the client is going to execute
		 * the game loop when the server is not, i.e. it desyncs. */
		_pause_mode &= ~PMB_PAUSED_NETWORK;
	}

	/* In very old versions, size of train stations was stored differently.
	 * They had swapped width and height if station was built along the Y axis.
	 * TTO and TTD used 3 bits for width/height, while OpenTTD used 4.
	 * Because the data stored by TTDPatch are unusable for rail stations > 7x7,
	 * recompute the width and height. Doing this unconditionally for all old
	 * savegames simplifies the code. */
	if (IsSavegameVersionBefore(SLV_2)) {
		for (Station *st : Station::Iterate()) {
			st->train_station.w = st->train_station.h = 0;
		}
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsTileType(t, MP_STATION)) continue;
			if (_m[t].m5 > 7) continue; // is it a rail station tile?
			Station *st = Station::Get(_m[t].m2);
			assert(st->train_station.tile != 0);
			int dx = TileX(t) - TileX(st->train_station.tile);
			int dy = TileY(t) - TileY(st->train_station.tile);
			assert(dx >= 0 && dy >= 0);
			st->train_station.w = std::max<uint>(st->train_station.w, dx + 1);
			st->train_station.h = std::max<uint>(st->train_station.h, dy + 1);
		}
	}

	if (IsSavegameVersionBefore(SLV_194) && SlXvIsFeatureMissing(XSLFI_HEIGHT_8_BIT)) {
		_settings_game.construction.map_height_limit = 15;

		/* In old savegame versions, the heightlevel was coded in bits 0..3 of the type field */
		for (TileIndex t = 0; t < map_size; t++) {
			_m[t].height = GB(_m[t].type, 0, 4);
			SB(_m[t].type, 0, 2, GB(_me[t].m6, 0, 2));
			SB(_me[t].m6, 0, 2, 0);
			if (MayHaveBridgeAbove(t)) {
				SB(_m[t].type, 2, 2, GB(_me[t].m6, 6, 2));
				SB(_me[t].m6, 6, 2, 0);
			} else {
				SB(_m[t].type, 2, 2, 0);
			}
		}
	} else if (IsSavegameVersionBefore(SLV_194) && SlXvIsFeaturePresent(XSLFI_HEIGHT_8_BIT)) {
		for (TileIndex t = 0; t < map_size; t++) {
			SB(_m[t].type, 0, 2, GB(_me[t].m6, 0, 2));
			SB(_me[t].m6, 0, 2, 0);
			if (MayHaveBridgeAbove(t)) {
				SB(_m[t].type, 2, 2, GB(_me[t].m6, 6, 2));
				SB(_me[t].m6, 6, 2, 0);
			} else {
				SB(_m[t].type, 2, 2, 0);
			}
		}
	}

	/* in version 2.1 of the savegame, town owner was unified. */
	if (IsSavegameVersionBefore(SLV_2, 1)) ConvertTownOwner();

	/* from version 4.1 of the savegame, exclusive rights are stored at towns */
	if (IsSavegameVersionBefore(SLV_4, 1)) UpdateExclusiveRights();

	/* from version 4.2 of the savegame, currencies are in a different order */
	if (IsSavegameVersionBefore(SLV_4, 2)) UpdateCurrencies();

	/* In old version there seems to be a problem that water is owned by
	 * OWNER_NONE, not OWNER_WATER.. I can't replicate it for the current
	 * (4.3) version, so I just check when versions are older, and then
	 * walk through the whole map.. */
	if (IsSavegameVersionBefore(SLV_4, 3)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_WATER) && GetTileOwner(t) >= MAX_COMPANIES) {
				SetTileOwner(t, OWNER_WATER);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_84)) {
		for (Company *c : Company::Iterate()) {
			c->name = CopyFromOldName(c->name_1);
			if (!c->name.empty()) c->name_1 = STR_SV_UNNAMED;
			c->president_name = CopyFromOldName(c->president_name_1);
			if (!c->president_name.empty()) c->president_name_1 = SPECSTR_PRESIDENT_NAME;
		}

		for (Station *st : Station::Iterate()) {
			st->name = CopyFromOldName(st->string_id);
			/* generating new name would be too much work for little effect, use the station name fallback */
			if (!st->name.empty()) st->string_id = STR_SV_STNAME_FALLBACK;
		}

		for (Town *t : Town::Iterate()) {
			t->name = CopyFromOldName(t->townnametype);
			if (!t->name.empty()) t->townnametype = SPECSTR_TOWNNAME_START + _settings_game.game_creation.town_name;
		}
	}

	/* From this point the old names array is cleared. */
	ResetOldNames();

	if (IsSavegameVersionBefore(SLV_106)) {
		/* no station is determined by 'tile == INVALID_TILE' now (instead of '0') */
		for (Station *st : Station::Iterate()) {
			if (st->airport.tile       == 0) st->airport.tile       = INVALID_TILE;
			if (st->train_station.tile == 0) st->train_station.tile = INVALID_TILE;
		}

		/* the same applies to Company::location_of_HQ */
		for (Company *c : Company::Iterate()) {
			if (c->location_of_HQ == 0 || (IsSavegameVersionBefore(SLV_4) && c->location_of_HQ == 0xFFFF)) {
				c->location_of_HQ = INVALID_TILE;
			}
		}
	}

	/* convert road side to my format. */
	if (_settings_game.vehicle.road_side) _settings_game.vehicle.road_side = 1;

	/* Check if all NewGRFs are present, we are very strict in MP mode */
	GRFListCompatibility gcf_res = IsGoodGRFConfigList(_grfconfig);
	for (GRFConfig *c = _grfconfig; c != nullptr; c = c->next) {
		if (c->status == GCS_NOT_FOUND) {
			GamelogGRFRemove(c->ident.grfid);
		} else if (HasBit(c->flags, GCF_COMPATIBLE)) {
			GamelogGRFCompatible(&c->ident);
		}
	}

	if (_networking && gcf_res != GLC_ALL_GOOD) {
		SetSaveLoadError(STR_NETWORK_ERROR_CLIENT_NEWGRF_MISMATCH);
		/* Restore the signals */
		ResetSignalHandlers();
		return false;
	}

	/* The value of _date_fract got divided, so make sure that old games are converted correctly. */
	if (IsSavegameVersionBefore(SLV_11, 1) || (IsSavegameVersionBefore(SLV_147) && CalTime::CurDateFract() > DAY_TICKS)) CalTime::Detail::now.cal_date_fract /= 885;

	if (SlXvIsFeaturePresent(XSLFI_SPRINGPP) || SlXvIsFeaturePresent(XSLFI_JOKERPP) || SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
		assert(DayLengthFactor() >= 1);
		DateDetail::_tick_skip_counter = CalTime::CurDateFract() % DayLengthFactor();
		CalTime::Detail::now.cal_date_fract /= DayLengthFactor();
		assert(CalTime::CurDateFract() < DAY_TICKS);
		assert(TickSkipCounter() < DayLengthFactor());
	}

	/* Set day length factor to 1 if loading a pre day length savegame */
	if (SlXvIsFeatureMissing(XSLFI_VARIABLE_DAY_LENGTH) && SlXvIsFeatureMissing(XSLFI_SPRINGPP) && SlXvIsFeatureMissing(XSLFI_JOKERPP) && SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
		_settings_game.economy.day_length_factor = 1;
		UpdateEffectiveDayLengthFactor();
		if (_file_to_saveload.abstract_ftype != FT_SCENARIO) {
			/* If this is obviously a vanilla/non-patchpack savegame (and not a scenario),
			 * set the savegame time units to be in days, as they would have been previously. */
			_settings_game.game_time.time_in_minutes = false;
		}
	}
	if (SlXvIsFeatureMissing(XSLFI_VARIABLE_DAY_LENGTH, 3)) {
		_scaled_tick_counter = (uint64_t)((_tick_counter * DayLengthFactor()) + TickSkipCounter());
	}
	if (SlXvIsFeaturePresent(XSLFI_VARIABLE_DAY_LENGTH, 1, 3)) {
		/* CalTime is used here because EconTime hasn't been set yet, but this needs to be done before setting EconTime::Detail::SetDate,
		 * because that calls RecalculateStateTicksOffset which overwrites DateDetail::_state_ticks_offset which is an input here */
		_state_ticks = GetStateTicksFromDateWithoutOffset(CalTime::CurDate().base(), CalTime::CurDateFract());
		if (SlXvIsFeaturePresent(XSLFI_VARIABLE_DAY_LENGTH, 3, 3)) _state_ticks += DateDetail::_state_ticks_offset;
	}

	/* Update current year
	 * must be done before loading sprites as some newgrfs check it */
	CalTime::Detail::SetDate(CalTime::CurDate(), CalTime::CurDateFract());

	if (SlXvIsFeaturePresent(XSLFI_VARIABLE_DAY_LENGTH, 5) || !IsSavegameVersionBefore(SLV_ECONOMY_DATE)) {
		EconTime::Detail::SetDate(EconTime::CurDate(), EconTime::CurDateFract());
	} else {
		/* Set economy date from calendar date */
		EconTime::Detail::SetDate(CalTime::CurDate().base(), CalTime::CurDateFract());
	}

	SetupTileLoopCounts();

	/*
	 * Force the old behaviour for compatibility reasons with old savegames. As new
	 * settings can only be loaded from new savegames loading old savegames with new
	 * versions of OpenTTD will normally initialize settings newer than the savegame
	 * version with "new game" defaults which the player can define to their liking.
	 * For some settings we override that to keep the behaviour the same as when the
	 * game was saved.
	 *
	 * Note that there is no non-stop in here. This is because the setting could have
	 * either value in TTDPatch. To convert it properly the user has to make sure the
	 * right value has been chosen in the settings. Otherwise we will be converting
	 * it incorrectly in half of the times without a means to correct that.
	 */
	if (IsSavegameVersionBefore(SLV_4, 2)) _settings_game.station.modified_catchment = false;
	if (IsSavegameVersionBefore(SLV_6, 1)) _settings_game.pf.forbid_90_deg = false;
	if (IsSavegameVersionBefore(SLV_21))   _settings_game.vehicle.train_acceleration_model = 0;
	if (IsSavegameVersionBefore(SLV_90))   _settings_game.vehicle.plane_speed = 4;
	if (IsSavegameVersionBefore(SLV_95))   _settings_game.vehicle.dynamic_engines = false;
	if (IsSavegameVersionBefore(SLV_96))   _settings_game.economy.station_noise_level = false;
	if (IsSavegameVersionBefore(SLV_133)) {
		_settings_game.vehicle.train_slope_steepness = 3;
	}
	if (IsSavegameVersionBefore(SLV_134))  _settings_game.economy.feeder_payment_share = 75;
	if (IsSavegameVersionBefore(SLV_138))  _settings_game.vehicle.plane_crashes = 2;
	if (IsSavegameVersionBefore(SLV_139)) {
		_settings_game.vehicle.roadveh_acceleration_model = 0;
		_settings_game.vehicle.roadveh_slope_steepness = 7;
	}
	if (IsSavegameVersionBefore(SLV_143))  _settings_game.economy.allow_town_level_crossings = true;
	if (IsSavegameVersionBefore(SLV_159)) {
		_settings_game.vehicle.max_train_length = 50;
		_settings_game.construction.max_bridge_length = 64;
		_settings_game.construction.max_tunnel_length = 64;
	}
	if (IsSavegameVersionBefore(SLV_166))  _settings_game.economy.infrastructure_maintenance = false;
	if (IsSavegameVersionBefore(SLV_183) && SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
		_settings_game.linkgraph.distribution_pax = DT_MANUAL;
		_settings_game.linkgraph.distribution_mail = DT_MANUAL;
		_settings_game.linkgraph.distribution_armoured = DT_MANUAL;
		_settings_game.linkgraph.distribution_default = DT_MANUAL;
	}

	if (IsSavegameVersionBefore(SLV_ENDING_YEAR)) {
		_settings_game.game_creation.ending_year = CalTime::DEF_END_YEAR;
	}

	/* Convert linkgraph update settings from days to seconds. */
	if (IsSavegameVersionBefore(SLV_LINKGRAPH_SECONDS) && SlXvIsFeatureMissing(XSLFI_LINKGRAPH_DAY_SCALE, 3)) {
		_settings_game.linkgraph.recalc_interval *= SECONDS_PER_DAY;
		_settings_game.linkgraph.recalc_time     *= SECONDS_PER_DAY;
	}

	/* Convert link graph last compression from date to scaled tick counter, or state ticks to scaled ticks. */
	if (SlXvIsFeatureMissing(XSLFI_LINKGRAPH_DAY_SCALE, 6)) {
		extern void LinkGraphFixupAfterLoad(bool compression_was_date);
		LinkGraphFixupAfterLoad(SlXvIsFeatureMissing(XSLFI_LINKGRAPH_DAY_SCALE, 4));
	}

	/* Load the sprites */
	GfxLoadSprites();
	LoadStringWidthTable();
	ReInitAllWindows(false);

	/* Copy temporary data to Engine pool */
	CopyTempEngineData();

	/* Connect front and rear engines of multiheaded trains and converts
	 * subtype to the new format */
	if (IsSavegameVersionBefore(SLV_17, 1)) ConvertOldMultiheadToNew();

	/* Connect front and rear engines of multiheaded trains */
	ConnectMultiheadedTrains();

	/* Fix the CargoPackets *and* fix the caches of CargoLists.
	 * If this isn't done before Stations and especially Vehicles are
	 * running their AfterLoad we might get in trouble. In the case of
	 * vehicles we could give the wrong (cached) count of items in a
	 * vehicle which causes different results when getting their caches
	 * filled; and that could eventually lead to desyncs. */
	CargoPacket::AfterLoad();

	/* Oilrig was moved from id 15 to 9. We have to do this conversion
	 * here as AfterLoadVehicles can check it indirectly via the newgrf
	 * code. */
	if (IsSavegameVersionBefore(SLV_139)) {
		for (Station *st : Station::Iterate()) {
			if (st->airport.tile != INVALID_TILE && st->airport.type == 15) {
				st->airport.type = AT_OILRIG;
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_SPRINGPP)) {
		/*
		 * Reject huge airports
		 * Annoyingly SpringPP v2.0.102 has a bug where it uses the same ID for AT_INTERCONTINENTAL2 and AT_OILRIG.
		 * Do this here as AfterLoadVehicles might also check it indirectly via the newgrf code.
		 */
		for (Station *st : Station::Iterate()) {
			if (st->airport.tile == INVALID_TILE) continue;
			StringID err = INVALID_STRING_ID;
			if (st->airport.type == 9) {
				if (st->ship_station.tile != INVALID_TILE && IsOilRig(st->ship_station.tile)) {
					/* this airport is probably an oil rig, not a huge airport */
				} else {
					err = STR_GAME_SAVELOAD_ERROR_HUGE_AIRPORTS_PRESENT;
				}
				st->airport.type = AT_OILRIG;
			} else if (st->airport.type == 10) {
				err = STR_GAME_SAVELOAD_ERROR_HUGE_AIRPORTS_PRESENT;
			}
			if (err != INVALID_STRING_ID) {
				SetSaveLoadError(err);
				/* Restore the signals */
				ResetSignalHandlers();
				return false;
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_SPRINGPP, 1, 1)) {
		/*
		 * Reject helicopters aproaching oil rigs using the wrong aircraft movement data
		 * Annoyingly SpringPP v2.0.102 has a bug where it uses the same ID for AT_INTERCONTINENTAL2 and AT_OILRIG
		 * Do this here as AfterLoadVehicles can also check it indirectly via the newgrf code.
		 */
		for (Aircraft *v : Aircraft::Iterate()) {
			Station *st = GetTargetAirportIfValid(v);
			if (st != nullptr && ((st->ship_station.tile != INVALID_TILE && IsOilRig(st->ship_station.tile)) || st->airport.type == AT_OILRIG)) {
				/* aircraft is on approach to an oil rig, bail out now */
				SetSaveLoadError(STR_GAME_SAVELOAD_ERROR_HELI_OILRIG_BUG);
				/* Restore the signals */
				ResetSignalHandlers();
				return false;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_MULTITILE_DOCKS)) {
		for (Station *st : Station::Iterate()) {
			st->ship_station.tile = INVALID_TILE;
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_REALISTIC_TRAIN_BRAKING)) {
		_settings_game.vehicle.train_braking_model = TBM_ORIGINAL;
	}

	if (SlXvIsFeatureMissing(XSLFI_TRAIN_SPEED_ADAPTATION)) {
		_settings_game.vehicle.train_speed_adaptation = false;
	}

	AfterLoadEngines();
	AnalyseIndustryTileSpriteGroups();
	extern void AnalyseHouseSpriteGroups();
	AnalyseHouseSpriteGroups();

	/* Update all vehicles */
	AfterLoadVehicles(true);

	CargoPacket::PostVehiclesAfterLoad();

	/* Update template vehicles */
	AfterLoadTemplateVehicles();

	/* make sure there is a town in the game */
	if (_game_mode == GM_NORMAL && Town::GetNumItems() == 0) {
		SetSaveLoadError(STR_ERROR_NO_TOWN_IN_SCENARIO);
		/* Restore the signals */
		ResetSignalHandlers();
		return false;
	}

	/* The void tiles on the southern border used to belong to a wrong class (pre 4.3).
	 * This problem appears in savegame version 21 too, see r3455. But after loading the
	 * savegame and saving again, the buggy map array could be converted to new savegame
	 * version. It didn't show up before r12070. */
	if (IsSavegameVersionBefore(SLV_87)) UpdateVoidTiles();

	/* Fix the cache for cargo payments. */
	for (CargoPayment *cp : CargoPayment::Iterate()) {
		cp->front->cargo_payment = cp;
		cp->current_station = cp->front->last_station_visited;
	}

	if (IsSavegameVersionBefore(SLV_72)) {
		/* Locks in very old savegames had OWNER_WATER as owner */
		for (TileIndex t = 0; t < MapSize(); t++) {
			switch (GetTileType(t)) {
				default: break;

				case MP_WATER:
					if (GetWaterTileType(t) == WATER_TILE_LOCK && GetTileOwner(t) == OWNER_WATER) SetTileOwner(t, OWNER_NONE);
					break;

				case MP_STATION: {
					if (HasBit(_me[t].m6, 3)) SetBit(_me[t].m6, 2);
					StationGfx gfx = GetStationGfx(t);
					StationType st;
					if (       IsInsideMM(gfx,   0,   8)) { // Rail station
						st = STATION_RAIL;
						SetStationGfx(t, gfx - 0);
					} else if (IsInsideMM(gfx,   8,  67)) { // Airport
						st = STATION_AIRPORT;
						SetStationGfx(t, gfx - 8);
					} else if (IsInsideMM(gfx,  67,  71)) { // Truck
						st = STATION_TRUCK;
						SetStationGfx(t, gfx - 67);
					} else if (IsInsideMM(gfx,  71,  75)) { // Bus
						st = STATION_BUS;
						SetStationGfx(t, gfx - 71);
					} else if (gfx == 75) {                 // Oil rig
						st = STATION_OILRIG;
						SetStationGfx(t, gfx - 75);
					} else if (IsInsideMM(gfx,  76,  82)) { // Dock
						st = STATION_DOCK;
						SetStationGfx(t, gfx - 76);
					} else if (gfx == 82) {                 // Buoy
						st = STATION_BUOY;
						SetStationGfx(t, gfx - 82);
					} else if (IsInsideMM(gfx,  83, 168)) { // Extended airport
						st = STATION_AIRPORT;
						SetStationGfx(t, gfx - 83 + 67 - 8);
					} else if (IsInsideMM(gfx, 168, 170)) { // Drive through truck
						st = STATION_TRUCK;
						SetStationGfx(t, gfx - 168 + GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET);
					} else if (IsInsideMM(gfx, 170, 172)) { // Drive through bus
						st = STATION_BUS;
						SetStationGfx(t, gfx - 170 + GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET);
					} else {
						/* Restore the signals */
						ResetSignalHandlers();
						return false;
					}
					SB(_me[t].m6, 3, 3, st);
					break;
				}
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_MORE_STATION_TYPES)) {
		/* Expansion of station type field in m6 */
		for (TileIndex t = 0; t < MapSize(); t++) {
			if (IsTileType(t, MP_STATION)) {
				ClrBit(_me[t].m6, 6);
			}
		}
	}

	for (TileIndex t = 0; t < map_size; t++) {
		switch (GetTileType(t)) {
			case MP_STATION: {
				BaseStation *bst = BaseStation::GetByTile(t);

				/* Sanity check */
				if (!IsBuoy(t) && bst->owner != GetTileOwner(t)) SlErrorCorrupt("Wrong owner for station tile");

				/* Set up station spread */
				bst->rect.BeforeAddTile(t, StationRect::ADD_FORCE);

				/* Waypoints don't have road stops/oil rigs in the old format */
				if (!Station::IsExpected(bst)) break;
				Station *st = Station::From(bst);

				switch (GetStationType(t)) {
					case STATION_TRUCK:
					case STATION_BUS:
						if (IsSavegameVersionBefore(SLV_6)) {
							/* Before version 5 you could not have more than 250 stations.
							 * Version 6 adds large maps, so you could only place 253*253
							 * road stops on a map (no freeform edges) = 64009. So, yes
							 * someone could in theory create such a full map to trigger
							 * this assertion, it's safe to assume that's only something
							 * theoretical and does not happen in normal games. */
							assert(RoadStop::CanAllocateItem());

							/* From this version on there can be multiple road stops of the
							 * same type per station. Convert the existing stops to the new
							 * internal data structure. */
							RoadStop *rs = new RoadStop(t);

							RoadStop **head =
								IsTruckStop(t) ? &st->truck_stops : &st->bus_stops;
							*head = rs;
						}
						break;

					case STATION_OILRIG: {
						/* The internal encoding of oil rigs was changed twice.
						 * It was 3 (till 2.2) and later 5 (till 5.1).
						 * DeleteOilRig asserts on the correct type, and
						 * setting it unconditionally does not hurt.
						 */
						Station::GetByTile(t)->airport.type = AT_OILRIG;

						/* Very old savegames sometimes have phantom oil rigs, i.e.
						 * an oil rig which got shut down, but not completely removed from
						 * the map
						 */
						TileIndex t1 = TileAddXY(t, 0, 1);
						if (!IsTileType(t1, MP_INDUSTRY) || GetIndustryGfx(t1) != GFX_OILRIG_1) {
							DeleteOilRig(t);
						}
						break;
					}

					default: break;
				}
				break;
			}

			default: break;
		}
	}

	/* In version 6.1 we put the town index in the map-array. To do this, we need
	 *  to use m2 (16bit big), so we need to clean m2, and that is where this is
	 *  all about ;) */
	if (IsSavegameVersionBefore(SLV_6, 1)) {
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_HOUSE:
					_m[t].m4 = _m[t].m2;
					SetTownIndex(t, CalcClosestTownFromTile(t)->index);
					break;

				case MP_ROAD:
					_m[t].m4 |= (_m[t].m2 << 4);
					if ((GB(_m[t].m5, 4, 2) == ROAD_TILE_CROSSING ? (Owner)_m[t].m3 : GetTileOwner(t)) == OWNER_TOWN) {
						SetTownIndex(t, CalcClosestTownFromTile(t)->index);
					} else {
						SetTownIndex(t, 0);
					}
					break;

				default: break;
			}
		}
	}

	/* Force the freeform edges to false for old savegames. */
	if (IsSavegameVersionBefore(SLV_111)) {
		_settings_game.construction.freeform_edges = false;
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->tile == 0) v->UpdatePosition();
		}
	}

	/* From version 9.0, we update the max passengers of a town (was sometimes negative
	 *  before that. */
	if (IsSavegameVersionBefore(SLV_9)) {
		for (Town *t : Town::Iterate()) UpdateTownMaxPass(t);
	}

	/* From version 16.0, we included autorenew on engines, which are now saved, but
	 *  of course, we do need to initialize them for older savegames. */
	if (IsSavegameVersionBefore(SLV_16)) {
		for (Company *c : Company::Iterate()) {
			c->engine_renew_list            = nullptr;
			c->settings.engine_renew        = false;
			c->settings.engine_renew_months = 6;
			c->settings.engine_renew_money  = 100000;
		}

		/* When loading a game, _local_company is not yet set to the correct value.
		 * However, in a dedicated server we are a spectator, so nothing needs to
		 * happen. In case we are not a dedicated server, the local company always
		 * becomes company 0, unless we are in the scenario editor where all the
		 * companies are 'invalid'.
		 */
		Company *c = Company::GetIfValid(COMPANY_FIRST);
		if (!_network_dedicated && c != nullptr) {
			c->settings = _settings_client.company;
		}
	}

	if (IsSavegameVersionBefore(SLV_48)) {
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_RAILWAY:
					if (IsPlainRail(t)) {
						/* Swap ground type and signal type for plain rail tiles, so the
						 * ground type uses the same bits as for depots and waypoints. */
						uint tmp = GB(_m[t].m4, 0, 4);
						SB(_m[t].m4, 0, 4, GB(_m[t].m2, 0, 4));
						SB(_m[t].m2, 0, 4, tmp);
					} else if (HasBit(_m[t].m5, 2)) {
						/* Split waypoint and depot rail type and remove the subtype. */
						ClrBit(_m[t].m5, 2);
						ClrBit(_m[t].m5, 6);
					}
					break;

				case MP_ROAD:
					/* Swap m3 and m4, so the track type for rail crossings is the
					 * same as for normal rail. */
					Swap(_m[t].m3, _m[t].m4);
					break;

				default: break;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_61)) {
		/* Added the RoadType */
		bool old_bridge = IsSavegameVersionBefore(SLV_42);
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_ROAD:
					SB(_m[t].m5, 6, 2, GB(_m[t].m5, 4, 2));
					switch (GetRoadTileType(t)) {
						default: SlErrorCorrupt("Invalid road tile type");
						case ROAD_TILE_NORMAL:
							SB(_m[t].m4, 0, 4, GB(_m[t].m5, 0, 4));
							SB(_m[t].m4, 4, 4, 0);
							SB(_me[t].m6, 2, 4, 0);
							break;
						case ROAD_TILE_CROSSING:
							SB(_m[t].m4, 5, 2, GB(_m[t].m5, 2, 2));
							break;
						case ROAD_TILE_DEPOT:    break;
					}
					SB(_me[t].m7, 6, 2, 1); // Set pre-NRT road type bits for conversion later.
					break;

				case MP_STATION:
					if (IsStationRoadStop(t)) SB(_me[t].m7, 6, 2, 1);
					break;

				case MP_TUNNELBRIDGE:
					/* Middle part of "old" bridges */
					if (old_bridge && IsBridge(t) && HasBit(_m[t].m5, 6)) break;
					if (((old_bridge && IsBridge(t)) ? (TransportType)GB(_m[t].m5, 1, 2) : GetTunnelBridgeTransportType(t)) == TRANSPORT_ROAD) {
						SB(_me[t].m7, 6, 2, 1); // Set pre-NRT road type bits for conversion later.
					}
					break;

				default: break;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_114)) {
		bool fix_roadtypes = !IsSavegameVersionBefore(SLV_61);
		bool old_bridge = IsSavegameVersionBefore(SLV_42);

		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_ROAD:
					if (fix_roadtypes) SB(_me[t].m7, 6, 2, (RoadTypes)GB(_me[t].m7, 5, 3));
					SB(_me[t].m7, 5, 1, GB(_m[t].m3, 7, 1)); // snow/desert
					switch (GetRoadTileType(t)) {
						default: SlErrorCorrupt("Invalid road tile type");
						case ROAD_TILE_NORMAL:
							SB(_me[t].m7, 0, 4, GB(_m[t].m3, 0, 4));  // road works
							SB(_me[t].m6, 3, 3, GB(_m[t].m3, 4, 3));  // ground
							SB(_m[t].m3, 0, 4, GB(_m[t].m4, 4, 4));   // tram bits
							SB(_m[t].m3, 4, 4, GB(_m[t].m5, 0, 4));   // tram owner
							SB(_m[t].m5, 0, 4, GB(_m[t].m4, 0, 4));   // road bits
							break;

						case ROAD_TILE_CROSSING:
							SB(_me[t].m7, 0, 5, GB(_m[t].m4, 0, 5));  // road owner
							SB(_me[t].m6, 3, 3, GB(_m[t].m3, 4, 3));  // ground
							SB(_m[t].m3, 4, 4, GB(_m[t].m5, 0, 4));   // tram owner
							SB(_m[t].m5, 0, 1, GB(_m[t].m4, 6, 1));   // road axis
							SB(_m[t].m5, 5, 1, GB(_m[t].m4, 5, 1));   // crossing state
							break;

						case ROAD_TILE_DEPOT:
							break;
					}
					if (!IsRoadDepot(t) && !HasTownOwnedRoad(t)) {
						const Town *town = CalcClosestTownFromTile(t);
						if (town != nullptr) SetTownIndex(t, town->index);
					}
					_m[t].m4 = 0;
					break;

				case MP_STATION:
					if (!IsStationRoadStop(t)) break;

					if (fix_roadtypes) SB(_me[t].m7, 6, 2, (RoadTypes)GB(_m[t].m3, 0, 3));
					SB(_me[t].m7, 0, 5, HasBit(_me[t].m6, 2) ? OWNER_TOWN : GetTileOwner(t));
					SB(_m[t].m3, 4, 4, _m[t].m1);
					_m[t].m4 = 0;
					break;

				case MP_TUNNELBRIDGE:
					if (old_bridge && IsBridge(t) && HasBit(_m[t].m5, 6)) break;
					if (((old_bridge && IsBridge(t)) ? (TransportType)GB(_m[t].m5, 1, 2) : GetTunnelBridgeTransportType(t)) == TRANSPORT_ROAD) {
						if (fix_roadtypes) SB(_me[t].m7, 6, 2, (RoadTypes)GB(_m[t].m3, 0, 3));

						Owner o = GetTileOwner(t);
						SB(_me[t].m7, 0, 5, o); // road owner
						SB(_m[t].m3, 4, 4, o == OWNER_NONE ? OWNER_TOWN : o); // tram owner
					}
					SB(_me[t].m6, 2, 4, GB(_m[t].m2, 4, 4)); // bridge type
					SB(_me[t].m7, 5, 1, GB(_m[t].m4, 7, 1)); // snow/desert

					_m[t].m2 = 0;
					_m[t].m4 = 0;
					break;

				default: break;
			}
		}
	}

	/* Railtype moved from m3 to m8 in version SLV_EXTEND_RAILTYPES. */
	if (IsSavegameVersionBefore(SLV_EXTEND_RAILTYPES)) {
		const bool has_extra_bit = SlXvIsFeaturePresent(XSLFI_MORE_RAIL_TYPES, 1, 1);
		auto update_railtype = [&](TileIndex t) {
			uint rt = GB(_m[t].m3, 0, 4);
			if (has_extra_bit) rt |= (GB(_m[t].m1, 7, 1) << 4);
			SetRailType(t, (RailType)rt);
		};
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_RAILWAY:
					update_railtype(t);
					break;

				case MP_ROAD:
					if (IsLevelCrossing(t)) {
						update_railtype(t);
					}
					break;

				case MP_STATION:
					if (HasStationRail(t)) {
						update_railtype(t);
					}
					break;

				case MP_TUNNELBRIDGE:
					if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) {
						update_railtype(t);
					}
					break;

				default:
					break;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_42)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (MayHaveBridgeAbove(t)) ClearBridgeMiddle(t);
			if (IsBridgeTile(t)) {
				if (HasBit(_m[t].m5, 6)) { // middle part
					Axis axis = (Axis)GB(_m[t].m5, 0, 1);

					if (HasBit(_m[t].m5, 5)) { // transport route under bridge?
						if (GB(_m[t].m5, 3, 2) == TRANSPORT_RAIL) {
							MakeRailNormal(
								t,
								GetTileOwner(t),
								axis == AXIS_X ? TRACK_BIT_Y : TRACK_BIT_X,
								GetRailType(t)
							);
						} else {
							TownID town = IsTileOwner(t, OWNER_TOWN) ? ClosestTownFromTile(t, UINT_MAX)->index : 0;

							/* MakeRoadNormal */
							SetTileType(t, MP_ROAD);
							_m[t].m2 = town;
							_m[t].m3 = 0;
							_m[t].m5 = (axis == AXIS_X ? ROAD_Y : ROAD_X) | ROAD_TILE_NORMAL << 6;
							SB(_me[t].m6, 2, 4, 0);
							_me[t].m7 = 1 << 6;
							SetRoadOwner(t, RTT_TRAM, OWNER_NONE);
						}
					} else {
						if (GB(_m[t].m5, 3, 2) == 0) {
							MakeClear(t, CLEAR_GRASS, 3);
						} else {
							if (!IsTileFlat(t)) {
								MakeShore(t);
							} else {
								if (GetTileOwner(t) == OWNER_WATER) {
									MakeSea(t);
								} else {
									MakeCanal(t, GetTileOwner(t), Random());
								}
							}
						}
					}
					SetBridgeMiddle(t, axis);
				} else { // ramp
					Axis axis = (Axis)GB(_m[t].m5, 0, 1);
					uint north_south = GB(_m[t].m5, 5, 1);
					DiagDirection dir = ReverseDiagDir(XYNSToDiagDir(axis, north_south));
					TransportType type = (TransportType)GB(_m[t].m5, 1, 2);

					_m[t].m5 = 1 << 7 | type << 2 | dir;
				}
			}
		}

		for (Vehicle *v : Vehicle::Iterate()) {
			if (!v->IsGroundVehicle()) continue;
			if (IsBridgeTile(v->tile)) {
				DiagDirection dir = GetTunnelBridgeDirection(v->tile);

				if (dir != DirToDiagDir(v->direction)) continue;
				switch (dir) {
					default: SlErrorCorrupt("Invalid vehicle direction");
					case DIAGDIR_NE: if ((v->x_pos & 0xF) !=  0)            continue; break;
					case DIAGDIR_SE: if ((v->y_pos & 0xF) != TILE_SIZE - 1) continue; break;
					case DIAGDIR_SW: if ((v->x_pos & 0xF) != TILE_SIZE - 1) continue; break;
					case DIAGDIR_NW: if ((v->y_pos & 0xF) !=  0)            continue; break;
				}
			} else if (v->z_pos > GetTileMaxPixelZ(TileVirtXY(v->x_pos, v->y_pos))) {
				v->tile = GetNorthernBridgeEnd(v->tile);
				v->UpdatePosition();
			} else {
				continue;
			}
			if (v->type == VEH_TRAIN) {
				Train::From(v)->track = TRACK_BIT_WORMHOLE;
			} else {
				RoadVehicle::From(v)->state = RVSB_WORMHOLE;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_ROAD_TYPES) && !SlXvIsFeaturePresent(XSLFI_JOKERPP, SL_JOKER_1_27)) {
		/* Add road subtypes */
		for (TileIndex t = 0; t < map_size; t++) {
			bool has_road = false;
			switch (GetTileType(t)) {
				case MP_ROAD:
					has_road = true;
					break;
				case MP_STATION:
					has_road = IsAnyRoadStop(t);
					break;
				case MP_TUNNELBRIDGE:
					has_road = GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD;
					break;
				default:
					break;
			}

			if (has_road) {
				RoadType road_rt = HasBit(_me[t].m7, 6) ? ROADTYPE_ROAD : INVALID_ROADTYPE;
				RoadType tram_rt = HasBit(_me[t].m7, 7) ? ROADTYPE_TRAM : INVALID_ROADTYPE;

				assert(road_rt != INVALID_ROADTYPE || tram_rt != INVALID_ROADTYPE);
				SetRoadTypes(t, road_rt, tram_rt);
				SB(_me[t].m7, 6, 2, 0); // Clear pre-NRT road type bits.
			}
		}
	} else if (SlXvIsFeaturePresent(XSLFI_JOKERPP, SL_JOKER_1_27)) {
		uint next_road_type = 2;
		uint next_tram_type = 2;
		RoadType road_types[32];
		RoadType tram_types[32];
		MemSetT(road_types, ROADTYPE_ROAD, 31);
		MemSetT(tram_types, ROADTYPE_TRAM, 31);
		road_types[31] = INVALID_ROADTYPE;
		tram_types[31] = INVALID_ROADTYPE;
		for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
			const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
			if (RoadTypeIsRoad(rt)) {
				if (rti->label == 'ROAD') {
					road_types[0] = rt;
				} else if (rti->label == 'ELRD') {
					road_types[1] = rt;
				} else if (next_road_type < 31) {
					road_types[next_road_type++] = rt;
				}
			} else {
				if (rti->label == 'RAIL') {
					tram_types[0] = rt;
				} else if (rti->label == 'ELRL') {
					tram_types[1] = rt;
				} else if (next_tram_type < 31) {
					tram_types[next_tram_type++] = rt;
				}
			}
		}
		for (TileIndex t = 0; t < map_size; t++) {
			bool has_road = false;
			switch (GetTileType(t)) {
				case MP_ROAD:
					has_road = true;
					break;
				case MP_STATION:
					has_road = IsAnyRoadStop(t);
					break;
				case MP_TUNNELBRIDGE:
					has_road = GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD;
					break;
				default:
					break;
			}
			if (has_road) {
				RoadType road_rt = road_types[(GB(_me[t].m7, 6, 1) << 4) | GB(_m[t].m4, 0, 4)];
				RoadType tram_rt = tram_types[(GB(_me[t].m7, 7, 1) << 4) | GB(_m[t].m4, 4, 4)];
				SetRoadTypes(t, road_rt, tram_rt);
				SB(_me[t].m7, 6, 2, 0);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_DUAL_RAIL_TYPES)) {
		/* Introduced dual rail types. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsPlainRailTile(t) || (IsRailTunnelBridgeTile(t) && IsBridge(t))) {
				SetSecondaryRailType(t, GetRailType(t));
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 6)) {
		/* m2 signal state bit allocation has shrunk */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsBridge(t) && IsTunnelBridgeSignalSimulationEntrance(t)) {
				extern void ShiftBridgeEntranceSimulatedSignalsExtended(TileIndex t, int shift, uint64_t in);
				const uint shift = 15 - BRIDGE_M2_SIGNAL_STATE_COUNT;
				ShiftBridgeEntranceSimulatedSignalsExtended(t, shift, GB(_m[t].m2, BRIDGE_M2_SIGNAL_STATE_COUNT, shift));
				SB(_m[t].m2, 0, 15, GB(_m[t].m2, 0, 15) << shift);
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
		/* fix signal tunnel/bridge PBS */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsTunnelBridgeSignalSimulationEntrance(t)) {
				UnreserveAcrossRailTunnelBridge(t);
			}
		}
	}

	if (!SlXvIsFeaturePresent(XSLFI_CUSTOM_BRIDGE_HEADS, 2)) {
		/* change map bits for rail bridge heads */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) {
				SetCustomBridgeHeadTrackBits(t, DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t)));
				SetBridgeReservationTrackBits(t, HasBit(_m[t].m5, 4) ? DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t)) : TRACK_BIT_NONE);
				ClrBit(_m[t].m5, 4);
			}
		}
	}

	if (!SlXvIsFeaturePresent(XSLFI_CUSTOM_BRIDGE_HEADS, 3)) {
		/* fence/ground type support for custom rail bridges */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE)) SB(_me[t].m7, 6, 2, 0);
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_CUSTOM_BRIDGE_HEADS, 1, 3)) {
		/* fix any mismatched road/tram bits */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD) {
				for (RoadTramType rtt : { RTT_TRAM, RTT_ROAD }) {
					RoadType rt = GetRoadType(t, rtt);
					if (rt == INVALID_ROADTYPE) continue;
					RoadBits rb = GetCustomBridgeHeadRoadBits(t, rtt);
					DiagDirection dir = GetTunnelBridgeDirection(t);
					if (!(rb & DiagDirToRoadBits(dir))) continue;

					if (HasAtMostOneBit(rb)) {
						DEBUG(misc, 0, "Fixing road bridge head state (case A) at tile 0x%X", t);
						rb |= DiagDirToRoadBits(ReverseDiagDir(dir));
						SetCustomBridgeHeadRoadBits(t, rtt, rb);
					}

					TileIndex end = GetOtherBridgeEnd(t);
					if (GetRoadType(end, rtt) == INVALID_ROADTYPE) {
						DEBUG(misc, 0, "Fixing road bridge head state (case B) at tile 0x%X -> 0x%X", t, end);
						SetRoadType(end, rtt, rt);
						SetCustomBridgeHeadRoadBits(end, rtt, AxisToRoadBits(DiagDirToAxis(dir)));
						continue;
					}

					if (GetRoadType(end, rtt) != rt) {
						DEBUG(misc, 0, "Fixing road bridge head state (case C) at tile 0x%X -> 0x%X", t, end);
						SetRoadType(end, rtt, rt);
					}

					RoadBits end_rb = GetCustomBridgeHeadRoadBits(end, rtt);
					if (!(end_rb & DiagDirToRoadBits(ReverseDiagDir(dir)))) {
						DEBUG(misc, 0, "Fixing road bridge head state (case D) at tile 0x%X -> 0x%X", t, end);
						end_rb |= DiagDirToRoadBits(ReverseDiagDir(dir));
						if (HasAtMostOneBit(end_rb)) end_rb |= DiagDirToRoadBits(dir);
						SetCustomBridgeHeadRoadBits(end, rtt, end_rb);
					}
				}
			}
		}
	}

	/* Elrails got added in rev 24 */
	if (IsSavegameVersionBefore(SLV_24)) {
		RailType min_rail = RAILTYPE_ELECTRIC;

		for (Train *v : Train::Iterate()) {
			RailType rt = RailVehInfo(v->engine_type)->railtype;

			v->railtype = rt;
			if (rt == RAILTYPE_ELECTRIC) min_rail = RAILTYPE_RAIL;
		}

		/* .. so we convert the entire map from normal to elrail (so maintain "fairness") */
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_RAILWAY:
					SetRailType(t, UpdateRailType(GetRailType(t), min_rail));
					break;

				case MP_ROAD:
					if (IsLevelCrossing(t)) {
						SetRailType(t, UpdateRailType(GetRailType(t), min_rail));
					}
					break;

				case MP_STATION:
					if (HasStationRail(t)) {
						SetRailType(t, UpdateRailType(GetRailType(t), min_rail));
					}
					break;

				case MP_TUNNELBRIDGE:
					if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) {
						SetRailType(t, UpdateRailType(GetRailType(t), min_rail));
					}
					break;

				default:
					break;
			}
			if (IsPlainRailTile(t) || (IsRailTunnelBridgeTile(t) && IsBridge(t))) {
				SetSecondaryRailType(t, GetRailType(t));
			}
		}

		for (Train *v : Train::IterateFrontOnly()) {
			if (v->IsFrontEngine() || v->IsFreeWagon()) v->ConsistChanged(CCF_TRACK);
		}

	}

	/* In version 16.1 of the savegame a company can decide if trains, which get
	 * replaced, shall keep their old length. In all prior versions, just default
	 * to false */
	if (IsSavegameVersionBefore(SLV_16, 1)) {
		for (Company *c : Company::Iterate()) c->settings.renew_keep_length = false;
	}

	if (IsSavegameVersionBefore(SLV_123)) {
		/* Waypoints became subclasses of stations ... */
		MoveWaypointsToBaseStations();
		/* ... and buoys were moved to waypoints. */
		MoveBuoysToWaypoints();
	}

	/* From version 15, we moved a semaphore bit from bit 2 to bit 3 in m4, making
	 *  room for PBS. Now in version 21 move it back :P. */
	if (IsSavegameVersionBefore(SLV_21) && !IsSavegameVersionBefore(SLV_15)) {
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_RAILWAY:
					if (HasSignals(t)) {
						/* Original signal type/variant was stored in m4 but since saveload
						 * version 48 they are in m2. The bits has been already moved to m2
						 * (see the code somewhere above) so don't use m4, use m2 instead. */

						/* convert PBS signals to combo-signals */
						if (HasBit(_m[t].m2, 2)) SB(_m[t].m2, 0, 2, SIGTYPE_COMBO);

						/* move the signal variant back */
						SB(_m[t].m2, 2, 1, HasBit(_m[t].m2, 3) ? SIG_SEMAPHORE : SIG_ELECTRIC);
						ClrBit(_m[t].m2, 3);
					}

					/* Clear PBS reservation on track */
					if (!IsRailDepotTile(t)) {
						SB(_m[t].m4, 4, 4, 0);
					} else {
						ClrBit(_m[t].m3, 6);
					}
					break;

				case MP_STATION: // Clear PBS reservation on station
					ClrBit(_m[t].m3, 6);
					break;

				default: break;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_25)) {
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			rv->vehstatus &= ~0x40;
		}
	}

	if (IsSavegameVersionBefore(SLV_26)) {
		for (Station *st : Station::Iterate()) {
			for (CargoID c = 0; c < NUM_CARGO; c++) {
				st->goods[c].last_vehicle_type = VEH_INVALID;
			}
		}
	}

	YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);

	if (IsSavegameVersionBefore(SLV_34)) {
		for (Company *c : Company::Iterate()) ResetCompanyLivery(c);
	}

	for (Company *c : Company::Iterate()) {
		c->avail_railtypes = GetCompanyRailTypes(c->index);
		c->avail_roadtypes = GetCompanyRoadTypes(c->index);
	}

	AfterLoadStations();

	/* Time starts at 0 instead of 1920.
	 * Account for this in older games by adding an offset */
	if (IsSavegameVersionBefore(SLV_31)) {
		CalTime::Detail::now.cal_date += CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
		EconTime::Detail::now.econ_date += EconTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
		CalTime::Detail::now.cal_ymd = CalTime::ConvertDateToYMD(CalTime::CurDate());
		EconTime::Detail::now.econ_ymd = EconTime::ConvertDateToYMD(EconTime::CurDate());
		RecalculateStateTicksOffset();
		UpdateCachedSnowLine();

		for (Station *st : Station::Iterate())   st->build_date      += CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
		for (Waypoint *wp : Waypoint::Iterate()) wp->build_date      += CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
		for (Engine *e : Engine::Iterate())      e->intro_date       += CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
		for (Company *c : Company::Iterate())    c->inaugurated_year += CalTime::ORIGINAL_BASE_YEAR.AsDelta();
		for (Industry *i : Industry::Iterate())  i->last_prod_year   += EconTime::ORIGINAL_BASE_YEAR.AsDelta();

		for (Vehicle *v : Vehicle::Iterate()) {
			v->date_of_last_service += EconTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
			v->build_year += CalTime::ORIGINAL_BASE_YEAR.AsDelta();
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_VARIABLE_DAY_LENGTH, 6)) {
		EconTime::Detail::years_elapsed = EconTime::CurYear().base() - 1;
		EconTime::Detail::period_display_offset = 0;
		for (Company *c : Company::Iterate()) {
			if (SlXvIsFeaturePresent(XSLFI_VARIABLE_DAY_LENGTH, 5, 5)) {
				/* inaugurated_year is calendar time in XSLFI_VARIABLE_DAY_LENGTH version 5 */
				c->age_years = std::max<YearDelta>(0, CalTime::CurYear() - c->inaugurated_year);
				c->display_inaugurated_period = EconTime::Detail::WallClockYearToDisplay(c->inaugurated_year.base() + EconTime::CurYear().base() - CalTime::CurYear().base());
			} else {
				c->age_years = std::max<YearDelta>(0, EconTime::CurYear().base() - c->inaugurated_year.base());
				c->display_inaugurated_period = EconTime::Detail::WallClockYearToDisplay(c->inaugurated_year.base());
				c->inaugurated_year += CalTime::CurYear().base() - EconTime::CurYear().base();
			}
		}
	}

	/* From 32 on we save the industry who made the farmland.
	 *  To give this prettiness to old savegames, we remove all farmfields and
	 *  plant new ones. */
	if (IsSavegameVersionBefore(SLV_32)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_CLEAR) && IsClearGround(t, CLEAR_FIELDS)) {
				/* remove fields */
				MakeClear(t, CLEAR_GRASS, 3);
			}
		}

		for (Industry *i : Industry::Iterate()) {
			uint j;

			if (GetIndustrySpec(i->type)->behaviour & INDUSTRYBEH_PLANT_ON_BUILT) {
				for (j = 0; j != 50; j++) PlantRandomFarmField(i);
			}
		}
	}

	/* Setting no refit flags to all orders in savegames from before refit in orders were added */
	if (IsSavegameVersionBefore(SLV_36)) {
		IterateVehicleAndOrderListOrders([](Order *order) {
			order->SetRefit(CARGO_NO_REFIT);
		});
	}

	/* from version 38 we have optional elrails, since we cannot know the
	 * preference of a user, let elrails enabled; it can be disabled manually */
	if (IsSavegameVersionBefore(SLV_38)) _settings_game.vehicle.disable_elrails = false;
	/* do the same as when elrails were enabled/disabled manually just now */
	SettingsDisableElrail(_settings_game.vehicle.disable_elrails);
	InitializeRailGUI();

	/* From version 53, the map array was changed for house tiles to allow
	 * space for newhouses grf features. A new byte, m7, was also added. */
	if (IsSavegameVersionBefore(SLV_53)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_HOUSE)) {
				if (GB(_m[t].m3, 6, 2) != TOWN_HOUSE_COMPLETED) {
					/* Move the construction stage from m3[7..6] to m5[5..4].
					 * The construction counter does not have to move. */
					SB(_m[t].m5, 3, 2, GB(_m[t].m3, 6, 2));
					SB(_m[t].m3, 6, 2, 0);

					/* The "house is completed" bit is now in m6[2]. */
					SetHouseCompleted(t, false);
				} else {
					/* The "lift has destination" bit has been moved from
					 * m5[7] to m7[0]. */
					SB(_me[t].m7, 0, 1, HasBit(_m[t].m5, 7));
					ClrBit(_m[t].m5, 7);

					/* The "lift is moving" bit has been removed, as it does
					 * the same job as the "lift has destination" bit. */
					ClrBit(_m[t].m1, 7);

					/* The position of the lift goes from m1[7..0] to m6[7..2],
					 * making m1 totally free, now. The lift position does not
					 * have to be a full byte since the maximum value is 36. */
					SetLiftPosition(t, GB(_m[t].m1, 0, 6 ));

					_m[t].m1 = 0;
					_m[t].m3 = 0;
					SetHouseCompleted(t, true);
				}
			}
		}
	}

	/* Check and update house and town values */
	UpdateHousesAndTowns(gcf_res != GLC_ALL_GOOD, true);

	if (IsSavegameVersionBefore(SLV_43)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_INDUSTRY)) {
				switch (GetIndustryGfx(t)) {
					case GFX_POWERPLANT_SPARKS:
						_m[t].m3 = GB(_m[t].m1, 2, 5);
						break;

					case GFX_OILWELL_ANIMATED_1:
					case GFX_OILWELL_ANIMATED_2:
					case GFX_OILWELL_ANIMATED_3:
						_m[t].m3 = GB(_m[t].m1, 0, 2);
						break;

					case GFX_COAL_MINE_TOWER_ANIMATED:
					case GFX_COPPER_MINE_TOWER_ANIMATED:
					case GFX_GOLD_MINE_TOWER_ANIMATED:
						 _m[t].m3 = _m[t].m1;
						 break;

					default: // No animation states to change
						break;
				}
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_45)) {
		/* Originally just the fact that some cargo had been paid for was
		 * stored to stop people cheating and cashing in several times. This
		 * wasn't enough though as it was cleared when the vehicle started
		 * loading again, even if it didn't actually load anything, so now the
		 * amount that has been paid is stored. */
		for (Vehicle *v : Vehicle::Iterate()) {
			ClrBit(v->vehicle_flags, 2);
		}
	}

	/* Buoys do now store the owner of the previous water tile, which can never
	 * be OWNER_NONE. So replace OWNER_NONE with OWNER_WATER. */
	if (IsSavegameVersionBefore(SLV_46)) {
		for (Waypoint *wp : Waypoint::Iterate()) {
			if ((wp->facilities & FACIL_DOCK) != 0 && IsTileOwner(wp->xy, OWNER_NONE) && TileHeight(wp->xy) == 0) SetTileOwner(wp->xy, OWNER_WATER);
		}
	}

	if (IsSavegameVersionBefore(SLV_50)) {
		/* Aircraft units changed from 8 mph to 1 km-ish/h */
		for (Aircraft *v : Aircraft::Iterate()) {
			if (v->subtype <= AIR_AIRCRAFT) {
				const AircraftVehicleInfo *avi = AircraftVehInfo(v->engine_type);
				v->cur_speed *= 128;
				v->cur_speed /= 10;
				v->acceleration = avi->acceleration;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_49)) for (Company *c : Company::Iterate()) c->face = ConvertFromOldCompanyManagerFace(c->face);

	if (IsSavegameVersionBefore(SLV_52)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_OBJECT) && _m[t].m5 == OBJECT_STATUE) {
				_m[t].m2 = CalcClosestTownFromTile(t)->index;
			}
		}
	}

	/* A setting containing the proportion of towns that grow twice as
	 * fast was added in version 54. From version 56 this is now saved in the
	 * town as cities can be built specifically in the scenario editor. */
	if (IsSavegameVersionBefore(SLV_56)) {
		for (Town *t : Town::Iterate()) {
			if (_settings_game.economy.larger_towns != 0 && (t->index % _settings_game.economy.larger_towns) == 0) {
				t->larger_town = true;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_57)) {
		/* Added a FIFO queue of vehicles loading at stations */
		for (Vehicle *v : Vehicle::Iterate()) {
			if ((v->type != VEH_TRAIN || Train::From(v)->IsFrontEngine()) &&  // for all locs
					!(v->vehstatus & (VS_STOPPED | VS_CRASHED)) && // not stopped or crashed
					v->current_order.IsType(OT_LOADING)) {         // loading
				Station::Get(v->last_station_visited)->loading_vehicles.push_back(v);

				/* The loading finished flag is *only* set when actually completely
				 * finished. Because the vehicle is loading, it is not finished. */
				ClrBit(v->vehicle_flags, VF_LOADING_FINISHED);
			}
		}
	} else if (IsSavegameVersionBefore(SLV_59)) {
		/* For some reason non-loading vehicles could be in the station's loading vehicle list */

		for (Station *st : Station::Iterate()) {
			st->loading_vehicles.erase(std::remove_if(st->loading_vehicles.begin(), st->loading_vehicles.end(),
				[](Vehicle *v) {
					return !v->current_order.IsType(OT_LOADING);
				}), st->loading_vehicles.end());
		}
	}

	if (IsSavegameVersionBefore(SLV_58)) {
		/* Setting difficulty industry_density other than zero get bumped to +1
		 * since a new option (very low at position 1) has been added */
		if (_settings_game.difficulty.industry_density > 0) {
			_settings_game.difficulty.industry_density++;
		}

		/* Same goes for number of towns, although no test is needed, just an increment */
		_settings_game.difficulty.number_towns++;
	}

	if (IsSavegameVersionBefore(SLV_64)) {
		/* Since now we allow different signal types and variants on a single tile.
		 * Move signal states to m4 to make room and clone the signal type/variant. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				/* move signal states */
				SetSignalStates(t, GB(_m[t].m2, 4, 4));
				SB(_m[t].m2, 4, 4, 0);
				/* clone signal type and variant */
				SB(_m[t].m2, 4, 3, GB(_m[t].m2, 0, 3));
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_69)) {
		/* In some old savegames a bit was cleared when it should not be cleared */
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (rv->state == 250 || rv->state == 251) {
				SetBit(rv->state, 2);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_70)) {
		/* Added variables to support newindustries */
		for (Industry *i : Industry::Iterate()) i->founder = OWNER_NONE;
	}

	/* From version 82, old style canals (above sealevel (0), WATER owner) are no longer supported.
	    Replace the owner for those by OWNER_NONE. */
	if (IsSavegameVersionBefore(SLV_82)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_WATER) &&
					GetWaterTileType(t) == WATER_TILE_CLEAR &&
					GetTileOwner(t) == OWNER_WATER &&
					TileHeight(t) != 0) {
				SetTileOwner(t, OWNER_NONE);
			}
		}
	}

	/*
	 * Add the 'previous' owner to the ship depots so we can reset it with
	 * the correct values when it gets destroyed. This prevents that
	 * someone can remove canals owned by somebody else and it prevents
	 * making floods using the removal of ship depots.
	 */
	if (IsSavegameVersionBefore(SLV_83)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsShipDepotTile(t)) {
				_m[t].m4 = (TileHeight(t) == 0) ? OWNER_WATER : OWNER_NONE;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_74)) {
		for (Station *st : Station::Iterate()) {
			for (GoodsEntry &ge : st->goods) {
				ge.last_speed = 0;
				if (ge.CargoAvailableCount() != 0) SetBit(ge.status, GoodsEntry::GES_RATING);
			}
		}
	}

	/* At version 78, industry cargo types can be changed, and are stored with the industry. For older save versions
	 * copy the IndustrySpec's cargo types over to the Industry. */
	if (IsSavegameVersionBefore(SLV_78)) {
		for (Industry *i : Industry::Iterate()) {
			const IndustrySpec *indsp = GetIndustrySpec(i->type);
			for (size_t j = 0; j < std::size(i->produced_cargo); j++) {
				i->produced_cargo[j] = indsp->produced_cargo[j];
			}
			for (size_t j = 0; j < std::size(i->accepts_cargo); j++) {
				i->accepts_cargo[j] = indsp->accepts_cargo[j];
			}
		}
	}

	/* Before version 81, the density of grass was always stored as zero, and
	 * grassy trees were always drawn fully grassy. Furthermore, trees on rough
	 * land used to have zero density, now they have full density. Therefore,
	 * make all grassy/rough land trees have a density of 3. */
	if (IsSavegameVersionBefore(SLV_81)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (GetTileType(t) == MP_TREES) {
				TreeGround groundType = (TreeGround)GB(_m[t].m2, 4, 2);
				if (groundType != TREE_GROUND_SNOW_DESERT) SB(_m[t].m2, 6, 2, 3);
			}
		}
	}


	if (IsSavegameVersionBefore(SLV_93)) {
		/* Rework of orders. */
		for (Order *order : Order::Iterate()) order->ConvertFromOldSavegame();

		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->orders != nullptr && v->orders->GetFirstOrder() != nullptr && v->orders->GetFirstOrder()->IsType(OT_NOTHING)) {
				v->orders->FreeChain();
				v->orders = nullptr;
			}

			v->current_order.ConvertFromOldSavegame();
			if (v->type == VEH_ROAD && v->IsPrimaryVehicle() && v->FirstShared() == v) {
				for (Order *order : v->Orders()) order->SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
			}
		}
		IntialiseOrderDestinationRefcountMap();
	} else if (IsSavegameVersionBefore(SLV_94)) {
		/* Unload and transfer are now mutual exclusive. */
		IterateVehicleAndOrderListOrders([](Order *order) {
			if ((order->GetUnloadType() & (OUFB_UNLOAD | OUFB_TRANSFER)) == (OUFB_UNLOAD | OUFB_TRANSFER)) {
				order->SetUnloadType(OUFB_TRANSFER);
				order->SetLoadType(OLFB_NO_LOAD);
			}
		});
	}

	if (IsSavegameVersionBefore(SLV_DEPOT_UNBUNCHING) && SlXvIsFeatureMissing(XSLFI_DEPOT_UNBUNCHING)) {
		/* OrderDepotActionFlags were moved, instead of starting at bit 4 they now start at bit 3,
		 * this clobbers the wait is timetabled flag of XSLFI_TT_WAIT_IN_DEPOT (version 1). */
		IterateVehicleAndOrderListOrders([](Order *order) {
			if (!order->IsType(OT_GOTO_DEPOT)) return;
			if (SlXvIsFeaturePresent(XSLFI_TT_WAIT_IN_DEPOT, 1, 1)) {
				/* Bit 3 was previously the wait is timetabled flag, move that to xflags (version 2 of XSLFI_TT_WAIT_IN_DEPOT) */
				order->SetWaitTimetabled(HasBit(order->GetRawFlags(), 3));
			}
			OrderDepotActionFlags flags = (OrderDepotActionFlags)(order->GetDepotActionType() >> 1);
			order->SetDepotActionType(flags);
		});
	} else if (SlXvIsFeaturePresent(XSLFI_TT_WAIT_IN_DEPOT, 1, 1)) {
		IterateVehicleAndOrderListOrders([](Order *order) {
			/* Bit 3 was previously the wait is timetabled flag, move that to xflags (version 2 of XSLFI_TT_WAIT_IN_DEPOT) */
			if (order->IsType(OT_GOTO_DEPOT)) order->SetWaitTimetabled(HasBit(order->GetRawFlags(), 3));
		});
	}
	if (!IsSavegameVersionBefore(SLV_DEPOT_UNBUNCHING)) {
		/* Move unbunch depot action from bit 2 to bit 3 */
		IterateVehicleAndOrderListOrders([](Order *order) {
			if (!order->IsType(OT_GOTO_DEPOT)) return;
			OrderDepotActionFlags flags = order->GetDepotActionType();
			if ((flags & ODATFB_SELL) != 0) {
				flags ^= (ODATFB_SELL | ODATFB_UNBUNCH); // Move unbunch from bit 2 to bit 3 (sell to unbunch)
				order->SetDepotActionType(flags);
			}
		});
	}

	if (SlXvIsFeaturePresent(XSLFI_JOKERPP, 1, SL_JOKER_1_23)) {
		for (Order *order : Order::Iterate()) {
			if (order->IsType(OT_CONDITIONAL) && order->GetConditionVariable() == OCV_SLOT_OCCUPANCY) {
				order->GetXDataRef() = order->GetConditionValue();
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_84)) {
		/* Set all share owners to INVALID_COMPANY for
		 * 1) all inactive companies
		 *     (when inactive companies were stored in the savegame - TTD, TTDP and some
		 *      *really* old revisions of OTTD; else it is already set in InitializeCompanies())
		 * 2) shares that are owned by inactive companies or self
		 *     (caused by cheating clients in earlier revisions) */
		for (Company *c : Company::Iterate()) {
			for (auto &share_owner : c->share_owners) {
				if (share_owner == INVALID_COMPANY) continue;
				if (!Company::IsValidID(share_owner) || share_owner == c->index) share_owner = INVALID_COMPANY;
			}
		}
	}

	/* The water class was moved/unified. */
	if (IsSavegameVersionBefore(SLV_146)) {
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_STATION:
					switch (GetStationType(t)) {
						case STATION_OILRIG:
						case STATION_DOCK:
						case STATION_BUOY:
							SetWaterClass(t, (WaterClass)GB(_m[t].m3, 0, 2));
							SB(_m[t].m3, 0, 2, 0);
							break;

						default:
							SetWaterClass(t, WATER_CLASS_INVALID);
							break;
					}
					break;

				case MP_WATER:
					SetWaterClass(t, (WaterClass)GB(_m[t].m3, 0, 2));
					SB(_m[t].m3, 0, 2, 0);
					break;

				case MP_OBJECT:
					SetWaterClass(t, WATER_CLASS_INVALID);
					break;

				default:
					/* No water class. */
					break;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_86)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* Move river flag and update canals to use water class */
			if (IsTileType(t, MP_WATER)) {
				if (GetWaterClass(t) != WATER_CLASS_RIVER) {
					if (IsWater(t)) {
						Owner o = GetTileOwner(t);
						if (o == OWNER_WATER) {
							MakeSea(t);
						} else {
							MakeCanal(t, o, Random());
						}
					} else if (IsShipDepot(t)) {
						Owner o = (Owner)_m[t].m4; // Original water owner
						SetWaterClass(t, o == OWNER_WATER ? WATER_CLASS_SEA : WATER_CLASS_CANAL);
					}
				}
			}
		}

		/* Update locks, depots, docks and buoys to have a water class based
		 * on its neighbouring tiles. Done after river and canal updates to
		 * ensure neighbours are correct. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsTileFlat(t)) continue;

			if (IsTileType(t, MP_WATER) && IsLock(t)) SetWaterClassDependingOnSurroundings(t, false);
			if (IsTileType(t, MP_STATION) && (IsDock(t) || IsBuoy(t))) SetWaterClassDependingOnSurroundings(t, false);
		}
	}

	if (IsSavegameVersionBefore(SLV_87)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* skip oil rigs at borders! */
			if ((IsTileType(t, MP_WATER) || IsBuoyTile(t)) &&
					(TileX(t) == 0 || TileY(t) == 0 || TileX(t) == MapMaxX() - 1 || TileY(t) == MapMaxY() - 1)) {
				/* Some version 86 savegames have wrong water class at map borders (under buoy, or after removing buoy).
				 * This conversion has to be done before buoys with invalid owner are removed. */
				SetWaterClass(t, WATER_CLASS_SEA);
			}

			if (IsBuoyTile(t) || IsDriveThroughStopTile(t) || IsTileType(t, MP_WATER)) {
				Owner o = GetTileOwner(t);
				if (o < MAX_COMPANIES && !Company::IsValidID(o)) {
					Backup<CompanyID> cur_company(_current_company, o, FILE_LINE);
					ChangeTileOwner(t, o, INVALID_OWNER);
					cur_company.Restore();
				}
				if (IsBuoyTile(t)) {
					/* reset buoy owner to OWNER_NONE in the station struct
					 * (even if it is owned by active company) */
					Waypoint::GetByTile(t)->owner = OWNER_NONE;
				}
			} else if (IsTileType(t, MP_ROAD)) {
				/* works for all RoadTileType */
				for (RoadTramType rtt : _roadtramtypes) {
					/* update even non-existing road types to update tile owner too */
					Owner o = GetRoadOwner(t, rtt);
					if (o < MAX_COMPANIES && !Company::IsValidID(o)) SetRoadOwner(t, rtt, OWNER_NONE);
				}
				if (IsLevelCrossing(t)) {
					if (!Company::IsValidID(GetTileOwner(t))) FixOwnerOfRailTrack(t);
				}
			} else if (IsPlainRailTile(t)) {
				if (!Company::IsValidID(GetTileOwner(t))) FixOwnerOfRailTrack(t);
			}
		}

		/* Convert old PF settings to new */
		if (_settings_game.pf.yapf.rail_use_yapf || IsSavegameVersionBefore(SLV_28)) {
			_settings_game.pf.pathfinder_for_trains = VPF_YAPF;
		} else {
			_settings_game.pf.pathfinder_for_trains = VPF_NPF;
		}

		if (_settings_game.pf.yapf.road_use_yapf || IsSavegameVersionBefore(SLV_28)) {
			_settings_game.pf.pathfinder_for_roadvehs = VPF_YAPF;
		} else {
			_settings_game.pf.pathfinder_for_roadvehs = VPF_NPF;
		}

		if (_settings_game.pf.yapf.ship_use_yapf) {
			_settings_game.pf.pathfinder_for_ships = VPF_YAPF;
		} else {
			_settings_game.pf.pathfinder_for_ships = VPF_NPF;
		}
	}

	if (IsSavegameVersionBefore(SLV_88)) {
		/* Profits are now with 8 bit fract */
		for (Vehicle *v : Vehicle::Iterate()) {
			v->profit_this_year <<= 8;
			v->profit_last_year <<= 8;
			v->running_ticks = 0;
		}
	}

	if (IsSavegameVersionBefore(SLV_91)) {
		/* Increase HouseAnimationFrame from 5 to 7 bits */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_HOUSE) && GetHouseType(t) >= NEW_HOUSE_OFFSET) {
				SB(_me[t].m6, 2, 6, GB(_me[t].m6, 3, 5));
				SB(_m[t].m3, 5, 1, 0);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_62)) {
		GroupStatistics::UpdateAfterLoad(); // Ensure statistics pool is initialised before trying to delete vehicles
		/* Remove all trams from savegames without tram support.
		 * There would be trams without tram track under causing crashes sooner or later. */
		for (RoadVehicle *v : RoadVehicle::IterateFrontOnly()) {
			if (HasBit(EngInfo(v->engine_type)->misc_flags, EF_ROAD_TRAM)) {
				ShowErrorMessage(STR_WARNING_LOADGAME_REMOVED_TRAMS, INVALID_STRING_ID, WL_CRITICAL);
				delete v;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_99)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* Set newly introduced WaterClass of industry tiles */
			if (IsTileType(t, MP_STATION) && IsOilRig(t)) {
				SetWaterClassDependingOnSurroundings(t, true);
			}
			if (IsTileType(t, MP_INDUSTRY)) {
				if ((GetIndustrySpec(GetIndustryType(t))->behaviour & INDUSTRYBEH_BUILT_ONWATER) != 0) {
					SetWaterClassDependingOnSurroundings(t, true);
				} else {
					SetWaterClass(t, WATER_CLASS_INVALID);
				}
			}

			/* Replace "house construction year" with "house age" */
			if (IsTileType(t, MP_HOUSE) && IsHouseCompleted(t)) {
				_m[t].m5 = ClampTo<uint8_t>(CalTime::CurYear() - (_m[t].m5 + CalTime::ORIGINAL_BASE_YEAR.base()));
			}
		}
	}

	/* Tunnel pool has to be initiated before reservations. */
	if (SlXvIsFeatureMissing(XSLFI_CHUNNEL)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTunnelTile(t)) {
				DiagDirection dir = GetTunnelBridgeDirection(t);
				if (dir == DIAGDIR_SE || dir == DIAGDIR_SW) {
					TileIndex start_tile = t;
					TileIndex end_tile = GetOtherTunnelBridgeEndOld(start_tile);

					if (!Tunnel::CanAllocateItem()) {
						SetSaveLoadError(STR_ERROR_TUNNEL_TOO_MANY);
						/* Restore the signals */
						ResetSignalHandlers();
						return false;
					}

					const Tunnel *t = new Tunnel(start_tile, end_tile, TileHeight(start_tile), false);

					SetTunnelIndex(start_tile, t->index);
					SetTunnelIndex(end_tile, t->index);
				}
			}
		}
	}

	/* Move the signal variant back up one bit for PBS. We don't convert the old PBS
	 * format here, as an old layout wouldn't work properly anyway. To be safe, we
	 * clear any possible PBS reservations as well. */
	if (IsSavegameVersionBefore(SLV_100)) {
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_RAILWAY:
					if (HasSignals(t)) {
						/* move the signal variant */
						SetSignalVariant(t, TRACK_UPPER, HasBit(_m[t].m2, 2) ? SIG_SEMAPHORE : SIG_ELECTRIC);
						SetSignalVariant(t, TRACK_LOWER, HasBit(_m[t].m2, 6) ? SIG_SEMAPHORE : SIG_ELECTRIC);
						ClrBit(_m[t].m2, 2);
						ClrBit(_m[t].m2, 6);
					}

					/* Clear PBS reservation on track */
					if (IsRailDepot(t)) {
						SetDepotReservation(t, false);
					} else {
						SetTrackReservation(t, TRACK_BIT_NONE);
					}
					break;

				case MP_ROAD: // Clear PBS reservation on crossing
					if (IsLevelCrossing(t)) SetCrossingReservation(t, false);
					break;

				case MP_STATION: // Clear PBS reservation on station
					if (HasStationRail(t)) SetRailStationReservation(t, false);
					break;

				case MP_TUNNELBRIDGE: // Clear PBS reservation on tunnels/bridges
					if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) UnreserveAcrossRailTunnelBridge(t);
					break;

				default: break;
			}
		}
	}

	/* Reserve all tracks trains are currently on. */
	if (IsSavegameVersionBefore(SLV_101)) {
		for (const Train *t : Train::IterateFrontOnly()) {
			t->ReserveTrackUnderConsist();
		}
	}

	if (IsSavegameVersionBefore(SLV_102)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* Now all crossings should be in correct state */
			if (IsLevelCrossingTile(t)) UpdateLevelCrossing(t, false);
		}
	}

	if (IsSavegameVersionBefore(SLV_103)) {
		/* Non-town-owned roads now store the closest town */
		UpdateNearestTownForRoadTiles(false);

		/* signs with invalid owner left from older savegames */
		for (Sign *si : Sign::Iterate()) {
			if (si->owner != OWNER_NONE && !Company::IsValidID(si->owner)) si->owner = OWNER_NONE;
		}

		/* Station can get named based on an industry type, but the current ones
		 * are not, so mark them as if they are not named by an industry. */
		for (Station *st : Station::Iterate()) {
			st->indtype = IT_INVALID;
		}
	}

	if (IsSavegameVersionBefore(SLV_104)) {
		for (Aircraft *a : Aircraft::Iterate()) {
			/* Set engine_type of shadow and rotor */
			if (!a->IsNormalAircraft()) {
				a->engine_type = a->First()->engine_type;
			}
		}

		/* More companies ... */
		for (Company *c : Company::Iterate()) {
			if (c->bankrupt_asked == 0xFF) c->bankrupt_asked = MAX_UVALUE(CompanyMask);
		}

		for (Engine *e : Engine::Iterate()) {
			if (e->company_avail == 0xFF) e->company_avail = MAX_UVALUE(CompanyMask);
		}

		for (Town *t : Town::Iterate()) {
			if (t->have_ratings == 0xFF) t->have_ratings = MAX_UVALUE(CompanyMask);
			for (uint i = 8; i != MAX_COMPANIES; i++) t->ratings[i] = RATING_INITIAL;
		}
	}

	if (IsSavegameVersionBefore(SLV_112)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* Check for HQ bit being set, instead of using map accessor,
			 * since we've already changed it code-wise */
			if (IsTileType(t, MP_OBJECT) && HasBit(_m[t].m5, 7)) {
				/* Move size and part identification of HQ out of the m5 attribute,
				 * on new locations */
				_m[t].m3 = GB(_m[t].m5, 0, 5);
				_m[t].m5 = OBJECT_HQ;
			}
		}
	}
	if (IsSavegameVersionBefore(SLV_144)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsTileType(t, MP_OBJECT)) continue;

			/* Reordering/generalisation of the object bits. */
			ObjectType type = _m[t].m5;
			SB(_me[t].m6, 2, 4, type == OBJECT_HQ ? GB(_m[t].m3, 2, 3) : 0);
			_m[t].m3 = type == OBJECT_HQ ? GB(_m[t].m3, 1, 1) | GB(_m[t].m3, 0, 1) << 4 : 0;

			/* Make sure those bits are clear as well! */
			_m[t].m4 = 0;
			_me[t].m7 = 0;
		}
	}

	if (IsSavegameVersionBefore(SLV_147) && Object::GetNumItems() == 0) {
		/* Make real objects for object tiles. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsTileType(t, MP_OBJECT)) continue;

			if (Town::GetNumItems() == 0) {
				/* No towns, so remove all objects! */
				DoClearSquare(t);
			} else {
				uint offset = _m[t].m3;

				/* Also move the animation state. */
				_m[t].m3 = GB(_me[t].m6, 2, 4);
				SB(_me[t].m6, 2, 4, 0);

				if (offset == 0) {
					/* No offset, so make the object. */
					ObjectType type = _m[t].m5;
					int size = type == OBJECT_HQ ? 2 : 1;

					if (!Object::CanAllocateItem()) {
						/* Nice... you managed to place 64k lighthouses and
						 * antennae on the map... boohoo. */
						SlError(STR_ERROR_TOO_MANY_OBJECTS);
					}

					Object *o = new Object();
					o->location.tile = t;
					o->location.w    = size;
					o->location.h    = size;
					o->build_date    = CalTime::CurDate();
					o->town          = type == OBJECT_STATUE ? Town::Get(_m[t].m2) : CalcClosestTownFromTile(t, UINT_MAX);
					_m[t].m2 = o->index;
					Object::IncTypeCount(type);
				} else {
					/* We're at an offset, so get the ID from our "root". */
					TileIndex northern_tile = t - TileXY(GB(offset, 0, 4), GB(offset, 4, 4));
					assert_tile(IsTileType(northern_tile, MP_OBJECT), northern_tile);
					_m[t].m2 = _m[northern_tile].m2;
				}
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_113)) {
		/* allow_town_roads is added, set it if town_layout wasn't TL_NO_ROADS */
		if (_settings_game.economy.town_layout == 0) { // was TL_NO_ROADS
			_settings_game.economy.allow_town_roads = false;
			_settings_game.economy.town_layout = TL_BETTER_ROADS;
		} else {
			_settings_game.economy.allow_town_roads = true;
			_settings_game.economy.town_layout = static_cast<TownLayout>(_settings_game.economy.town_layout - 1);
		}

		/* Initialize layout of all towns. Older versions were using different
		 * generator for random town layout, use it if needed. */
		for (Town *t : Town::Iterate()) {
			if (_settings_game.economy.town_layout != TL_RANDOM) {
				t->layout = _settings_game.economy.town_layout;
				continue;
			}

			/* Use old layout randomizer code */
			byte layout = TileHash(TileX(t->xy), TileY(t->xy)) % 6;
			switch (layout) {
				default: break;
				case 5: layout = 1; break;
				case 0: layout = 2; break;
			}
			t->layout = static_cast<TownLayout>(layout - 1);
		}
	}

	if (IsSavegameVersionBefore(SLV_114)) {
		/* There could be (deleted) stations with invalid owner, set owner to OWNER NONE.
		 * The conversion affects oil rigs and buoys too, but it doesn't matter as
		 * they have st->owner == OWNER_NONE already. */
		for (Station *st : Station::Iterate()) {
			if (!Company::IsValidID(st->owner)) st->owner = OWNER_NONE;
		}
	}

	/* Trains could now stop in a specific location. */
	if (IsSavegameVersionBefore(SLV_117)) {
		IterateVehicleAndOrderListOrders([](Order *o) {
			if (o->IsType(OT_GOTO_STATION)) o->SetStopLocation(OSL_PLATFORM_FAR_END);
		});
	}

	if (IsSavegameVersionBefore(SLV_120)) {
		extern VehicleDefaultSettings _old_vds;
		for (Company *c : Company::Iterate()) {
			c->settings.vehicle = _old_vds;
		}
	}

	if (IsSavegameVersionBefore(SLV_121)) {
		/* Delete small ufos heading for non-existing vehicles */
		for (DisasterVehicle *v : DisasterVehicle::Iterate()) {
			if (v->subtype == 2 /* ST_SMALL_UFO */ && v->state != 0) {
				const Vehicle *u = Vehicle::GetIfValid(v->dest_tile);
				if (u == nullptr || u->type != VEH_ROAD || !RoadVehicle::From(u)->IsFrontEngine()) {
					delete v;
				}
			}
		}

		/* We didn't store cargo payment yet, so make them for vehicles that are
		 * currently at a station and loading/unloading. If they don't get any
		 * payment anymore they just removed in the next load/unload cycle.
		 * However, some 0.7 versions might have cargo payment. For those we just
		 * add cargopayment for the vehicles that don't have it.
		 */
		for (Station *st : Station::Iterate()) {
			for (Vehicle *v : st->loading_vehicles) {
				/* There are always as many CargoPayments as Vehicles. We need to make the
				 * assert() in Pool::GetNew() happy by calling CanAllocateItem(). */
				static_assert(CargoPaymentPool::MAX_SIZE == VehiclePool::MAX_SIZE);
				assert(CargoPayment::CanAllocateItem());
				if (v->cargo_payment == nullptr) v->cargo_payment = new CargoPayment(v);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_122)) {
		/* Animated tiles would sometimes not be actually animated or
		 * in case of old savegames duplicate. */

		for (auto tile = _animated_tiles.begin(); tile != _animated_tiles.end(); /* Nothing */) {
			/* Remove if tile is not animated */
			bool remove = _tile_type_procs[GetTileType(tile->first)]->animate_tile_proc == nullptr;

			if (remove) {
				tile = _animated_tiles.erase(tile);
			} else {
				tile++;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_124) && !IsSavegameVersionBefore(SLV_1)) {
		/* The train station tile area was added, but for really old (TTDPatch) it's already valid. */
		for (Waypoint *wp : Waypoint::Iterate()) {
			if (wp->facilities & FACIL_TRAIN) {
				wp->train_station.tile = wp->xy;
				wp->train_station.w = 1;
				wp->train_station.h = 1;
			} else {
				wp->train_station.tile = INVALID_TILE;
				wp->train_station.w = 0;
				wp->train_station.h = 0;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_125)) {
		/* Convert old subsidies */
		for (Subsidy *s : Subsidy::Iterate()) {
			if (s->remaining < 12) {
				/* Converting nonawarded subsidy */
				s->remaining = 12 - s->remaining; // convert "age" to "remaining"
				s->awarded = INVALID_COMPANY; // not awarded to anyone
				const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
				switch (cs->town_acceptance_effect) {
					case TAE_PASSENGERS:
					case TAE_MAIL:
						/* Town -> Town */
						s->src_type = s->dst_type = SourceType::Town;
						if (Town::IsValidID(s->src) && Town::IsValidID(s->dst)) continue;
						break;
					case TAE_GOODS:
					case TAE_FOOD:
						/* Industry -> Town */
						s->src_type = SourceType::Industry;
						s->dst_type = SourceType::Town;
						if (Industry::IsValidID(s->src) && Town::IsValidID(s->dst)) continue;
						break;
					default:
						/* Industry -> Industry */
						s->src_type = s->dst_type = SourceType::Industry;
						if (Industry::IsValidID(s->src) && Industry::IsValidID(s->dst)) continue;
						break;
				}
			} else {
				/* Do our best for awarded subsidies. The original source or destination industry
				 * can't be determined anymore for awarded subsidies, so invalidate them.
				 * Town -> Town subsidies are converted using simple heuristic */
				s->remaining = 24 - s->remaining; // convert "age of awarded subsidy" to "remaining"
				const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
				switch (cs->town_acceptance_effect) {
					case TAE_PASSENGERS:
					case TAE_MAIL: {
						/* Town -> Town */
						const Station *ss = Station::GetIfValid(s->src);
						const Station *sd = Station::GetIfValid(s->dst);
						if (ss != nullptr && sd != nullptr && ss->owner == sd->owner &&
								Company::IsValidID(ss->owner)) {
							s->src_type = s->dst_type = SourceType::Town;
							s->src = ss->town->index;
							s->dst = sd->town->index;
							s->awarded = ss->owner;
							continue;
						}
						break;
					}
					default:
						break;
				}
			}
			/* Awarded non-town subsidy or invalid source/destination, invalidate */
			delete s;
		}
	}

	if (IsSavegameVersionBefore(SLV_126)) {
		/* Recompute inflation based on old unround loan limit
		 * Note: Max loan is 500000. With an inflation of 4% across 170 years
		 *       that results in a max loan of about 0.7 * 2^31.
		 *       So taking the 16 bit fractional part into account there are plenty of bits left
		 *       for unmodified savegames ...
		 */
		uint64_t aimed_inflation = (_economy.old_max_loan_unround << 16 | _economy.old_max_loan_unround_fract) / _settings_game.difficulty.max_loan;

		/* ... well, just clamp it then. */
		if (aimed_inflation > MAX_INFLATION) aimed_inflation = MAX_INFLATION;

		/* Simulate the inflation, so we also get the payment inflation */
		while (_economy.inflation_prices < aimed_inflation) {
			if (AddInflation(false)) break;
		}
	}

	if (IsSavegameVersionBefore(SLV_128)) {
		for (const Depot *d : Depot::Iterate()) {
			/* At some point, invalid depots were saved into the game (possibly those removed in the past?)
			 * Remove them here, so they don't cause issues further down the line */
			if (!IsDepotTile(d->xy)) {
				DEBUG(sl, 0, "Removing invalid depot %d at %d, %d", d->index, TileX(d->xy), TileY(d->xy));
				delete d;
				d = nullptr;
				continue;
			}
			_m[d->xy].m2 = d->index;
			if (IsTileType(d->xy, MP_WATER)) _m[GetOtherShipDepotTile(d->xy)].m2 = d->index;
		}
	}

	/* The behaviour of force_proceed has been changed. Now
	 * it counts signals instead of some random time out. */
	if (IsSavegameVersionBefore(SLV_131)) {
		for (Train *t : Train::Iterate()) {
			if (t->force_proceed != TFP_NONE) {
				t->force_proceed = TFP_STUCK;
			}
		}
	}

	/* The bits for the tree ground and tree density have
	 * been swapped (m2 bits 7..6 and 5..4. */
	if (IsSavegameVersionBefore(SLV_135)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_CLEAR)) {
				if (GetRawClearGround(t) == CLEAR_SNOW) {
					SetClearGroundDensity(t, CLEAR_GRASS, GetClearDensity(t));
					SetBit(_m[t].m3, 4);
				} else {
					ClrBit(_m[t].m3, 4);
				}
			}
			if (IsTileType(t, MP_TREES)) {
				uint density = GB(_m[t].m2, 6, 2);
				uint ground = GB(_m[t].m2, 4, 2);
				_m[t].m2 = ground << 6 | density << 4;
			}
		}
	}

	/* Wait counter and load/unload ticks got split. */
	if (IsSavegameVersionBefore(SLV_136)) {
		for (Aircraft *a : Aircraft::Iterate()) {
			a->turn_counter = a->current_order.IsType(OT_LOADING) ? 0 : a->load_unload_ticks;
		}

		for (Train *t : Train::Iterate()) {
			t->wait_counter = t->current_order.IsType(OT_LOADING) ? 0 : t->load_unload_ticks;
		}
	}

	/* Airport tile animation uses animation frame instead of other graphics id */
	if (IsSavegameVersionBefore(SLV_137)) {
		struct AirportTileConversion {
			byte old_start;
			byte num_frames;
		};
		static const AirportTileConversion atc[] = {
			{31,  12}, // APT_RADAR_GRASS_FENCE_SW
			{50,   4}, // APT_GRASS_FENCE_NE_FLAG
			{62,   2}, // 1 unused tile
			{66,  12}, // APT_RADAR_FENCE_SW
			{78,  12}, // APT_RADAR_FENCE_NE
			{101, 10}, // 9 unused tiles
			{111,  8}, // 7 unused tiles
			{119, 15}, // 14 unused tiles (radar)
			{140,  4}, // APT_GRASS_FENCE_NE_FLAG_2
		};
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsAirportTile(t)) {
				StationGfx old_gfx = GetStationGfx(t);
				byte offset = 0;
				for (uint i = 0; i < lengthof(atc); i++) {
					if (old_gfx < atc[i].old_start) {
						SetStationGfx(t, old_gfx - offset);
						break;
					}
					if (old_gfx < atc[i].old_start + atc[i].num_frames) {
						SetAnimationFrame(t, old_gfx - atc[i].old_start);
						SetStationGfx(t, atc[i].old_start - offset);
						break;
					}
					offset += atc[i].num_frames - 1;
				}
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_140)) {
		for (Station *st : Station::Iterate()) {
			if (st->airport.tile != INVALID_TILE) {
				st->airport.w = st->airport.GetSpec()->size_x;
				st->airport.h = st->airport.GetSpec()->size_y;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_141)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* Reset tropic zone for VOID tiles, they shall not have any. */
			if (IsTileType(t, MP_VOID)) SetTropicZone(t, TROPICZONE_NORMAL);
		}

		/* We need to properly number/name the depots.
		 * The first step is making sure none of the depots uses the
		 * 'default' names, after that we can assign the names. */
		for (Depot *d : Depot::Iterate()) d->town_cn = UINT16_MAX;

		for (Depot *d : Depot::Iterate()) MakeDefaultName(d);
	}

	if (IsSavegameVersionBefore(SLV_142)) {
		for (Depot *d : Depot::Iterate()) d->build_date = CalTime::CurDate();
	}

	if (SlXvIsFeatureMissing(XSLFI_INFRA_SHARING)) {
		for (Company *c : Company::Iterate()) {
			/* yearly_expenses has 3*15 entries now, saveload code gave us 3*13.
			 * Move the old data to the right place in the new array and clear the new data.
			 * The move has to be done in reverse order (first 2, then 1). */
			// MemMoveT(&c->yearly_expenses[2][0], &c->yearly_expenses[1][11], 13);
			// MemMoveT(&c->yearly_expenses[1][0], &c->yearly_expenses[0][13], 13);
			// The below are equivalent to the MemMoveT calls above
			std::copy_backward(&c->yearly_expenses[1][11], &c->yearly_expenses[1][11] + 13, &c->yearly_expenses[2][0] + 13);
			std::copy_backward(&c->yearly_expenses[0][13], &c->yearly_expenses[0][13] + 13, &c->yearly_expenses[1][0] + 13);
			/* Clear the old location of just-moved data, so sharing income/expenses is set to 0 */
			std::fill_n(&c->yearly_expenses[0][13], 2, 0);
			std::fill_n(&c->yearly_expenses[1][13], 2, 0);
		}
	}

	/* In old versions it was possible to remove an airport while a plane was
	 * taking off or landing. This gives all kind of problems when building
	 * another airport in the same station so we don't allow that anymore.
	 * For old savegames with such aircraft we just throw them in the air and
	 * treat the aircraft like they were flying already. */
	if (IsSavegameVersionBefore(SLV_146)) {
		for (Aircraft *v : Aircraft::Iterate()) {
			if (!v->IsNormalAircraft()) continue;
			Station *st = GetTargetAirportIfValid(v);
			if (st == nullptr && v->state != FLYING) {
				v->state = FLYING;
				UpdateAircraftCache(v);
				AircraftNextAirportPos_and_Order(v);
				/* get aircraft back on running altitude */
				if ((v->vehstatus & VS_CRASHED) == 0) {
					GetAircraftFlightLevelBounds(v, &v->z_pos, nullptr);
					SetAircraftPosition(v, v->x_pos, v->y_pos, GetAircraftFlightLevel(v));
				}
			}
		}
	}

	/* Move the animation frame to the same location (m7) for all objects. */
	if (IsSavegameVersionBefore(SLV_147)) {
		for (TileIndex t = 0; t < map_size; t++) {
			switch (GetTileType(t)) {
				case MP_HOUSE:
					if (GetHouseType(t) >= NEW_HOUSE_OFFSET) {
						uint per_proc = _me[t].m7;
						_me[t].m7 = GB(_me[t].m6, 2, 6) | (GB(_m[t].m3, 5, 1) << 6);
						SB(_m[t].m3, 5, 1, 0);
						SB(_me[t].m6, 2, 6, std::min(per_proc, 63U));
					}
					break;

				case MP_INDUSTRY: {
					uint rand = _me[t].m7;
					_me[t].m7 = _m[t].m3;
					_m[t].m3 = rand;
					break;
				}

				case MP_OBJECT:
					_me[t].m7 = _m[t].m3;
					_m[t].m3 = 0;
					break;

				default:
					/* For stations/airports it's already at m7 */
					break;
			}
		}
	}

	/* Add (random) colour to all objects. */
	if (IsSavegameVersionBefore(SLV_148)) {
		for (Object *o : Object::Iterate()) {
			Owner owner = GetTileOwner(o->location.tile);
			o->colour = (owner == OWNER_NONE) ? static_cast<Colours>(GB(Random(), 0, 4)) : Company::Get(owner)->livery->colour1;
		}
	}

	if (IsSavegameVersionBefore(SLV_149)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsTileType(t, MP_STATION)) continue;
			if (!IsBuoy(t) && !IsOilRig(t) && !(IsDock(t) && IsTileFlat(t))) {
				SetWaterClass(t, WATER_CLASS_INVALID);
			}
		}

		/* Waypoints with custom name may have a non-unique town_cn,
		 * renumber those. First set all affected waypoints to the
		 * highest possible number to get them numbered in the
		 * order they have in the pool. */
		for (Waypoint *wp : Waypoint::Iterate()) {
			if (!wp->name.empty()) wp->town_cn = UINT16_MAX;
		}

		for (Waypoint *wp : Waypoint::Iterate()) {
			if (!wp->name.empty()) MakeDefaultName(wp);
		}
	}

	if (IsSavegameVersionBefore(SLV_152)) {
		_industry_builder.Reset(); // Initialize industry build data.

		/* The moment vehicles go from hidden to visible changed. This means
		 * that vehicles don't always get visible anymore causing things to
		 * get messed up just after loading the savegame. This fixes that. */
		for (Vehicle *v : Vehicle::Iterate()) {
			/* Not all vehicle types can be inside a tunnel. Furthermore,
			 * testing IsTunnelTile() for invalid tiles causes a crash. */
			if (!v->IsGroundVehicle()) continue;

			/* Is the vehicle in a tunnel? */
			if (!IsTunnelTile(v->tile)) continue;

			/* Is the vehicle actually at a tunnel entrance/exit? */
			TileIndex vtile = TileVirtXY(v->x_pos, v->y_pos);
			if (!IsTunnelTile(vtile)) continue;

			/* Are we actually in this tunnel? Or maybe a lower tunnel? */
			if (GetSlopePixelZ(v->x_pos, v->y_pos, true) != v->z_pos) continue;

			/* What way are we going? */
			const DiagDirection dir = GetTunnelBridgeDirection(vtile);
			const DiagDirection vdir = DirToDiagDir(v->direction);

			/* Have we passed the visibility "switch" state already? */
			byte pos = (DiagDirToAxis(vdir) == AXIS_X ? v->x_pos : v->y_pos) & TILE_UNIT_MASK;
			byte frame = (vdir == DIAGDIR_NE || vdir == DIAGDIR_NW) ? TILE_SIZE - 1 - pos : pos;
			extern const byte _tunnel_visibility_frame[DIAGDIR_END];

			/* Should the vehicle be hidden or not? */
			bool hidden;
			if (dir == vdir) { // Entering tunnel
				hidden = frame >= _tunnel_visibility_frame[dir];
				v->tile = vtile;
				v->UpdatePosition();
			} else if (dir == ReverseDiagDir(vdir)) { // Leaving tunnel
				hidden = frame < TILE_SIZE - _tunnel_visibility_frame[dir];
				/* v->tile changes at the moment when the vehicle leaves the tunnel. */
				v->tile = hidden ? GetOtherTunnelBridgeEndOld(vtile) : vtile;
				v->UpdatePosition();
			} else {
				/* We could get here in two cases:
				 * - for road vehicles, it is reversing at the end of the tunnel
				 * - it is crashed in the tunnel entry (both train or RV destroyed by UFO)
				 * Whatever case it is, do not change anything and use the old values.
				 * Especially changing RV's state would break its reversing in the middle. */
				continue;
			}

			if (hidden) {
				v->vehstatus |= VS_HIDDEN;

				switch (v->type) {
					case VEH_TRAIN: Train::From(v)->track       = TRACK_BIT_WORMHOLE; break;
					case VEH_ROAD:  RoadVehicle::From(v)->state = RVSB_WORMHOLE;      break;
					default: NOT_REACHED();
				}
			} else {
				v->vehstatus &= ~VS_HIDDEN;

				switch (v->type) {
					case VEH_TRAIN: Train::From(v)->track       = DiagDirToDiagTrackBits(vdir); break;
					case VEH_ROAD:  RoadVehicle::From(v)->state = DiagDirToDiagTrackdir(vdir); RoadVehicle::From(v)->frame = frame; break;
					default: NOT_REACHED();
				}
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_153)) {
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (rv->state == RVSB_IN_DEPOT || rv->state == RVSB_WORMHOLE) continue;

			bool loading = rv->current_order.IsType(OT_LOADING) || rv->current_order.IsType(OT_LEAVESTATION);
			if (HasBit(rv->state, RVS_IN_ROAD_STOP)) {
				extern const byte _road_stop_stop_frame[];
				SB(rv->state, RVS_ENTERED_STOP, 1, loading || rv->frame > _road_stop_stop_frame[rv->state - RVSB_IN_ROAD_STOP + (_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)]);
			} else if (HasBit(rv->state, RVS_IN_DT_ROAD_STOP)) {
				SB(rv->state, RVS_ENTERED_STOP, 1, loading || rv->frame > RVC_DRIVE_THROUGH_STOP_FRAME);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_156)) {
		/* The train's pathfinder lost flag got moved. */
		for (Train *t : Train::Iterate()) {
			if (!HasBit(t->flags, 5)) continue;

			ClrBit(t->flags, 5);
			SetBit(t->vehicle_flags, VF_PATHFINDER_LOST);
		}

		/* Introduced terraform/clear limits. */
		for (Company *c : Company::Iterate()) {
			c->terraform_limit = _settings_game.construction.terraform_frame_burst << 16;
			c->clear_limit     = _settings_game.construction.clear_frame_burst << 16;
		}
	}

	if (IsSavegameVersionBefore(SLV_CONSISTENT_PARTIAL_Z) && SlXvIsFeatureMissing(XSLFI_CONSISTENT_PARTIAL_Z)) {
		/*
		 * The logic of GetPartialPixelZ has been changed, so the resulting Zs on
		 * the map are consistent. This requires that the Z position of some
		 * vehicles is updated to reflect this new situation.
		 *
		 * This needs to be before SLV_158, because that performs asserts using
		 * GetSlopePixelZ which internally uses GetPartialPixelZ.
		 */
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->IsGroundVehicle() && TileVirtXY(v->x_pos, v->y_pos) == v->tile) {
				/* Vehicle is on the ground, and not in a wormhole. */
				v->z_pos = GetSlopePixelZ(v->x_pos, v->y_pos, true);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_158)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			switch (v->type) {
				case VEH_TRAIN: {
					Train *t = Train::From(v);

					/* Clear old GOINGUP / GOINGDOWN flags.
					 * It was changed in savegame version 139, but savegame
					 * version 158 doesn't use these bits, so it doesn't hurt
					 * to clear them unconditionally. */
					ClrBit(t->flags, 1);
					ClrBit(t->flags, 2);

					/* Clear both bits first. */
					ClrBit(t->gv_flags, GVF_GOINGUP_BIT);
					ClrBit(t->gv_flags, GVF_GOINGDOWN_BIT);

					/* Crashed vehicles can't be going up/down. */
					if (t->vehstatus & VS_CRASHED) break;

					/* Only X/Y tracks can be sloped. */
					if (t->track != TRACK_BIT_X && t->track != TRACK_BIT_Y) break;

					t->gv_flags |= FixVehicleInclination(t, t->direction);
					break;
				}
				case VEH_ROAD: {
					RoadVehicle *rv = RoadVehicle::From(v);
					ClrBit(rv->gv_flags, GVF_GOINGUP_BIT);
					ClrBit(rv->gv_flags, GVF_GOINGDOWN_BIT);

					/* Crashed vehicles can't be going up/down. */
					if (rv->vehstatus & VS_CRASHED) break;

					if (rv->state == RVSB_IN_DEPOT || rv->state == RVSB_WORMHOLE) break;

					TrackBits trackbits = TrackdirBitsToTrackBits(GetTileTrackdirBits(rv->tile, TRANSPORT_ROAD, GetRoadTramType(rv->roadtype)));

					/* Only X/Y tracks can be sloped. */
					if (trackbits != TRACK_BIT_X && trackbits != TRACK_BIT_Y) break;

					Direction dir = rv->direction;

					/* Test if we are reversing. */
					Axis a = trackbits == TRACK_BIT_X ? AXIS_X : AXIS_Y;
					if (AxisToDirection(a) != dir &&
							AxisToDirection(a) != ReverseDir(dir)) {
						/* When reversing, the road vehicle is on the edge of the tile,
						 * so it can be safely compared to the middle of the tile. */
						dir = INVALID_DIR;
					}

					rv->gv_flags |= FixVehicleInclination(rv, dir);
					break;
				}
				case VEH_SHIP:
					break;

				default:
					continue;
			}

			if (IsBridgeTile(v->tile) && TileVirtXY(v->x_pos, v->y_pos) == v->tile) {
				/* In old versions, z_pos was 1 unit lower on bridge heads.
				 * However, this invalid state could be converted to new savegames
				 * by loading and saving the game in a new version. */
				v->z_pos = GetSlopePixelZ(v->x_pos, v->y_pos, true);
				DiagDirection dir = GetTunnelBridgeDirection(v->tile);
				if (v->type == VEH_TRAIN && !(v->vehstatus & VS_CRASHED) &&
						v->direction != DiagDirToDir(dir)) {
					/* If the train has left the bridge, it shouldn't have
					 * track == TRACK_BIT_WORMHOLE - this could happen
					 * when the train was reversed while on the last "tick"
					 * on the ramp before leaving the ramp to the bridge. */
					Train::From(v)->track = DiagDirToDiagTrackBits(dir);
				}
			}

			/* If the vehicle is really above v->tile (not in a wormhole),
			 * it should have set v->z_pos correctly. */
			assert(v->tile != TileVirtXY(v->x_pos, v->y_pos) || v->z_pos == GetSlopePixelZ(v->x_pos, v->y_pos, true));
		}

		/* Fill Vehicle::cur_real_order_index */
		for (Vehicle *v : Vehicle::IterateFrontOnly()) {
			if (!v->IsPrimaryVehicle()) continue;

			/* Older versions are less strict with indices being in range and fix them on the fly */
			if (v->cur_implicit_order_index >= v->GetNumOrders()) v->cur_implicit_order_index = 0;

			v->cur_real_order_index = v->cur_implicit_order_index;
			v->UpdateRealOrderIndex();
		}
	}

	if (IsSavegameVersionBefore(SLV_159)) {
		/* If the savegame is old (before version 100), then the value of 255
		 * for these settings did not mean "disabled". As such everything
		 * before then did reverse.
		 * To simplify stuff we disable all turning around or we do not
		 * disable anything at all. So, if some reversing was disabled we
		 * will keep reversing disabled, otherwise it'll be turned on. */
		_settings_game.pf.reverse_at_signals = IsSavegameVersionBefore(SLV_100) || (_settings_game.pf.wait_oneway_signal != 255 && _settings_game.pf.wait_twoway_signal != 255 && _settings_game.pf.wait_for_pbs_path != 255);

		for (Train *t : Train::Iterate()) {
			_settings_game.vehicle.max_train_length = std::max<uint8_t>(_settings_game.vehicle.max_train_length, CeilDiv(t->gcache.cached_total_length, TILE_SIZE));
		}
	}

	if (IsSavegameVersionBefore(SLV_160)) {
		/* Setting difficulty industry_density other than zero get bumped to +1
		 * since a new option (minimal at position 1) has been added */
		if (_settings_game.difficulty.industry_density > 0) {
			_settings_game.difficulty.industry_density++;
		}
	}

	if (IsSavegameVersionBefore(SLV_161)) {
		/* Before savegame version 161, persistent storages were not stored in a pool. */

		if (!IsSavegameVersionBefore(SLV_76)) {
			for (Industry *ind : Industry::Iterate()) {
				assert(ind->psa != nullptr);

				/* Check if the old storage was empty. */
				bool is_empty = true;
				for (uint i = 0; i < sizeof(ind->psa->storage); i++) {
					if (ind->psa->GetValue(i) != 0) {
						is_empty = false;
						break;
					}
				}

				if (!is_empty) {
					ind->psa->grfid = _industry_mngr.GetGRFID(ind->type);
				} else {
					delete ind->psa;
					ind->psa = nullptr;
				}
			}
		}

		if (!IsSavegameVersionBefore(SLV_145)) {
			for (Station *st : Station::Iterate()) {
				if (!(st->facilities & FACIL_AIRPORT)) continue;
				assert(st->airport.psa != nullptr);

				/* Check if the old storage was empty. */
				bool is_empty = true;
				for (uint i = 0; i < sizeof(st->airport.psa->storage); i++) {
					if (st->airport.psa->GetValue(i) != 0) {
						is_empty = false;
						break;
					}
				}

				if (!is_empty) {
					st->airport.psa->grfid = _airport_mngr.GetGRFID(st->airport.type);
				} else {
					delete st->airport.psa;
					st->airport.psa = nullptr;

				}
			}
		}
	}

	/* This triggers only when old snow_lines were copied into the snow_line_height. */
	if (IsSavegameVersionBefore(SLV_164) && _settings_game.game_creation.snow_line_height >= MIN_SNOWLINE_HEIGHT * TILE_HEIGHT && SlXvIsFeatureMissing(XSLFI_CHILLPP)) {
		_settings_game.game_creation.snow_line_height /= TILE_HEIGHT;
		UpdateCachedSnowLine();
		UpdateCachedSnowLineBounds();
	}

	if (IsSavegameVersionBefore(SLV_164) && !IsSavegameVersionBefore(SLV_32)) {
		/* We store 4 fences in the field tiles instead of only SE and SW. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsTileType(t, MP_CLEAR) && !IsTileType(t, MP_TREES)) continue;
			if (IsTileType(t, MP_CLEAR) && IsClearGround(t, CLEAR_FIELDS)) continue;
			uint fence = GB(_m[t].m4, 5, 3);
			if (fence != 0 && IsTileType(TileAddXY(t, 1, 0), MP_CLEAR) && IsClearGround(TileAddXY(t, 1, 0), CLEAR_FIELDS)) {
				SetFence(TileAddXY(t, 1, 0), DIAGDIR_NE, fence);
			}
			fence = GB(_m[t].m4, 2, 3);
			if (fence != 0 && IsTileType(TileAddXY(t, 0, 1), MP_CLEAR) && IsClearGround(TileAddXY(t, 0, 1), CLEAR_FIELDS)) {
				SetFence(TileAddXY(t, 0, 1), DIAGDIR_NW, fence);
			}
			SB(_m[t].m4, 2, 3, 0);
			SB(_m[t].m4, 5, 3, 0);
		}
	}

	if (IsSavegameVersionBefore(SLV_165)) {
		for (Town *t : Town::Iterate()) {
			/* Set the default cargo requirement for town growth */
			switch (_settings_game.game_creation.landscape) {
				case LT_ARCTIC:
					if (FindFirstCargoWithTownAcceptanceEffect(TAE_FOOD) != nullptr) t->goal[TAE_FOOD] = TOWN_GROWTH_WINTER;
					break;

				case LT_TROPIC:
					if (FindFirstCargoWithTownAcceptanceEffect(TAE_FOOD) != nullptr) t->goal[TAE_FOOD] = TOWN_GROWTH_DESERT;
					if (FindFirstCargoWithTownAcceptanceEffect(TAE_WATER) != nullptr) t->goal[TAE_WATER] = TOWN_GROWTH_DESERT;
					break;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_165)) {
		/* Adjust zoom level to account for new levels */
		_saved_scrollpos_zoom = static_cast<ZoomLevel>(_saved_scrollpos_zoom + ZOOM_LVL_SHIFT);
		_saved_scrollpos_x *= ZOOM_LVL_BASE;
		_saved_scrollpos_y *= ZOOM_LVL_BASE;
	}

	/* When any NewGRF has been changed the availability of some vehicles might
	 * have been changed too. e->company_avail must be set to 0 in that case
	 * which is done by StartupEngines(). */
	if (gcf_res != GLC_ALL_GOOD) StartupEngines();

	/* Set some breakdown-related variables to the correct values. */
	if (SlXvIsFeatureMissing(XSLFI_IMPROVED_BREAKDOWNS)) {
		_settings_game.vehicle.improved_breakdowns = false;
		for (Train *v : Train::Iterate()) {
			if (v->IsFrontEngine()) {
				if (v->breakdown_ctr == 1) SetBit(v->flags, VRF_BREAKDOWN_STOPPED);
			} else if (v->IsEngine() || v->IsMultiheaded()) {
				/** Non-front engines could have a reliability of 0.
				 * Set it to the reliability of the front engine or the maximum, whichever is lower. */
				const Engine *e = Engine::Get(v->engine_type);
				v->reliability_spd_dec = e->reliability_spd_dec;
				v->reliability = std::min(v->First()->reliability, e->reliability);
			}
		}
	}
	if (!SlXvIsFeaturePresent(XSLFI_IMPROVED_BREAKDOWNS, 3)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			switch(v->type) {
				case VEH_TRAIN:
				case VEH_ROAD:
					v->breakdown_chance_factor = 128;
					break;

				case VEH_SHIP:
					v->breakdown_chance_factor = 64;
					break;

				case VEH_AIRCRAFT:
					v->breakdown_chance_factor = Clamp(64 + (AircraftVehInfo(v->engine_type)->max_speed >> 3), 0, 255);
					v->breakdown_severity = 40;
					break;

				default:
					break;
			}
		}
	}
	if (!SlXvIsFeaturePresent(XSLFI_IMPROVED_BREAKDOWNS, 4)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			switch(v->type) {
				case VEH_AIRCRAFT:
					if (v->breakdown_type == BREAKDOWN_AIRCRAFT_SPEED && v->breakdown_severity == 0) {
						v->breakdown_severity = std::max(1, std::min(v->vcache.cached_max_speed >> 4, 255));
					}
					break;

				default:
					break;
			}
		}
	}
	if (SlXvIsFeatureMissing(XSLFI_CONSIST_BREAKDOWN_FLAG)) {
		for (Train *v : Train::Iterate()) {
			if (v->breakdown_ctr != 0 && (v->IsEngine() || v->IsMultiheaded())) {
				SetBit(v->First()->flags, VRF_CONSIST_BREAKDOWN);
			}
		}
	}

	/* The road owner of standard road stops was not properly accounted for. */
	if (IsSavegameVersionBefore(SLV_172)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (!IsBayRoadStopTile(t)) continue;
			Owner o = GetTileOwner(t);
			SetRoadOwner(t, RTT_ROAD, o);
			SetRoadOwner(t, RTT_TRAM, o);
		}
	}

	if (IsSavegameVersionBefore(SLV_175)) {
		/* Introduced tree planting limit. */
		for (Company *c : Company::Iterate()) c->tree_limit = _settings_game.construction.tree_frame_burst << 16;
	}

	if (IsSavegameVersionBefore(SLV_177)) {
		/* Fix too high inflation rates */
		if (_economy.inflation_prices > MAX_INFLATION) _economy.inflation_prices = MAX_INFLATION;
		if (_economy.inflation_payment > MAX_INFLATION) _economy.inflation_payment = MAX_INFLATION;

		/* We have to convert the quarters of bankruptcy into months of bankruptcy */
		for (Company *c : Company::Iterate()) {
			c->months_of_bankruptcy = 3 * c->months_of_bankruptcy;
		}
	}

	/* Station blocked, wires and pylon flags need to be stored in the map.
	 * This is done here as the SLV_182 check below needs the blocked status. */
	UpdateStationTileCacheFlags(SlXvIsFeatureMissing(XSLFI_STATION_TILE_CACHE_FLAGS));

	if (IsSavegameVersionBefore(SLV_182)) {
		/* Aircraft acceleration variable was bonkers */
		for (Aircraft *v : Aircraft::Iterate()) {
			if (v->subtype <= AIR_AIRCRAFT) {
				const AircraftVehicleInfo *avi = AircraftVehInfo(v->engine_type);
				v->acceleration = avi->acceleration;
			}
		}

		/* Blocked tiles could be reserved due to a bug, which causes
		 * other places to assert upon e.g. station reconstruction. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (HasStationTileRail(t) && IsStationTileBlocked(t)) {
				SetRailStationReservation(t, false);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_184)) {
		/* The global units configuration is split up in multiple configurations. */
		extern uint8_t _old_units;
		_settings_game.locale.units_velocity = Clamp(_old_units, 0, 2);
		_settings_game.locale.units_power    = Clamp(_old_units, 0, 2);
		_settings_game.locale.units_weight   = Clamp(_old_units, 1, 2);
		_settings_game.locale.units_volume   = Clamp(_old_units, 1, 2);
		_settings_game.locale.units_force    = 2;
		_settings_game.locale.units_height   = Clamp(_old_units, 0, 2);
	}

	if (IsSavegameVersionBefore(SLV_VELOCITY_NAUTICAL)) {
		/* Match nautical velocity with land velocity units. */
		_settings_game.locale.units_velocity_nautical = _settings_game.locale.units_velocity;
	}

	if (IsSavegameVersionBefore(SLV_186)) {
		/* Move ObjectType from map to pool */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_OBJECT)) {
				Object *o = Object::Get(_m[t].m2);
				o->type = _m[t].m5;
				_m[t].m5 = 0; // zero upper bits of (now bigger) ObjectID
			}
		}
	}

	/* In version 2.2 of the savegame, we have new airports, so status of all aircraft is reset.
	 * This has to be called after all map array updates */
	if (IsSavegameVersionBefore(SLV_2, 2)) UpdateOldAircraft();

	if (SlXvIsFeaturePresent(XSLFI_SPRINGPP)) {
		// re-arrange vehicle_flags
		for (Vehicle *v : Vehicle::Iterate()) {
			SB(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, 1, GB(v->vehicle_flags, 6, 1));
			SB(v->vehicle_flags, VF_STOP_LOADING, 4, GB(v->vehicle_flags, 7, 4));
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_CHILLPP, SL_CHILLPP_232)) {
		// re-arrange vehicle_flags
		for (Vehicle *v : Vehicle::Iterate()) {
			SB(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, 1, GB(v->vehicle_flags, 7, 1));
			SB(v->vehicle_flags, VF_PATHFINDER_LOST, 1, GB(v->vehicle_flags, 8, 1));
			SB(v->vehicle_flags, VF_SERVINT_IS_CUSTOM, 7, 0);
		}
	} else if (SlXvIsFeaturePresent(XSLFI_CHILLPP)) {
		// re-arrange vehicle_flags
		for (Vehicle *v : Vehicle::Iterate()) {
			SB(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, 1, GB(v->vehicle_flags, 6, 1));
			SB(v->vehicle_flags, VF_STOP_LOADING, 9, 0);
		}
	}

	if (IsSavegameVersionBefore(SLV_188)) {
		/* Fix articulated road vehicles.
		 * Some curves were shorter than other curves.
		 * Now they have the same length, but that means that trailing articulated parts will
		 * take longer to go through the curve than the parts in front which already left the courve.
		 * So, make articulated parts catch up. */
		bool roadside = _settings_game.vehicle.road_side == 1;
		std::vector<uint> skip_frames;
		for (RoadVehicle *v : RoadVehicle::IterateFrontOnly()) {
			if (!v->IsFrontEngine()) continue;
			skip_frames.clear();
			TileIndex prev_tile = v->tile;
			uint prev_tile_skip = 0;
			uint cur_skip = 0;
			for (RoadVehicle *u = v; u != nullptr; u = u->Next()) {
				if (u->tile != prev_tile) {
					prev_tile_skip = cur_skip;
					prev_tile = u->tile;
				} else {
					cur_skip = prev_tile_skip;
				}

				uint &this_skip = skip_frames.emplace_back(prev_tile_skip);

				/* The following 3 curves now take longer than before */
				switch (u->state) {
					case 2:
						cur_skip++;
						if (u->frame <= (roadside ? 9 : 5)) this_skip = cur_skip;
						break;

					case 4:
						cur_skip++;
						if (u->frame <= (roadside ? 5 : 9)) this_skip = cur_skip;
						break;

					case 5:
						cur_skip++;
						if (u->frame <= (roadside ? 4 : 2)) this_skip = cur_skip;
						break;

					default:
						break;
				}
			}
			while (cur_skip > skip_frames[0]) {
				RoadVehicle *u = v;
				RoadVehicle *prev = nullptr;
				for (uint sf : skip_frames) {
					if (sf >= cur_skip) IndividualRoadVehicleController(u, prev);

					prev = u;
					u = u->Next();
				}
				cur_skip--;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_190)) {
		for (Order *order : Order::Iterate()) {
			order->SetTravelTimetabled(order->GetTravelTime() > 0);
			order->SetWaitTimetabled(order->GetWaitTime() > 0);
		}
	} else if (SlXvIsFeatureMissing(XSLFI_TIMETABLE_EXTRA)) {
		for (Order *order : Order::Iterate()) {
			if (order->IsType(OT_CONDITIONAL)) {
				order->SetWaitTimetabled(order->GetWaitTime() > 0);
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_TT_WAIT_IN_DEPOT, 1, 1) || IsSavegameVersionBefore(SLV_190) || SlXvIsFeatureMissing(XSLFI_TIMETABLE_EXTRA)) {
		for (OrderList *orderlist : OrderList::Iterate()) {
			orderlist->RecalculateTimetableDuration();
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_REVERSE_AT_WAYPOINT)) {
		for (Train *t : Train::Iterate()) {
			t->reverse_distance = 0;
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_SPEED_RESTRICTION)) {
		for (Train *t : Train::Iterate()) {
			t->speed_restriction = 0;
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_JOKERPP)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				if (GetSignalType(t, TRACK_LOWER) == SIGTYPE_PROG) SetSignalType(t, TRACK_LOWER, SIGTYPE_BLOCK);
				if (GetSignalType(t, TRACK_UPPER) == SIGTYPE_PROG) SetSignalType(t, TRACK_UPPER, SIGTYPE_BLOCK);
			}
		}
		for (Vehicle *v : Vehicle::Iterate()) {
			SB(v->vehicle_flags, 10, 2, 0);
		}
		extern std::vector<OrderList *> _jokerpp_auto_separation;
		extern std::vector<OrderList *> _jokerpp_non_auto_separation;
		for (OrderList *list : _jokerpp_auto_separation) {
			for (Vehicle *w = list->GetFirstSharedVehicle(); w != nullptr; w = w->NextShared()) {
				SetBit(w->vehicle_flags, VF_TIMETABLE_SEPARATION);
				w->ClearSeparation();
			}
		}
		for (OrderList *list : _jokerpp_non_auto_separation) {
			for (Vehicle *w = list->GetFirstSharedVehicle(); w != nullptr; w = w->NextShared()) {
				ClrBit(w->vehicle_flags, VF_TIMETABLE_SEPARATION);
				w->ClearSeparation();
			}
		}
		_jokerpp_auto_separation.clear();
		_jokerpp_non_auto_separation.clear();
	}
	if (SlXvIsFeaturePresent(XSLFI_CHILLPP, SL_CHILLPP_232)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				if (GetSignalType(t, TRACK_LOWER) == 7) SetSignalType(t, TRACK_LOWER, SIGTYPE_BLOCK);
				if (GetSignalType(t, TRACK_UPPER) == 7) SetSignalType(t, TRACK_UPPER, SIGTYPE_BLOCK);
			}
		}
	}

	/*
	 * Only keep order-backups for network clients (and when replaying).
	 * If we are a network server or not networking, then we just loaded a previously
	 * saved-by-server savegame. There are no clients with a backup, so clear it.
	 * Furthermore before savegame version SLV_192 the actual content was always corrupt.
	 */
	if (!_networking || _network_server || IsSavegameVersionBefore(SLV_192)) {
#ifndef DEBUG_DUMP_COMMANDS
		/* Note: We cannot use CleanPool since that skips part of the destructor
		 * and then leaks un-reachable Orders in the order pool. */
		for (OrderBackup *ob : OrderBackup::Iterate()) {
			delete ob;
		}
#endif
	}

	if (IsSavegameVersionBefore(SLV_198) && !SlXvIsFeaturePresent(XSLFI_JOKERPP, SL_JOKER_1_27)) {
		/* Convert towns growth_rate and grow_counter to ticks */
		for (Town *t : Town::Iterate()) {
			/* 0x8000 = TOWN_GROWTH_RATE_CUSTOM previously */
			if (t->growth_rate & 0x8000) SetBit(t->flags, TOWN_CUSTOM_GROWTH);
			if (t->growth_rate != TOWN_GROWTH_RATE_NONE) {
				t->growth_rate = TownTicksToGameTicks(t->growth_rate & ~0x8000);
			}
			/* Add t->index % TOWN_GROWTH_TICKS to spread growth across ticks. */
			t->grow_counter = TownTicksToGameTicks(t->grow_counter) + t->index % TOWN_GROWTH_TICKS;
		}
	}

	if (IsSavegameVersionBefore(SLV_EXTEND_INDUSTRY_CARGO_SLOTS)) {
		/* Make sure added industry cargo slots are cleared */
		for (Industry *i : Industry::Iterate()) {
			for (size_t ci = 2; ci < lengthof(i->produced_cargo); ci++) {
				i->produced_cargo[ci] = INVALID_CARGO;
				i->produced_cargo_waiting[ci] = 0;
				i->production_rate[ci] = 0;
				i->last_month_production[ci] = 0;
				i->last_month_transported[ci] = 0;
				i->last_month_pct_transported[ci] = 0;
				i->this_month_production[ci] = 0;
				i->this_month_transported[ci] = 0;
			}
			for (size_t ci = 3; ci < lengthof(i->accepts_cargo); ci++) {
				i->accepts_cargo[ci] = INVALID_CARGO;
				i->incoming_cargo_waiting[ci] = 0;
			}
			/* Make sure last_cargo_accepted_at is copied to elements for every valid input cargo.
			 * The loading routine should put the original singular value into the first array element. */
			for (size_t ci = 0; ci < lengthof(i->accepts_cargo); ci++) {
				if (i->accepts_cargo[ci] != INVALID_CARGO) {
					i->last_cargo_accepted_at[ci] = i->last_cargo_accepted_at[0];
				} else {
					i->last_cargo_accepted_at[ci] = 0;
				}
			}
		}
	}

	if (!IsSavegameVersionBefore(SLV_TIMETABLE_START_TICKS)) {
		/* Convert timetable start from a date to an absolute tick in TimerGameTick::counter. */
		for (Vehicle *v : Vehicle::Iterate()) {
			/* If the start date is 0, the vehicle is not waiting to start and can be ignored. */
			if (v->timetable_start == 0) continue;

			v->timetable_start += _state_ticks.base() - _tick_counter;
		}
	} else if (!SlXvIsFeaturePresent(XSLFI_TIMETABLES_START_TICKS, 3)) {
		extern btree::btree_map<VehicleID, uint16_t> _old_timetable_start_subticks_map;

		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->timetable_start == 0) continue;

			if (SlXvIsFeatureMissing(XSLFI_TIMETABLES_START_TICKS)) {
				v->timetable_start.edit_base() *= DAY_TICKS;
			}

			v->timetable_start = DateTicksToStateTicks(v->timetable_start.base());

			if (SlXvIsFeaturePresent(XSLFI_TIMETABLES_START_TICKS, 2, 2)) {
				v->timetable_start += _old_timetable_start_subticks_map[v->index];
			}
		}

		_old_timetable_start_subticks_map.clear();
	}

	if (!IsSavegameVersionBefore(SLV_DEPOT_UNBUNCHING)) {
		for (Vehicle *v : Vehicle::IterateFrontOnly()) {
			if (v->unbunch_state != nullptr) {
				if (v->unbunch_state->depot_unbunching_last_departure > 0) {
					v->unbunch_state->depot_unbunching_last_departure += _state_ticks.base() - _tick_counter;
				} else {
					v->unbunch_state->depot_unbunching_last_departure = INVALID_STATE_TICKS;
				}
				if (v->unbunch_state->depot_unbunching_next_departure > 0) {
					v->unbunch_state->depot_unbunching_next_departure += _state_ticks.base() - _tick_counter;
				} else {
					v->unbunch_state->depot_unbunching_next_departure = INVALID_STATE_TICKS;
				}
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_SPRINGPP, 1, 1)) {
		/*
		 * Cost scaling changes:
		 * SpringPP v2.0.102 divides all prices by the difficulty factor, effectively making things about 8 times cheaper.
		 * Adjust the inflation factor to compensate for this, as otherwise the game is unplayable on load if inflation has been running for a while.
		 * To avoid making things too cheap, clamp the price inflation factor to no lower than the payment inflation factor.
		 */

		DEBUG(sl, 3, "Inflation prices: %f", _economy.inflation_prices / 65536.0);
		DEBUG(sl, 3, "Inflation payments: %f", _economy.inflation_payment / 65536.0);

		_economy.inflation_prices >>= 3;
		if (_economy.inflation_prices < _economy.inflation_payment) {
			_economy.inflation_prices = _economy.inflation_payment;
		}

		DEBUG(sl, 3, "New inflation prices: %f", _economy.inflation_prices / 65536.0);
	}

	if (SlXvIsFeaturePresent(XSLFI_MIGHT_USE_PAX_SIGNALS) || SlXvIsFeatureMissing(XSLFI_TRACE_RESTRICT)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (HasStationTileRail(t)) {
				/* clear station PAX bit */
				ClrBit(_me[t].m6, 6);
			}
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				/*
				 * tracerestrict uses same bit as 1st PAX signals bit
				 * only conditionally clear the bit, don't bother checking for whether to set it
				 */
				if (IsRestrictedSignal(t)) {
					TraceRestrictSetIsSignalRestrictedBit(t);
				}

				/* clear 2nd signal PAX bit */
				ClrBit(_m[t].m2, 13);
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_TRAFFIC_LIGHTS)) {
		/* remove traffic lights */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_ROAD) && (GetRoadTileType(t) == ROAD_TILE_NORMAL)) {
				DeleteAnimatedTile(t);
				ClrBit(_me[t].m7, 4);
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_RAIL_AGEING)) {
		/* remove rail aging data */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsPlainRailTile(t)) {
				SB(_me[t].m7, 0, 8, 0);
			}
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_SPRINGPP)) {
		/* convert wait for cargo orders to ordinary load if possible */
		IterateVehicleAndOrderListOrders([](Order *order) {
			if ((order->IsType(OT_GOTO_STATION) || order->IsType(OT_LOADING) || order->IsType(OT_IMPLICIT)) && order->GetLoadType() == static_cast<OrderLoadFlags>(1)) {
				order->SetLoadType(OLF_LOAD_IF_POSSIBLE);
			}
		});
	}

	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 1)) {
		/* set the semaphore bit to match what it would have been in v1 */
		/* clear the PBS bit, update the end signal state */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsTunnelBridgeWithSignalSimulation(t)) {
				SetTunnelBridgeSemaphore(t, CalTime::CurYear() < _settings_client.gui.semaphore_build_before);
				SetTunnelBridgePBS(t, false);
				UpdateSignalsOnSegment(t, INVALID_DIAGDIR, GetTileOwner(t));
			}
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 2)) {
		/* red/green signal state bit for tunnel entrances moved
		 * to no longer re-use signalled tunnel exit bit
		 */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsTunnelBridgeWithSignalSimulation(t)) {
				if (HasBit(_m[t].m5, 5)) {
					/* signalled tunnel entrance */
					SignalState state = HasBit(_m[t].m5, 6) ? SIGNAL_STATE_RED : SIGNAL_STATE_GREEN;
					ClrBit(_m[t].m5, 6);
					SetTunnelBridgeEntranceSignalState(t, state);
				}
			}
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 4)) {
		/* load_unload_ticks --> tunnel_bridge_signal_num */
		for (Train *t : Train::Iterate()) {
			TileIndex tile = t->tile;
			if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && IsTunnelBridgeWithSignalSimulation(tile)) {
				t->tunnel_bridge_signal_num = t->load_unload_ticks;
				t->load_unload_ticks = 0;
			}
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 5)) {
		/* entrance and exit signal red/green states now have separate bits */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsTunnelBridgeSignalSimulationExit(t)) {
				SetTunnelBridgeExitSignalState(t, HasBit(_me[t].m6, 0) ? SIGNAL_STATE_GREEN : SIGNAL_STATE_RED);
			}
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 7)) {
		/* spacing setting moved to company settings */
		for (Company *c : Company::Iterate()) {
			c->settings.old_simulated_wormhole_signals = _settings_game.construction.old_simulated_wormhole_signals;
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_SIG_TUNNEL_BRIDGE, 1, 8)) {
		/* spacing made per tunnel/bridge */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsTunnelBridgeWithSignalSimulation(t)) {
				DiagDirection dir = GetTunnelBridgeDirection(t);
				if (dir == DIAGDIR_NE || dir == DIAGDIR_SE) {
					TileIndex other = GetOtherTunnelBridgeEnd(t);
					Owner owner = GetTileOwner(t);
					int target;
					if (Company::IsValidID(owner)) {
						target = Company::Get(owner)->settings.old_simulated_wormhole_signals;
					} else {
						target = 4;
					}
					uint spacing = GetBestTunnelBridgeSignalSimulationSpacing(t, other, target);
					SetTunnelBridgeSignalSimulationSpacing(t, spacing);
					SetTunnelBridgeSignalSimulationSpacing(other, spacing);
				}
			}
		}
		/* force aspect re-calculation */
		_extra_aspects = 0;
		_aspect_cfg_hash = 0;
	}

	if (SlXvIsFeatureMissing(XSLFI_CUSTOM_BRIDGE_HEADS)) {
		/* ensure that previously unused custom bridge-head bits are cleared */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD) {
				SB(_m[t].m2, 0, 8, 0);
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_SHIPS_STOP_IN_LOCKS)) {
		/* Move ships from lock slope to upper or lower position. */
		for (Ship *s : Ship::Iterate()) {
			/* Suitable tile? */
			if (!IsTileType(s->tile, MP_WATER) || !IsLock(s->tile) || GetLockPart(s->tile) != LOCK_PART_MIDDLE) continue;

			/* We don't need to adjust position when at the tile centre */
			int x = s->x_pos & 0xF;
			int y = s->y_pos & 0xF;
			if (x == 8 && y == 8) continue;

			/* Test if ship is on the second half of the tile */
			bool second_half;
			DiagDirection shipdiagdir = DirToDiagDir(s->direction);
			switch (shipdiagdir) {
				default: NOT_REACHED();
				case DIAGDIR_NE: second_half = x < 8; break;
				case DIAGDIR_NW: second_half = y < 8; break;
				case DIAGDIR_SW: second_half = x > 8; break;
				case DIAGDIR_SE: second_half = y > 8; break;
			}

			DiagDirection slopediagdir = GetInclinedSlopeDirection(GetTileSlope(s->tile));

			/* Heading up slope == passed half way */
			if ((shipdiagdir == slopediagdir) == second_half) {
				/* On top half of lock */
				s->z_pos = GetTileMaxZ(s->tile) * (int)TILE_HEIGHT;
			} else {
				/* On lower half of lock */
				s->z_pos = GetTileZ(s->tile) * (int)TILE_HEIGHT;
			}
		}
	}

	if (IsSavegameVersionBefore(SLV_TOWN_CARGOGEN)) {
		/* Ensure the original cargo generation mode is used */
		_settings_game.economy.town_cargogen_mode = TCGM_ORIGINAL;
	}

	if (IsSavegameVersionBefore(SLV_SERVE_NEUTRAL_INDUSTRIES)) {
		/* Ensure the original neutral industry/station behaviour is used */
		_settings_game.station.serve_neutral_industries = true;

		/* Link oil rigs to their industry and back. */
		for (Station *st : Station::Iterate()) {
			if (IsTileType(st->xy, MP_STATION) && IsOilRig(st->xy)) {
				/* Industry tile is always adjacent during construction by TileDiffXY(0, 1) */
				st->industry = Industry::GetByTile(st->xy + TileDiffXY(0, 1));
				st->industry->neutral_station = st;
			}
		}
	} else {
		/* Link neutral station back to industry, as this is not saved. */
		for (Industry *ind : Industry::Iterate()) if (ind->neutral_station != nullptr) ind->neutral_station->industry = ind;
	}

	if (IsSavegameVersionBefore(SLV_TREES_WATER_CLASS) && !SlXvIsFeaturePresent(XSLFI_CHUNNEL, 2)) {
		/* Update water class for trees. */
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TREES)) SetWaterClass(t, GetTreeGround(t) == TREE_GROUND_SHORE ? WATER_CLASS_SEA : WATER_CLASS_INVALID);
		}
	}

	/* Update structures for multitile docks */
	if (IsSavegameVersionBefore(SLV_MULTITILE_DOCKS)) {
		for (TileIndex t = 0; t < map_size; t++) {
			/* Clear docking tile flag from relevant tiles as it
			 * was not previously cleared. */
			if (IsTileType(t, MP_WATER) || IsTileType(t, MP_RAILWAY) || IsTileType(t, MP_STATION) || IsTileType(t, MP_TUNNELBRIDGE)) {
				SetDockingTile(t, false);
			}
			/* Add docks and oilrigs to Station::ship_station. */
			if (IsTileType(t, MP_STATION)) {
				if (IsDock(t) || IsOilRig(t)) Station::GetByTile(t)->ship_station.Add(t);
			}
		}
	}

	if (IsSavegameVersionBeforeOrAt(SLV_ENDING_YEAR) || !SlXvIsFeaturePresent(XSLFI_MULTIPLE_DOCKS, 2) || !SlXvIsFeaturePresent(XSLFI_DOCKING_CACHE_VER, 3)) {
		/* Update station docking tiles. Was only needed for pre-SLV_MULTITLE_DOCKS
		 * savegames, but a bug in docking tiles touched all savegames between
		 * SLV_MULTITILE_DOCKS and SLV_ENDING_YEAR. */
		/* Placing objects on docking tiles was not updating adjacent station's docking tiles. */
		for (Station *st : Station::Iterate()) {
			if (st->ship_station.tile != INVALID_TILE) UpdateStationDockingTiles(st);
		}
	}

	/* Make sure all industries exclusive supplier/consumer set correctly. */
	if (IsSavegameVersionBefore(SLV_GS_INDUSTRY_CONTROL)) {
		for (Industry *i : Industry::Iterate()) {
			i->exclusive_supplier = INVALID_OWNER;
			i->exclusive_consumer = INVALID_OWNER;
		}
	}

	/* Make sure all industries exclusive supplier/consumer set correctly. */
	if (IsSavegameVersionBefore(SLV_GS_INDUSTRY_CONTROL)) {
		for (Industry *i : Industry::Iterate()) {
			i->exclusive_supplier = INVALID_OWNER;
			i->exclusive_consumer = INVALID_OWNER;
		}
	}

	if (IsSavegameVersionBefore(SLV_GROUP_REPLACE_WAGON_REMOVAL)) {
		/* Propagate wagon removal flag for compatibility */
		/* Temporary bitmask of company wagon removal setting */
		uint16_t wagon_removal = 0;
		for (const Company *c : Company::Iterate()) {
			if (c->settings.renew_keep_length) SetBit(wagon_removal, c->index);
		}
		for (Group *g : Group::Iterate()) {
			if (g->flags != 0) {
				/* Convert old replace_protection value to flag. */
				g->flags = 0;
				SetBit(g->flags, GroupFlags::GF_REPLACE_PROTECTION);
			}
			if (HasBit(wagon_removal, g->owner)) SetBit(g->flags, GroupFlags::GF_REPLACE_WAGON_REMOVAL);
		}
	}

	/* Use current order time to approximate last loading time */
	if (IsSavegameVersionBefore(SLV_LAST_LOADING_TICK) && SlXvIsFeatureMissing(XSLFI_LAST_LOADING_TICK)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			v->last_loading_tick = _state_ticks - v->current_order_time;
		}
	} else if (SlXvIsFeatureMissing(XSLFI_LAST_LOADING_TICK, 3)) {
		const StateTicksDelta delta = _state_ticks.base() - (int64_t)_scaled_tick_counter;
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->last_loading_tick != 0) {
				if (SlXvIsFeaturePresent(XSLFI_LAST_LOADING_TICK, 1, 1)) v->last_loading_tick = v->last_loading_tick.base() * DayLengthFactor();
				v->last_loading_tick += delta;
			}
		}
	}

	if (!IsSavegameVersionBefore(SLV_MULTITRACK_LEVEL_CROSSINGS)) {
		_settings_game.vehicle.adjacent_crossings = true;
	} else if (SlXvIsFeatureMissing(XSLFI_ADJACENT_CROSSINGS)) {
		_settings_game.vehicle.adjacent_crossings = false;
	}

	/* Compute station catchment areas. This is needed here in case UpdateStationAcceptance is called below. */
	Station::RecomputeCatchmentForAll();

	/* Station acceptance is some kind of cache */
	if (IsSavegameVersionBefore(SLV_127)) {
		for (Station *st : Station::Iterate()) UpdateStationAcceptance(st, false);
	}

	// setting moved from game settings to company settings
	if (SlXvIsFeaturePresent(XSLFI_ORDER_OCCUPANCY, 1, 1)) {
		for (Company *c : Company::Iterate()) {
			c->settings.order_occupancy_smoothness = _settings_game.order.old_occupancy_smoothness;
		}
	}

	/* Set lifetime vehicle profit to 0 if lifetime profit feature is missing */
	if (SlXvIsFeatureMissing(XSLFI_VEH_LIFETIME_PROFIT)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			v->profit_lifetime = 0;
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_AUTO_TIMETABLE, 1, 3)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			SB(v->vehicle_flags, VF_TIMETABLE_SEPARATION, 1, _settings_game.order.old_timetable_separation);
		}
	}

	if (_file_to_saveload.abstract_ftype == FT_SCENARIO) {
		/* Apply the new-game cargo scale values for scenarios */
		_settings_game.economy.town_cargo_scale = _settings_newgame.economy.town_cargo_scale;
		_settings_game.economy.industry_cargo_scale = _settings_newgame.economy.industry_cargo_scale;
		_settings_game.economy.town_cargo_scale_mode = _settings_newgame.economy.town_cargo_scale_mode;
		_settings_game.economy.industry_cargo_scale_mode = _settings_newgame.economy.industry_cargo_scale_mode;
	} else {
		if (SlXvIsFeatureMissing(XSLFI_TOWN_CARGO_ADJ)) {
			_settings_game.economy.town_cargo_scale = 100;
		} else if (SlXvIsFeaturePresent(XSLFI_TOWN_CARGO_ADJ, 1, 1)) {
			_settings_game.economy.town_cargo_scale = ScaleQuantity(100, _settings_game.old_economy.town_cargo_factor * 10);
		} else if (SlXvIsFeaturePresent(XSLFI_TOWN_CARGO_ADJ, 2, 2)) {
			_settings_game.economy.town_cargo_scale = ScaleQuantity(100, _settings_game.old_economy.town_cargo_scale_factor);
		}
		if (!SlXvIsFeaturePresent(XSLFI_TOWN_CARGO_ADJ, 3)) {
			_settings_game.economy.town_cargo_scale_mode = CSM_MONTHLY;
		}

		if (SlXvIsFeatureMissing(XSLFI_INDUSTRY_CARGO_ADJ)) {
			_settings_game.economy.industry_cargo_scale = 100;
		} else if (SlXvIsFeaturePresent(XSLFI_INDUSTRY_CARGO_ADJ, 1, 1)) {
			_settings_game.economy.industry_cargo_scale = ScaleQuantity(100, _settings_game.old_economy.industry_cargo_scale_factor);
		}
		if (!SlXvIsFeaturePresent(XSLFI_TOWN_CARGO_ADJ, 2)) {
			_settings_game.economy.industry_cargo_scale_mode = CSM_MONTHLY;
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_SAFER_CROSSINGS)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsLevelCrossingTile(t)) {
				SetCrossingOccupiedByRoadVehicle(t, IsTrainCollidableRoadVehicleOnGround(t));
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_TIMETABLE_EXTRA)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			v->cur_timetable_order_index = v->GetNumManualOrders() > 0 ? v->cur_real_order_index : INVALID_VEH_ORDER_ID;
		}
		for (OrderBackup *bckup : OrderBackup::Iterate()) {
			bckup->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
		}
		for (Order *order : Order::Iterate()) {
			if (order->IsType(OT_CONDITIONAL)) {
				if (order->GetTravelTime() != 0) {
					DEBUG(sl, 1, "Fixing: order->GetTravelTime() != 0, %u", order->GetTravelTime());
					order->SetTravelTime(0);
				}
			}
		}
#ifdef WITH_ASSERT
		for (OrderList *order_list : OrderList::Iterate()) {
			order_list->DebugCheckSanity();
		}
#endif
	}

	if (SlXvIsFeaturePresent(XSLFI_TRAIN_THROUGH_LOAD, 0, 1)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->cargo_payment == nullptr) {
				for (Vehicle *u = v; u != nullptr; u = u->Next()) {
					if (HasBit(v->vehicle_flags, VF_CARGO_UNLOADING)) ClrBit(v->vehicle_flags, VF_CARGO_UNLOADING);
				}
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_BUY_LAND_RATE_LIMIT)) {
		/* Introduced land purchasing limit. */
		for (Company *c : Company::Iterate()) {
			c->purchase_land_limit = _settings_game.construction.purchase_land_frame_burst << 16;
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_BUILD_OBJECT_RATE_LIMIT)) {
		/* Introduced build object limit. */
		for (Company *c : Company::Iterate()) {
			c->build_object_limit = _settings_game.construction.build_object_frame_burst << 16;
		}
	}

	if (SlXvIsFeaturePresent(XSLFI_MORE_COND_ORDERS, 1, 1)) {
		for (Order *order : Order::Iterate()) {
			/* Insertion of OCV_MAX_RELIABILITY between OCV_REMAINING_LIFETIME and OCV_CARGO_WAITING */
			if (order->IsType(OT_CONDITIONAL) && order->GetConditionVariable() > OCV_REMAINING_LIFETIME) {
				order->SetConditionVariable(static_cast<OrderConditionVariable>((uint)order->GetConditionVariable() + 1));
			}
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_MORE_COND_ORDERS, 1, 14)) {
		for (OrderList *order_list : OrderList::Iterate()) {
			auto get_real_station = [&order_list](const Order *order) -> StationID {
				const uint max = std::min<uint>(64, order_list->GetNumOrders());
				for (uint i = 0; i < max; i++) {
					if (order->IsType(OT_GOTO_STATION) && Station::IsValidID(order->GetDestination())) return order->GetDestination();

					order = (order->next != nullptr) ? order->next : order_list->GetFirstOrder();
				}
				return INVALID_STATION;
			};

			for (Order *order = order_list->GetFirstOrder(); order != nullptr; order = order->next) {
				/* Fixup station ID for OCV_CARGO_WAITING, OCV_CARGO_ACCEPTANCE, OCV_FREE_PLATFORMS, OCV_CARGO_WAITING_AMOUNT */
				if (order->IsType(OT_CONDITIONAL) && ConditionVariableHasStationID(order->GetConditionVariable())) {
					StationID next_id =  get_real_station(order);
					SB(order->GetXData2Ref(), 0, 16, next_id + 1);
					if (next_id != INVALID_STATION && GB(order->GetXData(), 16, 16) - 2 == next_id) {
						/* Duplicate next and via, remove via */
						SB(order->GetXDataRef(), 16, 16, 0);
					}
					if (GB(order->GetXData(), 16, 16) != 0 && !Station::IsValidID(GB(order->GetXData(), 16, 16) - 2)) {
						/* Via station is invalid */
						SB(order->GetXDataRef(), 16, 16, INVALID_STATION + 2);
					}
				}
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_CONSIST_SPEED_RD_FLAG)) {
		for (Train *t : Train::Iterate()) {
			if ((t->track & TRACK_BIT_WORMHOLE && !(t->vehstatus & VS_HIDDEN)) || t->track == TRACK_BIT_DEPOT) {
				SetBit(t->First()->flags, VRF_CONSIST_SPEED_REDUCTION);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_SAVEGAME_UNIQUE_ID)) {
		/* Generate a random id for savegames that didn't have one */
		/* We keep id 0 for old savegames that don't have an id */
		_settings_game.game_creation.generation_unique_id = _interactive_random.Next(UINT32_MAX-1) + 1; /* Generates between [1;UINT32_MAX] */
	}

	if (SlXvIsFeatureMissing(XSLFI_TOWN_MULTI_BUILDING)) {
		for (Town *t : Town::Iterate()) {
			t->church_count = HasBit(t->flags, 1) ? 1 : 0;
			t->stadium_count = HasBit(t->flags, 2) ? 1 : 0;
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_ONE_WAY_DT_ROAD_STOP)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsDriveThroughStopTile(t)) {
				SetDriveThroughStopDisallowedRoadDirections(t, DRD_NONE);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_ONE_WAY_ROAD_STATE)) {
		extern void RecalculateRoadCachedOneWayStates();
		RecalculateRoadCachedOneWayStates();
	}

	if (SlXvIsFeatureMissing(XSLFI_ANIMATED_TILE_EXTRA)) {
		UpdateAllAnimatedTileSpeeds();
	}

	if (!SlXvIsFeaturePresent(XSLFI_REALISTIC_TRAIN_BRAKING, 2)) {
		for (Train *t : Train::Iterate()) {
			if (!(t->vehstatus & VS_CRASHED)) {
				t->crash_anim_pos = 0;
			}
			if (t->lookahead != nullptr) SetBit(t->lookahead->flags, TRLF_APPLY_ADVISORY);
		}
	}

	if (!SlXvIsFeaturePresent(XSLFI_REALISTIC_TRAIN_BRAKING, 3) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		UpdateAllBlockSignals();
	}

	if (!SlXvIsFeaturePresent(XSLFI_REALISTIC_TRAIN_BRAKING, 5) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		for (Train *t : Train::IterateFrontOnly()) {
			if (t->lookahead != nullptr) {
				t->lookahead->SetNextExtendPosition();
			}
		}
	}

	if (!SlXvIsFeaturePresent(XSLFI_REALISTIC_TRAIN_BRAKING, 6) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		for (Train *t : Train::IterateFrontOnly()) {
			if (t->lookahead != nullptr) {
				t->lookahead->cached_zpos = t->CalculateOverallZPos();
				t->lookahead->zpos_refresh_remaining = t->GetZPosCacheUpdateInterval();
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_INFLATION_FIXED_DATES)) {
		_settings_game.economy.inflation_fixed_dates = !IsSavegameVersionBefore(SLV_GS_INDUSTRY_CONTROL);
	}

	if (SlXvIsFeatureMissing(XSLFI_MORE_HOUSES)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_HOUSE)) {
				/* Move upper bit of house ID from bit 6 of m3 to bits 6..5 of m3. */
				SB(_m[t].m3, 5, 2, GB(_m[t].m3, 6, 1));
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_CUSTOM_TOWN_ZONE)) {
		_settings_game.economy.city_zone_0_mult = _settings_game.economy.town_zone_0_mult;
		_settings_game.economy.city_zone_1_mult = _settings_game.economy.town_zone_1_mult;
		_settings_game.economy.city_zone_2_mult = _settings_game.economy.town_zone_2_mult;
		_settings_game.economy.city_zone_3_mult = _settings_game.economy.town_zone_3_mult;
		_settings_game.economy.city_zone_4_mult = _settings_game.economy.town_zone_4_mult;
	}

	if (!SlXvIsFeaturePresent(XSLFI_WATER_FLOODING, 2)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_WATER)) {
				SetNonFloodingWaterTile(t, false);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_TRACE_RESTRICT_TUNBRIDGE)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL && IsTunnelBridgeWithSignalSimulation(t)) {
				SetTunnelBridgeRestrictedSignal(t, false);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_OBJECT_GROUND_TYPES, 3)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_OBJECT)) {
				if (SlXvIsFeatureMissing(XSLFI_OBJECT_GROUND_TYPES)) _m[t].m4 = 0;
				if (SlXvIsFeatureMissing(XSLFI_OBJECT_GROUND_TYPES, 2)) {
					ObjectType type = GetObjectType(t);
					extern void SetObjectFoundationType(TileIndex tile, Slope tileh, ObjectType type, const ObjectSpec *spec);
					SetObjectFoundationType(t, SLOPE_ELEVATED, type, ObjectSpec::Get(type));
				}
				if (SlXvIsFeatureMissing(XSLFI_OBJECT_GROUND_TYPES, 3)) {
					if (ObjectSpec::GetByTile(t)->ctrl_flags & OBJECT_CTRL_FLAG_VPORT_MAP_TYPE) {
						SetObjectHasViewportMapViewOverride(t, true);
					}
				}
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_ST_INDUSTRY_CARGO_MODE)) {
		_settings_game.station.station_delivery_mode = SD_NEAREST_FIRST;
	}

	if (SlXvIsFeatureMissing(XSLFI_TL_SPEED_LIMIT)) {
		_settings_game.vehicle.through_load_speed_limit = 15;
	}

	if (SlXvIsFeatureMissing(XSLFI_RAIL_DEPOT_SPEED_LIMIT)) {
		_settings_game.vehicle.rail_depot_speed_limit = 61;
	}

	if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 1, 2)) {
		for (OrderList *order_list : OrderList::Iterate()) {
			if (order_list->GetScheduledDispatchScheduleCount() == 1) {
				const DispatchSchedule &ds = order_list->GetDispatchScheduleByIndex(0);
				if (!(ds.GetScheduledDispatchStartTick() >= 0 && ds.IsScheduledDispatchValid()) && ds.GetScheduledDispatch().empty()) {
					order_list->GetScheduledDispatchScheduleSet().clear();
				} else {
					VehicleOrderID idx = order_list->GetFirstSharedVehicle()->GetFirstWaitingLocation(false);
					if (idx != INVALID_VEH_ORDER_ID) {
						order_list->GetOrderAt(idx)->SetDispatchScheduleIndex(0);
					}
				}
			}
		}
	}
	if (SlXvIsFeaturePresent(XSLFI_SCHEDULED_DISPATCH, 1, 4)) {
		extern btree::btree_map<DispatchSchedule *, uint16_t> _old_scheduled_dispatch_start_full_date_fract_map;

		for (OrderList *order_list : OrderList::Iterate()) {
			for (DispatchSchedule &ds : order_list->GetScheduledDispatchScheduleSet()) {
				StateTicks start_tick = DateToStateTicks(ds.GetScheduledDispatchStartTick().base()) + _old_scheduled_dispatch_start_full_date_fract_map[&ds];
				ds.SetScheduledDispatchStartTick(start_tick);
			}
		}

		_old_scheduled_dispatch_start_full_date_fract_map.clear();
	}

	if (SlXvIsFeaturePresent(XSLFI_TRACE_RESTRICT, 7, 12)) {
		/* Move vehicle in slot flag */
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, 2)) { /* was VRF_HAVE_SLOT */
				SetBit(v->vehicle_flags, VF_HAVE_SLOT);
				ClrBit(Train::From(v)->flags, 2);
			} else {
				ClrBit(v->vehicle_flags, VF_HAVE_SLOT);
			}
		}
	} else if (SlXvIsFeatureMissing(XSLFI_TRACE_RESTRICT)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			ClrBit(v->vehicle_flags, VF_HAVE_SLOT);
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_INDUSTRY_ANIM_MASK)) {
		ApplyIndustryTileAnimMasking();
	}

	if (SlXvIsFeatureMissing(XSLFI_NEW_SIGNAL_STYLES)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				/* clear signal style field */
				_me[t].m6 = 0;
			}
			if (IsRailTunnelBridgeTile(t)) {
				/* Clear signal style is non-zero flag */
				ClrBit(_m[t].m3, 7);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_REALISTIC_TRAIN_BRAKING, 8)) {
		_aspect_cfg_hash = 0;
	}

	if (!SlXvIsFeaturePresent(XSLFI_REALISTIC_TRAIN_BRAKING, 9) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		for (Train *t : Train::IterateFrontOnly()) {
			if (t->lookahead != nullptr) {
				t->lookahead->lookahead_end_position = t->lookahead->reservation_end_position + 1;
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_NO_TREE_COUNTER)) {
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_TREES)) {
				ClearOldTreeCounter(t);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_REMAIN_NEXT_ORDER_STATION)) {
		for (Company *c : Company::Iterate()) {
			/* Approximately the same time as when this was feature was added and unconditionally enabled */
			c->settings.remain_if_next_order_same_station = SlXvIsFeaturePresent(XSLFI_TRACE_RESTRICT_TUNBRIDGE);
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_MORE_CARGO_AGE)) {
		_settings_game.economy.payment_algorithm = IsSavegameVersionBefore(SLV_MORE_CARGO_AGE) ? CPA_TRADITIONAL : CPA_MODERN;
	}

	if (SlXvIsFeatureMissing(XSLFI_VARIABLE_TICK_RATE)) {
		_settings_game.economy.tick_rate = IsSavegameVersionBeforeOrAt(SLV_MORE_CARGO_AGE) ? TRM_TRADITIONAL : TRM_MODERN;
	}

	if (SlXvIsFeatureMissing(XSLFI_ROAD_VEH_FLAGS)) {
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (IsLevelCrossingTile(rv->tile)) {
				SetBit(rv->First()->rvflags, RVF_ON_LEVEL_CROSSING);
			}
		}
	}

	if (SlXvIsFeatureMissing(XSLFI_AI_START_DATE) && IsSavegameVersionBefore(SLV_AI_START_DATE)) {
		/* For older savegames, we don't now the actual interval; so set it to the newgame value. */
		_settings_game.difficulty.competitors_interval = _settings_newgame.difficulty.competitors_interval;

		/* We did load the "period" of the timer, but not the fired/elapsed. We can deduce that here. */
		extern TimeoutTimer<TimerGameTick> _new_competitor_timeout;
		_new_competitor_timeout.storage.elapsed = 0;
		_new_competitor_timeout.fired = _new_competitor_timeout.period == 0;
	}

	if (SlXvIsFeatureMissing(XSLFI_SAVEGAME_ID) && IsSavegameVersionBefore(SLV_SAVEGAME_ID)) {
		GenerateSavegameId();
	}

	if (IsSavegameVersionBefore(SLV_NEWGRF_LAST_SERVICE) && SlXvIsFeatureMissing(XSLFI_NEWGRF_LAST_SERVICE)) {
		/* Set service date provided to NewGRF. */
		for (Vehicle *v : Vehicle::Iterate()) {
			v->date_of_last_service_newgrf = v->date_of_last_service.base();
		}
	}

	if (IsSavegameVersionBefore(SLV_SHIP_ACCELERATION) && SlXvIsFeatureMissing(XSLFI_SHIP_ACCELERATION)) {
		/* NewGRF acceleration information was added to ships. */
		for (Ship *s : Ship::Iterate()) {
			if (s->acceleration == 0) s->acceleration = ShipVehInfo(s->engine_type)->acceleration;
		}
	}

	if (IsSavegameVersionBefore(SLV_MAX_LOAN_FOR_COMPANY)) {
		for (Company *c : Company::Iterate()) {
			c->max_loan = COMPANY_MAX_LOAN_DEFAULT;
		}
	}

	if (IsSavegameVersionBefore(SLV_SCRIPT_RANDOMIZER)) {
		ScriptObject::InitializeRandomizers();
	}

	if (IsSavegameVersionBeforeOrAt(SLV_MULTITRACK_LEVEL_CROSSINGS) && SlXvIsFeatureMissing(XSLFI_AUX_TILE_LOOP)) {
		_settings_game.construction.flood_from_edges = false;
	}

	for (Company *c : Company::Iterate()) {
		UpdateCompanyLiveries(c);
	}

	/*
	 * The center of train vehicles was changed, fix up spacing.
	 * Delay this until all train and track updates have been performed.
	 */
	if (IsSavegameVersionBefore(SLV_164)) FixupTrainLengths();

	InitializeRoadGUI();

	/* This needs to be done after conversion. */
	RebuildViewportKdtree();
	ViewportMapBuildTunnelCache();

	/* Road stops is 'only' updating some caches */
	AfterLoadRoadStops();
	AfterLoadLabelMaps();
	AfterLoadCompanyStats();
	AfterLoadStoryBook();

	AfterLoadVehiclesRemoveAnyFoundInvalid();

	GamelogPrintDebug(1);

	SetupTickRate();

	InitializeWindowsAndCaches();
	/* Restore the signals */
	ResetSignalHandlers();

	AfterLoadLinkGraphs();

	AfterLoadTraceRestrict();
	AfterLoadTemplateVehiclesUpdate();
	if (SlXvIsFeaturePresent(XSLFI_TEMPLATE_REPLACEMENT, 1, 7)) {
		AfterLoadTemplateVehiclesUpdateProperties();
	}

	InvalidateVehicleTickCaches();
	ClearVehicleTickCaches();

	UpdateAllVehiclesIsDrawn();

	extern void YapfCheckRailSignalPenalties();
	YapfCheckRailSignalPenalties();

	bool update_always_reserve_through = SlXvIsFeaturePresent(XSLFI_REALISTIC_TRAIN_BRAKING, 8, 10);
	UpdateExtraAspectsVariable(update_always_reserve_through);

	UpdateCargoScalers();

	if (_networking && !_network_server) {
		SlProcessVENC();

		if (!_settings_client.client_locale.sync_locale_network_server) {
			_settings_game.locale = _settings_newgame.locale;
		}
	}

	/* Show this message last to avoid covering up an error message if we bail out part way */
	switch (gcf_res) {
		case GLC_COMPATIBLE: ShowErrorMessage(STR_NEWGRF_COMPATIBLE_LOAD_WARNING, INVALID_STRING_ID, WL_CRITICAL); break;
		case GLC_NOT_FOUND:  ShowErrorMessage(STR_NEWGRF_DISABLED_WARNING, INVALID_STRING_ID, WL_CRITICAL); _pause_mode = PM_PAUSED_ERROR; break;
		default: break;
	}

	if (!_networking || _network_server) {
		extern void AfterLoad_LinkGraphPauseControl();
		AfterLoad_LinkGraphPauseControl();
	}

	if (SlXvIsFeatureMissing(XSLFI_CONSISTENT_PARTIAL_Z)) {
		CheckGroundVehiclesAtCorrectZ();
	} else {
#ifdef _DEBUG
		CheckGroundVehiclesAtCorrectZ();
#endif
	}

	_game_load_cur_date_ymd = EconTime::CurYMD();
	_game_load_date_fract = EconTime::CurDateFract();
	_game_load_tick_skip_counter = TickSkipCounter();
	_game_load_state_ticks = _state_ticks;
	_game_load_time = time(nullptr);

	/* Start the scripts. This MUST happen after everything else except
	 * starting a new company. */
	StartScripts();

	/* If Load Scenario / New (Scenario) Game is used,
	 *  a company does not exist yet. So create one here.
	 * 1 exception: network-games. Those can have 0 companies
	 *   But this exception is not true for non-dedicated network servers! */
	if (!Company::IsValidID(GetDefaultLocalCompany()) && (!_networking || (_networking && _network_server && !_network_dedicated))) {
		Company *c = DoStartupNewCompany(DSNC_DURING_LOAD);
		c->settings = _settings_client.company;
	}

	return true;
}

/**
 * Reload all NewGRF files during a running game. This is a cut-down
 * version of AfterLoadGame().
 * XXX - We need to reset the vehicle position hash because with a non-empty
 * hash AfterLoadVehicles() will loop infinitely. We need AfterLoadVehicles()
 * to recalculate vehicle data as some NewGRF vehicle sets could have been
 * removed or added and changed statistics
 */
void ReloadNewGRFData()
{
	RegisterGameEvents(GEF_RELOAD_NEWGRF);
	AppendSpecialEventsLogEntry("NewGRF reload");

	RailTypeLabel rail_type_label_map[RAILTYPE_END];
	for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
		rail_type_label_map[rt] = GetRailTypeInfo(rt)->label;
	}

	/* reload grf data */
	GfxLoadSprites();
	RecomputePrices();
	LoadStringWidthTable();
	/* reload vehicles */
	ResetVehicleHash();
	AfterLoadEngines();
	AnalyseIndustryTileSpriteGroups();
	extern void AnalyseHouseSpriteGroups();
	AnalyseHouseSpriteGroups();
	AfterLoadVehicles(false);
	StartupEngines();
	GroupStatistics::UpdateAfterLoad();
	/* update station graphics */
	AfterLoadStations();
	UpdateStationTileCacheFlags(false);

	RailType rail_type_translate_map[RAILTYPE_END];
	for (RailType old_type = RAILTYPE_BEGIN; old_type != RAILTYPE_END; old_type++) {
		RailType new_type = GetRailTypeByLabel(rail_type_label_map[old_type]);
		rail_type_translate_map[old_type] = (new_type == INVALID_RAILTYPE) ? RAILTYPE_RAIL : new_type;
	}

	/* Restore correct railtype for all rail tiles.*/
	const TileIndex map_size = MapSize();
	for (TileIndex t = 0; t < map_size; t++) {
		if (GetTileType(t) == MP_RAILWAY ||
				IsLevelCrossingTile(t) ||
				IsRailStationTile(t) ||
				IsRailWaypointTile(t) ||
				IsRailTunnelBridgeTile(t)) {
			SetRailType(t, rail_type_translate_map[GetRailType(t)]);
			RailType secondary = GetTileSecondaryRailTypeIfValid(t);
			if (secondary != INVALID_RAILTYPE) SetSecondaryRailType(t, rail_type_translate_map[secondary]);
		}
	}

	UpdateExtraAspectsVariable();

	InitRoadTypesCaches();

	ReInitAllWindows(false);

	/* Update company statistics. */
	AfterLoadCompanyStats();
	/* Check and update house and town values */
	UpdateHousesAndTowns(true, false);
	/* Delete news referring to no longer existing entities */
	DeleteInvalidEngineNews();
	/* Update livery selection windows */
	for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) InvalidateWindowData(WC_COMPANY_COLOUR, i);
	/* Update company infrastructure counts. */
	InvalidateWindowClassesData(WC_COMPANY_INFRASTRUCTURE);
	/* redraw the whole screen */
	MarkWholeScreenDirty();
	CheckTrainsLengths();
	AfterLoadTemplateVehiclesUpdateImages();
	AfterLoadTemplateVehiclesUpdateProperties();
	UpdateAllAnimatedTileSpeeds();

	InvalidateWindowData(WC_BUILD_SIGNAL, 0);
}
