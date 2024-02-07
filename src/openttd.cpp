/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file openttd.cpp Functions related to starting OpenTTD. */

#include "stdafx.h"

#include "blitter/factory.hpp"
#include "sound/sound_driver.hpp"
#include "music/music_driver.hpp"
#include "video/video_driver.hpp"
#include "mixer.h"

#include "fontcache.h"
#include "error.h"
#include "gui.h"

#include "base_media_base.h"
#include "sl/saveload.h"
#include "company_func.h"
#include "command_func.h"
#include "command_log.h"
#include "news_func.h"
#include "fios.h"
#include "load_check.h"
#include "aircraft.h"
#include "roadveh.h"
#include "train.h"
#include "ship.h"
#include "console_func.h"
#include "screenshot.h"
#include "network/network.h"
#include "network/network_func.h"
#include "ai/ai.hpp"
#include "ai/ai_config.hpp"
#include "settings_func.h"
#include "genworld.h"
#include "progress.h"
#include "strings_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "gamelog.h"
#include "animated_tile_func.h"
#include "roadstop_base.h"
#include "elrail_func.h"
#include "rev.h"
#include "highscore.h"
#include "station_base.h"
#include "crashlog.h"
#include "engine_func.h"
#include "core/random_func.hpp"
#include "rail_gui.h"
#include "road_gui.h"
#include "core/backup_type.hpp"
#include "hotkeys.h"
#include "newgrf.h"
#include "newgrf_commons.h"
#include "misc/getoptdata.h"
#include "game/game.hpp"
#include "game/game_config.hpp"
#include "town.h"
#include "subsidy_func.h"
#include "gfx_layout.h"
#include "viewport_func.h"
#include "viewport_sprite_sorter.h"
#include "framerate_type.h"
#include "programmable_signals.h"
#include "smallmap_gui.h"
#include "viewport_func.h"
#include "thread.h"
#include "bridge_signal_map.h"
#include "zoning.h"
#include "cargopacket.h"
#include "tbtr_template_vehicle.h"
#include "string_func_extra.h"
#include "industry.h"
#include "network/network_gui.h"
#include "cargopacket.h"
#include "core/checksum_func.hpp"
#include "tbtr_template_vehicle_func.h"
#include "debug_settings.h"
#include "debug_desync.h"
#include "event_logs.h"
#include "tunnelbridge.h"
#include "worker_thread.h"
#include "scope_info.h"
#include "network/network_survey.h"
#include "timer/timer.h"
#include "timer/timer_game_realtime.h"
#include "timer/timer_game_tick.h"
#include "network/network_sync.h"
#include "plans_func.h"

#include "linkgraph/linkgraphschedule.h"
#include "tracerestrict.h"

#include "3rdparty/cpp-btree/btree_set.h"

#include <atomic>
#include <mutex>

#include <stdarg.h>
#include <system_error>

#include "safeguards.h"

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#	include <emscripten/html5.h>
#endif

void CallLandscapeTick();
void IncreaseDate();
void DoPaletteAnimations();
void MusicLoop();
void CallWindowGameTickEvent();
bool HandleBootstrap();
void OnTick_Companies(bool main_tick);

extern void AfterLoadCompanyStats();
extern Company *DoStartupNewCompany(bool is_ai, CompanyID company = INVALID_COMPANY);
extern void OSOpenBrowser(const std::string &url);
extern void RebuildTownCaches(bool cargo_update_required, bool old_map_position);
extern void ShowOSErrorBox(const char *buf, bool system);
extern void NORETURN DoOSAbort();
extern std::string _config_file;
extern uint64_t _station_tile_cache_hash;

bool _save_config = false;
bool _request_newgrf_scan = false;
NewGRFScanCallback *_request_newgrf_scan_callback = nullptr;

SimpleChecksum64 _state_checksum;

std::mutex _music_driver_mutex;
static std::string _music_driver_params;
static std::atomic<bool> _music_inited;

void NORETURN usererror_str(const char *msg)
{
	ShowOSErrorBox(msg, false);
	if (VideoDriver::GetInstance() != nullptr) VideoDriver::GetInstance()->Stop();

#ifdef __EMSCRIPTEN__
	emscripten_exit_pointerlock();
	/* In effect, the game ends here. As emscripten_set_main_loop() caused
	 * the stack to be unwound, the code after MainLoop() in
	 * openttd_main() is never executed. */
	EM_ASM(if (window["openttd_abort"]) openttd_abort());
#endif

	_exit(1);
}

/**
 * Error handling for fatal user errors.
 * @param s the string to print.
 * @note Does NEVER return.
 */
void CDECL usererror(const char *s, ...)
{
	va_list va;
	char buf[512];

	va_start(va, s);
	vseprintf(buf, lastof(buf), s, va);
	va_end(va);

	usererror_str(buf);
}

static void NORETURN fatalerror_common(const char *msg)
{
	if (VideoDriver::GetInstance() == nullptr || VideoDriver::GetInstance()->HasGUI()) {
		ShowOSErrorBox(msg, true);
	}

	/* Set the error message for the crash log and then invoke it. */
	CrashLog::SetErrorMessage(msg);
	DoOSAbort();
}

/**
 * Error handling for fatal non-user errors.
 * @param s the string to print.
 * @note Does NEVER return.
 */
void CDECL error(const char *s, ...)
{
	if (CrashLog::HaveAlreadyCrashed()) DoOSAbort();

	va_list va;
	char buf[2048];

	va_start(va, s);
	vseprintf(buf, lastof(buf), s, va);
	va_end(va);

	fatalerror_common(buf);
}

void fatalerror_str(const char *msg)
{
	if (CrashLog::HaveAlreadyCrashed()) DoOSAbort();

	fatalerror_common(msg);
}

void CDECL assert_msg_error(int line, const char *file, const char *expr, const char *extra, const char *str, ...)
{
	if (CrashLog::HaveAlreadyCrashed()) DoOSAbort();

	va_list va;
	char buf[2048];

	char *b = buf;
	b += seprintf(b, lastof(buf), "Assertion failed at line %i of %s: %s\n\t", line, file, expr);

	if (extra != nullptr) {
		b += seprintf(b, lastof(buf), "%s\n\t", extra);
	}

	va_start(va, str);
	vseprintf(b, lastof(buf), str, va);
	va_end(va);

	fatalerror_common(buf);
}

void assert_str_error(int line, const char *file, const char *expr, const char *str)
{
	char buf[2048];
	seprintf(buf, lastof(buf), "Assertion failed at line %i of %s: %s\n%s", line, file, expr, str);
	fatalerror_common(buf);
}

void assert_str_error(int line, const char *file, const char *expr, const std::string &str)
{
	assert_str_error(line, file, expr, str.c_str());
}

const char *assert_tile_info(uint32_t tile) {
	static char buffer[128];
	DumpTileInfo(buffer, lastof(buffer), tile);
	return buffer;
}

/**
 * Shows some information on the console/a popup box depending on the OS.
 * @param str the text to show.
 */
void CDECL ShowInfoF(const char *str, ...)
{
	va_list va;
	char buf[1024];
	va_start(va, str);
	vseprintf(buf, lastof(buf), str, va);
	va_end(va);
	ShowInfoI(buf);
}

/**
 * Show the help message when someone passed a wrong parameter.
 */
static void ShowHelp()
{
	char buf[8192];
	char *p = buf;

	p += seprintf(p, lastof(buf), "OpenTTD %s\n", _openttd_revision);
	p = strecpy(p,
		"\n"
		"\n"
		"Command line options:\n"
		"  -v drv              = Set video driver (see below)\n"
		"  -s drv              = Set sound driver (see below)\n"
		"  -m drv              = Set music driver (see below)\n"
		"  -b drv              = Set the blitter to use (see below)\n"
		"  -r res              = Set resolution (for instance 800x600)\n"
		"  -h                  = Display this help text\n"
		"  -t year             = Set starting year\n"
		"  -d [[fac=]lvl[,...]]= Debug mode\n"
		"  -e                  = Start Editor\n"
		"  -g [savegame]       = Start new/save game immediately\n"
		"  -G seed             = Set random seed\n"
		"  -n host[:port][#company]= Join network game\n"
		"  -p password         = Password to join server\n"
		"  -P password         = Password to join company\n"
		"  -D [host][:port]    = Start dedicated server\n"
#if !defined(_WIN32)
		"  -f                  = Fork into the background (dedicated only)\n"
#endif
		"  -I graphics_set     = Force the graphics set (see below)\n"
		"  -S sounds_set       = Force the sounds set (see below)\n"
		"  -M music_set        = Force the music set (see below)\n"
		"  -c config_file      = Use 'config_file' instead of 'openttd.cfg'\n"
		"  -x                  = Never save configuration changes to disk\n"
		"  -X                  = Don't use global folders to search for files\n"
		"  -q savegame         = Write some information about the savegame and exit\n"
		"  -Q                  = Don't scan for/load NewGRF files on startup\n"
		"  -QQ                 = Disable NewGRF scanning/loading entirely\n"
		"  -Z                  = Write detailed version information and exit\n"
		"\n",
		lastof(buf)
	);

	/* List the graphics packs */
	p = BaseGraphics::GetSetsList(p, lastof(buf));

	/* List the sounds packs */
	p = BaseSounds::GetSetsList(p, lastof(buf));

	/* List the music packs */
	p = BaseMusic::GetSetsList(p, lastof(buf));

	/* List the drivers */
	p = DriverFactoryBase::GetDriversInfo(p, lastof(buf));

	/* List the blitters */
	p = BlitterFactory::GetBlittersInfo(p, lastof(buf));

	/* List the debug facilities. */
	p = DumpDebugFacilityNames(p, lastof(buf));

	/* We need to initialize the AI, so it finds the AIs */
	AI::Initialize();
	const std::string ai_list = AI::GetConsoleList(true);
	p = strecpy(p, ai_list.c_str(), lastof(buf));
	AI::Uninitialize(true);

	/* We need to initialize the GameScript, so it finds the GSs */
	Game::Initialize();
	const std::string game_list = Game::GetConsoleList(true);
	p = strecpy(p, game_list.c_str(), lastof(buf));
	Game::Uninitialize(true);

	/* ShowInfo put output to stderr, but version information should go
	 * to stdout; this is the only exception */
#if !defined(_WIN32)
	printf("%s\n", buf);
#else
	ShowInfoI(buf);
#endif
}

