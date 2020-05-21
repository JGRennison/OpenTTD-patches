/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog.cpp Implementation of generic function to be called to log a crash */

#include "stdafx.h"
#include "crashlog.h"
#include "crashlog_bfd.h"
#include "gamelog.h"
#include "date_func.h"
#include "map_func.h"
#include "rev.h"
#include "strings_func.h"
#include "blitter/factory.hpp"
#include "base_media_base.h"
#include "music/music_driver.hpp"
#include "sound/sound_driver.hpp"
#include "video/video_driver.hpp"
#include "saveload/saveload.h"
#include "screenshot.h"
#include "gfx_func.h"
#include "network/network.h"
#include "language.h"
#include "fontcache.h"
#include "news_gui.h"
#include "scope_info.h"
#include "command_func.h"
#include "thread.h"

#include "ai/ai_info.hpp"
#include "game/game.hpp"
#include "game/game_info.hpp"
#include "company_base.h"
#include "company_func.h"

#include <time.h>

#ifdef WITH_ALLEGRO
#	include <allegro.h>
#endif /* WITH_ALLEGRO */
#ifdef WITH_FONTCONFIG
#	include <fontconfig/fontconfig.h>
#endif /* WITH_FONTCONFIG */
#ifdef WITH_PNG
	/* pngconf.h, included by png.h doesn't like something in the
	 * freetype headers. As such it's not alphabetically sorted. */
#	include <png.h>
#endif /* WITH_PNG */
#ifdef WITH_FREETYPE
#	include <ft2build.h>
#	include FT_FREETYPE_H
#endif /* WITH_FREETYPE */
#if defined(WITH_ICU_LX) || defined(WITH_ICU_I18N)
#	include <unicode/uversion.h>
#endif /* WITH_ICU_LX || WITH_ICU_I18N */
#ifdef WITH_LIBLZMA
#	include <lzma.h>
#endif
#ifdef WITH_LZO
#include <lzo/lzo1x.h>
#endif
#if defined(WITH_SDL) || defined(WITH_SDL2)
#	include <SDL.h>
#endif /* WITH_SDL || WITH_SDL2 */
#ifdef WITH_ZLIB
# include <zlib.h>
#endif

#include "safeguards.h"

/* static */ const char *CrashLog::message = nullptr;
/* static */ char *CrashLog::gamelog_buffer = nullptr;
/* static */ const char *CrashLog::gamelog_last = nullptr;
/* static */ const CrashLog *CrashLog::main_thread_pending_crashlog = nullptr;

char *CrashLog::LogCompiler(char *buffer, const char *last) const
{
			buffer += seprintf(buffer, last, " Compiler: "
#if defined(_MSC_VER)
			"MSVC %d", _MSC_VER
#elif defined(__clang__)
			"clang %s", __clang_version__
#elif defined(__ICC) && defined(__GNUC__)
			"ICC %d (GCC %d.%d.%d mode)", __ICC,  __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__
#elif defined(__ICC)
			"ICC %d", __ICC
#elif defined(__GNUC__)
			"GCC %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__
#elif defined(__WATCOMC__)
			"WatcomC %d", __WATCOMC__
#else
			"<unknown>"
#endif
			);
#if defined(__VERSION__)
			return buffer + seprintf(buffer, last,  " \"" __VERSION__ "\"\n\n");
#else
			return buffer + seprintf(buffer, last,  "\n\n");
#endif
}

/* virtual */ char *CrashLog::LogOSVersionDetail(char *buffer, const char *last) const
{
	/* Stub implementation; not all OSes support this. */
	return buffer;
}

/* virtual */ char *CrashLog::LogRegisters(char *buffer, const char *last) const
{
	/* Stub implementation; not all OSes support this. */
	return buffer;
}

/* virtual */ char *CrashLog::LogModules(char *buffer, const char *last) const
{
	/* Stub implementation; not all OSes support this. */
	return buffer;
}

#ifdef USE_SCOPE_INFO
/* virtual */ char *CrashLog::LogScopeInfo(char *buffer, const char *last) const
{
	return buffer + WriteScopeLog(buffer, last);
}
#endif

