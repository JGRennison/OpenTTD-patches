/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc_gui.cpp GUIs for a number of misc windows. */

#include "stdafx.h"
#include "debug.h"
#include "landscape.h"
#include "landscape_cmd.h"
#include "error.h"
#include "gui.h"
#include "gfx_layout.h"
#include "command_func.h"
#include "company_func.h"
#include "town.h"
#include "string_func.h"
#include "company_base.h"
#include "texteff.hpp"
#include "strings_func.h"
#include "window_func.h"
#include "querystring_gui.h"
#include "core/geometry_func.hpp"
#include "newgrf_debug.h"
#include "zoom_func.h"
#include "tunnelbridge_map.h"
#include "viewport_type.h"
#include "guitimer_func.h"
#include "viewport_func.h"
#include "rev.h"
#include "core/backup_type.hpp"
#include "pathfinder/water_regions.h"
#include "strings_internal.h"

#include "widgets/misc_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/** Method to open the OSK. */
enum OskActivation : uint8_t {
	OSKA_DISABLED,           ///< The OSK shall not be activated at all.
	OSKA_DOUBLE_CLICK,       ///< Double click on the edit box opens OSK.
	OSKA_SINGLE_CLICK,       ///< Single click after focus click opens OSK.
	OSKA_IMMEDIATELY,        ///< Focusing click already opens OSK.
};


static constexpr NWidgetPart _nested_land_info_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetStringTip(STR_LAND_AREA_INFORMATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_LI_LOCATION), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_LAND_AREA_INFORMATION_LOCATION_TOOLTIP),
		NWidget(WWT_DEBUGBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_LI_BACKGROUND), EndContainer(),
};

static WindowDesc _land_info_desc(__FILE__, __LINE__,
	WDP_AUTO, nullptr, 0, 0,
	WC_LAND_INFO, WC_NONE,
	{},
	_nested_land_info_widgets
);