static void WriteSavegameInfo(const char *name)
{
	extern SaveLoadVersion _sl_version;
	extern std::string _sl_xv_version_label;
	extern SaveLoadVersion _sl_xv_upstream_version;
	uint32_t last_ottd_rev = 0;
	byte ever_modified = 0;
	bool removed_newgrfs = false;

	GamelogInfo(_load_check_data.gamelog_actions, &last_ottd_rev, &ever_modified, &removed_newgrfs);

	char buf[65536];
	char *p = buf;
	p += seprintf(p, lastof(buf), "Name:         %s\n", name);
	const char *type = "";
	extern bool _sl_is_faked_ext;
	extern bool _sl_is_ext_version;
	if (_sl_is_faked_ext) {
		type = " (fake extended)";
	} else if (_sl_is_ext_version) {
		type = " (extended)";
	}
	p += seprintf(p, lastof(buf), "Savegame ver: %d%s\n", _sl_version, type);
	if (!_sl_xv_version_label.empty()) {
		p += seprintf(p, lastof(buf), "    Version label: %s\n", _sl_xv_version_label.c_str());
	}
	if (_sl_xv_upstream_version != SL_MIN_VERSION) {
		p += seprintf(p, lastof(buf), "    Upstream version: %u\n", _sl_xv_upstream_version);
	}
	for (size_t i = 0; i < XSLFI_SIZE; i++) {
		if (_sl_xv_feature_versions[i] > 0) {
			p += seprintf(p, lastof(buf), "    Feature: %s = %d\n", SlXvGetFeatureName((SlXvFeatureIndex) i), _sl_xv_feature_versions[i]);
		}
	}
	p += seprintf(p, lastof(buf), "NewGRF ver:   0x%08X\n", last_ottd_rev);
	p += seprintf(p, lastof(buf), "Modified:     %d\n", ever_modified);

	if (removed_newgrfs) {
		p += seprintf(p, lastof(buf), "NewGRFs have been removed\n");
	}

	p = strecpy(p, "NewGRFs:\n", lastof(buf));
	if (_load_check_data.HasNewGrfs()) {
		for (GRFConfig *c = _load_check_data.grfconfig; c != nullptr; c = c->next) {
			char md5sum[33];
			md5sumToString(md5sum, lastof(md5sum), HasBit(c->flags, GCF_COMPATIBLE) ? c->original_md5sum : c->ident.md5sum);
			p += seprintf(p, lastof(buf), "%08X %s %s\n", c->ident.grfid, md5sum, c->filename.c_str());
		}
	}

	/* ShowInfo put output to stderr, but version information should go
	 * to stdout; this is the only exception */
#if !defined(_WIN32)
	printf("%s\n", buf);
#else
	ShowInfoI(buf);
#endif
}

static void WriteSavegameDebugData(const char *name)
{
	char *buf = MallocT<char>(4096);
	char *buflast = buf + 4095;
	char *p = buf;
	auto bump_size = [&]() {
		size_t offset = p - buf;
		size_t new_size = buflast - buf + 1 + 4096;
		buf = ReallocT<char>(buf, new_size);
		buflast = buf + new_size - 1;
		p = buf + offset;
	};
	p += seprintf(p, buflast, "Name:         %s\n", name);
	if (_load_check_data.debug_log_data.size()) {
		p += seprintf(p, buflast, "%u bytes of debug log data in savegame\n", (uint) _load_check_data.debug_log_data.size());
		std::string buffer = _load_check_data.debug_log_data;
		ProcessLineByLine(buffer.data(), [&](const char *line) {
			if (buflast - p <= 1024) bump_size();
			p += seprintf(p, buflast, "> %s\n", line);
		});
	} else {
		p += seprintf(p, buflast, "No debug log data in savegame\n");
	}
	if (_load_check_data.debug_config_data.size()) {
		p += seprintf(p, buflast, "%u bytes of debug config data in savegame\n", (uint) _load_check_data.debug_config_data.size());
		std::string buffer = _load_check_data.debug_config_data;
		ProcessLineByLine(buffer.data(), [&](const char *line) {
			if (buflast - p <= 1024) bump_size();
			p += seprintf(p, buflast, "> %s\n", line);
		});
	} else {
		p += seprintf(p, buflast, "No debug config data in savegame\n");
	}

	/* ShowInfo put output to stderr, but version information should go
	 * to stdout; this is the only exception */
#if !defined(_WIN32)
	printf("%s\n", buf);
#else
	ShowInfoI(buf);
#endif
	free(buf);
}


/**
 * Extract the resolution from the given string and store
 * it in the 'res' parameter.
 * @param res variable to store the resolution in.
 * @param s   the string to decompose.
 */
static void ParseResolution(Dimension *res, const char *s)
{
	const char *t = strchr(s, 'x');
	if (t == nullptr) {
		ShowInfoF("Invalid resolution '%s'", s);
		return;
	}

	res->width  = std::max(std::strtoul(s, nullptr, 0), 64UL);
	res->height = std::max(std::strtoul(t + 1, nullptr, 0), 64UL);
}


/**
 * Uninitializes drivers, frees allocated memory, cleans pools, ...
 * Generally, prepares the game for shutting down
 */
static void ShutdownGame()
{
	IConsoleFree();

	if (_network_available) NetworkShutDown(); // Shut down the network and close any open connections

	DriverFactoryBase::ShutdownDrivers();

	UnInitWindowSystem();

	/* stop the scripts */
	AI::Uninitialize(false);
	Game::Uninitialize(false);

	/* Uninitialize variables that are allocated dynamically */
	GamelogReset();

	LinkGraphSchedule::Clear();
	ClearTraceRestrictMapping();
	ClearBridgeSimulatedSignalMapping();
	ClearBridgeSignalStyleMapping();
	ClearCargoPacketDeferredPayments();
	PoolBase::Clean(PT_ALL);

	FreeSignalPrograms();
	FreeSignalDependencies();

	extern void ClearNewSignalStyleMapping();
	ClearNewSignalStyleMapping();

	extern void ClearAllSignalSpeedRestrictions();
	ClearAllSignalSpeedRestrictions();

	ClearZoningCaches();
	InvalidatePlanCaches();
	ClearOrderDestinationRefcountMap();

	/* No NewGRFs were loaded when it was still bootstrapping. */
	if (_game_mode != GM_BOOTSTRAP) ResetNewGRFData();

	UninitFontCache();

	ViewportMapClearTunnelCache();
	InvalidateVehicleTickCaches();
	ClearVehicleTickCaches();
	InvalidateTemplateReplacementImages();
	ClearCommandLog();
	ClearCommandQueue();
	ClearSpecialEventsLog();
	ClearDesyncMsgLog();

	extern void UninitializeCompanies();
	UninitializeCompanies();

	_loaded_local_company = COMPANY_SPECTATOR;
	_game_events_since_load = (GameEventFlags) 0;
	_game_events_overall = (GameEventFlags) 0;
	_game_load_cur_date_ymd = { 0, 0, 0 };
	_game_load_date_fract = 0;
	_game_load_tick_skip_counter = 0;
	_game_load_state_ticks = 0;
	_game_load_time = 0;
	_extra_aspects = 0;
	_aspect_cfg_hash = 0;
	_station_tile_cache_hash = 0;
	InitGRFGlobalVars();
	_loadgame_DBGL_data.clear();
	_loadgame_DBGC_data.clear();
}

/**
 * Load the introduction game.
 * @param load_newgrfs Whether to load the NewGRFs or not.
 */
static void LoadIntroGame(bool load_newgrfs = true)
{
	UnshowCriticalError();
	for (Window *w : Window::Iterate()) {
		w->Close();
	}

	_game_mode = GM_MENU;

	if (load_newgrfs) ResetGRFConfig(false);

	/* Setup main window */
	ResetWindowSystem();
	SetupColoursAndInitialWindow();

	/* Load the default opening screen savegame */
	if (SaveOrLoad("opntitle.dat", SLO_LOAD, DFT_GAME_FILE, BASESET_DIR) != SL_OK) {
		GenerateWorld(GWM_EMPTY, 64, 64); // if failed loading, make empty world.
		SetLocalCompany(COMPANY_SPECTATOR);
	} else {
		SetLocalCompany(COMPANY_FIRST);
	}

	FixTitleGameZoom();
	_pause_mode = PM_UNPAUSED;
	_pause_countdown = 0;
	_cursor.fix_at = false;

	CheckForMissingGlyphs();

	MusicLoop(); // ensure music is correct
}

void MakeNewgameSettingsLive()
{
	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		if (_settings_game.ai_config[c] != nullptr) {
			delete _settings_game.ai_config[c];
		}
	}
	if (_settings_game.game_config != nullptr) {
		delete _settings_game.game_config;
	}

	/* Copy newgame settings to active settings.
	 * Also initialise old settings needed for savegame conversion. */
	_settings_game = _settings_newgame;
	_settings_time = _settings_game.game_time = (TimeSettings)_settings_client.gui;
	_old_vds = _settings_client.company.vehicle;

	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		_settings_game.ai_config[c] = nullptr;
		if (_settings_newgame.ai_config[c] != nullptr) {
			_settings_game.ai_config[c] = new AIConfig(_settings_newgame.ai_config[c]);
		}
	}
	_settings_game.game_config = nullptr;
	if (_settings_newgame.game_config != nullptr) {
		_settings_game.game_config = new GameConfig(_settings_newgame.game_config);
	}

	SetupTickRate();
}

void OpenBrowser(const std::string &url)
{
	/* Make sure we only accept urls that are sure to open a browser. */
	if (url.starts_with("http://") || url.starts_with("https://")) {
		OSOpenBrowser(url);
	}
}

/** Callback structure of statements to be executed after the NewGRF scan. */
struct AfterNewGRFScan : NewGRFScanCallback {
	Year startyear = INVALID_YEAR;                ///< The start year.
	uint32_t generation_seed = GENERATE_NEW_SEED; ///< Seed for the new game.
	std::string dedicated_host;                   ///< Hostname for the dedicated server.
	uint16_t dedicated_port = 0;                  ///< Port for the dedicated server.
	std::string connection_string;                ///< Information about the server to connect to
	std::string join_server_password;             ///< The password to join the server with.
	std::string join_company_password;            ///< The password to join the company with.
	bool save_config = true;                      ///< The save config setting.

	/**
	 * Create a new callback.
	 */
	AfterNewGRFScan()
	{
		/* Visual C++ 2015 fails compiling this line (AfterNewGRFScan::generation_seed undefined symbol)
		 * if it's placed outside a member function, directly in the struct body. */
		static_assert(sizeof(generation_seed) == sizeof(_settings_game.game_creation.generation_seed));
	}

	void OnNewGRFsScanned() override
	{
		ResetGRFConfig(false);

		TarScanner::DoScan(TarScanner::SCENARIO);

		AI::Initialize();
		Game::Initialize();

		/* We want the new (correct) NewGRF count to survive the loading. */
		uint last_newgrf_count = _settings_client.gui.last_newgrf_count;
		LoadFromConfig();
		_settings_client.gui.last_newgrf_count = last_newgrf_count;
		/* Since the default for the palette might have changed due to
		 * reading the configuration file, recalculate that now. */
		UpdateNewGRFConfigPalette();

		Game::Uninitialize(true);
		AI::Uninitialize(true);
		LoadFromHighScore();
		LoadHotkeysFromConfig();
		WindowDesc::LoadFromConfig();

		/* We have loaded the config, so we may possibly save it. */
		_save_config = save_config;

		/* restore saved music and effects volumes */
		MusicDriver::GetInstance()->SetVolume(_settings_client.music.music_vol);
		SetEffectVolume(_settings_client.music.effect_vol);

		if (startyear != INVALID_YEAR) IConsoleSetSetting("game_creation.starting_year", startyear);
		if (generation_seed != GENERATE_NEW_SEED) _settings_newgame.game_creation.generation_seed = generation_seed;

		if (!dedicated_host.empty()) {
			_network_bind_list.clear();
			_network_bind_list.emplace_back(dedicated_host);
		}
		if (dedicated_port != 0) _settings_client.network.server_port = dedicated_port;

		/* initialize the ingame console */
		IConsoleInit();
		InitializeGUI();
		IConsoleCmdExec("exec scripts/autoexec.scr 0");

		/* Make sure _settings is filled with _settings_newgame if we switch to a game directly */
		if (_switch_mode != SM_NONE) MakeNewgameSettingsLive();

		if (_network_available && !connection_string.empty()) {
			LoadIntroGame();
			_switch_mode = SM_NONE;

			NetworkClientConnectGame(connection_string, COMPANY_NEW_COMPANY, join_server_password, join_company_password);
		}

		/* After the scan we're not used anymore. */
		delete this;
	}
};

void PostMainLoop()
{
	WaitTillSaved();

	/* only save config if we have to */
	if (_save_config) {
		SaveToConfig(STCF_ALL);
		SaveHotkeysToConfig();
		WindowDesc::SaveToConfig();
		SaveToHighScore();
	}

	/* Reset windowing system, stop drivers, free used memory, ... */
	ShutdownGame();
}