/**
 * Writes OpenTTD's version to the buffer.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::LogOpenTTDVersion(char *buffer, const char *last) const
{
	return buffer + seprintf(buffer, last,
			"OpenTTD version:\n"
			" Version:    %s (%d)\n"
			" NewGRF ver: %08x\n"
			" Bits:       %d\n"
			" Endian:     %s\n"
			" Dedicated:  %s\n"
			" Build date: %s\n"
			" Configure:  %s\n"
			" Defines:    %s\n\n",
			_openttd_revision,
			_openttd_revision_modified,
			_openttd_newgrf_version,
#ifdef _SQ64
			64,
#else
			32,
#endif
#if (TTD_ENDIAN == TTD_LITTLE_ENDIAN)
			"little",
#else
			"big",
#endif
#ifdef DEDICATED
			"yes",
#else
			"no",
#endif
			_openttd_build_date,
			_openttd_build_configure,
			_openttd_build_configure_defines
	);
}

/**
 * Writes the (important) configuration settings to the buffer.
 * E.g. graphics set, sound set, blitter and AIs.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::LogConfiguration(char *buffer, const char *last) const
{
	auto pathfinder_name = [](uint8 pf) -> const char * {
		switch (pf) {
			case VPF_NPF: return "NPF";
			case VPF_YAPF: return "YAPF";
			default: return "-";
		};
	};
	buffer += seprintf(buffer, last,
			"Configuration:\n"
			" Blitter:      %s\n"
			" Graphics set: %s (%u)\n"
			" Language:     %s\n"
			" Music driver: %s\n"
			" Music set:    %s (%u)\n"
			" Network:      %s\n"
			" Sound driver: %s\n"
			" Sound set:    %s (%u)\n"
			" Video driver: %s\n"
			" Pathfinder:   %s %s %s\n\n",
			BlitterFactory::GetCurrentBlitter() == nullptr ? "none" : BlitterFactory::GetCurrentBlitter()->GetName(),
			BaseGraphics::GetUsedSet() == nullptr ? "none" : BaseGraphics::GetUsedSet()->name.c_str(),
			BaseGraphics::GetUsedSet() == nullptr ? UINT32_MAX : BaseGraphics::GetUsedSet()->version,
			_current_language == nullptr ? "none" : _current_language->file,
			MusicDriver::GetInstance() == nullptr ? "none" : MusicDriver::GetInstance()->GetName(),
			BaseMusic::GetUsedSet() == nullptr ? "none" : BaseMusic::GetUsedSet()->name.c_str(),
			BaseMusic::GetUsedSet() == nullptr ? UINT32_MAX : BaseMusic::GetUsedSet()->version,
			_networking ? (_network_server ? "server" : "client") : "no",
			SoundDriver::GetInstance() == nullptr ? "none" : SoundDriver::GetInstance()->GetName(),
			BaseSounds::GetUsedSet() == nullptr ? "none" : BaseSounds::GetUsedSet()->name.c_str(),
			BaseSounds::GetUsedSet() == nullptr ? UINT32_MAX : BaseSounds::GetUsedSet()->version,
			VideoDriver::GetInstance() == nullptr ? "none" : VideoDriver::GetInstance()->GetName(),
			pathfinder_name(_settings_game.pf.pathfinder_for_trains), pathfinder_name(_settings_game.pf.pathfinder_for_roadvehs), pathfinder_name(_settings_game.pf.pathfinder_for_ships)
	);

	buffer += seprintf(buffer, last,
			"Fonts:\n"
			" Small:  %s\n"
			" Medium: %s\n"
			" Large:  %s\n"
			" Mono:   %s\n\n",
			FontCache::Get(FS_SMALL)->GetFontName(),
			FontCache::Get(FS_NORMAL)->GetFontName(),
			FontCache::Get(FS_LARGE)->GetFontName(),
			FontCache::Get(FS_MONO)->GetFontName()
	);

	buffer += seprintf(buffer, last, "Map size: 0x%X (%u x %u)%s\n\n", MapSize(), MapSizeX(), MapSizeY(), (!_m || !_me) ? ", NO MAP ALLOCATED" : "");

	if (_settings_game.debug.chicken_bits != 0) {
		buffer += seprintf(buffer, last, "Chicken bits: 0x%08X\n\n", _settings_game.debug.chicken_bits);
	}

	buffer += seprintf(buffer, last, "AI Configuration (local: %i) (current: %i):\n", (int)_local_company, (int)_current_company);
	for (const Company *c : Company::Iterate()) {
		if (c->ai_info == nullptr) {
			buffer += seprintf(buffer, last, " %2i: Human\n", (int)c->index);
		} else {
			buffer += seprintf(buffer, last, " %2i: %s (v%d)\n", (int)c->index, c->ai_info->GetName(), c->ai_info->GetVersion());
		}
	}

	if (Game::GetInfo() != nullptr) {
		buffer += seprintf(buffer, last, " GS: %s (v%d)\n", Game::GetInfo()->GetName(), Game::GetInfo()->GetVersion());
	}
	buffer += seprintf(buffer, last, "\n");

	return buffer;
}

/**
 * Writes information (versions) of the used libraries.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::LogLibraries(char *buffer, const char *last) const
{
	buffer += seprintf(buffer, last, "Libraries:\n");

#ifdef WITH_ALLEGRO
	buffer += seprintf(buffer, last, " Allegro:    %s\n", allegro_id);
#endif /* WITH_ALLEGRO */