class LandInfoWindow : public Window {
	StringList  landinfo_data;    ///< Info lines to show.
	std::string cargo_acceptance; ///< Centered multi-line string for cargo acceptance.

public:
	TileIndex tile;

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_LI_BACKGROUND) return;

		Rect ir = r.Shrink(WidgetDimensions::scaled.frametext);
		for (size_t i = 0; i < this->landinfo_data.size(); i++) {
			DrawString(ir, this->landinfo_data[i], i == 0 ? TC_LIGHT_BLUE : TC_FROMSTRING, SA_HOR_CENTER);
			ir.top += GetCharacterHeight(FS_NORMAL) + (i == 0 ? WidgetDimensions::scaled.vsep_wide : WidgetDimensions::scaled.vsep_normal);
		}

		if (!this->cargo_acceptance.empty()) {
			SetDParamStr(0, this->cargo_acceptance);
			DrawStringMultiLine(ir, STR_JUST_RAW_STRING, TC_FROMSTRING, SA_CENTER);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_LI_BACKGROUND) return;

		size.height = WidgetDimensions::scaled.frametext.Vertical();
		for (size_t i = 0; i < this->landinfo_data.size(); i++) {
			uint width = GetStringBoundingBox(this->landinfo_data[i]).width + WidgetDimensions::scaled.frametext.Horizontal();
			size.width = std::max(size.width, width);

			size.height += GetCharacterHeight(FS_NORMAL) + (i == 0 ? WidgetDimensions::scaled.vsep_wide : WidgetDimensions::scaled.vsep_normal);
		}

		if (!this->cargo_acceptance.empty()) {
			uint width = GetStringBoundingBox(this->cargo_acceptance).width + WidgetDimensions::scaled.frametext.Horizontal();
			size.width = std::max(size.width, std::min(static_cast<uint>(ScaleGUITrad(300)), width));
			SetDParamStr(0, cargo_acceptance);
			size.height += GetStringHeight(STR_JUST_RAW_STRING, size.width - WidgetDimensions::scaled.frametext.Horizontal());
		}
	}

	LandInfoWindow(TileIndex tile) : Window(_land_info_desc), tile(tile)
	{
		this->InitNested();

#if defined(_DEBUG)
#	define LANDINFOD_LEVEL 0
#else
#	define LANDINFOD_LEVEL 1
#endif
		if (GetDebugLevel(DebugLevelID::misc) >= LANDINFOD_LEVEL) {
			Debug(misc, LANDINFOD_LEVEL, "TILE: {:#x} ({},{})", tile, TileX(tile), TileY(tile));
			if (IsTunnelTile(tile)) {
				Debug(misc, LANDINFOD_LEVEL, "tunnel pool size: {}", (uint)Tunnel::GetPoolSize());
				Debug(misc, LANDINFOD_LEVEL, "index: {:#x}"        , Tunnel::GetByTile(tile)->index);
				Debug(misc, LANDINFOD_LEVEL, "north tile: {:#x}"   , Tunnel::GetByTile(tile)->tile_n);
				Debug(misc, LANDINFOD_LEVEL, "south tile: {:#x}"   , Tunnel::GetByTile(tile)->tile_s);
				Debug(misc, LANDINFOD_LEVEL, "is chunnel: {}"      , Tunnel::GetByTile(tile)->is_chunnel);
			}
			if (IsBridgeTile(tile)) {
				const BridgeSpec *b = GetBridgeSpec(GetBridgeType(tile));
				Debug(misc, LANDINFOD_LEVEL, "bridge: flags: {:X}, ctrl_flags: {:X}", b->flags, b->ctrl_flags);
			}
			if (IsBridgeAbove(tile)) {
				BridgePieceDebugInfo info = GetBridgePieceDebugInfo(tile);
				Debug(misc, LANDINFOD_LEVEL, "bridge above: piece: {}, pillars: {:X}, pillar index: {}", info.piece, info.pillar_flags, info.pillar_index);
			}
			Debug(misc, LANDINFOD_LEVEL, "type   = {:#x}", _m[tile].type);
			Debug(misc, LANDINFOD_LEVEL, "height = {:#x}", _m[tile].height);
			Debug(misc, LANDINFOD_LEVEL, "m1     = {:#x}", _m[tile].m1);
			Debug(misc, LANDINFOD_LEVEL, "m2     = {:#x}", _m[tile].m2);
			Debug(misc, LANDINFOD_LEVEL, "m3     = {:#x}", _m[tile].m3);
			Debug(misc, LANDINFOD_LEVEL, "m4     = {:#x}", _m[tile].m4);
			Debug(misc, LANDINFOD_LEVEL, "m5     = {:#x}", _m[tile].m5);
			Debug(misc, LANDINFOD_LEVEL, "m6     = {:#x}", _me[tile].m6);
			Debug(misc, LANDINFOD_LEVEL, "m7     = {:#x}", _me[tile].m7);
			Debug(misc, LANDINFOD_LEVEL, "m8     = {:#x}", _me[tile].m8);

			PrintWaterRegionDebugInfo(tile);
		}
#undef LANDINFOD_LEVEL
	}

	void OnInit() override
	{
		Town *t = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);

		/* Because build_date is not set yet in every TileDesc, we make sure it is empty */
		TileDesc td;

		td.build_date = CalTime::INVALID_DATE;

		/* Most tiles have only one owner, but
		 *  - drivethrough roadstops can be build on town owned roads (up to 2 owners) and
		 *  - roads can have up to four owners (railroad, road, tram, 3rd-roadtype "highway").
		 */
		td.owner_type[0] = STR_LAND_AREA_INFORMATION_OWNER; // At least one owner is displayed, though it might be "N/A".
		td.owner_type[1] = STR_NULL;       // STR_NULL results in skipping the owner
		td.owner_type[2] = STR_NULL;
		td.owner_type[3] = STR_NULL;
		td.owner[0] = OWNER_NONE;
		td.owner[1] = OWNER_NONE;
		td.owner[2] = OWNER_NONE;
		td.owner[3] = OWNER_NONE;

		td.station_class = STR_NULL;
		td.station_name = STR_NULL;
		td.airport_class = STR_NULL;
		td.airport_name = STR_NULL;
		td.airport_tile_name = STR_NULL;
		td.railtype = STR_NULL;
		td.railtype2 = STR_NULL;
		td.rail_speed = 0;
		td.rail_speed2 = 0;
		td.roadtype = STR_NULL;
		td.road_speed = 0;
		td.tramtype = STR_NULL;
		td.tram_speed = 0;
		td.town_can_upgrade = std::nullopt;

		td.grf = nullptr;

		CargoArray acceptance{};
		AddAcceptedCargo(tile, acceptance, nullptr);
		GetTileDesc(tile, &td);

		this->landinfo_data.clear();

		/* Tiletype */
		SetDParam(0, td.dparam[0]);
		SetDParam(1, td.dparam[1]);
		SetDParam(2, td.dparam[2]);
		SetDParam(3, td.dparam[3]);
		this->landinfo_data.push_back(GetString(td.str));

		/* Up to four owners */
		for (uint i = 0; i < 4; i++) {
			if (td.owner_type[i] == STR_NULL) continue;

			SetDParam(0, STR_LAND_AREA_INFORMATION_OWNER_N_A);
			if (td.owner[i] != OWNER_NONE && td.owner[i] != OWNER_WATER) SetDParamsForOwnedBy(td.owner[i], tile);
			this->landinfo_data.push_back(GetString(td.owner_type[i]));
		}

		/* Cost to clear/revenue when cleared */
		StringID str = STR_LAND_AREA_INFORMATION_COST_TO_CLEAR_N_A;
		Company *c = Company::GetIfValid(_local_company);
		if (c != nullptr) {
			assert(_current_company == _local_company);
			CommandCost costclear = Command<CMD_LANDSCAPE_CLEAR>::Do(DC_QUERY_COST, tile);
			if (costclear.Succeeded()) {
				Money cost = costclear.GetCost();
				if (cost < 0) {
					cost = -cost; // Negate negative cost to a positive revenue
					str = STR_LAND_AREA_INFORMATION_REVENUE_WHEN_CLEARED;
				} else {
					str = STR_LAND_AREA_INFORMATION_COST_TO_CLEAR;
				}
				SetDParam(0, cost);
			}
		}
		this->landinfo_data.push_back(GetString(str));

		/* Location */
		SetDParam(0, TileX(tile));
		SetDParam(1, TileY(tile));
		SetDParam(2, GetTileZ(tile));
		this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_LANDINFO_COORDS));

		/* Tile index */
		SetDParam(0, tile);
		SetDParam(1, tile);
		this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_LANDINFO_INDEX));

		/* Local authority */
		SetDParam(0, STR_LAND_AREA_INFORMATION_LOCAL_AUTHORITY_NONE);
		if (t != nullptr) {
			SetDParam(0, STR_TOWN_NAME);
			SetDParam(1, t->index);
		}
		this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_LOCAL_AUTHORITY));

		/* Build date */
		if (td.build_date != CalTime::INVALID_DATE) {
			SetDParam(0, td.build_date);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_BUILD_DATE));
		}

		/* Station class */
		if (td.station_class != STR_NULL) {
			SetDParam(0, td.station_class);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_STATION_CLASS));
		}

		/* Station type name */
		if (td.station_name != STR_NULL) {
			SetDParam(0, td.station_name);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_STATION_TYPE));
		}

		/* Airport class */
		if (td.airport_class != STR_NULL) {
			SetDParam(0, td.airport_class);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_AIRPORT_CLASS));
		}

		/* Airport name */
		if (td.airport_name != STR_NULL) {
			SetDParam(0, td.airport_name);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_AIRPORT_NAME));
		}

		/* Airport tile name */
		if (td.airport_tile_name != STR_NULL) {
			SetDParam(0, td.airport_tile_name);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_AIRPORTTILE_NAME));
		}

		/* Rail type name */
		if (td.railtype != STR_NULL) {
			SetDParam(0, td.railtype);
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_RAIL_TYPE));
		}

		/* Rail speed limit */
		if (td.rail_speed != 0) {
			SetDParam(0, PackVelocity(td.rail_speed, VEH_TRAIN));
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_RAIL_SPEED_LIMIT));
		}

		/* 2nd Rail type name */
		if (td.railtype2 != STR_NULL) {
			SetDParam(0, td.railtype2);
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_RAIL_TYPE));
		}

		/* 2nd Rail speed limit */
		if (td.rail_speed2 != 0) {
			SetDParam(0, td.rail_speed2);
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_RAIL_SPEED_LIMIT));
		}

		/* Road type name */
		if (td.roadtype != STR_NULL) {
			SetDParam(0, td.roadtype);
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_ROAD_TYPE));
		}

		/* Road speed limit */
		if (td.road_speed != 0) {
			SetDParam(0, PackVelocity(td.road_speed, VEH_ROAD));
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_ROAD_SPEED_LIMIT));
		}

		/* Tram type name */
		if (td.tramtype != STR_NULL) {
			SetDParam(0, td.tramtype);
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_TRAM_TYPE));
		}

		/* Tram speed limit */
		if (td.tram_speed != 0) {
			SetDParam(0, PackVelocity(td.tram_speed, VEH_ROAD));
			this->landinfo_data.push_back(GetString(STR_LANG_AREA_INFORMATION_TRAM_SPEED_LIMIT));
		}

		/* Tile protection status */
		if (td.town_can_upgrade.has_value()) {
			this->landinfo_data.push_back(GetString(td.town_can_upgrade.value() ? STR_LAND_AREA_INFORMATION_TOWN_CAN_UPGRADE : STR_LAND_AREA_INFORMATION_TOWN_CANNOT_UPGRADE));
		}

		/* NewGRF name */
		if (td.grf != nullptr) {
			SetDParamStr(0, td.grf);
			this->landinfo_data.push_back(GetString(STR_LAND_AREA_INFORMATION_NEWGRF_NAME));
		}

		/* Cargo acceptance is displayed in a extra multiline */
		auto line = BuildCargoAcceptanceString(acceptance, STR_LAND_AREA_INFORMATION_CARGO_ACCEPTED);
		if (line.has_value()) {
			this->cargo_acceptance = std::move(*line);
		} else {
			this->cargo_acceptance.clear();
		}
	}

	bool IsNewGRFInspectable() const override
	{
		return ::IsNewGRFInspectable(GetGrfSpecFeature(this->tile), this->tile.base());
	}

	void ShowNewGRFInspectWindow() const override
	{
		::ShowNewGRFInspectWindow(GetGrfSpecFeature(this->tile), this->tile.base());
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_LI_LOCATION:
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(this->tile);
				} else {
					ScrollMainWindowToTile(this->tile);
				}
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		/* ReInit, "debug" sprite might have changed */
		if (data == 1) this->ReInit();
	}
};

/**
 * Show land information window.
 * @param tile The tile to show information about.
 */
void ShowLandInfo(TileIndex tile)
{
	CloseWindowById(WC_LAND_INFO, 0);
	new LandInfoWindow(tile);
}

static constexpr NWidgetPart _nested_about_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetStringTip(STR_ABOUT_OPENTTD, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY), SetPIP(4, 2, 4),
		NWidget(WWT_LABEL, INVALID_COLOUR), SetStringTip(STR_ABOUT_ORIGINAL_COPYRIGHT),
		NWidget(WWT_LABEL, INVALID_COLOUR), SetStringTip(STR_ABOUT_VERSION),
		NWidget(WWT_FRAME, COLOUR_GREY), SetPadding(0, 5, 1, 5),
			NWidget(WWT_EMPTY, INVALID_COLOUR, WID_A_SCROLLING_TEXT),
		EndContainer(),
		NWidget(WWT_LABEL, INVALID_COLOUR, WID_A_WEBSITE), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_LABEL, INVALID_COLOUR, WID_A_WEBSITE1), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_LABEL, INVALID_COLOUR, WID_A_WEBSITE2), SetStringTip(STR_JUST_RAW_STRING),
		NWidget(WWT_LABEL, INVALID_COLOUR, WID_A_COPYRIGHT), SetStringTip(STR_ABOUT_COPYRIGHT_OPENTTD),
	EndContainer(),
};

static WindowDesc _about_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_GAME_OPTIONS, WC_NONE,
	{},
	_nested_about_widgets
);