#if defined(UNIX)
extern void DedicatedFork();
#endif

/** Options of OpenTTD. */
static const OptionData _options[] = {
	 GETOPT_SHORT_VALUE('I'),
	 GETOPT_SHORT_VALUE('S'),
	 GETOPT_SHORT_VALUE('M'),
	 GETOPT_SHORT_VALUE('m'),
	 GETOPT_SHORT_VALUE('s'),
	 GETOPT_SHORT_VALUE('v'),
	 GETOPT_SHORT_VALUE('b'),
	GETOPT_SHORT_OPTVAL('D'),
	 GETOPT_SHORT_VALUE('n'),
	 GETOPT_SHORT_VALUE('p'),
	 GETOPT_SHORT_VALUE('P'),
#if !defined(_WIN32)
	 GETOPT_SHORT_NOVAL('f'),
#endif
	 GETOPT_SHORT_VALUE('r'),
	 GETOPT_SHORT_VALUE('t'),
	GETOPT_SHORT_OPTVAL('d'),
	 GETOPT_SHORT_NOVAL('e'),
	GETOPT_SHORT_OPTVAL('g'),
	 GETOPT_SHORT_VALUE('G'),
	 GETOPT_SHORT_VALUE('c'),
	 GETOPT_SHORT_NOVAL('x'),
	 GETOPT_SHORT_NOVAL('X'),
	 GETOPT_SHORT_VALUE('q'),
	 GETOPT_SHORT_VALUE('K'),
	 GETOPT_SHORT_NOVAL('h'),
	 GETOPT_SHORT_NOVAL('Q'),
	 GETOPT_SHORT_VALUE('J'),
	 GETOPT_SHORT_NOVAL('Z'),
	GETOPT_END()
};

/**
 * Main entry point for this lovely game.
 * @param argc The number of arguments passed to this game.
 * @param argv The values of the arguments.
 * @return 0 when there is no error.
 */
int openttd_main(int argc, char *argv[])
{
	SetSelfAsMainThread();
	PerThreadSetup();
	SlXvSetStaticCurrentVersions();
	std::string musicdriver;
	std::string sounddriver;
	std::string videodriver;
	std::string blitter;
	std::string graphics_set;
	std::string sounds_set;
	std::string music_set;
	Dimension resolution = {0, 0};
	std::unique_ptr<AfterNewGRFScan> scanner(new AfterNewGRFScan());
	bool dedicated = false;
	bool only_local_path = false;

	extern bool _dedicated_forks;
	_dedicated_forks = false;

	_game_mode = GM_MENU;
	_switch_mode = SM_MENU;

	GetOptData mgo(argc - 1, argv + 1, _options);
	int ret = 0;

	int i;
	while ((i = mgo.GetOpt()) != -1) {
		switch (i) {
		case 'I': graphics_set = mgo.opt; break;
		case 'S': sounds_set = mgo.opt; break;
		case 'M': music_set = mgo.opt; break;
		case 'm': musicdriver = mgo.opt; break;
		case 's': sounddriver = mgo.opt; break;
		case 'v': videodriver = mgo.opt; break;
		case 'b': blitter = mgo.opt; break;
		case 'D':
			musicdriver = "null";
			sounddriver = "null";
			videodriver = "dedicated";
			blitter = "null";
			dedicated = true;
			SetDebugString("net=3", ShowInfoI);
			if (mgo.opt != nullptr) {
				scanner->dedicated_host = ParseFullConnectionString(mgo.opt, scanner->dedicated_port);
			}
			break;
		case 'f': _dedicated_forks = true; break;
		case 'n':
			scanner->connection_string = mgo.opt; // host:port#company parameter
			break;
		case 'p':
			scanner->join_server_password = mgo.opt;
			break;
		case 'P':
			scanner->join_company_password = mgo.opt;
			break;
		case 'r': ParseResolution(&resolution, mgo.opt); break;
		case 't': scanner->startyear = atoi(mgo.opt); break;
		case 'd': {
#if defined(_WIN32)
				CreateConsole();
#endif
				if (mgo.opt != nullptr) SetDebugString(mgo.opt, ShowInfoI);
				break;
			}
		case 'e': _switch_mode = (_switch_mode == SM_LOAD_GAME || _switch_mode == SM_LOAD_SCENARIO ? SM_LOAD_SCENARIO : SM_EDITOR); break;
		case 'g':
			if (mgo.opt != nullptr) {
				_file_to_saveload.name = mgo.opt;
				bool is_scenario = _switch_mode == SM_EDITOR || _switch_mode == SM_LOAD_SCENARIO;
				_switch_mode = is_scenario ? SM_LOAD_SCENARIO : SM_LOAD_GAME;
				_file_to_saveload.SetMode(SLO_LOAD, is_scenario ? FT_SCENARIO : FT_SAVEGAME, DFT_GAME_FILE);

				/* if the file doesn't exist or it is not a valid savegame, let the saveload code show an error */
				auto t = _file_to_saveload.name.find_last_of('.');
				if (t != std::string::npos) {
					FiosType ft = FiosGetSavegameListCallback(SLO_LOAD, _file_to_saveload.name, _file_to_saveload.name.substr(t).c_str(), nullptr, nullptr);
					if (ft != FIOS_TYPE_INVALID) _file_to_saveload.SetMode(ft);
				}

				break;
			}

			_switch_mode = SM_NEWGAME;
			/* Give a random map if no seed has been given */
			if (scanner->generation_seed == GENERATE_NEW_SEED) {
				scanner->generation_seed = InteractiveRandom();
			}
			break;
		case 'q':
		case 'K': {
			DeterminePaths(argv[0], only_local_path);
			if (StrEmpty(mgo.opt)) {
				ret = 1;
				return ret;
			}

			char title[80];
			title[0] = '\0';
			FiosGetSavegameListCallback(SLO_LOAD, mgo.opt, strrchr(mgo.opt, '.'), title, lastof(title));

			_load_check_data.Clear();
			if (i == 'K') _load_check_data.want_debug_data = true;
			_load_check_data.want_grf_compatibility = false;
			SaveOrLoadResult res = SaveOrLoad(mgo.opt, SLO_CHECK, DFT_GAME_FILE, SAVE_DIR, false);
			if (res != SL_OK || _load_check_data.HasErrors()) {
				fprintf(stderr, "Failed to open savegame\n");
				if (_load_check_data.HasErrors()) {
					InitializeLanguagePacks(); // A language pack is needed for GetString()
					std::string buf;
					SetDParamStr(0, _load_check_data.error_msg);
					GetString(StringBuilder(buf), _load_check_data.error);
					buf += '\n';
					fputs(buf.c_str(), stderr);
				}
				return ret;
			}

			if (i == 'q') {
				WriteSavegameInfo(title);
			} else {
				WriteSavegameDebugData(title);
			}
			return ret;
		}
		case 'Q': {
			extern int _skip_all_newgrf_scanning;
			_skip_all_newgrf_scanning += 1;
			break;
		}
		case 'G': scanner->generation_seed = std::strtoul(mgo.opt, nullptr, 10); break;
		case 'c': _config_file = mgo.opt; break;
		case 'x': scanner->save_config = false; break;
		case 'J': _quit_after_days = Clamp(atoi(mgo.opt), 0, INT_MAX); break;
		case 'Z': {
			CrashLog::VersionInfoLog();
			return ret;
		}
		case 'X': only_local_path = true; break;
		case 'h':
			i = -2; // Force printing of help.
			break;
		}
		if (i == -2) break;
	}

	if (i == -2 || mgo.numleft > 0) {
		/* Either the user typed '-h', they made an error, or they added unrecognized command line arguments.
		 * In all cases, print the help, and exit.
		 *
		 * The next two functions are needed to list the graphics sets. We can't do them earlier
		 * because then we cannot show it on the debug console as that hasn't been configured yet. */
		DeterminePaths(argv[0], only_local_path);
		TarScanner::DoScan(TarScanner::BASESET);
		BaseGraphics::FindSets();
		BaseSounds::FindSets();
		BaseMusic::FindSets();
		ShowHelp();
		return ret;
	}

	DeterminePaths(argv[0], only_local_path);
	TarScanner::DoScan(TarScanner::BASESET);

	if (dedicated) DEBUG(net, 3, "Starting dedicated server, version %s", _openttd_revision);
	if (_dedicated_forks && !dedicated) _dedicated_forks = false;

#if defined(UNIX)
	/* We must fork here, or we'll end up without some resources we need (like sockets) */
	if (_dedicated_forks) DedicatedFork();
#endif

	LoadFromConfig(true);

	if (resolution.width != 0) _cur_resolution = resolution;

	/* Limit width times height times bytes per pixel to fit a 32 bit
	 * integer, This way all internal drawing routines work correctly.
	 * A resolution that has one component as 0 is treated as a marker to
	 * auto-detect a good window size. */
	_cur_resolution.width  = std::min(_cur_resolution.width, UINT16_MAX / 2u);
	_cur_resolution.height = std::min(_cur_resolution.height, UINT16_MAX / 2u);

	/* Assume the cursor starts within the game as not all video drivers
	 * get an event that the cursor is within the window when it is opened.
	 * Saying the cursor is there makes no visible difference as it would
	 * just be out of the bounds of the window. */
	_cursor.in_window = true;

	/* enumerate language files */
	InitializeLanguagePacks();

	/* Initialize the font cache */
	InitFontCache(false);

	/* This must be done early, since functions use the SetWindowDirty* calls */
	InitWindowSystem();

	BaseGraphics::FindSets();
	bool valid_graphics_set;
	if (!graphics_set.empty()) {
		valid_graphics_set = BaseGraphics::SetSetByName(graphics_set);
	} else if (BaseGraphics::ini_data.shortname != 0) {
		graphics_set = BaseGraphics::ini_data.name;
		valid_graphics_set = BaseGraphics::SetSetByShortname(BaseGraphics::ini_data.shortname);
		if (valid_graphics_set && !BaseGraphics::ini_data.extra_params.empty()) {
			GRFConfig &extra_cfg = BaseGraphics::GetUsedSet()->GetOrCreateExtraConfig();
			if (extra_cfg.IsCompatible(BaseGraphics::ini_data.extra_version)) {
				extra_cfg.SetParams(BaseGraphics::ini_data.extra_params);
			}
		}
	} else if (!BaseGraphics::ini_data.name.empty()) {
		graphics_set = BaseGraphics::ini_data.name;
		valid_graphics_set = BaseGraphics::SetSetByName(BaseGraphics::ini_data.name);
	} else {
		valid_graphics_set = true;
		BaseGraphics::SetSet(nullptr); // ignore error, continue to bootstrap GUI
	}
	if (!valid_graphics_set) {
		BaseGraphics::SetSet(nullptr);

		ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_BASE_GRAPHICS_NOT_FOUND);
		msg.SetDParamStr(0, graphics_set);
		ScheduleErrorMessage(msg);
	}

	/* Initialize game palette */
	GfxInitPalettes();

	DEBUG(misc, 1, "Loading blitter...");
	if (blitter.empty() && !_ini_blitter.empty()) blitter = _ini_blitter;
	_blitter_autodetected = blitter.empty();
	/* Activate the initial blitter.
	 * This is only some initial guess, after NewGRFs have been loaded SwitchNewGRFBlitter may switch to a different one.
	 *  - Never guess anything, if the user specified a blitter. (_blitter_autodetected)
	 *  - Use 32bpp blitter if baseset or 8bpp-support settings says so.
	 *  - Use 8bpp blitter otherwise.
	 */
	if (!_blitter_autodetected ||
			(_support8bpp != S8BPP_NONE && (BaseGraphics::GetUsedSet() == nullptr || BaseGraphics::GetUsedSet()->blitter == BLT_8BPP)) ||
			BlitterFactory::SelectBlitter("32bpp-anim") == nullptr) {
		if (BlitterFactory::SelectBlitter(blitter) == nullptr) {
			blitter.empty() ?
				usererror("Failed to autoprobe blitter") :
				usererror("Failed to select requested blitter '%s'; does it exist?", blitter.c_str());
		}
	}

	if (videodriver.empty() && !_ini_videodriver.empty()) videodriver = _ini_videodriver;
	DriverFactoryBase::SelectDriver(videodriver, Driver::DT_VIDEO);

	InitializeSpriteSorter();

	/* Initialize the zoom level of the screen to normal */
	_screen.zoom = ZOOM_LVL_NORMAL;

	/* The video driver is now selected, now initialise GUI zoom */
	AdjustGUIZoom(AGZM_STARTUP);

	NetworkStartUp(); // initialize network-core

	if (!HandleBootstrap()) {
		ShutdownGame();
		return ret;
	}

	VideoDriver::GetInstance()->ClaimMousePointer();

	/* initialize screenshot formats */
	InitializeScreenshotFormats();

	BaseSounds::FindSets();
	if (sounds_set.empty() && !BaseSounds::ini_set.empty()) sounds_set = BaseSounds::ini_set;
	if (!BaseSounds::SetSetByName(sounds_set)) {
		if (sounds_set.empty() || !BaseSounds::SetSet({})) {
			usererror("Failed to find a sounds set. Please acquire a sounds set for OpenTTD. See section 1.4 of README.md.");
		} else {
			ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_BASE_SOUNDS_NOT_FOUND);
			msg.SetDParamStr(0, sounds_set);
			ScheduleErrorMessage(msg);
		}
	}

	BaseMusic::FindSets();
	if (music_set.empty() && !BaseMusic::ini_set.empty()) music_set = BaseMusic::ini_set;
	if (!BaseMusic::SetSetByName(music_set)) {
		if (music_set.empty() || !BaseMusic::SetSet({})) {
			usererror("Failed to find a music set. Please acquire a music set for OpenTTD. See section 1.4 of README.md.");
		} else {
			ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_BASE_MUSIC_NOT_FOUND);
			msg.SetDParamStr(0, music_set);
			ScheduleErrorMessage(msg);
		}
	}

	if (sounddriver.empty() && !_ini_sounddriver.empty()) sounddriver = _ini_sounddriver;
	DriverFactoryBase::SelectDriver(sounddriver, Driver::DT_SOUND);

	if (musicdriver.empty() && !_ini_musicdriver.empty()) musicdriver = _ini_musicdriver;
	_music_driver_params = std::move(musicdriver);
	if (_music_driver_params.empty() && BaseMusic::GetUsedSet()->name == "NoMusic") {
		DEBUG(driver, 1, "Deferring loading of music driver until a music set is loaded");
		DriverFactoryBase::SelectDriver("null", Driver::DT_MUSIC);
	} else {
		InitMusicDriver(false);
	}

	GenerateWorld(GWM_EMPTY, 64, 64); // Make the viewport initialization happy
	LoadIntroGame(false);

	CheckForMissingGlyphs();

	/* ScanNewGRFFiles now has control over the scanner. */
	RequestNewGRFScan(scanner.release());

	_general_worker_pool.Start("ottd:worker", 8);

	VideoDriver::GetInstance()->MainLoop();

	_general_worker_pool.Stop();

	PostMainLoop();
	return ret;
}