#ifdef WITH_FONTCONFIG
	int version = FcGetVersion();
	buffer += seprintf(buffer, last, " FontConfig: %d.%d.%d\n", version / 10000, (version / 100) % 100, version % 100);
#endif /* WITH_FONTCONFIG */

#ifdef WITH_FREETYPE
	FT_Library library;
	int major, minor, patch;
	FT_Init_FreeType(&library);
	FT_Library_Version(library, &major, &minor, &patch);
	FT_Done_FreeType(library);
	buffer += seprintf(buffer, last, " FreeType:   %d.%d.%d\n", major, minor, patch);
#endif /* WITH_FREETYPE */

#if defined(WITH_ICU_LX) || defined(WITH_ICU_I18N)
	/* 4 times 0-255, separated by dots (.) and a trailing '\0' */
	char buf[4 * 3 + 3 + 1];
	UVersionInfo ver;
	u_getVersion(ver);
	u_versionToString(ver, buf);
#ifdef WITH_ICU_I18N
	buffer += seprintf(buffer, last, " ICU i18n:   %s\n", buf);
#endif
#ifdef WITH_ICU_LX
	buffer += seprintf(buffer, last, " ICU lx:     %s\n", buf);
#endif
#endif /* WITH_ICU_LX || WITH_ICU_I18N */

#ifdef WITH_LIBLZMA
	buffer += seprintf(buffer, last, " LZMA:       %s\n", lzma_version_string());
#endif

#ifdef WITH_LZO
	buffer += seprintf(buffer, last, " LZO:        %s\n", lzo_version_string());
#endif

#ifdef WITH_PNG
	buffer += seprintf(buffer, last, " PNG:        %s\n", png_get_libpng_ver(nullptr));
#endif /* WITH_PNG */

#ifdef WITH_SDL
	const SDL_version *sdl_v = SDL_Linked_Version();
	buffer += seprintf(buffer, last, " SDL1:       %d.%d.%d\n", sdl_v->major, sdl_v->minor, sdl_v->patch);
#elif defined(WITH_SDL2)
	SDL_version sdl2_v;
	SDL_GetVersion(&sdl2_v);
	buffer += seprintf(buffer, last, " SDL2:       %d.%d.%d", sdl2_v.major, sdl2_v.minor, sdl2_v.patch);
#if defined(SDL_USE_IME)
	buffer += seprintf(buffer, last, " IME?");
#endif
#if defined(HAVE_FCITX_FRONTEND_H)
	buffer += seprintf(buffer, last, " FCITX?");
#endif
#if defined(HAVE_IBUS_IBUS_H)
	buffer += seprintf(buffer, last, " IBUS?");
#endif
#if !(defined(_WIN32) || defined(__APPLE__))
	const char *sdl_im_module = getenv("SDL_IM_MODULE");
	if (sdl_im_module != nullptr) buffer += seprintf(buffer, last, " (SDL_IM_MODULE=%s)", sdl_im_module);
	const char *xmod = getenv("XMODIFIERS");
	if (xmod != nullptr && strstr(xmod, "@im=fcitx") != nullptr) buffer += seprintf(buffer, last, " (XMODIFIERS has @im=fcitx)");
#endif
	buffer += seprintf(buffer, last, "\n");
#endif