static const std::initializer_list<const std::string_view> _credits = {
	"Original design by Chris Sawyer",
	"Original graphics by Simon Foster",
	"",
	"The OpenTTD team (in alphabetical order):",
	"  Matthijs Kooijman (blathijs) - Pathfinder-guru, Debian port (since 0.3)",
	"  Christoph Elsenhans (frosch) - General coding (since 0.6)",
	"  Lo\u00efc Guilloux (glx) - General / Windows Expert (since 0.4.5)",
	"  Koen Bussemaker (Kuhnovic) - General / Ship pathfinder (since 14)",
	"  Charles Pigott (LordAro) - General / Correctness police (since 1.9)",
	"  Michael Lutz (michi_cc) - Path based signals (since 0.7)",
	"  Niels Martin Hansen (nielsm) - Music system, general coding (since 1.9)",
	"  Owen Rudge (orudge) - Forum host, OS/2 port (since 0.1)",
	"  Peter Nelson (peter1138) - Spiritual descendant from NewGRF gods (since 0.4.5)",
	"  Remko Bijker (Rubidium) - Coder and way more (since 0.4.5)",
	"  Patric Stout (TrueBrain) - NoProgrammer (since 0.3), sys op",
	"  Tyler Trahan (2TallTyler) - General / Time Lord (since 13)",
	"",
	"Inactive Developers:",
	"  Grzegorz Duczy\u0144ski (adf88) - General coding (1.7 - 1.8)",
	"  Albert Hofkamp (Alberth) - GUI expert (0.7 - 1.9)",
	"  Jean-Fran\u00e7ois Claeys (Belugas) - GUI, NewGRF and more (0.4.5 - 1.0)",
	"  Bjarni Corfitzen (Bjarni) - MacOSX port, coder and vehicles (0.3 - 0.7)",
	"  Victor Fischer (Celestar) - Programming everywhere you need him to (0.3 - 0.6)",
	"  Ulf Hermann (fonsinchen) - Cargo Distribution (1.3 - 1.6)",
	"  Jaroslav Mazanec (KUDr) - YAPG (Yet Another Pathfinder God) ;) (0.4.5 - 0.6)",
	"  Jonathan Coome (Maedhros) - High priest of the NewGRF Temple (0.5 - 0.6)",
	"  Attila B\u00e1n (MiHaMiX) - Developer WebTranslator 1 and 2 (0.3 - 0.5)",
	"  Ingo von Borstel (planetmaker) - General coding, Support (1.1 - 1.9)",
	"  Zden\u011bk Sojka (SmatZ) - Bug finder and fixer (0.6 - 1.3)",
	"  Jos\u00e9 Soler (Terkhen) - General coding (1.0 - 1.4)",
	"  Christoph Mallon (Tron) - Programmer, code correctness police (0.3 - 0.5)",
	"  Thijs Marinussen (Yexo) - AI Framework, General (0.6 - 1.3)",
	"  Leif Linse (Zuu) - AI/Game Script (1.2 - 1.6)",
	"",
	"Retired Developers:",
	"  Tam\u00e1s Farag\u00f3 (Darkvater) - Ex-Lead coder (0.3 - 0.5)",
	"  Dominik Scherer (dominik81) - Lead programmer, GUI expert (0.3 - 0.3)",
	"  Emil Djupfeld (egladil) - MacOSX (0.4.5 - 0.6)",
	"  Simon Sasburg (HackyKid) - Many bugfixes (0.4 - 0.4.5)",
	"  Ludvig Strigeus (ludde) - Original author of OpenTTD, main coder (0.1 - 0.3)",
	"  Cian Duffy (MYOB) - BeOS port / manual writing (0.1 - 0.3)",
	"  Petr Baudi\u0161 (pasky) - Many patches, NewGRF support (0.3 - 0.3)",
	"  Benedikt Br\u00fcggemeier (skidd13) - Bug fixer and code reworker (0.6 - 0.7)",
	"  Serge Paquet (vurlix) - 2nd contributor after ludde (0.1 - 0.3)",
	"",
	"Special thanks go out to:",
	"  Josef Drexler - For his great work on TTDPatch",
	"  Marcin Grzegorczyk - Track foundations and for describing TTD internals",
	"  Stefan Mei\u00dfner (sign_de) - For his work on the console",
	"  Mike Ragsdale - OpenTTD installer",
	"  Christian Rosentreter (tokai) - MorphOS / AmigaOS port",
	"  Richard Kempton (richK) - additional airports, initial TGP implementation",
	"  Alberto Demichelis - Squirrel scripting language \u00a9 2003-2008",
	"  L. Peter Deutsch - MD5 implementation \u00a9 1999, 2000, 2002",
	"  Michael Blunck - Pre-signals and semaphores \u00a9 2003",
	"  George - Canal/Lock graphics \u00a9 2003-2004",
	"  Andrew Parkhouse (andythenorth) - River graphics",
	"  David Dallaston (Pikka) - Tram tracks",
	"  All Translators - Who made OpenTTD a truly international game",
	"  Bug Reporters - Without whom OpenTTD would still be full of bugs!",
	"",
	"Developer of this patchpack:",
	"  Jonathan G. Rennison (JGR)",
	"",
	"And last but not least:",
	"  Chris Sawyer - For an amazing game!"
};

struct AboutWindow : public Window {
	int text_position;                       ///< The top of the scrolling text
	int line_height;                         ///< The height of a single line
	static const int num_visible_lines = 19; ///< The number of lines visible simultaneously

	static const uint TIMER_INTERVAL = 2100; ///< Scrolling interval, scaled by line text line height. This value chosen to maintain parity: 2100 / GetCharacterHeight(FS_NORMAL) = 150ms
	GUITimer timer;

	AboutWindow() : Window(_about_desc)
	{
		this->InitNested(WN_GAME_OPTIONS_ABOUT);

		this->text_position = this->GetWidget<NWidgetBase>(WID_A_SCROLLING_TEXT)->pos_y + this->GetWidget<NWidgetBase>(WID_A_SCROLLING_TEXT)->current_y;
	}

	void SetStringParameters(WidgetID widget) const override
	{
		if (widget == WID_A_WEBSITE) SetDParamStr(0, "Main project website: https://www.openttd.org");
		if (widget == WID_A_WEBSITE1) SetDParamStr(0, "Patchpack thread: https://www.tt-forums.net/viewtopic.php?f=33&t=73469");
		if (widget == WID_A_WEBSITE2) SetDParamStr(0, "Patchpack Github: https://github.com/JGRennison/OpenTTD-patches");
		if (widget == WID_A_COPYRIGHT) SetDParamStr(0, _openttd_revision_year);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_A_SCROLLING_TEXT) return;

		this->line_height = GetCharacterHeight(FS_NORMAL);

		Dimension d;
		d.height = this->line_height * num_visible_lines;

		d.width = 0;
		for (const auto &str : _credits) {
			d.width = std::max(d.width, GetStringBoundingBox(str).width);
		}
		size = maxdim(size, d);

		/* Set scroll interval based on required speed. To keep scrolling smooth,
		 * the interval is adjusted rather than the distance moved. */
		this->timer.SetInterval(TIMER_INTERVAL / GetCharacterHeight(FS_NORMAL));
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_A_SCROLLING_TEXT) return;

		int y = this->text_position;

		/* Show all scrolling _credits */
		for (const auto &str : _credits) {
			if (y >= r.top + 7 && y < r.bottom - this->line_height) {
				DrawString(r.left, r.right, y, str, TC_BLACK, SA_LEFT | SA_FORCE);
			}
			y += this->line_height;
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		uint count = this->timer.CountElapsed(delta_ms);
		if (count > 0) {
			this->text_position -= count;
			/* If the last text has scrolled start a new from the start */
			if (this->text_position < (int)(this->GetWidget<NWidgetBase>(WID_A_SCROLLING_TEXT)->pos_y - std::size(_credits) * this->line_height)) {
				this->text_position = this->GetWidget<NWidgetBase>(WID_A_SCROLLING_TEXT)->pos_y + this->GetWidget<NWidgetBase>(WID_A_SCROLLING_TEXT)->current_y;
			}
			this->SetWidgetDirty(WID_A_SCROLLING_TEXT);
		}
	}
};

void ShowAboutWindow()
{
	CloseWindowByClass(WC_GAME_OPTIONS);
	new AboutWindow();
}

/**
 * Display estimated costs.
 * @param cost Estimated cost (or income if negative).
 * @param x    X position of the notification window.
 * @param y    Y position of the notification window.
 */
