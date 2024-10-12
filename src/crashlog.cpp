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
#include "settings_type.h"
#include "settings_internal.h"
#include "social_integration.h"

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
/* static */ bool CrashLog::have_crashed = false;

void CrashLog::LogCompiler(format_target &buffer) const
{
			buffer.format(" Compiler: "
#if defined(_MSC_VER)
			"MSVC {}", _MSC_VER
#elif defined(__clang__)
			"clang {}", __clang_version__
#elif defined(__ICC) && defined(__GNUC__)
			"ICC {} (GCC {}.{}.{} mode)", __ICC,  __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__
#elif defined(__ICC)
			"ICC {}", __ICC
#elif defined(__GNUC__)
			"GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__
#else
			"<unknown>"
#endif
			);
#if defined(__VERSION__)
			buffer.append(" \"" __VERSION__ "\"\n\n");
#else
			buffer.append("\n\n");
#endif
}

/* virtual */ void CrashLog::LogOSVersionDetail(format_target &buffer) const
{
	/* Stub implementation; not all OSes support this. */
}

/* virtual */ void CrashLog::LogDebugExtra(format_target &buffer) const
{
	/* Stub implementation; not all OSes support this. */
}

/* virtual */ void CrashLog::LogRegisters(format_target &buffer) const
{
	/* Stub implementation; not all OSes support this. */
}

/* virtual */ void CrashLog::LogCrashTrailer(format_target &buffer) const
{
	/* Stub implementation; not all OSes have anything to output for this section. */
}

#if !defined(DISABLE_SCOPE_INFO)
/* virtual */ void CrashLog::LogScopeInfo(format_target &buffer) const
{
	WriteScopeLog(buffer);
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
	this->FlushCrashLogBuffer(buffer);
	format_to_fixed buf(buffer, last - buffer);
	writer(this, buf);
	return buffer + buf.size();
}

/* virtual */ void CrashLog::CrashLogFaultSectionCheckpoint(format_target &buffer) const
{
	/* Stub implementation; not all OSes support this. */
	const_cast<CrashLog *>(this)->FlushCrashLogBuffer(buffer.end());
}

/**
 * Writes OpenTTD's version to the buffer.
 * @param buffer The output buffer.
 */