void InitMusicDriver(bool init_volume)
{
	if (_music_inited.exchange(true)) return;

	{
		std::unique_lock<std::mutex> lock(_music_driver_mutex);

		static std::unique_ptr<MusicDriver> old_driver;
		old_driver = MusicDriver::ExtractDriver();

		DriverFactoryBase::SelectDriver(_music_driver_params, Driver::DT_MUSIC);
	}

	if (init_volume) MusicDriver::GetInstance()->SetVolume(_settings_client.music.music_vol);
}

void HandleExitGameRequest()
{
	if (_game_mode == GM_MENU || _game_mode == GM_BOOTSTRAP) { // do not ask to quit on the main screen
		_exit_game = true;
	} else if (_settings_client.gui.autosave_on_exit) {
		DoExitSave();
		_survey.Transmit(NetworkSurveyHandler::Reason::EXIT, true);
		_exit_game = true;
	} else {
		AskExitGame();
	}
}

/**
 * Triggers everything required to set up a saved scenario for a new game.
 */
static void OnStartScenario()
{
	/* Reset engine pool to simplify changing engine NewGRFs in scenario editor. */
	EngineOverrideManager::ResetToCurrentNewGRFConfig();

	/* Make sure all industries were built "this year", to avoid too early closures. (#9918) */
	for (Industry *i : Industry::Iterate()) {
		i->last_prod_year = _cur_year;
	}
}

/**
 * Triggers everything that should be triggered when starting a game.
 * @param dedicated_server Whether this is a dedicated server or not.
 */
static void OnStartGame(bool dedicated_server)
{
	/* Update the local company for a loaded game. It is either always
	 * a company or in the case of a dedicated server a spectator */
	if (_network_server && !dedicated_server) {
		NetworkServerDoMove(CLIENT_ID_SERVER, GetDefaultLocalCompany());
	} else {
		SetLocalCompany(dedicated_server ? COMPANY_SPECTATOR : GetDefaultLocalCompany());
	}
	if (_ctrl_pressed && !dedicated_server) {
		DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);
	}
	/* Update the static game info to set the values from the new game. */
	NetworkServerUpdateGameInfo();
	/* Execute the game-start script */
	IConsoleCmdExec("exec scripts/game_start.scr 0");
}

static void MakeNewGameDone()
{
	SettingsDisableElrail(_settings_game.vehicle.disable_elrails);

	extern void PostCheckNewGRFLoadWarnings();
	PostCheckNewGRFLoadWarnings();

	/* In a dedicated server, the server does not play */
	if (!VideoDriver::GetInstance()->HasGUI()) {
		OnStartGame(true);
		if (_settings_client.gui.pause_on_newgame) DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);
		return;
	}

	/* Create a single company */
	DoStartupNewCompany(DSNC_NONE);

	Company *c = Company::Get(COMPANY_FIRST);
	c->settings = _settings_client.company;

	/* Overwrite color from settings if needed
	 * COLOUR_END corresponds to Random colour */

	if (_settings_client.gui.starting_colour != COLOUR_END) {
		c->colour = _settings_client.gui.starting_colour;
		ResetCompanyLivery(c);
		_company_colours[c->index] = (Colours)c->colour;
		BuildOwnerLegend();
	}

	if (_settings_client.gui.starting_colour_secondary != COLOUR_END && HasBit(_loaded_newgrf_features.used_liveries, LS_DEFAULT)) {
		DoCommandP(0, LS_DEFAULT | 1 << 8, _settings_client.gui.starting_colour_secondary, CMD_SET_COMPANY_COLOUR);
	}

	OnStartGame(false);

	InitializeRailGUI();
	InitializeRoadGUI();

	/* We are the server, we start a new company (not dedicated),
	 * so set the default password *if* needed. */
	if (_network_server && !_settings_client.network.default_company_pass.empty()) {
		NetworkChangeCompanyPassword(_local_company, _settings_client.network.default_company_pass);
	}

	if (_settings_client.gui.pause_on_newgame) DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);

	CheckEngines();
	CheckIndustries();
	MarkWholeScreenDirty();

	if (_network_server && !_network_dedicated) ShowClientList();
}

/*
 * Too large size may be stored in settings (especially if switching between between OpenTTD
 * versions with different map size limits), we have to check if it is valid before generating world.
 * Simple separate checking of X and Y map sizes is not enough, as their sum is what counts for the limit.
 * Check the size and decrease the larger of the sizes till the size is in limit.
 */
static void FixConfigMapSize()
{
	while (_settings_game.game_creation.map_x + _settings_game.game_creation.map_y > MAX_MAP_TILES_BITS) {
		/* Repeat reducing larger of X/Y dimensions until the map size is within allowable limits */
		if (_settings_game.game_creation.map_x > _settings_game.game_creation.map_y) {
			_settings_game.game_creation.map_x--;
		} else {
			_settings_game.game_creation.map_y--;
		}
	}
}

static void MakeNewGame(bool from_heightmap, bool reset_settings)
{
	_game_mode = GM_NORMAL;
	if (!from_heightmap) {
		/* "reload" command needs to know what mode we were in. */
		_file_to_saveload.SetMode(SLO_INVALID, FT_INVALID, DFT_INVALID);
	}

	ResetGRFConfig(true);

	GenerateWorldSetCallback(&MakeNewGameDone);
	FixConfigMapSize();
	GenerateWorld(from_heightmap ? GWM_HEIGHTMAP : GWM_NEWGAME, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y, reset_settings);
}

static void MakeNewEditorWorldDone()
{
	SetLocalCompany(OWNER_NONE);

	extern void PostCheckNewGRFLoadWarnings();
	PostCheckNewGRFLoadWarnings();
}

static void MakeNewEditorWorld()
{
	_game_mode = GM_EDITOR;
	/* "reload" command needs to know what mode we were in. */
	_file_to_saveload.SetMode(SLO_INVALID, FT_INVALID, DFT_INVALID);

	ResetGRFConfig(true);

	GenerateWorldSetCallback(&MakeNewEditorWorldDone);
	FixConfigMapSize();
	GenerateWorld(GWM_EMPTY, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y);
}

/**
 * Load the specified savegame but on error do different things.
 * If loading fails due to corrupt savegame, bad version, etc. go back to
 * a previous correct state. In the menu for example load the intro game again.
 * @param filename file to be loaded
 * @param fop mode of loading, always SLO_LOAD
 * @param newgm switch to this mode of loading fails due to some unknown error
 * @param subdir default directory to look for filename, set to 0 if not needed
 * @param lf Load filter to use, if nullptr: use filename + subdir.
 * @param error_detail Optional string to fill with detaied error information.
 */
bool SafeLoad(const std::string &filename, SaveLoadOperation fop, DetailedFileType dft, GameMode newgm, Subdirectory subdir,
		struct LoadFilter *lf = nullptr, std::string *error_detail = nullptr)
{
	assert(fop == SLO_LOAD);
	assert(dft == DFT_GAME_FILE || (lf == nullptr && dft == DFT_OLD_GAME_FILE));
	GameMode ogm = _game_mode;

	_game_mode = newgm;

	SaveOrLoadResult result = (lf == nullptr) ? SaveOrLoad(filename, fop, dft, subdir) : LoadWithFilter(lf);
	if (result == SL_OK) return true;

	if (error_detail != nullptr) *error_detail = GetSaveLoadErrorString();

	if (_network_dedicated && ogm == GM_MENU) {
		/*
		 * If we are a dedicated server *and* we just were in the menu, then we
		 * are loading the first savegame. If that fails, not starting the
		 * server is a better reaction than starting the server with a newly
		 * generated map as it is quite likely to be started from a script.
		 */
		DEBUG(net, 0, "Loading requested map failed; closing server.");
		_exit_game = true;
		return false;
	}

	if (result != SL_REINIT) {
		_game_mode = ogm;
		return false;
	}

	if (_network_dedicated) {
		/*
		 * If we are a dedicated server, have already loaded/started a game,
		 * and then loading the savegame fails in a manner that we need to
		 * reinitialize everything. We must not fall back into the menu mode
		 * with the intro game, as that is unjoinable by clients. So there is
		 * nothing else to do than start a new game, as it might have failed
		 * trying to reload the originally loaded savegame/scenario.
		 */
		DEBUG(net, 0, "Loading game failed, so a new (random) game will be started");
		MakeNewGame(false, true);
		return false;
	}