#ifdef WITH_ZLIB
	buffer += seprintf(buffer, last, " Zlib:       %s\n", zlibVersion());
#endif

	buffer += seprintf(buffer, last, "\n");
	return buffer;
}

/**
 * Helper function for printing the gamelog.
 * @param s the string to print.
 */
/* static */ void CrashLog::GamelogFillCrashLog(const char *s)
{
	CrashLog::gamelog_buffer += seprintf(CrashLog::gamelog_buffer, CrashLog::gamelog_last, "%s\n", s);
}

/**
 * Writes the gamelog data to the buffer.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::LogGamelog(char *buffer, const char *last) const
{
	if (_game_events_since_load || _game_events_overall) {
		buffer += seprintf(buffer, last, "Events: ");
		buffer = DumpGameEventFlags(_game_events_since_load, buffer, last);
		buffer += seprintf(buffer, last, ", ");
		buffer = DumpGameEventFlags(_game_events_overall, buffer, last);
		buffer += seprintf(buffer, last, "\n\n");
	}

	CrashLog::gamelog_buffer = buffer;
	CrashLog::gamelog_last = last;
	GamelogPrint(&CrashLog::GamelogFillCrashLog);
	return CrashLog::gamelog_buffer + seprintf(CrashLog::gamelog_buffer, last, "\n");
}

/**
 * Writes up to 32 recent news messages to the buffer, with the most recent first.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::LogRecentNews(char *buffer, const char *last) const
{
	uint total = 0;
	for (NewsItem *news = _latest_news; news != nullptr; news = news->prev) {
		total++;
	}
	uint show = min<uint>(total, 32);
	buffer += seprintf(buffer, last, "Recent news messages (%u of %u):\n", show, total);

	int i = 0;
	for (NewsItem *news = _latest_news; i < 32 && news != nullptr; news = news->prev, i++) {
		YearMonthDay ymd;
		ConvertDateToYMD(news->date, &ymd);
		buffer += seprintf(buffer, last, "(%i-%02i-%02i) StringID: %u, Type: %u, Ref1: %u, %u, Ref2: %u, %u\n",
		                   ymd.year, ymd.month + 1, ymd.day, news->string_id, news->type,
		                   news->reftype1, news->ref1, news->reftype2, news->ref2);
	}
	buffer += seprintf(buffer, last, "\n");
	return buffer;
}

/**
 * Writes the command log data to the buffer.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::LogCommandLog(char *buffer, const char *last) const
{
	buffer = DumpCommandLog(buffer, last);
	buffer += seprintf(buffer, last, "\n");
	return buffer;
}

/**
 * Fill the crash log buffer with all data of a crash log.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::FillCrashLog(char *buffer, const char *last) const
{
	time_t cur_time = time(nullptr);
	buffer += seprintf(buffer, last, "*** OpenTTD Crash Report ***\n\n");

	if (GamelogTestEmergency()) {
		buffer += seprintf(buffer, last, "-=-=- As you loaded an emergency savegame no crash information would ordinarily be generated. -=-=-\n\n");
	}
	if (SaveloadCrashWithMissingNewGRFs()) {
		buffer += seprintf(buffer, last, "-=-=- As you loaded a savegame for which you do not have the required NewGRFs no crash information would ordinarily be generated. -=-=-\n\n");
	}

	buffer += seprintf(buffer, last, "Crash at: %s", asctime(gmtime(&cur_time)));

	YearMonthDay ymd;
	ConvertDateToYMD(_date, &ymd);
	buffer += seprintf(buffer, last, "In game date: %i-%02i-%02i (%i, %i) (DL: %u)\n", _cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day, _date_fract, _tick_skip_counter, _settings_game.economy.day_length_factor);
	if (_game_load_time != 0) {
		buffer += seprintf(buffer, last, "Game loaded at: %i-%02i-%02i (%i, %i), %s",
				_game_load_cur_date_ymd.year, _game_load_cur_date_ymd.month + 1, _game_load_cur_date_ymd.day, _game_load_date_fract, _game_load_tick_skip_counter, asctime(gmtime(&_game_load_time)));
	}
	buffer += seprintf(buffer, last, "\n");

	buffer = this->LogError(buffer, last, CrashLog::message);

#ifdef USE_SCOPE_INFO
	if (IsMainThread()) {
		buffer += WriteScopeLog(buffer, last);
	}
#endif

	if (IsNonMainThread()) {
		buffer += seprintf(buffer, last, "Non-main thread (");
		buffer += GetCurrentThreadName(buffer, last);
		buffer += seprintf(buffer, last, ")\n\n");
	}

	buffer = this->LogOpenTTDVersion(buffer, last);
	buffer = this->LogStacktrace(buffer, last);
	buffer = this->LogRegisters(buffer, last);
	buffer = this->LogOSVersion(buffer, last);
	buffer = this->LogCompiler(buffer, last);
	buffer = this->LogOSVersionDetail(buffer, last);
	buffer = this->LogConfiguration(buffer, last);
	buffer = this->LogLibraries(buffer, last);
	buffer = this->LogModules(buffer, last);
	buffer = this->LogGamelog(buffer, last);
	buffer = this->LogRecentNews(buffer, last);
	buffer = this->LogCommandLog(buffer, last);

	buffer += seprintf(buffer, last, "*** End of OpenTTD Crash Report ***\n");
	return buffer;
}

/**
 * Fill the crash log buffer with all data of a desync event.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::FillDesyncCrashLog(char *buffer, const char *last, const DesyncExtraInfo &info) const
{
	time_t cur_time = time(nullptr);
	buffer += seprintf(buffer, last, "*** OpenTTD Multiplayer %s Desync Report ***\n\n", _network_server ? "Server" : "Client");

	buffer += seprintf(buffer, last, "Desync at: %s", asctime(gmtime(&cur_time)));
	if (!_network_server && info.flags) {
		auto flag_check = [&](DesyncExtraInfo::Flags flag, const char *str) {
			return info.flags & flag ? str : "";
		};
		buffer += seprintf(buffer, last, "Flags: %s%s%s%s\n",
				flag_check(DesyncExtraInfo::DEIF_RAND1, "R"),
				flag_check(DesyncExtraInfo::DEIF_RAND2, "Z"),
				flag_check(DesyncExtraInfo::DEIF_STATE, "S"),
				flag_check(DesyncExtraInfo::DEIF_DBL_RAND, "D"));
	}

	YearMonthDay ymd;
	ConvertDateToYMD(_date, &ymd);
	buffer += seprintf(buffer, last, "In game date: %i-%02i-%02i (%i, %i) (DL: %u)\n", _cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day, _date_fract, _tick_skip_counter, _settings_game.economy.day_length_factor);
	if (_game_load_time != 0) {
		buffer += seprintf(buffer, last, "Game loaded at: %i-%02i-%02i (%i, %i), %s",
				_game_load_cur_date_ymd.year, _game_load_cur_date_ymd.month + 1, _game_load_cur_date_ymd.day, _game_load_date_fract, _game_load_tick_skip_counter, asctime(gmtime(&_game_load_time)));
	}
	buffer += seprintf(buffer, last, "\n");

	buffer = this->LogOpenTTDVersion(buffer, last);
	buffer = this->LogOSVersion(buffer, last);
	buffer = this->LogCompiler(buffer, last);
	buffer = this->LogOSVersionDetail(buffer, last);
	buffer = this->LogConfiguration(buffer, last);
	buffer = this->LogLibraries(buffer, last);
	buffer = this->LogGamelog(buffer, last);
	buffer = this->LogRecentNews(buffer, last);
	buffer = this->LogCommandLog(buffer, last);
	buffer = DumpDesyncMsgLog(buffer, last);

	bool have_cache_log = false;
	extern void CheckCaches(bool force_check, std::function<void(const char *)> log);
	CheckCaches(true, [&](const char *str) {
		if (!have_cache_log) buffer += seprintf(buffer, last, "CheckCaches:\n");
		buffer += seprintf(buffer, last, "  %s\n", str);
		have_cache_log = true;
		LogDesyncMsg(stdstr_fmt("[prev desync]: %s", str));
	});
	if (have_cache_log) buffer += seprintf(buffer, last, "\n");

	buffer += seprintf(buffer, last, "*** End of OpenTTD Multiplayer %s Desync Report ***\n", _network_server ? "Server" : "Client");
	return buffer;
}

/**
 * Fill the version info log buffer.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::FillVersionInfoLog(char *buffer, const char *last) const
{
	buffer += seprintf(buffer, last, "*** OpenTTD Version Info Report ***\n\n");

	buffer = this->LogOpenTTDVersion(buffer, last);
	buffer = this->LogOSVersion(buffer, last);
	buffer = this->LogCompiler(buffer, last);
	buffer = this->LogOSVersionDetail(buffer, last);
	buffer = this->LogLibraries(buffer, last);

	buffer += seprintf(buffer, last, "*** End of OpenTTD Version Info Report ***\n");
	return buffer;
}

/**
 * Write the crash log to a file.
 * @note On success the filename will be filled with the full path of the
 *       crash log file. Make sure filename is at least \c MAX_PATH big.
 * @param buffer The begin of the buffer to write to the disk.
 * @param filename      Output for the filename of the written file.
 * @param filename_last The last position in the filename buffer.
 * @return true when the crash log was successfully written.
 */