void CrashLog::LogOpenTTDVersion(format_target &buffer) const
{
	buffer.format(
			"OpenTTD version:\n"
			" Version:     {} ({})\n"
			" Release ver: {}\n"
			" NewGRF ver:  {:08x}\n"
			" Bits:        {}\n"
			" Endian:      {}\n"
			" Dedicated:   {}\n"
			" Build date:  {}\n"
			" Defines:     {}\n\n",
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
 * @param buffer The output buffer.
 */
void CrashLog::LogConfiguration(format_target &buffer) const
{
	auto mode_name = []() -> const char * {
		switch (_game_mode) {
			case GM_MENU: return "MENU";
			case GM_NORMAL: return "NORMAL";
			case GM_EDITOR: return "EDITOR";
			case GM_BOOTSTRAP:  return "BOOTSTRAP";
			default: return "-";
		};
	};
	buffer.format(
			"Configuration:\n"
			" Blitter:      {}\n"
			" Graphics set: {} ({})\n"
			" Language:     {}\n"
			" Music driver: {}\n"
			" Music set:    {} ({})\n"
			" Network:      {}\n"
			" Sound driver: {}\n"
			" Sound set:    {} ({})\n"
			" Video driver: {}\n",
			BlitterFactory::GetCurrentBlitter() == nullptr ? (std::string_view)"none" : BlitterFactory::GetCurrentBlitter()->GetName(),
			BaseGraphics::GetUsedSet() == nullptr ? (std::string_view)"none" : BaseGraphics::GetUsedSet()->name,
			BaseGraphics::GetUsedSet() == nullptr ? UINT32_MAX : BaseGraphics::GetUsedSet()->version,
			_current_language == nullptr ? (std::string_view)"none" : _current_language->file,
			MusicDriver::GetInstance() == nullptr ? "none" : MusicDriver::GetInstance()->GetName(),
			BaseMusic::GetUsedSet() == nullptr ? (std::string_view)"none" : BaseMusic::GetUsedSet()->name,
			BaseMusic::GetUsedSet() == nullptr ? UINT32_MAX : BaseMusic::GetUsedSet()->version,
			_networking ? (_network_server ? "server" : "client") : "no",
			SoundDriver::GetInstance() == nullptr ? "none" : SoundDriver::GetInstance()->GetName(),
			BaseSounds::GetUsedSet() == nullptr ? (std::string_view)"none" : BaseSounds::GetUsedSet()->name,
			BaseSounds::GetUsedSet() == nullptr ? UINT32_MAX : BaseSounds::GetUsedSet()->version,
			VideoDriver::GetInstance() == nullptr ? "none" : VideoDriver::GetInstance()->GetInfoString()
	);
	buffer.format(" Game mode:    {}", mode_name());
	if (_switch_mode != SM_NONE) buffer.format(", SM: {}", _switch_mode);
	if (HasModalProgress()) buffer.append(", HMP");
	buffer.append("\n\n");

	this->CrashLogFaultSectionCheckpoint(buffer);

	auto log_font = [&](FontSize fs) -> std::string {
		FontCache *fc = FontCache::Get(fs);
		if (fc != nullptr) {
			return fc->GetFontName();
		} else {
			return "[NULL]";
		}
	};

	buffer.format(
			"Fonts:\n"
			" Small:  {}\n"
			" Medium: {}\n"
			" Large:  {}\n"
			" Mono:   {}\n\n",
			log_font(FS_SMALL),
			log_font(FS_NORMAL),
			log_font(FS_LARGE),
			log_font(FS_MONO)
	);

	this->CrashLogFaultSectionCheckpoint(buffer);

	buffer.format("Map size: 0x{:X} ({} x {}){}\n\n", MapSize(), MapSizeX(), MapSizeY(), (!_m || !_me) ? ", NO MAP ALLOCATED" : "");

	if (_settings_game.debug.chicken_bits != 0) {
		buffer.format("Chicken bits: 0x{:08X}\n\n", _settings_game.debug.chicken_bits);
	}
	if (_settings_game.debug.newgrf_optimiser_flags != 0) {
		buffer.format("NewGRF optimiser flags: 0x{:08X}\n\n", _settings_game.debug.newgrf_optimiser_flags);
	}

	this->CrashLogFaultSectionCheckpoint(buffer);

	buffer.format("AI Configuration (local: {}) (current: {}):\n", (int)_local_company, (int)_current_company);
	for (const Company *c : Company::Iterate()) {
		if (c->ai_info == nullptr) {
			buffer.format(" {:2}: Human\n", (int)c->index);
		} else {
			buffer.format(" {:2}: {} (v{})\n", (int)c->index, c->ai_info->GetName().c_str(), c->ai_info->GetVersion());
		}
	}

	if (Game::GetInfo() != nullptr) {
		buffer.format(" GS: {} (v{})\n", Game::GetInfo()->GetName(), Game::GetInfo()->GetVersion());
	}
	buffer.push_back('\n');

	this->CrashLogFaultSectionCheckpoint(buffer);

	if (_grfconfig_static != nullptr) {
		buffer.append("Static NewGRFs present:\n");
		for (GRFConfig *c = _grfconfig_static; c != nullptr; c = c->next) {
			buffer.format(" GRF ID: {:08X}, checksum {}, {}", BSWAP32(c->ident.grfid), c->ident.md5sum, c->GetDisplayPath());
			const char *name = GetDefaultLangGRFStringFromGRFText(c->name);
			if (name != nullptr) buffer.format(", '{}'", name);
			buffer.push_back('\n');
		}
		buffer.push_back('\n');
	}

	this->CrashLogFaultSectionCheckpoint(buffer);

	if (_network_server) {
		extern void NetworkServerDumpClients(format_target &buffer);
		buffer.append("Clients:\n");
		NetworkServerDumpClients(buffer);
		buffer.append("\n");
	}
}

/**
 * Writes information (versions) of the used libraries.
 * @param buffer The output buffer.
 */
void CrashLog::LogLibraries(format_target &buffer) const
{
	buffer.append("Libraries:\n");

#ifdef WITH_ALLEGRO
	buffer.format(" Allegro:    {}\n", allegro_id);
#endif /* WITH_ALLEGRO */

#ifdef WITH_FONTCONFIG
	int version = FcGetVersion();
	buffer.format(" FontConfig: {}.{}.{}\n", version / 10000, (version / 100) % 100, version % 100);
#endif /* WITH_FONTCONFIG */

#ifdef WITH_FREETYPE
	FT_Library library;
	int major, minor, patch;
	FT_Init_FreeType(&library);
	FT_Library_Version(library, &major, &minor, &patch);
	FT_Done_FreeType(library);
	buffer.format(" FreeType:   {}.{}.{}\n", major, minor, patch);
#endif /* WITH_FREETYPE */

#if defined(WITH_HARFBUZZ)
	buffer.format(" HarfBuzz:   {}\n", hb_version_string());
#endif /* WITH_HARFBUZZ */

#if defined(WITH_ICU_I18N)
	/* 4 times 0-255, separated by dots (.) and a trailing '\0' */
	char buf[4 * 3 + 3 + 1];
	UVersionInfo ver;
	u_getVersion(ver);
	u_versionToString(ver, buf);
	buffer.format(" ICU i18n:   {}\n", buf);
#endif /* WITH_ICU_I18N */

#ifdef WITH_LIBLZMA
	buffer.format(" LZMA:       {}\n", lzma_version_string());
#endif

#ifdef WITH_ZSTD
	buffer.format(" ZSTD:       {}\n", ZSTD_versionString());
#endif

#ifdef WITH_LZO
	buffer.format(" LZO:        {}\n", lzo_version_string());
#endif

#ifdef WITH_PNG
	buffer.format(" PNG:        {}\n", png_get_libpng_ver(nullptr));
#endif /* WITH_PNG */

#ifdef WITH_SDL
	const SDL_version *sdl_v = SDL_Linked_Version();
	buffer.format(" SDL1:       {}.{}.{}\n", sdl_v->major, sdl_v->minor, sdl_v->patch);
#elif defined(WITH_SDL2)
	SDL_version sdl2_v;
	SDL_GetVersion(&sdl2_v);
	buffer.format(" SDL2:       {}.{}.{}", sdl2_v.major, sdl2_v.minor, sdl2_v.patch);
#if defined(SDL_USE_IME)
	buffer.append(" IME?");
#endif
#if defined(HAVE_FCITX_FRONTEND_H)
	buffer.append(" FCITX?");
#endif
#if defined(HAVE_IBUS_IBUS_H)
	buffer.append(" IBUS?");
#endif
#if !(defined(_WIN32) || defined(__APPLE__))
	const char *sdl_im_module = getenv("SDL_IM_MODULE");
	if (sdl_im_module != nullptr) buffer.append(" (SDL_IM_MODULE={})", sdl_im_module);
	const char *xmod = getenv("XMODIFIERS");
	if (xmod != nullptr && strstr(xmod, "@im=fcitx") != nullptr) buffer.append(" (XMODIFIERS has @im=fcitx)");
#endif
	buffer.push_back('\n');
#endif

#ifdef WITH_ZLIB
	buffer.format(" Zlib:       {}\n", zlibVersion());
#endif

#ifdef WITH_CURL
	auto *curl_v = curl_version_info(CURLVERSION_NOW);
	buffer.format(" Curl:       {}\n", curl_v->version);
	if (curl_v->ssl_version != nullptr) {
		buffer.format(" Curl SSL:   {}\n", curl_v->ssl_version);
	} else {
		buffer.append(" Curl SSL:   none\n");
	}
#endif

	buffer.push_back('\n');
}

/**
 * Writes information (versions) of the used plugins.
 * @param buffer The output buffer.
 */
void CrashLog::LogPlugins(format_target &buffer) const
{
	if (SocialIntegration::GetPluginCount() == 0) return;

	buffer.append("Plugins:\n");
	SocialIntegration::LogPluginSummary(buffer);
}

/**
 * Writes the gamelog data to the buffer.
 * @param buffer The output buffer.
 */
void CrashLog::LogGamelog(format_target &buffer) const
{
	if (_game_events_since_load || _game_events_overall) {
		buffer.append("Events: ");
		DumpGameEventFlags(_game_events_since_load, buffer);
		buffer.append(", ");
		DumpGameEventFlags(_game_events_overall, buffer);
		buffer.append("\n\n");
	}

	GamelogPrint(buffer);
	buffer.push_back('\n');
}

/**
 * Writes up to 32 recent news messages to the buffer, with the most recent first.
 * @param buffer The output buffer.
 */
void CrashLog::LogRecentNews(format_target &buffer) const
{
	uint total = static_cast<uint>(GetNews().size());
	uint show = std::min<uint>(total, 32);
	buffer.format("Recent news messages ({} of {}):\n", show, total);

	int i = 0;
	for (const auto &news : GetNews()) {
		CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(news.date);
		buffer.format("({}-{:02}-{:02}) StringID: {}, Type: {}, Ref1: {}, {}, Ref2: {}, {}\n",
			   ymd.year, ymd.month + 1, ymd.day, news.string_id, news.type,
			   news.reftype1, news.ref1, news.reftype2, news.ref2);
		if (++i > 32) break;
	}
	buffer.push_back('\n');
}

/**
 * Writes the command log data to the buffer.
 * @param buffer The output buffer.
 */
void CrashLog::LogCommandLog(format_target &buffer) const
{
	DumpCommandLog(buffer);
	buffer.push_back('\n');
	DumpSpecialEventsLog(buffer);
	buffer.push_back('\n');
}

/**
 * Writes the non-default settings to the buffer.
 * @param buffer The output buffer.
 */
void CrashLog::LogSettings(format_target &buffer) const
{
	buffer.append("Non-default settings:");

	IterateSettingsTables([&](const SettingTable &table, void *object) {
		for (auto &sd : table) {
			/* Skip any old settings we no longer save/load. */
			if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;

			if (sd->IsDefaultValue(object)) continue;
			buffer.format("\n  {}: ", sd->name);
			sd->FormatValue(buffer, object);
		}
	});

	buffer.append("\n\n");
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
	buffer = format_to_fixed_z::format_to(buffer, last, "*** OpenTTD Crash Report ***\n\n");

	buffer = this->TryCrashLogFaultSection(buffer, last, "emergency test", [](CrashLog *self, format_target &buffer) {
		if (GamelogTestEmergency()) {
			buffer.append("-=-=- As you loaded an emergency savegame no crash information would ordinarily be generated. -=-=-\n\n");
		}
		if (SaveloadCrashWithMissingNewGRFs()) {
			buffer.append("-=-=- As you loaded a savegame for which you do not have the required NewGRFs no crash information would ordinarily be generated. -=-=-\n\n");
		}
	});

	buffer = this->TryCrashLogFaultSection(buffer, last, "times", [](CrashLog *self, format_target &buffer) {
		UTCTime::FormatTo(buffer, "Crash at: %Y-%m-%d %H:%M:%S (UTC)\n");

		buffer.format("In game date: {}-{:02}-{:02} ({}, {}) (DL: {})\n", EconTime::CurYear().base(), EconTime::CurMonth() + 1, EconTime::CurDay(), EconTime::CurDateFract(), TickSkipCounter(), DayLengthFactor());
		buffer.format("Calendar date: {}-{:02}-{:02} ({}, {})\n", CalTime::CurYear().base(), CalTime::CurMonth() + 1, CalTime::CurDay(), CalTime::CurDateFract(), CalTime::CurSubDateFract());
		LogGameLoadDateTimes(buffer);
	});

	buffer = format_to_fixed_z::format_to(buffer, last, "\n");

	buffer = this->TryCrashLogFaultSection(buffer, last, "message", [](CrashLog *self, format_target &buffer) {
		self->LogError(buffer, CrashLog::message);
	});

#if !defined(DISABLE_SCOPE_INFO)
	buffer = this->TryCrashLogFaultSection(buffer, last, "scope", [](CrashLog *self, format_target &buffer) {
		if (IsGameThread()) {
			WriteScopeLog(buffer);
		}
	});
#endif

	if (_networking) {
		buffer = this->TryCrashLogFaultSection(buffer, last, "network sync", [](CrashLog *self, format_target &buffer) {
			if (IsGameThread() && _record_sync_records && !_network_sync_records.empty()) {
				uint total = 0;
				for (uint32_t count : _network_sync_record_counts) {
					total += count;
				}
				NetworkSyncRecordEvents event = NSRE_BEGIN;
				if (_network_sync_records.size() > total + 1) {
					event = (NetworkSyncRecordEvents)(_network_sync_records.back().frame);
				}
				buffer.format("Last sync record type: {}\n\n", GetSyncRecordEventName(event));
			}
		});
	}

	buffer = this->TryCrashLogFaultSection(buffer, last, "thread", [](CrashLog *self, format_target &buffer) {
		if (IsNonMainThread()) {
			buffer.append("Non-main thread (");
			GetCurrentThreadName(buffer);
			buffer.append(")\n\n");
		}
	});

	buffer = this->TryCrashLogFaultSection(buffer, last, "OpenTTD version", [](CrashLog *self, format_target &buffer) {
		self->LogOpenTTDVersion(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "stacktrace", [](CrashLog *self, format_target &buffer) {
		self->LogStacktrace(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "debug extra", [](CrashLog *self, format_target &buffer) {
		self->LogDebugExtra(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "registers", [](CrashLog *self, format_target &buffer) {
		self->LogRegisters(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "OS version", [](CrashLog *self, format_target &buffer) {
		self->LogOSVersion(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "compiler", [](CrashLog *self, format_target &buffer) {
		self->LogCompiler(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "OS version detail", [](CrashLog *self, format_target &buffer) {
		self->LogOSVersionDetail(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "config", [](CrashLog *self, format_target &buffer) {
		self->LogConfiguration(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "libraries", [](CrashLog *self, format_target &buffer) {
		self->LogLibraries(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "plugins", [](CrashLog *self, format_target &buffer) {
		self->LogPlugins(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "settings", [](CrashLog *self, format_target &buffer) {
		self->LogSettings(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "command log", [](CrashLog *self, format_target &buffer) {
		self->LogCommandLog(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "gamelog", [](CrashLog *self, format_target &buffer) {
		self->LogGamelog(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "news", [](CrashLog *self, format_target &buffer) {
		self->LogRecentNews(buffer);
	});
	buffer = this->TryCrashLogFaultSection(buffer, last, "trailer", [](CrashLog *self, format_target &buffer) {
		self->LogCrashTrailer(buffer);
	});

	buffer = format_to_fixed_z::format_to(buffer, last, "*** End of OpenTTD Crash Report ***\n");
	*buffer = '\0';
	this->StopCrashLogFaultHandler();
	return buffer;
}

static void LogDesyncDateHeader(format_target &buffer)
{
	extern uint32_t _frame_counter;

	buffer.format("In game date: {}-{:02}-{:02} ({}, {}) (DL: {}), {:08X}\n",
			EconTime::CurYear().base(), EconTime::CurMonth() + 1, EconTime::CurDay(), EconTime::CurDateFract(), TickSkipCounter(), DayLengthFactor(), _frame_counter);
	buffer.format("Calendar date: {}-{:02}-{:02} ({}, {})\n", CalTime::CurYear().base(), CalTime::CurMonth() + 1, CalTime::CurDay(), CalTime::CurDateFract(), CalTime::CurSubDateFract());
	LogGameLoadDateTimes(buffer);
	if (_networking && !_network_server) {
		extern EconTime::Date _last_sync_date;
		extern EconTime::DateFract _last_sync_date_fract;
		extern uint8_t _last_sync_tick_skip_counter;
		extern uint32_t _last_sync_frame_counter;

		EconTime::YearMonthDay ymd = EconTime::ConvertDateToYMD(_last_sync_date);
		buffer.format("Last sync at: {}-{:02}-{:02} ({}, {}), {:08X}\n",
				ymd.year.base(), ymd.month + 1, ymd.day, _last_sync_date_fract, _last_sync_tick_skip_counter, _last_sync_frame_counter);
	}
}

/**
 * Fill the crash log buffer with all data of a desync event.
 * @param buffer The output buffer.
 */
void CrashLog::FillDesyncCrashLog(format_target &buffer, const DesyncExtraInfo &info) const
{
	buffer.format("*** OpenTTD Multiplayer {} Desync Report ***\n\n", _network_server ? "Server" : "Client");

	UTCTime::FormatTo(buffer, "Desync at: %Y-%m-%d %H:%M:%S (UTC)\n");

	if (!_network_server && info.flags) {
		auto flag_check = [&](DesyncExtraInfo::Flags flag, const char *str) {
			return info.flags & flag ? str : "";
		};
		buffer.format("Flags: {}{}\n",
				flag_check(DesyncExtraInfo::DEIF_RAND, "R"),
				flag_check(DesyncExtraInfo::DEIF_STATE, "S"));
	}
	if (_network_server && !info.desync_frame_info.empty()) {
		buffer.format("{}\n", info.desync_frame_info);
	}
	LogDesyncDateHeader(buffer);

	if (info.client_id >= 0) {
		buffer.format("Client #{}, \"{}\"\n", info.client_id, info.client_name != nullptr ? info.client_name : "");
	}
	buffer.push_back('\n');

	this->LogOpenTTDVersion(buffer);
	this->LogOSVersion(buffer);
	this->LogCompiler(buffer);
	this->LogOSVersionDetail(buffer);
	this->LogConfiguration(buffer);
	this->LogLibraries(buffer);
	this->LogSettings(buffer);
	this->LogCommandLog(buffer);
	this->LogGamelog(buffer);
	this->LogRecentNews(buffer);

	DumpDesyncMsgLog(buffer);

	bool have_cache_log = false;
	CheckCaches(true, [&](std::string_view str) {
		if (!have_cache_log) buffer.append("CheckCaches:\n");
		buffer.format("  {}\n", str);
		have_cache_log = true;
		LogDesyncMsg(fmt::format("[prev desync]: {}", str));
	});
	if (have_cache_log) buffer.push_back('\n');

	buffer.format("*** End of OpenTTD Multiplayer {} Desync Report ***\n", _network_server ? "Server" : "Client");
}

/**
 * Fill the crash log buffer with all data of an inconsistency event.
 * @param buffer The output buffer.
 */
void CrashLog::FillInconsistencyLog(format_target &buffer, const InconsistencyExtraInfo &info) const
{
	buffer.append("*** OpenTTD Inconsistency Report ***\n\n");

	UTCTime::FormatTo(buffer, "Inconsistency at: %Y-%m-%d %H:%M:%S (UTC)\n");

#if !defined(DISABLE_SCOPE_INFO)
	WriteScopeLog(buffer);
#endif

	LogDesyncDateHeader(buffer);
	buffer.push_back('\n');

	this->LogOpenTTDVersion(buffer);
	this->LogOSVersion(buffer);
	this->LogCompiler(buffer);
	this->LogOSVersionDetail(buffer);
	this->LogConfiguration(buffer);
	this->LogLibraries(buffer);
	this->LogSettings(buffer);
	this->LogCommandLog(buffer);
	this->LogGamelog(buffer);
	this->LogRecentNews(buffer);

	DumpDesyncMsgLog(buffer);

	if (!info.check_caches_result.empty()) {
		buffer.append("CheckCaches:\n");
		for (const std::string &str : info.check_caches_result) {
			buffer.format("  {}\n", str);
		}
	}

	buffer.append("*** End of OpenTTD Inconsistency Report ***\n");
}

/**
 * Fill the version info log buffer.
 * @param buffer The output buffer.
 */
void CrashLog::FillVersionInfoLog(format_target &buffer) const
{
	buffer.append("*** OpenTTD Version Info Report ***\n\n");

	this->LogOpenTTDVersion(buffer);
	this->LogOSVersion(buffer);
	this->LogCompiler(buffer);
	this->LogOSVersionDetail(buffer);
	this->LogLibraries(buffer);
	this->LogPlugins(buffer);

	buffer.append("*** End of OpenTTD Version Info Report ***\n");
}

/**
 * Prepare the log file name.
 * @param data          The data to write to the disk.
 * @param filename      Output for the filename of the written file.
 * @param filename_last The last position in the filename buffer.
 */
void CrashLog::PrepareLogFileName(char *filename, const char *filename_last, const char *name) const
{
	format_to_fixed_z::format_to(filename, filename_last, "{}{}.log", _personal_dir, name);
}

/**
 * Write the crash log to a file.
 * @note On success the filename will be filled with the full path of the
 *       crash log file. Make sure filename is at least \c MAX_PATH big.
 * @param data          The data to write to the disk.
 * @param filename      Output for the filename of the written file.
 * @param filename_last The last position in the filename buffer.
 * @param keep_file_open If non-nullptr, store the FILE * instead of closing it after writing.
 * @return true when the crash log was successfully written.
 */
bool CrashLog::WriteGeneralLogFile(std::string_view data, char *filename, const char *filename_last, const char *name, std::optional<FileHandle> *keep_file_open) const
{
	this->PrepareLogFileName(filename, filename_last, name);

	auto file = FioFOpenFile(filename, "w", NO_DIRECTORY);
	if (!file.has_value()) return false;

	size_t written = (!data.empty()) ? fwrite(data.data(), 1, data.size(), *file) : 0;

	if (keep_file_open != nullptr) {
		*keep_file_open = std::move(file);
	}
	return data.size() == written;
}

void CrashLog::FlushCrashLogBuffer(const char *end)
{
	if (this->crash_buffer_write == nullptr) return;

	size_t len = ttd_strnlen(this->crash_buffer_write, end - this->crash_buffer_write);
	if (len == 0) return;

	this->WriteToLogFile(std::string_view(this->crash_buffer_write, len));
#if !defined(_WIN32)
	this->WriteToStdout(std::string_view(this->crash_buffer_write, len));
#endif

	this->crash_buffer_write += len;
}

void CrashLog::CloseCrashLogFile(const char *end)
{
	this->FlushCrashLogBuffer(end);
	this->CloseLogFile();
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

		format_to_fixed_z::format_to(filename, filename_last, "{}{}.sav", _personal_dir, name);

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
		format_to_fixed_z::format_to(filename, filename_last, "{}{}.sav", _personal_dir, name);

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
	if (res) strecpy(filename, _full_screenshot_path.c_str(), filename_last);
	return res;
}

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
 */
void CrashLog::MakeCrashLog(char *buffer, const char *last)
{
	/* Don't keep looping logging crashes. */
	if (CrashLog::HaveAlreadyCrashed()) return;
	CrashLog::RegisterCrashed();

	char name_buffer[64];
	{
		format_to_fixed_z buf(name_buffer, lastof(name_buffer));
		buf.append("crash-");
		UTCTime::FormatTo(buf, "%Y%m%dT%H%M%SZ");
		buf.finalise();
	}

#ifdef DEDICATED
	if (!_settings_client.gui.keep_all_autosave) {
		extern FiosNumberedSaveName &GetAutoSaveFiosNumberedSaveName();
		FiosNumberedSaveName &autosave = GetAutoSaveFiosNumberedSaveName();
		int num = autosave.GetLastNumber();
		if (num >= 0) {
			char old_file[MAX_PATH];
			{
				format_to_fixed_z buf(old_file, lastof(old_file));
				buf.append(autosave.GetSavePath());
				autosave.FilenameUsingNumber(buf, num, "");
				buf.finalise();
			}
			char new_file[MAX_PATH];
			{
				char save_suffix[MAX_PATH];
				format_to_fixed_z::format_to(save_suffix, lastof(save_suffix), "-({})", name_buffer);

				format_to_fixed_z buf(new_file, lastof(new_file));
				buf.append(autosave.GetSavePath());
				autosave.FilenameUsingNumber(buf, num, save_suffix);
				buf.finalise();
			}

			extern bool FioCopyFile(const char *old_name, const char *new_name);
			if (FioCopyFile(old_file, new_file)) {
				format_buffer_fixed<1024> buf;
				buf.format("Saving copy of last autosave: {} -> {}\n\n", old_file, new_file);
				this->WriteToStdout(buf);
			}
		}
	}
#endif

	if (!VideoDriver::EmergencyAcquireGameLock(20, 2)) {
		this->WriteToStdout("Failed to acquire gamelock before filling crash log\n\n");
	}

	this->WriteToStdout("Crash encountered, generating crash log...\n");

	this->WriteToStdout("Writing crash log to disk...\n");
	this->PrepareLogFileName(this->crashlog_filename, lastof(this->crashlog_filename), name_buffer);
	bool bret = this->OpenLogFile(this->crashlog_filename);
	if (bret) {
		format_buffer_fixed<1024> buf;
		buf.format("Crash log written to {}. Please add this file to any bug reports.\n\n", this->crashlog_filename);
		this->WriteToStdout(buf);
	} else {
		this->WriteToStdout("Writing crash log failed. Please attach the output above to any bug reports.\n\n");
		strecpy(this->crashlog_filename, "(failed to write crash log)");
	}
	this->crash_buffer_write = buffer;

	char *end = this->FillCrashLog(buffer, last);
	this->CloseCrashLogFile(end);
	this->WriteToStdout("Crash log generated.\n\n");


	/* Don't mention writing crash dumps because not all platforms support it. */
	int dret = this->WriteCrashDump(this->crashdump_filename, lastof(this->crashdump_filename));
	if (dret < 0) {
		this->WriteToStdout("Writing crash dump failed.\n\n");
		strecpy(this->crashdump_filename, "(failed to write crash dump)");
	} else if (dret > 0) {
		format_buffer_fixed<1024> buf;
		buf.format("Crash dump written to {}. Please add this file to any bug reports.\n\n", this->crashdump_filename);
		this->WriteToStdout(buf);
	}

	SetScreenshotAuxiliaryText("Crash Log", buffer);
	_savegame_DBGL_data = buffer;
	_save_DBGC_data = true;

	if (!VideoDriver::EmergencyAcquireGameLock(1000, 5)) {
		this->WriteToStdout("Failed to acquire gamelock before writing crash savegame and screenshot, proceeding without lock as current owner is probably stuck\n\n");
	}

	CrashLog::MakeCrashSavegameAndScreenshot(name_buffer);
}

void CrashLog::MakeCrashLogWithStackBuffer()
{
	char buffer[65536 * 4];
	this->MakeCrashLog(buffer, lastof(buffer));
}

/**
 * Makes a desync crash log, writes it to a file and then subsequently tries
 * to make a crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
void CrashLog::MakeDesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info) const
{
	char filename[MAX_PATH];

	const char *mode = _network_server ? "server" : "client";

	char name_buffer[64];
	{
		format_to_fixed_z buf(name_buffer, lastof(name_buffer));
		buf.format("desync-{}-", mode);
		UTCTime::FormatTo(buf, "%Y%m%dT%H%M%SZ");
		buf.finalise();
	}

	printf("Desync encountered (%s), generating desync log...\n", mode);

	format_buffer buffer;
	this->FillDesyncCrashLog(buffer, info);

	if (log_out) log_out->assign(buffer);

	if (log_in != nullptr && !log_in->empty()) {
		buffer.push_back('\n');
		buffer.append(*log_in);
	}

	bool bret = this->WriteGeneralLogFile(buffer, filename, lastof(filename), name_buffer, info.log_file);
	if (bret) {
		printf("Desync log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing desync log failed.\n\n");
	}

	if (info.defer_savegame_write != nullptr) {
		info.defer_savegame_write->name_buffer = name_buffer;
	} else {
		this->WriteDesyncSavegame(buffer.c_str(), name_buffer);
	}
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
void CrashLog::MakeInconsistencyLog(const InconsistencyExtraInfo &info) const
{
	char filename[MAX_PATH];

	char name_buffer[64];
	{
		format_to_fixed_z buf(name_buffer, lastof(name_buffer));
		buf.append("inconsistency-");
		UTCTime::FormatTo(buf, "%Y%m%dT%H%M%SZ");
		buf.finalise();
	}

	printf("Inconsistency encountered, generating diagnostics log...\n");

	format_buffer buffer;
	this->FillInconsistencyLog(buffer, info);

	bool bret = this->WriteGeneralLogFile(buffer, filename, lastof(filename), name_buffer);
	if (bret) {
		printf("Inconsistency log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing inconsistency log failed.\n\n");
	}

	_savegame_DBGL_data = buffer.c_str();
	_save_DBGC_data = true;
	bret = this->WriteDiagnosticSavegame(filename, lastof(filename), name_buffer);
	if (bret) {
		printf("info savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", filename);
	} else {
		printf("Writing inconsistency savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
	}
	_savegame_DBGL_data = nullptr;
	_save_DBGC_data = false;
}

/**
 * Makes a crash dump and crash savegame. It uses DEBUG to write
 * information like paths to the console.
 * @return true when everything is made successfully.
 */
void CrashLog::MakeCrashSavegameAndScreenshot(const char *name_buffer)
{
	printf("Writing crash savegame...\n");
	bool bret = this->WriteSavegame(this->savegame_filename, lastof(this->savegame_filename), name_buffer);
	if (bret) {
		printf("Crash savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", this->savegame_filename);
	} else {
		printf("Writing crash savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
		strecpy(this->savegame_filename, "(failed to write crash savegame)");
	}

	printf("Writing crash screenshot...\n");
	bret = this->WriteScreenshot(this->screenshot_filename, lastof(this->screenshot_filename), name_buffer);
	if (bret) {
		printf("Crash screenshot written to %s. Please add this file to any bug reports.\n\n", this->screenshot_filename);
	} else {
		printf("Writing crash screenshot failed.\n\n");
		strecpy(this->screenshot_filename, "(failed to write crash screenshot)");
	}

	this->SendSurvey();
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

sym_bfd_obj::~sym_bfd_obj()
{
	free(this->syms);
	if (this->abfd != nullptr) bfd_close(this->abfd);
}

static void find_address_in_section(bfd *abfd, asection *section, void *data)
{
	sym_info_bfd *info = static_cast<sym_info_bfd *>(data);
	if (info->found) return;

	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0) return;

	bfd_vma addr = info->addr + info->image_base;
	bfd_vma vma = bfd_get_section_vma(abfd, section);
	bfd_size_type size = get_bfd_section_size(abfd, section);

	if (addr < vma || addr >= vma + size) return;

	info->found = bfd_find_nearest_line(abfd, section, info->syms, addr - vma,
			&(info->file_name), &(info->function_name), &(info->line));

	if (info->found && info->function_name) {
		for (long i = 0; i < info->sym_count; i++) {
			asymbol *sym = info->syms[i];
			if (sym->flags & (BSF_LOCAL | BSF_GLOBAL) && strcmp(sym->name, info->function_name) == 0) {
				info->function_addr = sym->value + vma;
			}
		}
	} else if (info->found) {
		bfd_vma target = addr - vma;
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

void lookup_addr_bfd(const char *obj_file_name, sym_bfd_obj_cache &bfdc, sym_info_bfd &info)
{
	auto res = bfdc.cache.try_emplace(obj_file_name);
	sym_bfd_obj &obj = res.first->second;
	if (res.second) {
		/* New sym_bfd_obj */
		obj.abfd = bfd_openr(obj_file_name, nullptr);

		if (obj.abfd == nullptr) return;

		if (!bfd_check_format(obj.abfd, bfd_object) || (bfd_get_file_flags(obj.abfd) & HAS_SYMS) == 0) return;

		unsigned int size;
		obj.sym_count = bfd_read_minisymbols(obj.abfd, false, (void**) &(obj.syms), &size);
		if (obj.sym_count <= 0) {
			obj.sym_count = bfd_read_minisymbols(obj.abfd, true, (void**) &(obj.syms), &size);
		}
		if (obj.sym_count <= 0) return;

		obj.usable = true;

#if defined(__MINGW32__)
		/* Handle Windows PE relocation.
		 * libbfd sections (and thus symbol addresses) are relative to the image base address (i.e. absolute).
		 * Due to relocation/ASLR/etc the module base address in memory may not match the image base address.
		 * Instead of using the absolute addresses, expect the inputs here to be relative to the module base address
		 * in memory, which is easy to get.
		 * The original image base address is very awkward to get, but as it's always the same, just hard-code it
		 * via the expected .text section address here. */
#ifdef _M_AMD64
		asection *section = bfd_get_section_by_name(obj.abfd, ".text");
		if (section != nullptr && section->vma == 0x140001000) {
			obj.image_base = 0x140000000;
		}
#elif defined(_M_IX86)
		asection *section = bfd_get_section_by_name(obj.abfd, ".text");
		if (section != nullptr && section->vma == 0x401000) {
			obj.image_base = 0x400000;
		}
#endif
		if (obj.image_base == 0) obj.usable = false;
#endif
	}

	if (!obj.usable) return;

	info.abfd = obj.abfd;
	info.image_base = obj.image_base;
	info.syms = obj.syms;
	info.sym_count = obj.sym_count;

	bfd_map_over_sections(info.abfd, find_address_in_section, &info);
}
#endif