	if (_network_server) {
		/* We can't load the intro game as server, so disconnect first. */
		NetworkDisconnect();
	}

	switch (ogm) {
		default:
		case GM_MENU:   LoadIntroGame();      break;
		case GM_EDITOR: MakeNewEditorWorld(); break;
	}
	return false;
}

void SwitchToMode(SwitchMode new_mode)
{
	/* If we are saving something, the network stays in its current state */
	if (new_mode != SM_SAVE_GAME) {
		/* If the network is active, make it not-active */
		if (_networking) {
			if (_network_server && (new_mode == SM_LOAD_GAME || new_mode == SM_NEWGAME || new_mode == SM_RESTARTGAME)) {
				NetworkReboot();
			} else {
				NetworkDisconnect();
			}
		}

		/* If we are a server, we restart the server */
		if (_is_network_server) {
			/* But not if we are going to the menu */
			if (new_mode != SM_MENU) {
				/* check if we should reload the config */
				if (_settings_client.network.reload_cfg) {
					LoadFromConfig();
					MakeNewgameSettingsLive();
					ResetGRFConfig(false);
				}
				NetworkServerStart();
			} else {
				/* This client no longer wants to be a network-server */
				_is_network_server = false;
			}
		}
	}

	/* Make sure all AI controllers are gone at quitting game */
	if (new_mode != SM_SAVE_GAME) AI::KillAll();

	/* When we change mode, reset the autosave. */
	if (new_mode != SM_SAVE_GAME) ChangeAutosaveFrequency(true);

	/* Transmit the survey if we were in normal-mode and not saving. It always means we leaving the current game. */
	if (_game_mode == GM_NORMAL && new_mode != SM_SAVE_GAME) _survey.Transmit(NetworkSurveyHandler::Reason::LEAVE);

	/* Keep track when we last switch mode. Used for survey, to know how long someone was in a game. */
	if (new_mode != SM_SAVE_GAME) _switch_mode_time = std::chrono::steady_clock::now();

	switch (new_mode) {
		case SM_EDITOR: // Switch to scenario editor
			MakeNewEditorWorld();
			GenerateSavegameId();
			break;

		case SM_RELOADGAME: // Reload with what-ever started the game
			if (_file_to_saveload.abstract_ftype == FT_SAVEGAME || _file_to_saveload.abstract_ftype == FT_SCENARIO) {
				/* Reload current savegame/scenario */
				_switch_mode = _game_mode == GM_EDITOR ? SM_LOAD_SCENARIO : SM_LOAD_GAME;
				SwitchToMode(_switch_mode);
				break;
			} else if (_file_to_saveload.abstract_ftype == FT_HEIGHTMAP) {
				/* Restart current heightmap */
				_switch_mode = _game_mode == GM_EDITOR ? SM_LOAD_HEIGHTMAP : SM_RESTART_HEIGHTMAP;
				SwitchToMode(_switch_mode);
				break;
			}

			MakeNewGame(false, new_mode == SM_NEWGAME);
			GenerateSavegameId();
			break;

		case SM_RESTARTGAME: // Restart --> 'Random game' with current settings
		case SM_NEWGAME: // New Game --> 'Random game'
			MakeNewGame(false, new_mode == SM_NEWGAME);
			GenerateSavegameId();
			break;

		case SM_LOAD_GAME: { // Load game, Play Scenario
			ResetGRFConfig(true);
			ResetWindowSystem();

			if (!SafeLoad(_file_to_saveload.name, _file_to_saveload.file_op, _file_to_saveload.detail_ftype, GM_NORMAL, NO_DIRECTORY)) {
				SetDParamStr(0, GetSaveLoadErrorString());
				ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_CRITICAL);
			} else {
				if (_file_to_saveload.abstract_ftype == FT_SCENARIO) {
					OnStartScenario();
				}
				OnStartGame(_network_dedicated);
				/* Decrease pause counter (was increased from opening load dialog) */
				DoCommandP(0, PM_PAUSED_SAVELOAD, 0, CMD_PAUSE);
			}
			break;
		}

		case SM_RESTART_HEIGHTMAP: // Load a heightmap and start a new game from it with current settings
		case SM_START_HEIGHTMAP: // Load a heightmap and start a new game from it
			MakeNewGame(true, new_mode == SM_START_HEIGHTMAP);
			GenerateSavegameId();
			break;

		case SM_LOAD_HEIGHTMAP: // Load heightmap from scenario editor
			SetLocalCompany(OWNER_NONE);

			FixConfigMapSize();
			GenerateWorld(GWM_HEIGHTMAP, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y);
			GenerateSavegameId();
			MarkWholeScreenDirty();
			break;

		case SM_LOAD_SCENARIO: { // Load scenario from scenario editor
			if (SafeLoad(_file_to_saveload.name, _file_to_saveload.file_op, _file_to_saveload.detail_ftype, GM_EDITOR, NO_DIRECTORY)) {
				SetLocalCompany(OWNER_NONE);
				GenerateSavegameId();
				_settings_newgame.game_creation.starting_year = _cur_year;
				/* Cancel the saveload pausing */
				DoCommandP(0, PM_PAUSED_SAVELOAD, 0, CMD_PAUSE);
			} else {
				SetDParamStr(0, GetSaveLoadErrorString());
				ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_CRITICAL);
			}
			break;
		}

		case SM_JOIN_GAME: // Join a multiplayer game
			LoadIntroGame();
			NetworkClientJoinGame();
			break;

		case SM_MENU: // Switch to game intro menu
			LoadIntroGame();
			if (BaseSounds::ini_set.empty() && BaseSounds::GetUsedSet()->fallback && SoundDriver::GetInstance()->HasOutput()) {
				ShowErrorMessage(STR_WARNING_FALLBACK_SOUNDSET, INVALID_STRING_ID, WL_CRITICAL);
				BaseSounds::ini_set = BaseSounds::GetUsedSet()->name;
			}
			if (_settings_client.network.participate_survey == PS_ASK) {
				/* No matter how often you go back to the main menu, only ask the first time. */
				static bool asked_once = false;
				if (!asked_once) {
					asked_once = true;
					ShowNetworkAskSurvey();
				}
			}
			break;

		case SM_SAVE_GAME: { // Save game.
			/* Make network saved games on pause compatible to singleplayer mode */
			SaveModeFlags flags = SMF_NONE;
			if (_game_mode == GM_EDITOR) flags |= SMF_SCENARIO;
			if (SaveOrLoad(_file_to_saveload.name, SLO_SAVE, DFT_GAME_FILE, NO_DIRECTORY, true, flags) != SL_OK) {
				SetDParamStr(0, GetSaveLoadErrorString());
				ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_ERROR);
			} else {
				CloseWindowById(WC_SAVELOAD, 0);
			}
			break;
		}

		case SM_SAVE_HEIGHTMAP: // Save heightmap.
			MakeHeightmapScreenshot(_file_to_saveload.name.c_str());
			CloseWindowById(WC_SAVELOAD, 0);
			break;

		case SM_GENRANDLAND: // Generate random land within scenario editor
			SetLocalCompany(OWNER_NONE);
			FixConfigMapSize();
			GenerateWorld(GWM_RANDOM, 1 << _settings_game.game_creation.map_x, 1 << _settings_game.game_creation.map_y);
			/* XXX: set date */
			MarkWholeScreenDirty();
			break;

		default: NOT_REACHED();
	}

	SmallMapWindow::RebuildColourIndexIfNecessary();
}

void WriteVehicleInfo(char *&p, const char *last, const Vehicle *u, const Vehicle *v, uint length)
{
	p += seprintf(p, last, ": type %i, vehicle %i (%i), company %i, unit number %i, wagon %i, engine: ",
			(int)u->type, u->index, v->index, (int)u->owner, v->unitnumber, length);
	SetDParam(0, u->engine_type);
	p = strecpy(p, GetString(STR_ENGINE_NAME).c_str(), last, true);
	uint32_t grfid = u->GetGRFID();
	if (grfid) {
		p += seprintf(p, last, ", GRF: %08X", BSWAP32(grfid));
		GRFConfig *grfconfig = GetGRFConfig(grfid);
		if (grfconfig) {
			p += seprintf(p, last, ", %s, %s", grfconfig->GetName(), grfconfig->filename.c_str());
		}
	}
}

static bool SignalInfraTotalMatches()
{
	std::array<int, MAX_COMPANIES> old_signal_totals = {};
	for (const Company *c : Company::Iterate()) {
		old_signal_totals[c->index] = c->infrastructure.signal;
	}

	std::array<int, MAX_COMPANIES> new_signal_totals = {};
	for (TileIndex tile = 0; tile < MapSize(); tile++) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY:
				if (HasSignals(tile)) {
					const Company *c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) new_signal_totals[c->index] += CountBits(GetPresentSignals(tile));
				}
				break;

			case MP_TUNNELBRIDGE: {
				/* Only count the tunnel/bridge if we're on the northern end tile. */
				DiagDirection dir = GetTunnelBridgeDirection(tile);
				if (dir == DIAGDIR_NE || dir == DIAGDIR_NW) break;

				if (IsTunnelBridgeWithSignalSimulation(tile)) {
					const Company *c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) new_signal_totals[c->index] += GetTunnelBridgeSignalSimulationSignalCount(tile, GetOtherTunnelBridgeEnd(tile));
				}
				break;
			}

			default:
				break;
		}
	}

	return old_signal_totals == new_signal_totals;
}

/**
 * Check the validity of some of the caches.
 * Especially in the sense of desyncs between
 * the cached value and what the value would
 * be when calculated from the 'base' data.
 */
void CheckCaches(bool force_check, std::function<void(const char *)> log, CheckCachesFlags flags)
{
	if (!force_check) {
		int desync_level = _debug_desync_level;

		if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC)) && desync_level < 1) {
			desync_level = 1;
			if (HasChickenBit(DCBF_DESYNC_CHECK_NO_GENERAL)) flags &= ~CHECK_CACHE_GENERAL;
		}
		if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC_SIGNALS)) && desync_level < 2 && _state_ticks.base() % 256 == 0) {
			if (!SignalInfraTotalMatches()) desync_level = 2;
		}

		/* Return here so it is easy to add checks that are run
		 * always to aid testing of caches. */
		if (desync_level < 1) return;

		if (desync_level == 1 && _state_ticks.base() % 500 != 0) return;
	}

	SCOPE_INFO_FMT([flags], "CheckCaches: %X", flags);

	std::vector<std::string> saved_messages;
	std::function<void(const char *)> log_orig;
	if (flags & CHECK_CACHE_EMIT_LOG) {
		log_orig = std::move(log);
		log = [&saved_messages, &log_orig](const char *str) {
			if (log_orig) log_orig(str);
			saved_messages.emplace_back(str);
		};
	}

	char cclog_buffer[1024];
	auto cclog_common = [&]() {
		DEBUG(desync, 0, "%s", cclog_buffer);
		if (log) {
			log(cclog_buffer);
		} else {
			LogDesyncMsg(cclog_buffer);
		}
	};

#define CCLOG(...) { \
	seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	cclog_common(); \
}

	auto output_veh_info = [&](char *&p, const Vehicle *u, const Vehicle *v, uint length) {
		WriteVehicleInfo(p, lastof(cclog_buffer), u, v, length);
	};
	auto output_veh_info_single = [&](char *&p, const Vehicle *v) {
		uint length = 0;
		for (const Vehicle *u = v->First(); u != v; u = u->Next()) {
			length++;
		}
		WriteVehicleInfo(p, lastof(cclog_buffer), v, v->First(), length);
	};

