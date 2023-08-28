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
#include "sl/saveload.h"
#include "screenshot.h"
#include "gfx_func.h"
#include "network/network.h"
#include "network/network_survey.h"
#include "network/network_sync.h"
#include "language.h"
#include "fontcache.h"
#include "news_gui.h"
#include "scope_info.h"
#include "command_func.h"
#include "command_log.h"
#include "thread.h"
#include "debug_desync.h"
#include "event_logs.h"
#include "scope.h"
#include "progress.h"

#include "ai/ai_info.hpp"
#include "game/game.hpp"
#include "game/game_info.hpp"
#include "company_base.h"
#include "company_func.h"
#include "walltime_func.h"

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
#ifdef WITH_HARFBUZZ
#	include <hb.h>
#endif /* WITH_HARFBUZZ */
#ifdef WITH_ICU_I18N
#	include <unicode/uversion.h>
#endif /* WITH_ICU_I18N */
#ifdef WITH_LIBLZMA
#	include <lzma.h>
#endif
#ifdef WITH_ZSTD
#include <zstd.h>
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
#ifdef WITH_CURL
# include <curl/curl.h>
#endif

#include "safeguards.h"

/* static */ const char *CrashLog::message = nullptr;
/* static */ char *CrashLog::gamelog_buffer = nullptr;
/* static */ const char *CrashLog::gamelog_last = nullptr;
/* static */ bool CrashLog::have_crashed = false;

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

/* virtual */ char *CrashLog::LogDebugExtra(char *buffer, const char *last) const
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

/* virtual */ void CrashLog::StartCrashLogFaultHandler()
{
	/* Stub implementation; not all OSes support this. */
}

/* virtual */ void CrashLog::StopCrashLogFaultHandler()
{
	/* Stub implementation; not all OSes support this. */
}

/* virtual */ char *CrashLog::TryCrashLogFaultSection(char *buffer, const char *last, const char *section_name, CrashLogSectionWriter writer)
{
	/* Stub implementation; not all OSes support internal fault handling. */
	this->FlushCrashLogBuffer();
	return writer(this, buffer, last);
}

/* virtual */ void CrashLog::CrashLogFaultSectionCheckpoint(char *buffer) const
{
	/* Stub implementation; not all OSes support this. */
	const_cast<CrashLog *>(this)->FlushCrashLogBuffer();
}

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
			" Version:     %s (%d)\n"
			" Release ver: %s\n"
			" NewGRF ver:  %08x\n"
			" Bits:        %d\n"
			" Endian:      %s\n"
			" Dedicated:   %s\n"
			" Build date:  %s\n"
			" Defines:     %s\n\n",
			_openttd_revision,
			_openttd_revision_modified,
			_openttd_release_version,
			_openttd_newgrf_version,