bool CrashLog::WriteCrashLog(const char *buffer, char *filename, const char *filename_last, const char *name, FILE **crashlog_file) const
{
	seprintf(filename, filename_last, "%s%s.log", _personal_dir, name);

	FILE *file = FioFOpenFile(filename, "w", NO_DIRECTORY);
	if (file == nullptr) return false;

	size_t len = strlen(buffer);
	size_t written = fwrite(buffer, 1, len, file);

	if (crashlog_file) {
		*crashlog_file = file;
	} else {
		FioFCloseFile(file);
	}
	return len == written;
}

/* virtual */ int CrashLog::WriteCrashDump(char *filename, const char *filename_last) const
{
	/* Stub implementation; not all OSes support this. */
	return 0;
}

/**
 * Write the (crash) savegame to a file.
 * @note On success the filename will be filled with the full path of the
 *       crash save file. Make sure filename is at least \c MAX_PATH big.
 * @param filename      Output for the filename of the written file.
 * @param filename_last The last position in the filename buffer.
 * @return true when the crash save was successfully made.
 */
bool CrashLog::WriteSavegame(char *filename, const char *filename_last, const char *name) const
{
	/* If the map array doesn't exist, saving will fail too. If the map got
	 * initialised, there is a big chance the rest is initialised too. */
	if (_m == nullptr) return false;

	try {
		GamelogEmergency();

		seprintf(filename, filename_last, "%s%s.sav", _personal_dir, name);

		/* Don't do a threaded saveload. */
		return SaveOrLoad(filename, SLO_SAVE, DFT_GAME_FILE, NO_DIRECTORY, false) == SL_OK;
	} catch (...) {
		return false;
	}
}