#define CCLOGV(...) { \
	char *p = cclog_buffer + seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	output_veh_info(p, u, v, length); \
	cclog_common(); \
}

#define CCLOGV1(...) { \
	char *p = cclog_buffer + seprintf(cclog_buffer, lastof(cclog_buffer), __VA_ARGS__); \
	output_veh_info_single(p, v); \
	cclog_common(); \
}

	if (flags & CHECK_CACHE_GENERAL) {
		/* Check the town caches. */
		std::vector<TownCache> old_town_caches;
		std::vector<StationList> old_town_stations_nears;
		for (const Town *t : Town::Iterate()) {
			old_town_caches.push_back(t->cache);
			old_town_stations_nears.push_back(t->stations_near);
		}

		std::vector<IndustryList> old_station_industries_nears;
		std::vector<BitmapTileArea> old_station_catchment_tiles;
		std::vector<uint> old_station_tiles;
		for (Station *st : Station::Iterate()) {
			old_station_industries_nears.push_back(st->industries_near);
			old_station_catchment_tiles.push_back(st->catchment_tiles);
			old_station_tiles.push_back(st->station_tiles);
		}

		std::vector<StationList> old_industry_stations_nears;
		for (Industry *ind : Industry::Iterate()) {
			old_industry_stations_nears.push_back(ind->stations_near);
		}

		RebuildTownCaches(false, false);
		RebuildSubsidisedSourceAndDestinationCache();

		Station::RecomputeCatchmentForAll();

		uint i = 0;
		for (Town *t : Town::Iterate()) {
			if (old_town_caches[i].num_houses != t->cache.num_houses) {
				CCLOG("town cache num_houses mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, old_town_caches[i].num_houses, t->cache.num_houses);
			}
			if (old_town_caches[i].population != t->cache.population) {
				CCLOG("town cache population mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, old_town_caches[i].population, t->cache.population);
			}
			if (old_town_caches[i].part_of_subsidy != t->cache.part_of_subsidy) {
				CCLOG("town cache population mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, old_town_caches[i].part_of_subsidy, t->cache.part_of_subsidy);
			}
			if (MemCmpT(old_town_caches[i].squared_town_zone_radius, t->cache.squared_town_zone_radius, lengthof(t->cache.squared_town_zone_radius)) != 0) {
				CCLOG("town cache squared_town_zone_radius mismatch: town %i", (int)t->index);
			}
			if (MemCmpT(&old_town_caches[i].building_counts, &t->cache.building_counts) != 0) {
				CCLOG("town cache building_counts mismatch: town %i", (int)t->index);
			}
			if (old_town_stations_nears[i] != t->stations_near) {
				CCLOG("town stations_near mismatch: town %i, (old size: %u, new size: %u)", (int)t->index, (uint)old_town_stations_nears[i].size(), (uint)t->stations_near.size());
			}
			i++;
		}
		i = 0;
		for (Station *st : Station::Iterate()) {
			if (old_station_industries_nears[i] != st->industries_near) {
				CCLOG("station industries_near mismatch: st %i, (old size: %u, new size: %u)", (int)st->index, (uint)old_station_industries_nears[i].size(), (uint)st->industries_near.size());
			}
			if (!(old_station_catchment_tiles[i] == st->catchment_tiles)) {
				CCLOG("station catchment_tiles mismatch: st %i", (int)st->index);
			}
			if (!(old_station_tiles[i] == st->station_tiles)) {
				CCLOG("station station_tiles mismatch: st %i, (old: %u, new: %u)", (int)st->index, old_station_tiles[i], st->station_tiles);
			}
			i++;
		}
		i = 0;
		for (Industry *ind : Industry::Iterate()) {
			if (old_industry_stations_nears[i] != ind->stations_near) {
				CCLOG("industry stations_near mismatch: ind %i, (old size: %u, new size: %u)", (int)ind->index, (uint)old_industry_stations_nears[i].size(), (uint)ind->stations_near.size());
			}
			StationList stlist;
			if (ind->neutral_station != nullptr && !_settings_game.station.serve_neutral_industries) {
				stlist.insert(ind->neutral_station);
				if (ind->stations_near != stlist) {
					CCLOG("industry neutral station stations_near mismatch: ind %i, (recalc size: %u, neutral size: %u)", (int)ind->index, (uint)ind->stations_near.size(), (uint)stlist.size());
				}
			} else {
				ForAllStationsAroundTiles(ind->location, [ind, &stlist](Station *st, TileIndex tile) {
					if (!IsTileType(tile, MP_INDUSTRY) || GetIndustryIndex(tile) != ind->index) return false;
					stlist.insert(st);
					return true;
				});
				if (ind->stations_near != stlist) {
					CCLOG("industry FindStationsAroundTiles mismatch: ind %i, (recalc size: %u, find size: %u)", (int)ind->index, (uint)ind->stations_near.size(), (uint)stlist.size());
				}
			}
			i++;
		}
	}

	if (flags & CHECK_CACHE_INFRA_TOTALS) {
		/* Check company infrastructure cache. */
		std::vector<CompanyInfrastructure> old_infrastructure;
		for (const Company *c : Company::Iterate()) old_infrastructure.push_back(c->infrastructure);

		AfterLoadCompanyStats();

		uint i = 0;
		for (const Company *c : Company::Iterate()) {
			if (MemCmpT(old_infrastructure.data() + i, &c->infrastructure) != 0) {
				CCLOG("infrastructure cache mismatch: company %i", (int)c->index);
				char buffer[4096];
				old_infrastructure[i].Dump(buffer, lastof(buffer));
				CCLOG("Previous:");
				ProcessLineByLine(buffer, [&](const char *line) {
					CCLOG("  %s", line);
				});
				c->infrastructure.Dump(buffer, lastof(buffer));
				CCLOG("Recalculated:");
				ProcessLineByLine(buffer, [&](const char *line) {
					CCLOG("  %s", line);
				});
				if (old_infrastructure[i].signal != c->infrastructure.signal && _network_server && !HasChickenBit(DCBF_DESYNC_CHECK_PERIODIC_SIGNALS)) {
					DoCommandP(0, 0, _settings_game.debug.chicken_bits | (1 << DCBF_DESYNC_CHECK_PERIODIC_SIGNALS), CMD_CHANGE_SETTING, nullptr, "debug.chicken_bits");
				}
			}
			i++;
		}
	}

	if (flags & CHECK_CACHE_GENERAL) {
		/* Strict checking of the road stop cache entries */
		for (const RoadStop *rs : RoadStop::Iterate()) {
			if (IsBayRoadStopTile(rs->xy)) continue;

			assert(rs->GetEntry(DIAGDIR_NE) != rs->GetEntry(DIAGDIR_NW));
			rs->GetEntry(DIAGDIR_NE)->CheckIntegrity(rs);
			rs->GetEntry(DIAGDIR_NW)->CheckIntegrity(rs);
		}

		for (Vehicle *v : Vehicle::Iterate()) {
			extern bool ValidateVehicleTileHash(const Vehicle *v);
			if (!ValidateVehicleTileHash(v)) {
				CCLOG("vehicle tile hash mismatch: type %i, vehicle %i, company %i, unit number %i", (int)v->type, v->index, (int)v->owner, v->unitnumber);
			}

			extern void FillNewGRFVehicleCache(const Vehicle *v);
			if (v != v->First() || v->vehstatus & VS_CRASHED || !v->IsPrimaryVehicle()) continue;

			uint length = 0;
			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				if (u->IsGroundVehicle() && (HasBit(u->GetGroundVehicleFlags(), GVF_GOINGUP_BIT) || HasBit(u->GetGroundVehicleFlags(), GVF_GOINGDOWN_BIT)) && u->GetGroundVehicleCache()->cached_slope_resistance && HasBit(v->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST)) {
					CCLOGV("VCF_GV_ZERO_SLOPE_RESIST set incorrectly (1)");
				}
				if (u->type == VEH_TRAIN && u->breakdown_ctr != 0 && !HasBit(Train::From(v)->flags, VRF_CONSIST_BREAKDOWN) && (Train::From(u)->IsEngine() || Train::From(u)->IsMultiheaded())) {
					CCLOGV("VRF_CONSIST_BREAKDOWN incorrectly not set");
				}
				if (u->type == VEH_TRAIN && ((Train::From(u)->track & TRACK_BIT_WORMHOLE && !(Train::From(u)->vehstatus & VS_HIDDEN)) || Train::From(u)->track == TRACK_BIT_DEPOT) && !HasBit(Train::From(v)->flags, VRF_CONSIST_SPEED_REDUCTION)) {
					CCLOGV("VRF_CONSIST_SPEED_REDUCTION incorrectly not set");
				}
				length++;
			}

			NewGRFCache        *grf_cache = CallocT<NewGRFCache>(length);
			VehicleCache       *veh_cache = CallocT<VehicleCache>(length);
			GroundVehicleCache *gro_cache = CallocT<GroundVehicleCache>(length);
			AircraftCache      *air_cache = CallocT<AircraftCache>(length);
			TrainCache         *tra_cache = CallocT<TrainCache>(length);
			Vehicle           **veh_old   = CallocT<Vehicle *>(length);

			length = 0;
			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				FillNewGRFVehicleCache(u);
				grf_cache[length] = u->grf_cache;
				veh_cache[length] = u->vcache;
				switch (u->type) {
					case VEH_TRAIN:
						gro_cache[length] = Train::From(u)->gcache;
						tra_cache[length] = Train::From(u)->tcache;
						veh_old[length] = CallocT<Train>(1);
						memcpy((void *) veh_old[length], (const void *) Train::From(u), sizeof(Train));
						break;
					case VEH_ROAD:
						gro_cache[length] = RoadVehicle::From(u)->gcache;
						veh_old[length] = CallocT<RoadVehicle>(1);
						memcpy((void *) veh_old[length], (const void *) RoadVehicle::From(u), sizeof(RoadVehicle));
						break;
					case VEH_AIRCRAFT:
						air_cache[length] = Aircraft::From(u)->acache;
						veh_old[length] = CallocT<Aircraft>(1);
						memcpy((void *) veh_old[length], (const void *) Aircraft::From(u), sizeof(Aircraft));
						break;
					default:
						veh_old[length] = CallocT<Vehicle>(1);
						memcpy((void *) veh_old[length], (const void *) u, sizeof(Vehicle));
						break;
				}
				length++;
			}

			switch (v->type) {
				case VEH_TRAIN:    Train::From(v)->ConsistChanged(CCF_TRACK); break;
				case VEH_ROAD:     RoadVehUpdateCache(RoadVehicle::From(v)); break;
				case VEH_AIRCRAFT: UpdateAircraftCache(Aircraft::From(v));   break;
				case VEH_SHIP:     Ship::From(v)->UpdateCache();             break;
				default: break;
			}

			length = 0;
			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				FillNewGRFVehicleCache(u);
				if (grf_cache[length] != u->grf_cache) {
					CCLOGV("newgrf cache mismatch");
				}
				if (veh_cache[length].cached_max_speed != u->vcache.cached_max_speed || veh_cache[length].cached_cargo_age_period != u->vcache.cached_cargo_age_period ||
						veh_cache[length].cached_vis_effect != u->vcache.cached_vis_effect || HasBit(veh_cache[length].cached_veh_flags ^ u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT)) {
					CCLOGV("vehicle cache mismatch: %c%c%c%c",
							veh_cache[length].cached_max_speed != u->vcache.cached_max_speed ? 'm' : '-',
							veh_cache[length].cached_cargo_age_period != u->vcache.cached_cargo_age_period ? 'c' : '-',
							veh_cache[length].cached_vis_effect != u->vcache.cached_vis_effect ? 'v' : '-',
							HasBit(veh_cache[length].cached_veh_flags ^ u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT) ? 'l' : '-');
				}
				if (u->IsGroundVehicle() && (HasBit(u->GetGroundVehicleFlags(), GVF_GOINGUP_BIT) || HasBit(u->GetGroundVehicleFlags(), GVF_GOINGDOWN_BIT)) && u->GetGroundVehicleCache()->cached_slope_resistance && HasBit(v->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST)) {
					CCLOGV("VCF_GV_ZERO_SLOPE_RESIST set incorrectly (2)");
				}
				if (veh_old[length]->acceleration != u->acceleration) {
					CCLOGV("acceleration mismatch");
				}
				if (veh_old[length]->breakdown_chance != u->breakdown_chance) {
					CCLOGV("breakdown_chance mismatch");
				}
				if (veh_old[length]->breakdown_ctr != u->breakdown_ctr) {
					CCLOGV("breakdown_ctr mismatch");
				}
				if (veh_old[length]->breakdown_delay != u->breakdown_delay) {
					CCLOGV("breakdown_delay mismatch");
				}
				if (veh_old[length]->breakdowns_since_last_service != u->breakdowns_since_last_service) {
					CCLOGV("breakdowns_since_last_service mismatch");
				}
				if (veh_old[length]->breakdown_severity != u->breakdown_severity) {
					CCLOGV("breakdown_severity mismatch");
				}
				if (veh_old[length]->breakdown_type != u->breakdown_type) {
					CCLOGV("breakdown_type mismatch");
				}
				if (veh_old[length]->vehicle_flags != u->vehicle_flags) {
					CCLOGV("vehicle_flags mismatch");
				}
				auto print_gv_cache_diff = [&](const char *vtype, const GroundVehicleCache &a, const GroundVehicleCache &b) {
					CCLOGV("%s ground vehicle cache mismatch: %c%c%c%c%c%c%c%c%c%c",
							vtype,
							a.cached_weight != b.cached_weight ? 'w' : '-',
							a.cached_slope_resistance != b.cached_slope_resistance ? 'r' : '-',
							a.cached_max_te != b.cached_max_te ? 't' : '-',
							a.cached_axle_resistance != b.cached_axle_resistance ? 'a' : '-',
							a.cached_max_track_speed != b.cached_max_track_speed ? 's' : '-',
							a.cached_power != b.cached_power ? 'p' : '-',
							a.cached_air_drag != b.cached_air_drag ? 'd' : '-',
							a.cached_total_length != b.cached_total_length ? 'l' : '-',
							a.first_engine != b.first_engine ? 'e' : '-',
							a.cached_veh_length != b.cached_veh_length ? 'L' : '-');
				};
				switch (u->type) {
					case VEH_TRAIN:
						if (memcmp(&gro_cache[length], &Train::From(u)->gcache, sizeof(GroundVehicleCache)) != 0) {
							print_gv_cache_diff("train", gro_cache[length], Train::From(u)->gcache);
						}
						if (memcmp(&tra_cache[length], &Train::From(u)->tcache, sizeof(TrainCache)) != 0) {
							CCLOGV("train cache mismatch: %c%c%c%c%c%c%c%c%c%c%c",
									tra_cache[length].cached_override != Train::From(u)->tcache.cached_override ? 'o' : '-',
									tra_cache[length].cached_curve_speed_mod != Train::From(u)->tcache.cached_curve_speed_mod ? 'C' : '-',
									tra_cache[length].cached_tflags != Train::From(u)->tcache.cached_tflags ? 'f' : '-',
									tra_cache[length].cached_num_engines != Train::From(u)->tcache.cached_num_engines ? 'e' : '-',
									tra_cache[length].cached_centre_mass != Train::From(u)->tcache.cached_centre_mass ? 'm' : '-',
									tra_cache[length].cached_braking_length != Train::From(u)->tcache.cached_braking_length ? 'b' : '-',
									tra_cache[length].cached_veh_weight != Train::From(u)->tcache.cached_veh_weight ? 'w' : '-',
									tra_cache[length].cached_uncapped_decel != Train::From(u)->tcache.cached_uncapped_decel ? 'D' : '-',
									tra_cache[length].cached_deceleration != Train::From(u)->tcache.cached_deceleration ? 'd' : '-',
									tra_cache[length].user_def_data != Train::From(u)->tcache.user_def_data ? 'u' : '-',
									tra_cache[length].cached_max_curve_speed != Train::From(u)->tcache.cached_max_curve_speed ? 'c' : '-');
						}
						if (Train::From(veh_old[length])->railtype != Train::From(u)->railtype) {
							CCLOGV("railtype mismatch");
						}
						if (Train::From(veh_old[length])->compatible_railtypes != Train::From(u)->compatible_railtypes) {
							CCLOGV("compatible_railtypes mismatch");
						}
						if (Train::From(veh_old[length])->flags != Train::From(u)->flags) {
							CCLOGV("train flags mismatch");
						}
						break;
					case VEH_ROAD:
						if (memcmp(&gro_cache[length], &RoadVehicle::From(u)->gcache, sizeof(GroundVehicleCache)) != 0) {
							print_gv_cache_diff("road vehicle", gro_cache[length], Train::From(u)->gcache);
						}
						break;
					case VEH_AIRCRAFT:
						if (memcmp(&air_cache[length], &Aircraft::From(u)->acache, sizeof(AircraftCache)) != 0) {
							CCLOGV("Aircraft vehicle cache mismatch: %c%c",
									air_cache[length].cached_max_range != Aircraft::From(u)->acache.cached_max_range ? 'r' : '-',
									air_cache[length].cached_max_range_sqr != Aircraft::From(u)->acache.cached_max_range_sqr ? 's' : '-');
						}
						break;
					default:
						break;
				}
				free(veh_old[length]);
				length++;
			}

			free(grf_cache);
			free(veh_cache);
			free(gro_cache);
			free(air_cache);
			free(tra_cache);
			free(veh_old);
		}

		/* Check whether the caches are still valid */
		for (Vehicle *v : Vehicle::Iterate()) {
			Money old_feeder_share = v->cargo.GetFeederShare();
			uint old_count = v->cargo.TotalCount();
			uint64_t old_cargo_periods_in_transit = v->cargo.CargoPeriodsInTransit();

			v->cargo.InvalidateCache();

			uint changed = 0;
			if (v->cargo.GetFeederShare() != old_feeder_share) SetBit(changed, 0);
			if (v->cargo.TotalCount() != old_count) SetBit(changed, 1);
			if (v->cargo.CargoPeriodsInTransit() != old_cargo_periods_in_transit) SetBit(changed, 2);
			if (changed != 0) {
				CCLOGV1("vehicle cargo cache mismatch: %c%c%c",
						HasBit(changed, 0) ? 'f' : '-',
						HasBit(changed, 1) ? 't' : '-',
						HasBit(changed, 2) ? 'p' : '-');
			}
		}

		for (Station *st : Station::Iterate()) {
			for (CargoID c = 0; c < NUM_CARGO; c++) {
				if (st->goods[c].data == nullptr) continue;

				uint old_count = st->goods[c].data->cargo.TotalCount();
				uint64_t old_cargo_periods_in_transit = st->goods[c].data->cargo.CargoPeriodsInTransit();

				st->goods[c].data->cargo.InvalidateCache();

				uint changed = 0;
				if (st->goods[c].data->cargo.TotalCount() != old_count) SetBit(changed, 0);
				if (st->goods[c].data->cargo.CargoPeriodsInTransit() != old_cargo_periods_in_transit) SetBit(changed, 1);
				if (changed != 0) {
					CCLOG("station cargo cache mismatch: station %i, company %i, cargo %u: %c%c",
							st->index, (int)st->owner, c,
							HasBit(changed, 0) ? 't' : '-',
							HasBit(changed, 1) ? 'd' : '-');
				}
			}

			/* Check docking tiles */
			TileArea ta;
			btree::btree_set<TileIndex> docking_tiles;
			for (TileIndex tile : st->docking_station) {
				ta.Add(tile);
				if (IsDockingTile(tile)) docking_tiles.insert(tile);
			}
			UpdateStationDockingTiles(st);
			if (ta.tile != st->docking_station.tile || ta.w != st->docking_station.w || ta.h != st->docking_station.h) {
				CCLOG("station docking mismatch: station %i, company %i, prev: (%X, %u, %u), recalc: (%X, %u, %u)",
						st->index, (int)st->owner, ta.tile, ta.w, ta.h, st->docking_station.tile, st->docking_station.w, st->docking_station.h);
			}
			for (TileIndex tile : ta) {
				if ((docking_tiles.find(tile) != docking_tiles.end()) != IsDockingTile(tile)) {
					CCLOG("docking tile mismatch: tile %i", (int)tile);
				}
			}
		}

#ifdef WITH_ASSERT
		for (OrderList *order_list : OrderList::Iterate()) {
			order_list->DebugCheckSanity();
		}
#endif

		extern void ValidateVehicleTickCaches();
		ValidateVehicleTickCaches();

		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->Previous()) assert_msg(v->Previous()->Next() == v, "%u", v->index);
			if (v->Next()) assert_msg(v->Next()->Previous() == v, "%u", v->index);
		}
		for (const TemplateVehicle *tv : TemplateVehicle::Iterate()) {
			if (tv->Prev()) assert_msg(tv->Prev()->Next() == tv, "%u", tv->index);
			if (tv->Next()) assert_msg(tv->Next()->Prev() == tv, "%u", tv->index);
		}

		{
			extern std::string ValidateTemplateReplacementCaches();
			std::string template_validation_result = ValidateTemplateReplacementCaches();
			if (!template_validation_result.empty()) {
				CCLOG("Template replacement cache validation failed: %s", template_validation_result.c_str());
			}
		}

		if (!TraceRestrictSlot::ValidateVehicleIndex()) CCLOG("Trace restrict slot vehicle index validation failed");
		TraceRestrictSlot::ValidateSlotOccupants(log);

		if (!CargoPacket::ValidateDeferredCargoPayments()) CCLOG("Cargo packets deferred payments validation failed");

		if (_order_destination_refcount_map_valid) {
			btree::btree_map<uint32_t, uint32_t> saved_order_destination_refcount_map = std::move(_order_destination_refcount_map);
			for (auto iter = saved_order_destination_refcount_map.begin(); iter != saved_order_destination_refcount_map.end();) {
				if (iter->second == 0) {
					iter = saved_order_destination_refcount_map.erase(iter);
				} else {
					++iter;
				}
			}
			IntialiseOrderDestinationRefcountMap();
			if (saved_order_destination_refcount_map != _order_destination_refcount_map) CCLOG("Order destination refcount map mismatch");
		} else {
			CCLOG("Order destination refcount map not valid");
		}
	}

	if (flags & CHECK_CACHE_WATER_REGIONS) {
		extern void WaterRegionCheckCaches(std::function<void(const char *)> log);
		WaterRegionCheckCaches(log);
	}

	if ((flags & CHECK_CACHE_EMIT_LOG) && !saved_messages.empty()) {
		InconsistencyExtraInfo info;
		info.check_caches_result = std::move(saved_messages);
		CrashLog::InconsistencyLog(info);
		for (std::string &str : info.check_caches_result) {
			LogDesyncMsg(std::move(str));
		}
	}