void ShowEstimatedCostOrIncome(Money cost, int x, int y)
{
	StringID msg = STR_MESSAGE_ESTIMATED_COST;

	if (cost < 0) {
		cost = -cost;
		msg = STR_MESSAGE_ESTIMATED_INCOME;
	}
	SetDParam(0, cost);
	ShowErrorMessage(msg, INVALID_STRING_ID, WL_INFO, x, y);
}

/**
 * Display animated income or costs on the map. Does nothing if cost is zero.
 * @param x    World X position of the animation location.
 * @param y    World Y position of the animation location.
 * @param z    World Z position of the animation location.
 * @param cost Estimated cost (or income if negative).
 */
void ShowCostOrIncomeAnimation(int x, int y, int z, Money cost)
{
	if (IsHeadless() || !HasBit(_extra_display_opt, XDO_SHOW_MONEY_TEXT_EFFECTS) || cost == 0) return;

	Point pt = RemapCoords(x, y, z);
	StringID msg = STR_INCOME_FLOAT_COST;

	if (cost < 0) {
		cost = -cost;
		msg = STR_INCOME_FLOAT_INCOME;
	}
	AddTextEffect(msg, pt.x, pt.y, DAY_TICKS, TE_RISING, cost);
}

/**
 * Display animated feeder income.
 * @param x        World X position of the animation location.
 * @param y        World Y position of the animation location.
 * @param z        World Z position of the animation location.
 * @param transfer Estimated feeder income.
 * @param income   Real income from goods being delivered to their final destination.
 */
void ShowFeederIncomeAnimation(int x, int y, int z, Money transfer, Money income)
{
	if (IsHeadless() || !HasBit(_extra_display_opt, XDO_SHOW_MONEY_TEXT_EFFECTS)) return;

	Point pt = RemapCoords(x, y, z);

	if (income == 0) {
		AddTextEffect(STR_FEEDER, pt.x, pt.y, DAY_TICKS, TE_RISING, transfer);
	} else {
		StringID msg = STR_FEEDER_COST;
		if (income < 0) {
			income = -income;
			msg = STR_FEEDER_INCOME;
		}
		AddTextEffect(msg, pt.x, pt.y, DAY_TICKS, TE_RISING, transfer, income);
	}
}

/**
 * Display vehicle loading indicators.
 * @param x       World X position of the animation location.
 * @param y       World Y position of the animation location.
 * @param z       World Z position of the animation location.
 * @param percent Estimated feeder income.
 * @param string  String which is drawn on the map.
 * @return        TextEffectID to be used for future updates of the loading indicators.
 */
TextEffectID ShowFillingPercent(int x, int y, int z, uint8_t percent, StringID string)
{
	Point pt = RemapCoords(x, y, z);

	assert(string != STR_NULL);

	return AddTextEffect(string, pt.x, pt.y, 0, TE_STATIC, percent);
}

/**
 * Update vehicle loading indicators.
 * @param te_id   TextEffectID to be updated.
 * @param string  String which is printed.
 */
void UpdateFillingPercent(TextEffectID te_id, uint8_t percent, StringID string)
{
	assert(string != STR_NULL);

	UpdateTextEffect(te_id, string, percent);
}

/**
 * Hide vehicle loading indicators.
 * @param *te_id TextEffectID which is supposed to be hidden.
 */
void HideFillingPercent(TextEffectID *te_id)
{
	if (*te_id == INVALID_TE_ID) return;

	RemoveTextEffect(*te_id);
	*te_id = INVALID_TE_ID;
}

static constexpr NWidgetPart _nested_tooltips_widgets[] = {
	NWidget(WWT_EMPTY, INVALID_COLOUR, WID_TT_BACKGROUND),
};

static WindowDesc _tool_tips_desc(__FILE__, __LINE__,
	WDP_MANUAL, nullptr, 0, 0, // Coordinates and sizes are not used,
	WC_TOOLTIPS, WC_NONE,
	{WindowDefaultFlag::NoFocus, WindowDefaultFlag::NoClose},
	_nested_tooltips_widgets
);

/** Window for displaying a tooltip. */
struct TooltipsWindow : public Window
{
	StringID string_id;               ///< String to display as tooltip.
	std::vector<StringParameterBackup> params; ///< The string parameters.
	TooltipCloseCondition close_cond; ///< Condition for closing the window.
	std::string buffer;               ///< Text to draw
	int viewport_virtual_left;        ///< Owner viewport state: left
	int viewport_virtual_top;         ///< Owner viewport state: top
	bool delete_next_mouse_loop;      ///< Delete window on the next mouse loop

	TooltipsWindow(Window *parent, StringID str, uint paramcount, TooltipCloseCondition close_tooltip) : Window(_tool_tips_desc)
	{
		this->parent = parent;
		this->string_id = str;
		CopyOutDParam(this->params, paramcount);
		this->close_cond = close_tooltip;
		this->delete_next_mouse_loop = false;
		if (this->params.size() == 0) this->buffer = GetString(str); // Get the text while params are available
		if (close_tooltip == TCC_HOVER_VIEWPORT) {
			this->viewport_virtual_left = parent->viewport->virtual_left;
			this->viewport_virtual_top = parent->viewport->virtual_top;
		}

		this->InitNested();

		this->flags.Reset(WindowFlag::WhiteBorder);
	}

	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, int window_number) override
	{
		/* Find the free screen space between the main toolbar at the top, and the statusbar at the bottom.
		 * Add a fixed distance 2 so the tooltip floats free from both bars.
		 */
		int scr_top = GetMainViewTop() + 2;
		int scr_bot = GetMainViewBottom() - 2;

		Point pt;

		/* Correctly position the tooltip position, watch out for window and cursor size
		 * Clamp value to below main toolbar and above statusbar. If tooltip would
		 * go below window, flip it so it is shown above the cursor */
		pt.y = SoftClamp(_cursor.pos.y + _cursor.total_size.y + _cursor.total_offs.y + 5, scr_top, scr_bot);
		if (pt.y + sm_height > scr_bot) pt.y = std::min(_cursor.pos.y + _cursor.total_offs.y - 5, scr_bot) - sm_height;
		pt.x = sm_width >= _screen.width ? 0 : SoftClamp(_cursor.pos.x - (sm_width >> 1), 0, _screen.width - sm_width);

		return pt;
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_TT_BACKGROUND) return;
		if (this->params.size() == 0) {
			size.width  = std::min<uint>(GetStringBoundingBox(this->buffer).width, ScaleGUITrad(194));
			size.height = GetStringHeight(this->buffer, size.width);
		} else {
			CopyInDParam(this->params);
			size.width  = std::min<uint>(GetStringBoundingBox(this->string_id).width, ScaleGUITrad(194));
			size.height = GetStringHeight(this->string_id, size.width);
		}

		/* Increase slightly to have some space around the box. */
		size.width  += WidgetDimensions::scaled.framerect.Horizontal()  + WidgetDimensions::scaled.fullbevel.Horizontal();
		size.height += WidgetDimensions::scaled.framerect.Vertical()    + WidgetDimensions::scaled.fullbevel.Vertical();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_TT_BACKGROUND) return;
		GfxFillRect(r, PC_BLACK);
		GfxFillRect(r.Shrink(WidgetDimensions::scaled.bevel), PC_LIGHT_YELLOW);

		if (this->params.size() == 0) {
			DrawStringMultiLine(r.Shrink(WidgetDimensions::scaled.framerect).Shrink(WidgetDimensions::scaled.fullbevel), this->buffer, TC_BLACK, SA_CENTER);
		} else {
			CopyInDParam(this->params);
			DrawStringMultiLine(r.Shrink(WidgetDimensions::scaled.framerect).Shrink(WidgetDimensions::scaled.fullbevel), this->string_id, TC_BLACK, SA_CENTER);
		}
	}

	void OnMouseLoop() override
	{
		/* Always close tooltips when the cursor is not in our window. */
		if (!_cursor.in_window || this->delete_next_mouse_loop) {
			this->Close();
			return;
		}

		/* We can show tooltips while dragging tools. These are shown as long as
		 * we are dragging the tool. Normal tooltips work with hover or rmb. */
		switch (this->close_cond) {
			case TCC_RIGHT_CLICK: if (!_right_button_down) this->Close();; break;
			case TCC_HOVER: if (!_mouse_hovering) this->Close();; break;
			case TCC_NONE: break;
			case TCC_NEXT_LOOP: this->delete_next_mouse_loop = true; break;

			case TCC_HOVER_VIEWPORT:
				if (_settings_client.gui.hover_delay_ms == 0) {
					if (!_right_button_down) this->delete_next_mouse_loop = true;
				} else if (!_mouse_hovering) {
					this->Close();
					break;
				}
				if (this->viewport_virtual_left != this->parent->viewport->virtual_left ||
						this->viewport_virtual_top != this->parent->viewport->virtual_top) {
					this->delete_next_mouse_loop = true;
				}
				break;

			case TCC_EXIT_VIEWPORT: {
				Window *w = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);
				if (w == nullptr || IsPtInWindowViewport(w, _cursor.pos.x, _cursor.pos.y) == nullptr) this->Close();
				break;
			}
		}
	}
};