/**
 * Write the (crash) screenshot to a file.
 * @note On success the filename will be filled with the full path of the
 *       screenshot. Make sure filename is at least \c MAX_PATH big.
 * @param filename      Output for the filename of the written file.
 * @param filename_last The last position in the filename buffer.
 * @return true when the crash screenshot was successfully made.
 */
bool CrashLog::WriteScreenshot(char *filename, const char *filename_last, const char *name) const
{
	/* Don't draw when we have invalid screen size */
	if (_screen.width < 1 || _screen.height < 1 || _screen.dst_ptr == nullptr) return false;

	bool res = MakeScreenshot(SC_CRASHLOG, name);
	if (res) strecpy(filename, _full_screenshot_name, filename_last);
	return res;
}

/**
 * Makes the crash log, writes it to a file and then subsequently tries
 * to make a crash dump and crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
bool CrashLog::MakeCrashLog()
{
	/* Don't keep looping logging crashes. */
	static bool crashlogged = false;
	if (crashlogged) return false;
	crashlogged = true;

	char filename[MAX_PATH];
	char buffer[65536 * 4];
	bool ret = true;

	char *name_buffer_date = this->name_buffer + seprintf(this->name_buffer, lastof(this->name_buffer), "crash-");
	time_t cur_time = time(nullptr);
	strftime(name_buffer_date, lastof(this->name_buffer) - name_buffer_date, "%Y%m%dT%H%M%SZ", gmtime(&cur_time));

	printf("Crash encountered, generating crash log...\n");
	this->FillCrashLog(buffer, lastof(buffer));
	printf("%s\n", buffer);
	printf("Crash log generated.\n\n");

	printf("Writing crash log to disk...\n");
	bool bret = this->WriteCrashLog(buffer, filename, lastof(filename), this->name_buffer);
	if (bret) {
		printf("Crash log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing crash log failed. Please attach the output above to any bug reports.\n\n");
		ret = false;
	}

	/* Don't mention writing crash dumps because not all platforms support it. */
	int dret = this->WriteCrashDump(filename, lastof(filename));
	if (dret < 0) {
		printf("Writing crash dump failed.\n\n");
		ret = false;
	} else if (dret > 0) {
		printf("Crash dump written to %s. Please add this file to any bug reports.\n\n", filename);
	}

	SetScreenshotAuxiliaryText("Crash Log", buffer);
	_savegame_DBGL_data = buffer;
	_save_DBGC_data = true;

	if (IsNonMainThread()) {
		printf("Asking main thread to write crash savegame and screenshot...\n\n");
		CrashLog::main_thread_pending_crashlog = this;
		_exit_game = true;
		CSleep(60000);
		if (!CrashLog::main_thread_pending_crashlog) return ret;
		printf("Main thread did not write crash savegame and screenshot within 60s, trying it from this thread...\n\n");
	}
	CrashLog::main_thread_pending_crashlog = nullptr;
	bret = CrashLog::MakeCrashSavegameAndScreenshot();
	if (!bret) ret = false;

	return ret;
}