#undef CCLOG
#undef CCLOGV
#undef CCLOGV1
}

/**
 * Network-safe forced desync check.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDesyncCheck(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (flags & DC_EXEC) {
		CheckCaches(true, nullptr, CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG);
	}

	return CommandCost();
}

/**
 * State controlling game loop.
 * The state must not be changed from anywhere but here.
 * That check is enforced in DoCommand.
 */
void StateGameLoop()
{
	if (!_networking || _network_server) {
		StateGameLoop_LinkGraphPauseControl();
	}

	/* Don't execute the state loop during pause or when modal windows are open. */
	if (_pause_mode != PM_UNPAUSED || HasModalProgress()) {
		PerformanceMeasurer::Paused(PFE_GAMELOOP);
		PerformanceMeasurer::Paused(PFE_GL_ECONOMY);
		PerformanceMeasurer::Paused(PFE_GL_TRAINS);
		PerformanceMeasurer::Paused(PFE_GL_ROADVEHS);
		PerformanceMeasurer::Paused(PFE_GL_SHIPS);
		PerformanceMeasurer::Paused(PFE_GL_AIRCRAFT);
		PerformanceMeasurer::Paused(PFE_GL_LANDSCAPE);

		if (!HasModalProgress()) UpdateLandscapingLimits();
#ifndef DEBUG_DUMP_COMMANDS
		Game::GameLoop();
#endif
		return;
	}

	PerformanceMeasurer framerate(PFE_GAMELOOP);
	PerformanceAccumulator::Reset(PFE_GL_LANDSCAPE);

	Layouter::ReduceLineCache();

	if (_game_mode == GM_EDITOR) {
		BasePersistentStorageArray::SwitchMode(PSM_ENTER_GAMELOOP);

		/* _state_ticks and _state_ticks_offset must update in lockstep here,
		 * as _date, _tick_skip_counter, etc are not updated in the scenario editor,
		 * but _state_ticks should still update in case there are vehicles running,
		 * to avoid problems with timetables and train speed adaptation
		 */
		_state_ticks++;
		_state_ticks_offset++;

		RunTileLoop();
		CallVehicleTicks();
		CallLandscapeTick();
		TimerManager<TimerGameTick>::Elapsed(1);
		BasePersistentStorageArray::SwitchMode(PSM_LEAVE_GAMELOOP);
		UpdateLandscapingLimits();

		CallWindowGameTickEvent();
		NewsLoop();
	} else {
		if (_debug_desync_level > 2 && _tick_skip_counter == 0 && _date_fract == 0 && (_date.base() & 0x1F) == 0) {
			/* Save the desync savegame if needed. */
			char name[MAX_PATH];
			seprintf(name, lastof(name), "dmp_cmds_%08x_%08x.sav", _settings_game.game_creation.generation_seed, _date.base());
			SaveOrLoad(name, SLO_SAVE, DFT_GAME_FILE, AUTOSAVE_DIR, false);
		}

		CheckCaches(false, nullptr, CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG);

		/* All these actions has to be done from OWNER_NONE
		 *  for multiplayer compatibility */
		Backup<CompanyID> cur_company(_current_company, OWNER_NONE, FILE_LINE);

		BasePersistentStorageArray::SwitchMode(PSM_ENTER_GAMELOOP);
		_tick_skip_counter++;
		_scaled_tick_counter++;
		if (_game_mode != GM_MENU && _game_mode != GM_BOOTSTRAP) {
			_state_ticks++;   // This must update in lock-step with _tick_skip_counter, such that _state_ticks_offset doesn't need to be changed.
		}

		if (!(_game_mode == GM_MENU || _game_mode == GM_BOOTSTRAP) && !_settings_client.gui.autosave_realtime &&
				(_state_ticks.base() % (_settings_client.gui.autosave_interval * (_settings_game.economy.tick_rate == TRM_MODERN ? (60000 / 27) : (60000 / 30)))) == 0) {
			_do_autosave = true;
			_check_special_modes = true;
			SetWindowDirty(WC_STATUS_BAR, 0);
		}

		RunAuxiliaryTileLoop();
		if (_tick_skip_counter < _settings_game.economy.day_length_factor) {
			AnimateAnimatedTiles();
			RunTileLoop(true);
			CallVehicleTicks();
			OnTick_Companies(false);
		} else {
			_tick_skip_counter = 0;
			IncreaseDate();
			AnimateAnimatedTiles();
			RunTileLoop(true);
			CallVehicleTicks();
			CallLandscapeTick();
			OnTick_Companies(true);
		}
		TimerManager<TimerGameTick>::Elapsed(1);
		BasePersistentStorageArray::SwitchMode(PSM_LEAVE_GAMELOOP);

#ifndef DEBUG_DUMP_COMMANDS
		{
			PerformanceMeasurer script_framerate(PFE_ALLSCRIPTS);
			AI::GameLoop();
			Game::GameLoop();
		}
#endif
		UpdateLandscapingLimits();

		CallWindowGameTickEvent();
		NewsLoop();

		if (_networking) {
			RecordSyncEvent(NSRE_PRE_COMPANY_STATE);
			for (Company *c : Company::Iterate()) {
				DEBUG_UPDATESTATECHECKSUM("Company: %u, Money: " OTTD_PRINTF64, c->index, (int64_t)c->money);
				UpdateStateChecksum(c->money);

				for (uint i = 0; i < ROADTYPE_END; i++) {
					DEBUG_UPDATESTATECHECKSUM("Company: %u, road[%u]: %u", c->index, i, c->infrastructure.road[i]);
					UpdateStateChecksum(c->infrastructure.road[i]);
				}

				for (uint i = 0; i < RAILTYPE_END; i++) {
					DEBUG_UPDATESTATECHECKSUM("Company: %u, rail[%u]: %u", c->index, i, c->infrastructure.rail[i]);
					UpdateStateChecksum(c->infrastructure.rail[i]);
				}

				DEBUG_UPDATESTATECHECKSUM("Company: %u, signal: %u, water: %u, station: %u, airport: %u",
						c->index, c->infrastructure.signal, c->infrastructure.water, c->infrastructure.station, c->infrastructure.airport);
				UpdateStateChecksum(c->infrastructure.signal);
				UpdateStateChecksum(c->infrastructure.water);
				UpdateStateChecksum(c->infrastructure.station);
				UpdateStateChecksum(c->infrastructure.airport);
			}
		}
		cur_company.Restore();
	}
	if (_extra_aspects > 0) FlushDeferredAspectUpdates();

	if (_pause_countdown > 0 && --_pause_countdown == 0) {
		_pause_mode = PM_PAUSED_NORMAL;
		SetWindowDirty(WC_MAIN_TOOLBAR, 0);
	}

	dbg_assert(IsLocalCompany());
}