/**
 * Shows a tooltip
 * @param parent The window this tooltip is related to.
 * @param str String to be displayed
 * @param close_tooltip the condition under which the tooltip closes
 * @param paramcount number of params to deal with
 */
void GuiShowTooltips(Window *parent, StringID str, TooltipCloseCondition close_tooltip, uint paramcount)
{
	CloseWindowById(WC_TOOLTIPS, 0);

	if (str == STR_NULL || !_cursor.in_window) return;

	new TooltipsWindow(parent, str, paramcount, close_tooltip);
}

void QueryString::HandleEditBox(Window *w, WidgetID wid)
{
	if (w->IsWidgetGloballyFocused(wid) && this->text.HandleCaret()) {
		w->SetWidgetDirty(wid);

		/* For the OSK also invalidate the parent window */
		if (w->window_class == WC_OSK) w->InvalidateData();
	}
}

static int GetCaretWidth()
{
	return GetCharacterWidth(FS_NORMAL, '_');
}

/**
 * Reposition edit text box rect based on textbuf length can caret position.
 * @param r Initial rect of edit text box.
 * @param tb The Textbuf being processed.
 * @return Updated rect.
 */
static Rect ScrollEditBoxTextRect(Rect r, const Textbuf &tb)
{
	const int linewidth = tb.pixels + GetCaretWidth();
	const int boxwidth = r.Width();
	if (linewidth <= boxwidth) return r;

	/* Extend to cover whole string. This is left-aligned, adjusted by caret position. */
	r = r.WithWidth(linewidth, false);

	/* Slide so that the caret is at the centre unless limited by bounds of the line, i.e. near either end. */
	return r.Translate(-std::clamp(tb.caretxoffs - (boxwidth / 2), 0, linewidth - boxwidth), 0);
}

void QueryString::DrawEditBox(const Window *w, WidgetID wid) const
{
	const NWidgetLeaf *wi = w->GetWidget<NWidgetLeaf>(wid);

	assert((wi->type & WWT_MASK) == WWT_EDITBOX);

	bool rtl = _current_text_dir == TD_RTL;
	Dimension sprite_size = GetScaledSpriteSize(rtl ? SPR_IMG_DELETE_RIGHT : SPR_IMG_DELETE_LEFT);
	int clearbtn_width = sprite_size.width + WidgetDimensions::scaled.imgbtn.Horizontal();

	Rect r = wi->GetCurrentRect();
	Rect cr = r.WithWidth(clearbtn_width, !rtl);
	Rect fr = r.Indent(clearbtn_width, !rtl);

	DrawFrameRect(cr, wi->colour, wi->IsLowered() ? FrameFlag::Lowered : FrameFlags{});
	DrawSpriteIgnorePadding(rtl ? SPR_IMG_DELETE_RIGHT : SPR_IMG_DELETE_LEFT, PAL_NONE, cr, SA_CENTER);
	if (StrEmpty(this->text.GetText())) GfxFillRect(cr.Shrink(WidgetDimensions::scaled.bevel), GetColourGradient(wi->colour, SHADE_DARKER), FILLRECT_CHECKER);

	DrawFrameRect(fr, wi->colour, {FrameFlag::Lowered, FrameFlag::Darkened});
	GfxFillRect(fr.Shrink(WidgetDimensions::scaled.bevel), PC_BLACK);

	fr = fr.Shrink(WidgetDimensions::scaled.framerect);
	/* Limit the drawing of the string inside the widget boundaries */
	DrawPixelInfo dpi;
	if (!FillDrawPixelInfo(&dpi, fr)) return;
	/* Keep coordinates relative to the window. */
	dpi.left += fr.left;
	dpi.top += fr.top;

	AutoRestoreBackup dpi_backup(_cur_dpi, &dpi);

	/* We will take the current widget length as maximum width, with a small
	 * space reserved at the end for the caret to show */
	const Textbuf *tb = &this->text;
	fr = ScrollEditBoxTextRect(fr, *tb);

	/* If we have a marked area, draw a background highlight. */
	if (tb->marklength != 0) GfxFillRect(fr.left + tb->markxoffs, fr.top, fr.left + tb->markxoffs + tb->marklength - 1, fr.bottom, PC_GREY);

	DrawString(fr.left, fr.right, CenterBounds(fr.top, fr.bottom, GetCharacterHeight(FS_NORMAL)), tb->GetText(), TC_YELLOW);
	bool focussed = w->IsWidgetGloballyFocused(wid) || IsOSKOpenedFor(w, wid);
	if (focussed && tb->caret) {
		int caret_width = GetCaretWidth();
		if (rtl) {
			DrawString(fr.right - tb->pixels + tb->caretxoffs - caret_width, fr.right - tb->pixels + tb->caretxoffs, CenterBounds(fr.top, fr.bottom, GetCharacterHeight(FS_NORMAL)), "_", TC_WHITE);
		} else {
			DrawString(fr.left + tb->caretxoffs, fr.left + tb->caretxoffs + caret_width, CenterBounds(fr.top, fr.bottom, GetCharacterHeight(FS_NORMAL)), "_", TC_WHITE);
		}
	}
}

/**
 * Get the current caret position.
 * @param w Window the edit box is in.
 * @param wid Widget index.
 * @return Top-left location of the caret, relative to the window.
 */
Point QueryString::GetCaretPosition(const Window *w, WidgetID wid) const
{
	const NWidgetLeaf *wi = w->GetWidget<NWidgetLeaf>(wid);

	assert((wi->type & WWT_MASK) == WWT_EDITBOX);

	bool rtl = _current_text_dir == TD_RTL;
	Dimension sprite_size = GetScaledSpriteSize(rtl ? SPR_IMG_DELETE_RIGHT : SPR_IMG_DELETE_LEFT);
	int clearbtn_width = sprite_size.width + WidgetDimensions::scaled.imgbtn.Horizontal();

	Rect r = wi->GetCurrentRect().Indent(clearbtn_width, !rtl).Shrink(WidgetDimensions::scaled.framerect);

	/* Clamp caret position to be inside out current width. */
	const Textbuf *tb = &this->text;
	r = ScrollEditBoxTextRect(r, *tb);

	Point pt = {r.left + tb->caretxoffs, r.top};
	return pt;
}

/**
 * Get the bounding rectangle for a range of the query string.
 * @param w Window the edit box is in.
 * @param wid Widget index.
 * @param from Start of the string range.
 * @param to End of the string range.
 * @return Rectangle encompassing the string range, relative to the window.
 */
Rect QueryString::GetBoundingRect(const Window *w, WidgetID wid, const char *from, const char *to) const
{
	const NWidgetLeaf *wi = w->GetWidget<NWidgetLeaf>(wid);

	assert((wi->type & WWT_MASK) == WWT_EDITBOX);

	bool rtl = _current_text_dir == TD_RTL;
	Dimension sprite_size = GetScaledSpriteSize(rtl ? SPR_IMG_DELETE_RIGHT : SPR_IMG_DELETE_LEFT);
	int clearbtn_width = sprite_size.width + WidgetDimensions::scaled.imgbtn.Horizontal();

	Rect r = wi->GetCurrentRect().Indent(clearbtn_width, !rtl).Shrink(WidgetDimensions::scaled.framerect);

	/* Clamp caret position to be inside our current width. */
	const Textbuf *tb = &this->text;
	r = ScrollEditBoxTextRect(r, *tb);

	/* Get location of first and last character. */
	const auto p1 = GetCharPosInString(tb->GetText(), from, FS_NORMAL);
	const auto p2 = from != to ? GetCharPosInString(tb->GetText(), to, FS_NORMAL) : p1;

	return { Clamp(r.left + p1.left, r.left, r.right), r.top, Clamp(r.left + p2.right, r.left, r.right), r.bottom };
}