#ifdef POINTER_IS_64BIT
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
	auto mode_name = []() -> const char * {
		switch (_game_mode) {
			case GM_MENU: return "MENU";
			case GM_NORMAL: return "NORMAL";
			case GM_EDITOR: return "EDITOR";
			case GM_BOOTSTRAP:  return "BOOTSTRAP";
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
			" Pathfinder:   %s %s %s\n",
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
			VideoDriver::GetInstance() == nullptr ? "none" : VideoDriver::GetInstance()->GetInfoString(),
			pathfinder_name(_settings_game.pf.pathfinder_for_trains), pathfinder_name(_settings_game.pf.pathfinder_for_roadvehs), pathfinder_name(_settings_game.pf.pathfinder_for_ships)
	);
	buffer += seprintf(buffer, last, " Game mode:    %s", mode_name());
	if (_switch_mode != SM_NONE) buffer += seprintf(buffer, last, ", SM: %u", _switch_mode);
	if (HasModalProgress()) buffer += seprintf(buffer, last, ", HMP");
	buffer += seprintf(buffer, last, "\n\n");

	this->CrashLogFaultSectionCheckpoint(buffer);

	auto log_font = [&](FontSize fs) -> std::string {
		FontCache *fc = FontCache::Get(fs);
		if (fc != nullptr) {
			return fc->GetFontName();
		} else {
			return "[NULL]";
		}
	};

	buffer += seprintf(buffer, last,
			"Fonts:\n"
			" Small:  %s\n"
			" Medium: %s\n"
			" Large:  %s\n"
			" Mono:   %s\n\n",
			log_font(FS_SMALL).c_str(),
			log_font(FS_NORMAL).c_str(),
			log_font(FS_LARGE).c_str(),
			log_font(FS_MONO).c_str()
	);

	this->CrashLogFaultSectionCheckpoint(buffer);

	buffer += seprintf(buffer, last, "Map size: 0x%X (%u x %u)%s\n\n", MapSize(), MapSizeX(), MapSizeY(), (!_m || !_me) ? ", NO MAP ALLOCATED" : "");

	if (_settings_game.debug.chicken_bits != 0) {
		buffer += seprintf(buffer, last, "Chicken bits: 0x%08X\n\n", _settings_game.debug.chicken_bits);
	}
	if (_settings_game.debug.newgrf_optimiser_flags != 0) {
		buffer += seprintf(buffer, last, "NewGRF optimiser flags: 0x%08X\n\n", _settings_game.debug.newgrf_optimiser_flags);
	}

	this->CrashLogFaultSectionCheckpoint(buffer);

	buffer += seprintf(buffer, last, "AI Configuration (local: %i) (current: %i):\n", (int)_local_company, (int)_current_company);
	for (const Company *c : Company::Iterate()) {
		if (c->ai_info == nullptr) {
			buffer += seprintf(buffer, last, " %2i: Human\n", (int)c->index);
		} else {
			buffer += seprintf(buffer, last, " %2i: %s (v%d)\n", (int)c->index, c->ai_info->GetName().c_str(), c->ai_info->GetVersion());
		}
	}

	if (Game::GetInfo() != nullptr) {
		buffer += seprintf(buffer, last, " GS: %s (v%d)\n", Game::GetInfo()->GetName().c_str(), Game::GetInfo()->GetVersion());
	}
	buffer += seprintf(buffer, last, "\n");

	this->CrashLogFaultSectionCheckpoint(buffer);

	if (_grfconfig_static != nullptr) {
		buffer += seprintf(buffer, last, "Static NewGRFs present:\n");
		for (GRFConfig *c = _grfconfig_static; c != nullptr; c = c->next) {
			char md5sum[33];
			md5sumToString(md5sum, lastof(md5sum), c->ident.md5sum);
			buffer += seprintf(buffer, last, " GRF ID: %08X, checksum %s, %s, '%s'\n", BSWAP32(c->ident.grfid), md5sum, c->GetDisplayPath(), GetDefaultLangGRFStringFromGRFText(c->name));
		}
		buffer += seprintf(buffer, last, "\n");
	}

	this->CrashLogFaultSectionCheckpoint(buffer);

	if (_network_server) {
		extern char *NetworkServerDumpClients(char *buffer, const char *last);
		buffer += seprintf(buffer, last, "Clients:\n");
		buffer = NetworkServerDumpClients(buffer, last);
		buffer += seprintf(buffer, last, "\n");
	}

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

#if defined(WITH_HARFBUZZ)
	buffer += seprintf(buffer, last, " HarfBuzz:   %s\n", hb_version_string());
#endif /* WITH_HARFBUZZ */

#if defined(WITH_ICU_I18N)
	/* 4 times 0-255, separated by dots (.) and a trailing '\0' */
	char buf[4 * 3 + 3 + 1];
	UVersionInfo ver;
	u_getVersion(ver);
	u_versionToString(ver, buf);
	buffer += seprintf(buffer, last, " ICU i18n:   %s\n", buf);
#endif /* WITH_ICU_I18N */

#ifdef WITH_LIBLZMA
	buffer += seprintf(buffer, last, " LZMA:       %s\n", lzma_version_string());
#endif

#ifdef WITH_ZSTD
	buffer += seprintf(buffer, last, " ZSTD:       %s\n", ZSTD_versionString());
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

#ifdef WITH_CURL
	auto *curl_v = curl_version_info(CURLVERSION_NOW);
	buffer += seprintf(buffer, last, " Curl:       %s\n", curl_v->version);
	if (curl_v->ssl_version != nullptr) {
		buffer += seprintf(buffer, last, " Curl SSL:   %s\n", curl_v->ssl_version);
	} else {
		buffer += seprintf(buffer, last, " Curl SSL:   none\n");
	}
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
	uint show = std::min<uint>(total, 32);
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
	buffer = DumpCommandLog(buffer, last, nullptr);
	buffer += seprintf(buffer, last, "\n");
	buffer = DumpSpecialEventsLog(buffer, last);
	buffer += seprintf(buffer, last, "\n");
	return buffer;
}