/**
 * Makes a desync crash log, writes it to a file and then subsequently tries
 * to make a crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
bool CrashLog::MakeDesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info) const
{
	char filename[MAX_PATH];
	char buffer[65536 * 2];
	bool ret = true;

	const char *mode = _network_server ? "server" : "client";

	char name_buffer[64];
	char *name_buffer_date = name_buffer + seprintf(name_buffer, lastof(name_buffer), "desync-%s-", mode);
	time_t cur_time = time(nullptr);
	strftime(name_buffer_date, lastof(name_buffer) - name_buffer_date, "%Y%m%dT%H%M%SZ", gmtime(&cur_time));

	printf("Desync encountered (%s), generating desync log...\n", mode);
	char *b = this->FillDesyncCrashLog(buffer, lastof(buffer), info);

	if (log_out) log_out->assign(buffer);

	if (log_in && !log_in->empty()) {
		b = strecpy(b, "\n", lastof(buffer), true);
		b = strecpy(b, log_in->c_str(), lastof(buffer), true);
	}

	bool bret = this->WriteCrashLog(buffer, filename, lastof(filename), name_buffer, info.log_file);
	if (bret) {
		printf("Desync log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing desync log failed.\n\n");
		ret = false;
	}

	_savegame_DBGL_data = buffer;
	_save_DBGC_data = true;
	bret = this->WriteSavegame(filename, lastof(filename), name_buffer);
	if (bret) {
		printf("Desync savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", filename);
	} else {
		ret = false;
		printf("Writing desync savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
	}
	_savegame_DBGL_data = nullptr;
	_save_DBGC_data = false;

	if (!(_screen.width < 1 || _screen.height < 1 || _screen.dst_ptr == nullptr)) {
		SetScreenshotAuxiliaryText("Desync Log", buffer);
		bret = this->WriteScreenshot(filename, lastof(filename), name_buffer);
		if (bret) {
			printf("Desync screenshot written to %s. Please add this file to any bug reports.\n\n", filename);
		} else {
			ret = false;
			printf("Writing desync screenshot failed.\n\n");
		}
		ClearScreenshotAuxiliaryText();
	}

	return ret;
}

/**
 * Makes a version info log, writes it to a file. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
bool CrashLog::MakeVersionInfoLog() const
{
	char buffer[65536];
	this->FillVersionInfoLog(buffer, lastof(buffer));
	printf("%s\n", buffer);
	return true;
}

/**
 * Makes a crash dump and crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
bool CrashLog::MakeCrashSavegameAndScreenshot() const
{
	char filename[MAX_PATH];
	bool ret = true;

	printf("Writing crash savegame...\n");
	bool bret = this->WriteSavegame(filename, lastof(filename), this->name_buffer);
	if (bret) {
		printf("Crash savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", filename);
	} else {
		ret = false;
		printf("Writing crash savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
	}

	printf("Writing crash screenshot...\n");
	bret = this->WriteScreenshot(filename, lastof(filename), this->name_buffer);
	if (bret) {
		printf("Crash screenshot written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		ret = false;
		printf("Writing crash screenshot failed.\n\n");
	}

	return ret;
}

/* static */ void CrashLog::MainThreadExitCheckPendingCrashlog()
{
	const CrashLog *cl = CrashLog::main_thread_pending_crashlog;
	if (cl) {
		CrashLog::main_thread_pending_crashlog = nullptr;
		cl->MakeCrashSavegameAndScreenshot();

		CrashLog::AfterCrashLogCleanup();
		abort();
	}
}