/**
 * Get the character that is rendered at a position.
 * @param w Window the edit box is in.
 * @param wid Widget index.
 * @param pt Position to test.
 * @return Index of the character position or -1 if no character is at the position.
 */
ptrdiff_t QueryString::GetCharAtPosition(const Window *w, WidgetID wid, const Point &pt) const
{
	const NWidgetLeaf *wi = w->GetWidget<NWidgetLeaf>(wid);

	assert((wi->type & WWT_MASK) == WWT_EDITBOX);

	bool rtl = _current_text_dir == TD_RTL;
	Dimension sprite_size = GetScaledSpriteSize(rtl ? SPR_IMG_DELETE_RIGHT : SPR_IMG_DELETE_LEFT);
	int clearbtn_width = sprite_size.width + WidgetDimensions::scaled.imgbtn.Horizontal();

	Rect r = wi->GetCurrentRect().Indent(clearbtn_width, !rtl).Shrink(WidgetDimensions::scaled.framerect);

	if (!IsInsideMM(pt.y, r.top, r.bottom)) return -1;

	/* Clamp caret position to be inside our current width. */
	const Textbuf *tb = &this->text;
	r = ScrollEditBoxTextRect(r, *tb);

	return ::GetCharAtPosition(tb->GetText(), pt.x - r.left);
}

void QueryString::ClickEditBox(Window *w, Point pt, WidgetID wid, int click_count, bool focus_changed)
{
	const NWidgetLeaf *wi = w->GetWidget<NWidgetLeaf>(wid);

	assert((wi->type & WWT_MASK) == WWT_EDITBOX);

	bool rtl = _current_text_dir == TD_RTL;
	Dimension sprite_size = GetScaledSpriteSize(rtl ? SPR_IMG_DELETE_RIGHT : SPR_IMG_DELETE_LEFT);
	int clearbtn_width = sprite_size.width + WidgetDimensions::scaled.imgbtn.Horizontal();

	Rect cr = wi->GetCurrentRect().WithWidth(clearbtn_width, !rtl);

	if (IsInsideMM(pt.x, cr.left, cr.right)) {
		if (!StrEmpty(this->text.GetText())) {
			this->text.DeleteAll();
			w->HandleButtonClick(wid);
			w->OnEditboxChanged(wid);
		}
		return;
	}

	if (w->window_class != WC_OSK && _settings_client.gui.osk_activation != OSKA_DISABLED &&
		(!focus_changed || _settings_client.gui.osk_activation == OSKA_IMMEDIATELY) &&
		(click_count == 2 || _settings_client.gui.osk_activation != OSKA_DOUBLE_CLICK)) {
		/* Open the OSK window */
		ShowOnScreenKeyboard(w, wid);
	}
}

/**
 * Class for the string query window.
 *
 * @tparam N The number of editboxes to show.
 * @pre N == 1 || N == 2
 */
template <int N = 1>
struct QueryStringWindow : public Window {
	static_assert(N == 1 || N == 2);
	QueryString editboxes[N]; ///< Editboxes.
	StringID window_caption;  ///< Title for the whole query window
	std::string capture_str;  ///< Pre-composed caption string.
	QueryStringFlags flags;   ///< Flags controlling behaviour of the window.
	Dimension warning_size;   ///< How much space to use for the warning text

	/**
	 * Compute the maximum size in bytes of the described editbox.
	 *
	 * @see QueryString::QueryString
	 */
	static uint max_bytes(const QueryEditboxDescription &ed, QueryStringFlags flags)
	{
		return ((flags & QSF_LEN_IN_CHARS) ? MAX_CHAR_LENGTH : 1) * ed.max_size;
	}

	/**
	 * Public constructor.
	 *
	 * This just forwards to the private constructor, because the latter needs to have
	 * a template parameter pack in order to initialize \a editboxes correctly regardless
	 * of the value of \a N.
	 *
	 * For the parameters, see #ShowQueryString.
	 */
	QueryStringWindow(std::span<QueryEditboxDescription, N> ed, StringID window_caption, std::string capture_str, WindowDesc &desc, Window *parent, QueryStringFlags flags)
			: QueryStringWindow(std::make_index_sequence<N>{}, ed, window_caption, capture_str, desc, parent, flags)
	{}

private:
	/**
	 * Private constructor.
	 *
	 * @tparam j (parameter pack) A compile-time sequence of 0 through \a N-1, used to
	 * initialize \a editboxes with the correct number of QueryString objects, even
	 * though #QueryString is neither default- nor copy-constructible.
	 */
	template <std::size_t... j>
	QueryStringWindow(std::index_sequence<j...>, std::span<QueryEditboxDescription, N> ed, StringID window_caption, std::string capture_str, WindowDesc &desc, Window *parent, QueryStringFlags flags)
			: Window(desc),
			editboxes{QueryString(max_bytes(ed[j], flags), ed[j].max_size)...},
			window_caption(window_caption),
			capture_str(std::move(capture_str))
	{
		static_assert(sizeof...(j) == N);

		for (int i = 0; i < N; ++i) {
			if (ed[i].strparams) {
				this->editboxes[i].text.Assign(GetStringWithArgs(ed[i].str, *ed[i].strparams));
			} else {
				this->editboxes[i].text.Assign(GetString(ed[i].str));
			}
		}

		if constexpr (N > 1)
			this->Window::flags.Set(WindowFlag::NoTabFastForward);

		if ((flags & QSF_ACCEPT_UNCHANGED) == 0) {
			for (QueryString &editbox : this->editboxes) {
				editbox.orig = editbox.text.GetText();
			}
		}

		this->querystrings[WID_QS_TEXT] = &this->editboxes[0];
		if constexpr (N > 1) {
			this->querystrings[WID_QS_TEXT2] = &this->editboxes[1];
		}
		for (int i = 0; i < N; ++i) {
			this->editboxes[i].caption = ed[i].caption;
			this->editboxes[i].cancel_button = WID_QS_CANCEL;
			this->editboxes[i].ok_button = WID_QS_OK;
			this->editboxes[i].text.afilter = ed[i].afilter;
		}
		this->flags = flags;

		this->CreateNestedTree();
		if (!this->capture_str.empty()) {
			this->GetWidget<NWidgetCore>(WID_QS_CAPTION)->SetString(STR_JUST_RAW_STRING);
		}
		this->FinishInitNested(WN_QUERY_STRING);
		if constexpr (N > 1) {
			this->GetWidget<NWidgetCore>(WID_QS_LABEL1)->SetString(ed[0].label);
			this->GetWidget<NWidgetCore>(WID_QS_LABEL2)->SetString(ed[1].label);
		}
		this->UpdateWarningStringSize();

		this->parent = parent;

		this->SetFocusedWidget(WID_QS_TEXT);
	}

public:
	void UpdateWarningStringSize()
	{
		if (this->flags & QSF_PASSWORD) {
			assert(this->nested_root->smallest_x > 0);
			this->warning_size.width = this->nested_root->current_x - WidgetDimensions::scaled.frametext.Horizontal() - WidgetDimensions::scaled.framerect.Horizontal();
			this->warning_size.height = GetStringHeight(STR_WARNING_PASSWORD_SECURITY, this->warning_size.width);
			this->warning_size.height += WidgetDimensions::scaled.frametext.Vertical() + WidgetDimensions::scaled.framerect.Vertical();
		} else {
			this->warning_size = Dimension{ 0, 0 };
		}

		this->ReInit();
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_QS_DEFAULT && (this->flags & QSF_ENABLE_DEFAULT) == 0) {
			/* We don't want this widget to show! */
			fill.width = 0;
			resize.width = 0;
			size.width = 0;
		}