/**
 * Fill the crash log buffer with all data of a crash log.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::FillCrashLog(char *buffer, const char *last)
{
	this->StartCrashLogFaultHandler();
	buffer += seprintf(buffer, last, "*** OpenTTD Crash Report ***\n\n");

	buffer = this->TryCrashLogFaultSection(buffer, last, "emergency test", [](CrashLog *self, char *buffer, const char *last) -> char * {
		if (GamelogTestEmergency()) {
			buffer += seprintf(buffer, last, "-=-=- As you loaded an emergency savegame no crash information would ordinarily be generated. -=-=-\n\n");
		}
		if (SaveloadCrashWithMissingNewGRFs()) {
			buffer += seprintf(buffer, last, "-=-=- As you loaded a savegame for which you do not have the required NewGRFs no crash information would ordinarily be generated. -=-=-\n\n");
		}
		return buffer;
	});

	buffer = this->TryCrashLogFaultSection(buffer, last, "times", [](CrashLog *self, char *buffer, const char *last) -> char * {
		buffer += UTCTime::Format(buffer, last, "Crash at: %Y-%m-%d %H:%M:%S (UTC)\n");

		buffer += seprintf(buffer, last, "In game date: %i-%02i-%02i (%i, %i) (DL: %u)\n", _cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day, _date_fract, _tick_skip_counter, _settings_game.economy.day_length_factor);
		if (_game_load_time != 0) {
			buffer += seprintf(buffer, last, "Game loaded at: %i-%02i-%02i (%i, %i), ",
					_game_load_cur_date_ymd.year, _game_load_cur_date_ymd.month + 1, _game_load_cur_date_ymd.day, _game_load_date_fract, _game_load_tick_skip_counter);
			buffer += UTCTime::Format(buffer, last, _game_load_time, "%Y-%m-%d %H:%M:%S");
		}
		return buffer;
	});

	buffer += seprintf(buffer, last, "\n");

	buffer = this->TryCrashLogFaultSection(buffer, last, "message", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogError(buffer, last, CrashLog::message);
	});

#ifdef USE_SCOPE_INFO
	buffer = this->TryCrashLogFaultSection(buffer, last, "scope", [](CrashLog *self, char *buffer, const char *last) -> char * {
		if (IsGameThread()) {
			buffer += WriteScopeLog(buffer, last);
		}
		return buffer;
	});
#endif

	if (_networking) {
		buffer = this->TryCrashLogFaultSection(buffer, last, "network sync", [](CrashLog *self, char *buffer, const char *last) -> char * {
			if (IsGameThread() && _record_sync_records && !_network_sync_records.empty()) {
				uint total = 0;
				for (uint32 count : _network_sync_record_counts) {
					total += count;
				}
				NetworkSyncRecordEvents event = NSRE_BEGIN;
				if (_network_sync_records.size() > total + 1) {
					event = (NetworkSyncRecordEvents)(_network_sync_records.back().frame);
				}
				buffer += seprintf(buffer, last, "Last sync record type: %s\n\n", GetSyncRecordEventName(event));
			}
			return buffer;
		});
	}

	buffer = this->TryCrashLogFaultSection(buffer, last, "thread", [](CrashLog *self, char *buffer, const char *last) -> char * {
		if (IsNonMainThread()) {
			buffer += seprintf(buffer, last, "Non-main thread (");
			buffer += GetCurrentThreadName(buffer, last);
			buffer += seprintf(buffer, last, ")\n\n");
		}
		return buffer;
	});

	buffer = this->TryCrashLogFaultSection(buffer, last, "OpenTTD version", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogOpenTTDVersion(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "stacktrace", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogStacktrace(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "debug extra", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogDebugExtra(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "registers", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogRegisters(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "OS version", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogOSVersion(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "compiler", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogCompiler(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "OS version detail", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogOSVersionDetail(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "config", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogConfiguration(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "libraries", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogLibraries(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "modules", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogModules(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "gamelog", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogGamelog(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "news", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogRecentNews(buffer, last);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "command log", [](CrashLog *self, char *buffer, const char *last) -> char * {
		return self->LogCommandLog(buffer, last);
	});

	buffer += seprintf(buffer, last, "*** End of OpenTTD Crash Report ***\n");
	this->StopCrashLogFaultHandler();
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
	buffer += seprintf(buffer, last, "*** OpenTTD Multiplayer %s Desync Report ***\n\n", _network_server ? "Server" : "Client");

	buffer += UTCTime::Format(buffer, last, "Desync at: %Y-%m-%d %H:%M:%S (UTC)\n");

	if (!_network_server && info.flags) {
		auto flag_check = [&](DesyncExtraInfo::Flags flag, const char *str) {
			return info.flags & flag ? str : "";
		};
		buffer += seprintf(buffer, last, "Flags: %s%s\n",
				flag_check(DesyncExtraInfo::DEIF_RAND, "R"),
				flag_check(DesyncExtraInfo::DEIF_STATE, "S"));
	}
	if (_network_server && !info.desync_frame_info.empty()) {
		buffer += seprintf(buffer, last, "%s\n", info.desync_frame_info.c_str());
	}

	extern uint32 _frame_counter;

	buffer += seprintf(buffer, last, "In game date: %i-%02i-%02i (%i, %i) (DL: %u), %08X\n",
			_cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day, _date_fract, _tick_skip_counter, _settings_game.economy.day_length_factor, _frame_counter);
	if (_game_load_time != 0) {
		buffer += seprintf(buffer, last, "Game loaded at: %i-%02i-%02i (%i, %i), ",
				_game_load_cur_date_ymd.year, _game_load_cur_date_ymd.month + 1, _game_load_cur_date_ymd.day, _game_load_date_fract, _game_load_tick_skip_counter);
		buffer += UTCTime::Format(buffer, last, _game_load_time, "%Y-%m-%d %H:%M:%S");
		buffer += seprintf(buffer, last, "\n");
	}
	if (!_network_server) {
		extern Date   _last_sync_date;
		extern DateFract _last_sync_date_fract;
		extern uint8  _last_sync_tick_skip_counter;
		extern uint32 _last_sync_frame_counter;

		YearMonthDay ymd;
		ConvertDateToYMD(_last_sync_date, &ymd);
		buffer += seprintf(buffer, last, "Last sync at: %i-%02i-%02i (%i, %i), %08X\n",
				ymd.year, ymd.month + 1, ymd.day, _last_sync_date_fract, _last_sync_tick_skip_counter, _last_sync_frame_counter);
	}
	if (info.client_id >= 0) {
		buffer += seprintf(buffer, last, "Client #%d, \"%s\"\n", info.client_id, info.client_name != nullptr ? info.client_name : "");
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
 * Fill the crash log buffer with all data of an inconsistency event.
 * @param buffer The begin where to write at.
 * @param last   The last position in the buffer to write to.
 * @return the position of the \c '\0' character after the buffer.
 */