FiosNumberedSaveName &GetAutoSaveFiosNumberedSaveName()
{
	static FiosNumberedSaveName _autosave_ctr("autosave");
	return _autosave_ctr;
}

FiosNumberedSaveName &GetLongTermAutoSaveFiosNumberedSaveName()
{
	static FiosNumberedSaveName _autosave_lt_ctr("ltautosave");
	return _autosave_lt_ctr;
}

/**
 * Create an autosave. The default name is "autosave#.sav". However with
 * the setting 'keep_all_autosave' the name defaults to company-name + date
 */
static void DoAutosave()
{
	FiosNumberedSaveName *lt_counter = nullptr;
	if (_settings_client.gui.max_num_autosaves > 0) {
		lt_counter = &GetLongTermAutoSaveFiosNumberedSaveName();
	}
	DoAutoOrNetsave(GetAutoSaveFiosNumberedSaveName(), true, lt_counter);
}

/** Interval for regular autosaves. Initialized at zero to disable till settings are loaded. */
static IntervalTimer<TimerGameRealtime> _autosave_interval({std::chrono::milliseconds::zero(), TimerGameRealtime::AUTOSAVE}, [](auto)
{
	/* We reset the command-during-pause mode here, so we don't continue
	 * to make auto-saves when nothing more is changing. */
	_pause_mode &= ~PM_COMMAND_DURING_PAUSE;

	_do_autosave = true;
	DoAutosave();
	_do_autosave = false;
	SetWindowDirty(WC_STATUS_BAR, 0);
});

/**
 * Reset the interval of the autosave.
 *
 * If reset is not set, this does not set the elapsed time on the timer,
 * so if the interval is smaller, it might result in an autosave being done
 * immediately.
 *
 * @param reset Whether to reset the timer back to zero, or to continue.
 */
void ChangeAutosaveFrequency(bool reset)
{
	std::chrono::minutes interval = _settings_client.gui.autosave_realtime ? std::chrono::minutes(_settings_client.gui.autosave_interval) : std::chrono::minutes::zero();
	_autosave_interval.SetInterval({interval, TimerGameRealtime::AUTOSAVE}, reset);
}

/**
 * Request a new NewGRF scan. This will be executed on the next game-tick.
 * This is mostly needed to ensure NewGRF scans (which are blocking) are
 * done in the game-thread, and not in the draw-thread (which most often
 * triggers this request).
 * @param callback Optional callback to call when NewGRF scan is completed.
 * @return True when the NewGRF scan was actually requested, false when the scan was already running.
 */
bool RequestNewGRFScan(NewGRFScanCallback *callback)
{
	if (_request_newgrf_scan) return false;

	_request_newgrf_scan = true;
	_request_newgrf_scan_callback = callback;
	return true;
}

void GameLoopSpecial()
{
	/* autosave game? */
	if (_do_autosave) {
		DoAutosave();
		_do_autosave = false;
		SetWindowDirty(WC_STATUS_BAR, 0);
	}

	extern std::string _switch_baseset;
	if (!_switch_baseset.empty()) {
		if (BaseGraphics::GetUsedSet()->name != _switch_baseset) {
			BaseGraphics::SetSetByName(_switch_baseset);

			ReloadNewGRFData();
		}
		_switch_baseset.clear();
	}

	_check_special_modes = false;
}

void GameLoop()
{
	if (_game_mode == GM_BOOTSTRAP) {
		/* Check for UDP stuff */
		if (_network_available) NetworkBackgroundLoop();
		return;
	}

	if (_request_newgrf_scan) {
		ScanNewGRFFiles(_request_newgrf_scan_callback);
		_request_newgrf_scan = false;
		_request_newgrf_scan_callback = nullptr;
		/* In case someone closed the game during our scan, don't do anything else. */
		if (_exit_game) return;
	}

	ProcessAsyncSaveFinish();

	if (unlikely(_check_special_modes)) GameLoopSpecial();

	if (_game_mode == GM_NORMAL) {
		static auto last_time = std::chrono::steady_clock::now();
		auto now = std::chrono::steady_clock::now();
		auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time);
		if (delta_ms.count() != 0) {
			TimerManager<TimerGameRealtime>::Elapsed(delta_ms);
			last_time = now;
		}
	}

	/* switch game mode? */
	if (_switch_mode != SM_NONE && !HasModalProgress()) {
		SwitchToMode(_switch_mode);
		_switch_mode = SM_NONE;
		if (_exit_game) return;
	}

	IncreaseSpriteLRU();

	/* Check for UDP stuff */
	if (_network_available) NetworkBackgroundLoop();

	DebugSendRemoteMessages();

	if (_networking && !HasModalProgress()) {
		/* Multiplayer */
		NetworkGameLoop();
	} else {
		if (_network_reconnect > 0 && --_network_reconnect == 0) {
			/* This means that we want to reconnect to the last host
			 * We do this here, because it means that the network is really closed */
			NetworkClientConnectGame(_settings_client.network.last_joined, COMPANY_SPECTATOR);
		}
		/* Singleplayer */
		StateGameLoop();
	}
	ExecuteCommandQueue();

	if (!_pause_mode && HasBit(_display_opt, DO_FULL_ANIMATION)) {
		extern std::mutex _cur_palette_mutex;
		std::lock_guard<std::mutex> lock_state(_cur_palette_mutex);
		DoPaletteAnimations();
	}

	SoundDriver::GetInstance()->MainLoop();
	MusicLoop();
}