		if constexpr (N == 1) {
			if (widget == WID_QS_LABEL1 || widget == WID_QS_LABEL2 || widget == WID_QS_TEXT2) {
				fill.height = 0;
				resize.height = 0;
				size.height = 0;
				fill.width = 0;
				resize.width = 0;
				size.width = 0;
			}
			if (widget == WID_QS_TEXT2) {
				this->GetWidget<NWidgetCore>(widget)->SetPadding(0, 0, 0, 0);
			}
		} else if (widget == WID_QS_LABEL1 || widget == WID_QS_LABEL2) {
			static_assert(N == 2);
			const StringID label1 = this->GetWidget<NWidgetCore>(WID_QS_LABEL1)->GetString();
			const StringID label2 = this->GetWidget<NWidgetCore>(WID_QS_LABEL2)->GetString();
			const auto width1 = GetStringBoundingBox(label1).width;
			const auto width2 = GetStringBoundingBox(label2).width;
			size.width = std::max(width1, width2);
		}

		if (widget == WID_QS_WARNING) {
			size = this->warning_size;
		}
	}

	EventState OnKeyPress(char32_t key, uint16_t keycode) override
	{
		if constexpr (N == 1) {
			return ES_NOT_HANDLED;
		} else if (keycode == WKC_TAB) {
			static_assert(N == 2);
			if (this->GetFocusedTextbuf() == &this->editboxes[1].text) {
				this->SetFocusedWidget(WID_QS_TEXT);
			} else {
				this->SetFocusedWidget(WID_QS_TEXT2);
			}
			return ES_HANDLED;
		} else {
			return ES_NOT_HANDLED;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_QS_WARNING) return;

		if (this->flags & QSF_PASSWORD) {
			DrawStringMultiLine(r.Shrink(WidgetDimensions::scaled.framerect).Shrink(WidgetDimensions::scaled.frametext),
				STR_WARNING_PASSWORD_SECURITY, TC_FROMSTRING, SA_CENTER);
		}
	}

	void SetStringParameters(WidgetID widget) const override
	{
		if (widget == WID_QS_CAPTION) {
			if (!this->capture_str.empty()) {
				SetDParamStr(0, this->capture_str);
			} else {
				SetDParam(0, this->window_caption);
			}
		}
	}

	void OnOk()
	{
		auto has_new_value = [](const QueryString &editbox) -> bool {
			return !editbox.orig.has_value() || editbox.text.GetText() != editbox.orig;
		};
		if (std::ranges::any_of(this->editboxes, has_new_value)) {
			assert(this->parent != nullptr);

			if constexpr (N == 1) {
				this->parent->OnQueryTextFinished(this->editboxes[0].text.GetText());
			} else {
				static_assert(N == 2);
				this->parent->OnQueryTextFinished(this->editboxes[0].text.GetText(), this->editboxes[1].text.GetText());
			}

			for (QueryString &editbox : this->editboxes) {
				editbox.handled = true;
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_QS_DEFAULT:
				for (QueryString &editbox : this->editboxes) {
					editbox.text.DeleteAll();
				}
				[[fallthrough]];

			case WID_QS_OK:
				this->OnOk();
				[[fallthrough]];

			case WID_QS_CANCEL:
				this->Close();
				break;
		}
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		auto has_been_handled = [](const QueryString &editbox) { return editbox.handled; };
		if (!std::ranges::any_of(editboxes, has_been_handled) && this->parent != nullptr) {
			Window *parent = this->parent;
			this->parent = nullptr; // so parent doesn't try to close us again
			parent->OnQueryTextFinished(std::nullopt);
		}
		this->Window::Close();
	}
};

static constexpr NWidgetPart _nested_query_string_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_QS_CAPTION), SetStringTip(STR_JUST_STRING), SetTextStyle(TC_WHITE),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL, NC_BIGFIRST),
			NWidget(WWT_LABEL, INVALID_COLOUR, WID_QS_LABEL1), SetToolTip(STR_NULL), SetPadding(2,2,2,2),
			NWidget(WWT_EDITBOX, COLOUR_GREY, WID_QS_TEXT), SetMinimalSize(256, 0), SetFill(1, 0), SetPadding(2, 2, 2, 2),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_BIGFIRST), // TODO: WID_QS_ROW2
			NWidget(WWT_LABEL, INVALID_COLOUR, WID_QS_LABEL2), SetToolTip(STR_NULL), SetPadding(2,2,2,2),
			NWidget(WWT_EDITBOX, COLOUR_GREY, WID_QS_TEXT2), SetMinimalSize(256, 0), SetFill(1, 0), SetPadding(2, 2, 2, 2),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_QS_WARNING), EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_QS_DEFAULT), SetMinimalSize(87, 12), SetFill(1, 1), SetStringTip(STR_BUTTON_DEFAULT),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_QS_CANCEL), SetMinimalSize(86, 12), SetFill(1, 1), SetStringTip(STR_BUTTON_CANCEL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_QS_OK), SetMinimalSize(87, 12), SetFill(1, 1), SetStringTip(STR_BUTTON_OK),
	EndContainer(),
};

static WindowDesc _query_string_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_QUERY_STRING, WC_NONE,
	{},
	_nested_query_string_widgets
);

/**
 * Show a query popup window with a textbox in it.
 * @param ed Textbox properties.
 * @param window_caption title bar of the query popup window
 * @param parent pointer to a Window that will handle the events (ok/cancel) of this
 *        window. If nullptr, results are handled by global function HandleOnEditText
 * @param flags various flags, @see QueryStringFlags
 */
void ShowQueryString(const std::span<QueryEditboxDescription, 1> &ed, StringID window_caption, Window *parent, QueryStringFlags flags) {
	CloseWindowByClass(WC_QUERY_STRING);
	new QueryStringWindow<1>(ed, window_caption, {}, _query_string_desc, parent, flags);
}

/** Ditto, but with two textboxes. */
void ShowQueryString(const std::span<QueryEditboxDescription, 2> &ed, StringID window_caption, Window *parent, QueryStringFlags flags)
{
	CloseWindowByClass(WC_QUERY_STRING);
	new QueryStringWindow<2>(ed, window_caption, {}, _query_string_desc, parent, flags);
}

/**
 * Like the above, but with \a ed broken out to separate parameters, and \a caption
 * is used not only as \a window_caption but also for the edited string's caption.
 */
void ShowQueryString(StringID str, StringID caption, uint maxsize, Window *parent, CharSetFilter afilter, QueryStringFlags flags)
{
	QueryEditboxDescription ed[1]{
		{str, {}, caption, INVALID_STRING_ID, afilter, maxsize }
	};
	CloseWindowByClass(WC_QUERY_STRING);
	new QueryStringWindow<1>(ed, caption, {}, _query_string_desc, parent, flags);
}

/**
 * Like the above, but with \a capture_str instead of a \a caption or a \a window_caption.
 *
 * @param capture_str Precomposed string for the query window's title bar. Not used for the editbox's caption.
 */
void ShowQueryString(StringID str, std::string capture_str, uint maxsize, Window *parent, CharSetFilter afilter, QueryStringFlags flags)
{
	QueryEditboxDescription ed[1]{
		{str, {}, STR_EMPTY, INVALID_STRING_ID, afilter, maxsize }
	};
	CloseWindowByClass(WC_QUERY_STRING);
	new QueryStringWindow<1>(ed, {}, std::move(capture_str), _query_string_desc, parent, flags);
}

/**
 * Window used for asking the user a YES/NO question.
 */
struct QueryWindow : public Window {
	QueryCallbackProc *proc; ///< callback function executed on closing of popup. Window* points to parent, bool is true if 'yes' clicked, false otherwise
	std::vector<StringParameterBackup> params; ///< local copy of #_global_string_params
	StringID message;        ///< message shown for query window
	StringID caption;        ///< title of window
	bool precomposed;
	std::string caption_str;
	mutable std::string message_str;

	QueryWindow(WindowDesc &desc, StringID caption, StringID message, Window *parent, QueryCallbackProc *callback) : Window(desc)
	{
		/* Create a backup of the variadic arguments to strings because it will be
		 * overridden pretty often. We will copy these back for drawing */
		this->precomposed = false;
		CopyOutDParam(this->params, 10);
		this->message = message;
		this->proc    = callback;
		this->parent  = parent;

		this->CreateNestedTree();
		this->GetWidget<NWidgetCore>(WID_Q_CAPTION)->SetString(caption);
		this->FinishInitNested(WN_CONFIRM_POPUP_QUERY);
	}