char *CrashLog::FillInconsistencyLog(char *buffer, const char *last, const InconsistencyExtraInfo &info) const
{
	buffer += seprintf(buffer, last, "*** OpenTTD Inconsistency Report ***\n\n");

	buffer += UTCTime::Format(buffer, last, "Inconsistency at: %Y-%m-%d %H:%M:%S (UTC)\n");

#ifdef USE_SCOPE_INFO
	buffer += WriteScopeLog(buffer, last);
#endif

	extern uint32 _frame_counter;

	buffer += seprintf(buffer, last, "In game date: %i-%02i-%02i (%i, %i) (DL: %u), %08X\n",
			_cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day, _date_fract, _tick_skip_counter, _settings_game.economy.day_length_factor, _frame_counter);
	if (_game_load_time != 0) {
		buffer += seprintf(buffer, last, "Game loaded at: %i-%02i-%02i (%i, %i), ",
				_game_load_cur_date_ymd.year, _game_load_cur_date_ymd.month + 1, _game_load_cur_date_ymd.day, _game_load_date_fract, _game_load_tick_skip_counter);
		buffer += UTCTime::Format(buffer, last, _game_load_time, "%Y-%m-%d %H:%M:%S");
		buffer += seprintf(buffer, last, "\n");
	}
	if (_networking && !_network_server) {
		extern Date   _last_sync_date;
		extern DateFract _last_sync_date_fract;
		extern uint8  _last_sync_tick_skip_counter;
		extern uint32 _last_sync_frame_counter;

		YearMonthDay ymd;
		ConvertDateToYMD(_last_sync_date, &ymd);
		buffer += seprintf(buffer, last, "Last sync at: %i-%02i-%02i (%i, %i), %08X\n",
				ymd.year, ymd.month + 1, ymd.day, _last_sync_date_fract, _last_sync_tick_skip_counter, _last_sync_frame_counter);
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

	if (!info.check_caches_result.empty()) {
		buffer += seprintf(buffer, last, "CheckCaches:\n");
		for (const std::string &str : info.check_caches_result) {
			buffer += seprintf(buffer, last, "  %s\n", str.c_str());
		}
	}

	buffer += seprintf(buffer, last, "*** End of OpenTTD Inconsistency Report ***\n");
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
	seprintf(filename, filename_last, "%s%s.log", _personal_dir.c_str(), name);

	FILE *file = FioFOpenFile(filename, "w", NO_DIRECTORY);
	if (file == nullptr) return false;

	size_t len = strlen(buffer);
	size_t written = (len != 0) ? fwrite(buffer, 1, len, file) : 0;

	if (crashlog_file) {
		*crashlog_file = file;
	} else {
		FioFCloseFile(file);
	}
	return len == written;
}

void CrashLog::FlushCrashLogBuffer()
{
	if (this->crash_buffer_write == nullptr) return;

	size_t len = strlen(this->crash_buffer_write);
	if (len == 0) return;

	if (this->crash_file != nullptr) {
		fwrite(this->crash_buffer_write, 1, len, this->crash_file);
		fflush(this->crash_file);
	}
#if !defined(_WIN32)
	fwrite(this->crash_buffer_write, 1, len, stdout);
	fflush(stdout);
#endif

	this->crash_buffer_write += len;
}

void CrashLog::CloseCrashLogFile()
{
	this->FlushCrashLogBuffer();
	if (this->crash_file != nullptr) {
		FioFCloseFile(this->crash_file);
		this->crash_file = nullptr;
	}
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
/* static */ bool CrashLog::WriteSavegame(char *filename, const char *filename_last, const char *name)
{
	/* If the map array doesn't exist, saving will fail too. If the map got
	 * initialised, there is a big chance the rest is initialised too. */
	if (_m == nullptr) return false;

	try {
		GamelogEmergency();

		seprintf(filename, filename_last, "%s%s.sav", _personal_dir.c_str(), name);

		/* Don't do a threaded saveload. */
		return SaveOrLoad(filename, SLO_SAVE, DFT_GAME_FILE, NO_DIRECTORY, false) == SL_OK;
	} catch (...) {
		return false;
	}
}

/**
 * Write the (desync) savegame to a file, threaded.
 * @note On success the filename will be filled with the full path of the
 *       crash save file. Make sure filename is at least \c MAX_PATH big.
 * @param filename      Output for the filename of the written file.
 * @param filename_last The last position in the filename buffer.
 * @return true when the crash save was successfully made.
 */
/* static */ bool CrashLog::WriteDiagnosticSavegame(char *filename, const char *filename_last, const char *name)
{
	/* If the map array doesn't exist, saving will fail too. If the map got
	 * initialised, there is a big chance the rest is initialised too. */
	if (_m == nullptr) return false;

	try {
		seprintf(filename, filename_last, "%s%s.sav", _personal_dir.c_str(), name);

		return SaveOrLoad(filename, SLO_SAVE, DFT_GAME_FILE, NO_DIRECTORY, true) == SL_OK;
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
/* static */ bool CrashLog::WriteScreenshot(char *filename, const char *filename_last, const char *name)
{
	/* Don't draw when we have invalid screen size */
	if (_screen.width < 1 || _screen.height < 1 || _screen.dst_ptr == nullptr) return false;

	bool res = MakeScreenshot(SC_CRASHLOG, name);
	if (res) strecpy(filename, _full_screenshot_name, filename_last);
	return res;
}

#ifdef DEDICATED
static bool CopyAutosave(const std::string &old_name, const std::string &new_name)
{
	FILE *old_fh = FioFOpenFile(old_name, "rb", AUTOSAVE_DIR);
	if (old_fh == nullptr) return false;
	auto guard1 = scope_guard([=]() {
		FioFCloseFile(old_fh);
	});
	FILE *new_fh = FioFOpenFile(new_name, "wb", AUTOSAVE_DIR);
	if (new_fh == nullptr) return false;
	auto guard2 = scope_guard([=]() {
		FioFCloseFile(new_fh);
	});

	char buffer[4096 * 4];
	size_t length;
	do {
		length = fread(buffer, 1, lengthof(buffer), old_fh);
		if (fwrite(buffer, 1, length, new_fh) != length) {
			return false;
		}
	} while (length == lengthof(buffer));
	return true;
}
#endif

void CrashLog::SendSurvey() const
{
	if (_game_mode == GM_NORMAL) {
		_survey.Transmit(NetworkSurveyHandler::Reason::CRASH, true);
	}
}

/**
 * Makes the crash log, writes it to a file and then subsequently tries
 * to make a crash dump and crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
bool CrashLog::MakeCrashLog(char *buffer, const char *last)
{
	/* Don't keep looping logging crashes. */
	if (CrashLog::HaveAlreadyCrashed()) return false;
	CrashLog::RegisterCrashed();

	char *name_buffer_date = this->name_buffer + seprintf(this->name_buffer, lastof(this->name_buffer), "crash-");
	UTCTime::Format(name_buffer_date, lastof(this->name_buffer), "%Y%m%dT%H%M%SZ");

#ifdef DEDICATED
	if (!_settings_client.gui.keep_all_autosave) {
		extern FiosNumberedSaveName &GetAutoSaveFiosNumberedSaveName();
		FiosNumberedSaveName &autosave = GetAutoSaveFiosNumberedSaveName();
		int num = autosave.GetLastNumber();
		if (num >= 0) {
			std::string old_file = autosave.FilenameUsingNumber(num, "");
			char save_suffix[MAX_PATH];
			seprintf(save_suffix, lastof(save_suffix), "-(%s)", this->name_buffer);
			std::string new_file = autosave.FilenameUsingNumber(num, save_suffix);
			if (CopyAutosave(old_file, new_file)) {
				printf("Saving copy of last autosave: %s -> %s\n\n", old_file.c_str(), new_file.c_str());
			}
		}
	}
#endif

	if (!VideoDriver::EmergencyAcquireGameLock(20, 2)) {
		printf("Failed to acquire gamelock before filling crash log\n\n");
	}

	char filename[MAX_PATH];
	bool ret = true;

	printf("Crash encountered, generating crash log...\n");

	printf("Writing crash log to disk...\n");
	bool bret = this->WriteCrashLog("", filename, lastof(filename), this->name_buffer, &(this->crash_file));
	if (bret) {
		printf("Crash log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing crash log failed. Please attach the output above to any bug reports.\n\n");
		ret = false;
	}
	this->crash_buffer_write = buffer;

	this->FillCrashLog(buffer, last);
	this->CloseCrashLogFile();
	printf("Crash log generated.\n\n");


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

	if (!VideoDriver::EmergencyAcquireGameLock(1000, 5)) {
		printf("Failed to acquire gamelock before writing crash savegame and screenshot, proceeding without lock as current owner is probably stuck\n\n");
	}

	bret = CrashLog::MakeCrashSavegameAndScreenshot();
	if (!bret) ret = false;

	return ret;
}

bool CrashLog::MakeCrashLogWithStackBuffer()
{
	char buffer[65536 * 4];
	return this->MakeCrashLog(buffer, lastof(buffer));
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

	const size_t length = 65536 * 16;
	char * const buffer = MallocT<char>(length);
	auto guard = scope_guard([=]() {
		free(buffer);
	});
	const char * const last = buffer + length - 1;

	bool ret = true;

	const char *mode = _network_server ? "server" : "client";

	char name_buffer[64];
	char *name_buffer_date = name_buffer + seprintf(name_buffer, lastof(name_buffer), "desync-%s-", mode);
	UTCTime::Format(name_buffer_date, lastof(this->name_buffer), "%Y%m%dT%H%M%SZ");

	printf("Desync encountered (%s), generating desync log...\n", mode);
	char *b = this->FillDesyncCrashLog(buffer, last, info);

	if (log_out) log_out->assign(buffer);

	if (log_in && !log_in->empty()) {
		b = strecpy(b, "\n", last, true);
		b = strecpy(b, log_in->c_str(), last, true);
	}

	bool bret = this->WriteCrashLog(buffer, filename, lastof(filename), name_buffer, info.log_file);
	if (bret) {
		printf("Desync log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing desync log failed.\n\n");
		ret = false;
	}

	if (info.defer_savegame_write != nullptr) {
		info.defer_savegame_write->name_buffer = name_buffer;
	} else {
		bret = this->WriteDesyncSavegame(buffer, name_buffer);
		if (!bret) ret = false;
	}

	return ret;
}

/* static */ bool CrashLog::WriteDesyncSavegame(const char *log_data, const char *name_buffer)
{
	char filename[MAX_PATH];

	_savegame_DBGL_data = log_data;
	_save_DBGC_data = true;
	bool ret = CrashLog::WriteDiagnosticSavegame(filename, lastof(filename), name_buffer);
	if (ret) {
		printf("Desync savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", filename);
	} else {
		printf("Writing desync savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
	}
	_savegame_DBGL_data = nullptr;
	_save_DBGC_data = false;

	return ret;
}

/**
 * Makes an inconsistency log, writes it to a file and then subsequently tries
 * to make a crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
bool CrashLog::MakeInconsistencyLog(const InconsistencyExtraInfo &info) const
{
	char filename[MAX_PATH];

	const size_t length = 65536 * 16;
	char * const buffer = MallocT<char>(length);
	auto guard = scope_guard([=]() {
		free(buffer);
	});
	const char * const last = buffer + length - 1;

	bool ret = true;

	char name_buffer[64];
	char *name_buffer_date = name_buffer + seprintf(name_buffer, lastof(name_buffer), "inconsistency-");
	UTCTime::Format(name_buffer_date, lastof(this->name_buffer), "%Y%m%dT%H%M%SZ");

	printf("Inconsistency encountered, generating diagnostics log...\n");
	this->FillInconsistencyLog(buffer, last, info);

	bool bret = this->WriteCrashLog(buffer, filename, lastof(filename), name_buffer);
	if (bret) {
		printf("Inconsistency log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing inconsistency log failed.\n\n");
		ret = false;
	}

	_savegame_DBGL_data = buffer;
	_save_DBGC_data = true;
	bret = this->WriteDiagnosticSavegame(filename, lastof(filename), name_buffer);
	if (bret) {
		printf("info savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", filename);
	} else {
		ret = false;
		printf("Writing inconsistency savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
	}
	_savegame_DBGL_data = nullptr;
	_save_DBGC_data = false;

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

	this->SendSurvey();

	return ret;
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

	bfd_size_type size = get_bfd_section_size(abfd, section);
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