/**
 * Sets a message for the error message handler.
 * @param message The error message of the error.
 */
/* static */ void CrashLog::SetErrorMessage(const char *message)
{
	CrashLog::message = message;
}

/**
 * Try to close the sound/video stuff so it doesn't keep lingering around
 * incorrect video states or so, e.g. keeping dpmi disabled.
 */
/* static */ void CrashLog::AfterCrashLogCleanup()
{
	if (MusicDriver::GetInstance() != nullptr) MusicDriver::GetInstance()->Stop();
	if (SoundDriver::GetInstance() != nullptr) SoundDriver::GetInstance()->Stop();
	if (VideoDriver::GetInstance() != nullptr) VideoDriver::GetInstance()->Stop();
}

/* static */ const char *CrashLog::GetAbortCrashlogReason()
{
	if (_settings_client.gui.developer > 0) return nullptr;

	if (GamelogTestEmergency()) {
		return "As you loaded an emergency savegame no crash information will be generated.\n";
	}

	if (SaveloadCrashWithMissingNewGRFs()) {
		return "As you loaded an savegame for which you do not have the required NewGRFs\n" \
				"no crash information will be generated.\n";
	}

	return nullptr;
}

#if defined(WITH_BFD)
sym_info_bfd::sym_info_bfd(bfd_vma addr_) : addr(addr_), abfd(nullptr), syms(nullptr), sym_count(0),
		file_name(nullptr), function_name(nullptr), function_addr(0), line(0), found(false) {}

sym_info_bfd::~sym_info_bfd()
{
	free(syms);
	if (abfd != nullptr) bfd_close(abfd);
}

static void find_address_in_section(bfd *abfd, asection *section, void *data)
{
	sym_info_bfd *info = static_cast<sym_info_bfd *>(data);
	if (info->found) return;

	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0) return;

	bfd_vma vma = bfd_get_section_vma(abfd, section);
	if (info->addr < vma) return;

	bfd_size_type size = bfd_section_size(abfd, section);
	if (info->addr >= vma + size) return;

	info->found = bfd_find_nearest_line(abfd, section, info->syms, info->addr - vma,
			&(info->file_name), &(info->function_name), &(info->line));

	if (info->found && info->function_name) {
		for (long i = 0; i < info->sym_count; i++) {
			asymbol *sym = info->syms[i];
			if (sym->flags & (BSF_LOCAL | BSF_GLOBAL) && strcmp(sym->name, info->function_name) == 0) {
				info->function_addr = sym->value + vma;
			}
		}
	} else if (info->found) {
		bfd_vma target = info->addr - vma;
		bfd_vma best_diff = size;
		for (long i = 0; i < info->sym_count; i++) {
			asymbol *sym = info->syms[i];
			if (!(sym->flags & (BSF_LOCAL | BSF_GLOBAL))) continue;
			if (sym->value > target) continue;
			bfd_vma diff = target - sym->value;
			if (diff < best_diff) {
				best_diff = diff;
				info->function_name = sym->name;
				info->function_addr = sym->value + vma;
			}
		}
	}
}

void lookup_addr_bfd(const char *obj_file_name, sym_info_bfd &info)
{
	info.abfd = bfd_openr(obj_file_name, nullptr);

	if (info.abfd == nullptr) return;

	if (!bfd_check_format(info.abfd, bfd_object) || (bfd_get_file_flags(info.abfd) & HAS_SYMS) == 0) return;

	unsigned int size;
	info.sym_count = bfd_read_minisymbols(info.abfd, false, (void**) &(info.syms), &size);
	if (info.sym_count <= 0) {
		info.sym_count = bfd_read_minisymbols(info.abfd, true, (void**) &(info.syms), &size);
	}
	if (info.sym_count <= 0) return;

	bfd_map_over_sections(info.abfd, find_address_in_section, &info);
}
#endif