	QueryWindow(WindowDesc &desc, std::string caption, std::string message, Window *parent, QueryCallbackProc *callback) : Window(desc)
	{
		this->precomposed = true;
		this->message = STR_EMPTY;
		this->caption_str = std::move(caption);
		this->message_str = std::move(message);
		this->proc    = callback;
		this->parent  = parent;

		this->CreateNestedTree();
		this->GetWidget<NWidgetCore>(WID_Q_CAPTION)->SetStringTip(STR_JUST_RAW_STRING, STR_NULL);
		this->FinishInitNested(WN_CONFIRM_POPUP_QUERY);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		if (this->proc != nullptr) this->proc(this->parent, false);
		this->Window::Close();
	}

	void FindWindowPlacementAndResize([[maybe_unused]] int def_width, [[maybe_unused]] int def_height) override
	{
		/* Position query window over the calling window, ensuring it's within screen bounds. */
		this->left = SoftClamp(parent->left + (parent->width / 2) - (this->width / 2), 0, _screen.width - this->width);
		this->top = SoftClamp(parent->top + (parent->height / 2) - (this->height / 2), 0, _screen.height - this->height);
		this->SetDirty();
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_Q_CAPTION:
				if (this->precomposed) {
					SetDParamStr(0, this->caption_str.c_str());
				} else {
					CopyInDParam(this->params);
				}
				break;

			case WID_Q_TEXT:
				if (!this->precomposed) {
					CopyInDParam(this->params);
				}
				break;
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_Q_TEXT) return;

		if (!this->precomposed) this->message_str = GetString(this->message);

		size = GetStringMultiLineBoundingBox(this->message_str, size);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_Q_TEXT) return;

		if (!this->precomposed) this->message_str = GetString(this->message);

		DrawStringMultiLine(r, this->message_str, TC_FROMSTRING, SA_CENTER);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_Q_YES: {
				/* in the Generate New World window, clicking 'Yes' causes
				 * CloseNonVitalWindows() to be called - we shouldn't be in a window then */
				QueryCallbackProc *proc = this->proc;
				Window *parent = this->parent;
				/* Prevent the destructor calling the callback function */
				this->proc = nullptr;
				this->Close();
				if (proc != nullptr) {
					proc(parent, true);
					proc = nullptr;
				}
				break;
			}
			case WID_Q_NO:
				this->Close();
				break;
		}
	}

	EventState OnKeyPress(char32_t key, uint16_t keycode) override
	{
		/* ESC closes the window, Enter confirms the action */
		switch (keycode) {
			case WKC_RETURN:
			case WKC_NUM_ENTER:
				if (this->proc != nullptr) {
					this->proc(this->parent, true);
					this->proc = nullptr;
				}
				[[fallthrough]];

			case WKC_ESC:
				this->Close();
				return ES_HANDLED;
		}
		return ES_NOT_HANDLED;
	}
};

static constexpr NWidgetPart _nested_query_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_RED),
		NWidget(WWT_CAPTION, COLOUR_RED, WID_Q_CAPTION), // The caption's string is set in the constructor
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_RED),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_wide, 0), SetPadding(WidgetDimensions::unscaled.modalpopup),
			NWidget(WWT_TEXT, INVALID_COLOUR, WID_Q_TEXT), SetMinimalSize(200, 12),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(WidgetDimensions::unscaled.hsep_indent, WidgetDimensions::unscaled.hsep_indent, WidgetDimensions::unscaled.hsep_indent),
				NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_Q_NO), SetMinimalSize(71, 12), SetFill(1, 1), SetStringTip(STR_QUIT_NO),
				NWidget(WWT_PUSHTXTBTN, COLOUR_YELLOW, WID_Q_YES), SetMinimalSize(71, 12), SetFill(1, 1), SetStringTip(STR_QUIT_YES),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _query_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_CONFIRM_POPUP_QUERY, WC_NONE,
	WindowDefaultFlag::Modal,
	_nested_query_widgets
);

static void RemoveExistingQueryWindow(Window *parent, QueryCallbackProc *callback)
{
	if (!HaveWindowByClass(WC_CONFIRM_POPUP_QUERY)) return;
	for (Window *w : Window::IterateFromBack()) {
		if (w->window_class != WC_CONFIRM_POPUP_QUERY) continue;

		QueryWindow *qw = (QueryWindow *)w;
		if (qw->parent != parent || qw->proc != callback) continue;

		qw->Close();
		break;
	}
}

/**
 * Show a confirmation window with standard 'yes' and 'no' buttons
 * The window is aligned to the centre of its parent.
 * @param caption string shown as window caption
 * @param message string that will be shown for the window
 * @param parent pointer to parent window, if this pointer is nullptr the parent becomes
 * the main window WC_MAIN_WINDOW
 * @param callback callback function pointer to set in the window descriptor
 * @param focus whether the window should be focussed (by default false)
 */
void ShowQuery(StringID caption, StringID message, Window *parent, QueryCallbackProc *callback, bool focus)
{
	if (parent == nullptr) parent = GetMainWindow();

	RemoveExistingQueryWindow(parent, callback);

	QueryWindow *q = new QueryWindow(_query_desc, caption, message, parent, callback);
	if (focus) SetFocusedWindow(q);
}

/**
 * Show a modal confirmation window with standard 'yes' and 'no' buttons
 * The window is aligned to the centre of its parent.
 * @param caption string shown as window caption
 * @param message string that will be shown for the window
 * @param parent pointer to parent window, if this pointer is nullptr the parent becomes
 * the main window WC_MAIN_WINDOW
 * @param callback callback function pointer to set in the window descriptor
 */
void ShowQuery(std::string caption, std::string message, Window *parent, QueryCallbackProc *callback, bool focus)
{
	if (parent == nullptr) parent = GetMainWindow();

	RemoveExistingQueryWindow(parent, callback);

	QueryWindow *q = new QueryWindow(_query_desc, std::move(caption), std::move(message), parent, callback);
	if (focus) SetFocusedWindow(q);
}

static constexpr NWidgetPart _modifier_key_toggle_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetStringTip(STR_MODIFIER_KEY_TOGGLE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MKT_SHIFT), SetMinimalSize(78, 12), SetFill(1, 0),
										SetStringTip(STR_SHIFT_KEY_NAME, STR_MODIFIER_TOGGLE_SHIFT_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_MKT_CTRL), SetMinimalSize(78, 12), SetFill(1, 0),
										SetStringTip(STR_CTRL_KEY_NAME, STR_MODIFIER_TOGGLE_CTRL_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
	EndContainer(),
};

struct ModifierKeyToggleWindow : Window {
	ModifierKeyToggleWindow(WindowDesc &desc, WindowNumber window_number) :
			Window(desc)
	{
		this->InitNested(window_number);
		this->UpdateButtons();
	}

	void Close(int data = 0) override
	{
		_invert_shift = false;
		_invert_ctrl = false;
		this->Window::Close();
	}

	void UpdateButtons()
	{
		this->SetWidgetLoweredState(WID_MKT_SHIFT, _shift_pressed);
		this->SetWidgetLoweredState(WID_MKT_CTRL, _ctrl_pressed);
		this->SetDirty();
	}

	void OnCTRLStateChangeAlways() override
	{
		this->UpdateButtons();
	}

	void OnShiftStateChange() override
	{
		this->UpdateButtons();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_MKT_SHIFT:
				_invert_shift = !_invert_shift;
				UpdateButtons();
				break;

			case WID_MKT_CTRL:
				_invert_ctrl = !_invert_ctrl;
				UpdateButtons();
				break;
		}
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->UpdateButtons();
	}
};

static WindowDesc _modifier_key_toggle_desc(__FILE__, __LINE__,
	WDP_AUTO, "modifier_key_toggle", 0, 0,
	WC_MODIFIER_KEY_TOGGLE, WC_NONE,
	WindowDefaultFlag::NoFocus,
	_modifier_key_toggle_widgets
);

void ShowModifierKeyToggleWindow()
{
	AllocateWindowDescFront<ModifierKeyToggleWindow>(_modifier_key_toggle_desc, 0);
}
