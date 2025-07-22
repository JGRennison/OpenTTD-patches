/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_gui.cpp GUI to show engine related information. */

#include "stdafx.h"
#include "window_gui.h"
#include "engine_base.h"
#include "engine_cmd.h"
#include "command_func.h"
#include "core/string_builder.hpp"
#include "strings_func.h"
#include "engine_gui.h"
#include "articulated_vehicles.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "date_func.h"
#include "rail.h"
#include "road.h"
#include "settings_type.h"
#include "train.h"
#include "roadveh.h"
#include "ship.h"
#include "aircraft.h"
#include "zoom_func.h"

#include "widgets/engine_widget.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Return the category of an engine.
 * @param engine Engine to examine.
 * @return String describing the category ("road veh", "train". "airplane", or "ship") of the engine.
 */
StringID GetEngineCategoryName(EngineID engine)
{
	const Engine *e = Engine::Get(engine);
	switch (e->type) {
		default: NOT_REACHED();
		case VEH_ROAD:
			return GetRoadTypeInfo(e->u.road.roadtype)->strings.new_engine;
		case VEH_AIRCRAFT:          return STR_ENGINE_PREVIEW_AIRCRAFT;
		case VEH_SHIP:              return STR_ENGINE_PREVIEW_SHIP;
		case VEH_TRAIN:
			return GetRailTypeInfo(e->u.rail.railtype)->strings.new_loco;
	}
}

static constexpr NWidgetPart _nested_engine_preview_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE), SetStringTip(STR_ENGINE_PREVIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE),
		NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_wide, 0), SetPadding(WidgetDimensions::unscaled.modalpopup),
			NWidget(WWT_EMPTY, INVALID_COLOUR, WID_EP_QUESTION), SetMinimalSize(300, 0), SetFill(1, 0),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(85, WidgetDimensions::unscaled.hsep_wide, 85),
				NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, WID_EP_NO), SetStringTip(STR_QUIT_NO), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_LIGHT_BLUE, WID_EP_YES), SetStringTip(STR_QUIT_YES), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

struct EnginePreviewWindow : Window {
	int vehicle_space; // The space to show the vehicle image

	EnginePreviewWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);

		/* There is no way to recover the window; so disallow closure via DEL; unless SHIFT+DEL */
		this->flags.Set(WindowFlag::Sticky);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget != WID_EP_QUESTION) return;

		/* Get size of engine sprite, on loan from depot_gui.cpp */
		EngineID engine = static_cast<EngineID>(this->window_number);
		EngineImageType image_type = EIT_PREVIEW;
		uint x, y;
		int x_offs, y_offs;

		const Engine *e = Engine::Get(engine);
		switch (e->type) {
			default: NOT_REACHED();
			case VEH_TRAIN:    GetTrainSpriteSize(   engine, x, y, x_offs, y_offs, image_type); break;
			case VEH_ROAD:     GetRoadVehSpriteSize( engine, x, y, x_offs, y_offs, image_type); break;
			case VEH_SHIP:     GetShipSpriteSize(    engine, x, y, x_offs, y_offs, image_type); break;
			case VEH_AIRCRAFT: GetAircraftSpriteSize(engine, x, y, x_offs, y_offs, image_type); break;
		}
		this->vehicle_space = std::max<int>(ScaleSpriteTrad(40), y - y_offs);

		size.width = std::max(size.width, x + std::abs(x_offs));
		SetDParam(0, GetEngineCategoryName(engine));
		size.height = GetStringHeight(STR_ENGINE_PREVIEW_MESSAGE, size.width) + WidgetDimensions::scaled.vsep_wide + GetCharacterHeight(FS_NORMAL) + this->vehicle_space;
		SetDParam(0, engine);
		size.height += GetStringHeight(GetEngineInfoString(engine), size.width);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_EP_QUESTION) return;

		EngineID engine = static_cast<EngineID>(this->window_number);
		SetDParam(0, GetEngineCategoryName(engine));
		int y = DrawStringMultiLine(r, STR_ENGINE_PREVIEW_MESSAGE, TC_FROMSTRING, SA_HOR_CENTER | SA_TOP) + WidgetDimensions::scaled.vsep_wide;

		SetDParam(0, PackEngineNameDParam(engine, EngineNameContext::PreviewNews));
		DrawString(r.left, r.right, y, STR_ENGINE_NAME, TC_BLACK, SA_HOR_CENTER);
		y += GetCharacterHeight(FS_NORMAL);

		DrawVehicleEngine(r.left, r.right, this->width >> 1, y + this->vehicle_space / 2, engine, GetEnginePalette(engine, _local_company), EIT_PREVIEW);

		y += this->vehicle_space;
		DrawStringMultiLine(r.left, r.right, y, r.bottom, GetEngineInfoString(engine), TC_BLACK, SA_CENTER);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_EP_YES:
				Command<CMD_WANT_ENGINE_PREVIEW>::Post(static_cast<EngineID>(this->window_number));
				[[fallthrough]];
			case WID_EP_NO:
				if (!_shift_pressed) this->Close();
				break;
		}
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		EngineID engine = static_cast<EngineID>(this->window_number);
		if (Engine::Get(engine)->preview_company != _local_company) this->Close();
	}
};

static WindowDesc _engine_preview_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_ENGINE_PREVIEW, WC_NONE,
	WindowDefaultFlag::Construction,
	_nested_engine_preview_widgets
);


void ShowEnginePreviewWindow(EngineID engine)
{
	AllocateWindowDescFront<EnginePreviewWindow>(_engine_preview_desc, engine);
}

/**
 * Get the capacity of an engine with articulated parts.
 * @param engine The engine to get the capacity of.
 * @param attempt_refit Attempt to get capacity when refitting to this cargo.
 * @return The capacity.
 */
uint GetTotalCapacityOfArticulatedParts(EngineID engine, CargoType attempt_refit)
{
	CargoArray cap = GetCapacityOfArticulatedParts(engine, attempt_refit);
	return cap.GetSum<uint>();
}

static StringID GetEngineInfoCapacityStringParameter(EngineID engine)
{
	CargoArray cap = GetCapacityOfArticulatedParts(engine);
	if (cap.GetSum<uint>() == 0) {
		/* no cargo at all */
		auto tmp_params = MakeParameters(INVALID_CARGO, 0);
		_temp_special_strings[1] = GetStringWithArgs(STR_JUST_CARGO, tmp_params);
	} else {
		format_buffer buffer;
		for (uint i = 0; i < NUM_CARGO; i++) {
			if (cap[i] == 0) continue;

			if (!buffer.empty()) {
				buffer.append(GetListSeparator());
			}

			AppendStringInPlace(buffer, STR_JUST_CARGO, i, cap[i]);
		}
		_temp_special_strings[1] = buffer.to_string();
	}

	return SPECSTR_TEMP_START + 1;
}

static StringID ProcessEngineCapacityString(StringID str)
{
	char str_buffer[1024];
	strecpy(str_buffer, GetStringPtr(str), lastof(str_buffer));
	str_replace_wchar(str_buffer, lastof(str_buffer), SCC_CARGO_LONG, SCC_STRING1);
	_temp_special_strings[0].assign(str_buffer);
	return SPECSTR_TEMP_START;
}

static StringID GetRunningCostString()
{
	if (DayLengthFactor() > 1 && !_settings_client.gui.show_running_costs_calendar_year) {
		return STR_ENGINE_PREVIEW_RUNCOST_ORIG_YEAR;
	} else if (EconTime::UsingWallclockUnits()) {
		return STR_ENGINE_PREVIEW_RUNCOST_PERIOD;
	} else {
		return STR_ENGINE_PREVIEW_RUNCOST_YEAR;
	}
}

static std::string GetTrainEngineInfoString(const Engine &e)
{
	format_buffer res;

	AppendStringInPlace(res, STR_ENGINE_PREVIEW_COST_WEIGHT, e.GetCost(), e.GetDisplayWeight());
	res.push_back('\n');

	if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL && GetRailTypeInfo(e.u.rail.railtype)->acceleration_type != 2) {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_SPEED_POWER_MAX_TE, PackVelocity(e.GetDisplayMaxSpeed(), e.type), e.GetPower(), e.GetDisplayMaxTractiveEffort());
		res.push_back('\n');
	} else {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_SPEED_POWER, PackVelocity(e.GetDisplayMaxSpeed(), e.type), e.GetPower());
		res.push_back('\n');
	}

	AppendStringInPlace(res, GetRunningCostString(), e.GetDisplayRunningCost());
	res.push_back('\n');

	AppendStringInPlace(res, ProcessEngineCapacityString(STR_ENGINE_PREVIEW_CAPACITY), GetEngineInfoCapacityStringParameter(e.index));

	return res.to_string();
}

static std::string GetAircraftEngineInfoString(const Engine &e)
{
	format_buffer res;

	AppendStringInPlace(res, STR_ENGINE_PREVIEW_COST_MAX_SPEED, e.GetCost(), PackVelocity(e.GetDisplayMaxSpeed(), e.type));
	res.push_back('\n');

	if (uint16_t range = e.GetRange(); range > 0) {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_TYPE_RANGE, e.GetAircraftTypeText(), range);
		res.push_back('\n');
	} else {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_TYPE, e.GetAircraftTypeText());
		res.push_back('\n');
	}

	AppendStringInPlace(res, GetRunningCostString(), e.GetDisplayRunningCost());
	res.push_back('\n');

	CargoType cargo = e.GetDefaultCargoType();
	uint16_t mail_capacity;
	uint capacity = e.GetDisplayDefaultCapacity(&mail_capacity);
	if (mail_capacity > 0) {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_CAPACITY_2, cargo, capacity, GetCargoTypeByLabel(CT_MAIL), mail_capacity);
	} else {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_CAPACITY, cargo, capacity);
	}

	return res.to_string();
}

static std::string GetRoadVehEngineInfoString(const Engine &e)
{
	format_buffer res;

	if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_COST_MAX_SPEED, e.GetCost(), PackVelocity(e.GetDisplayMaxSpeed(), e.type));
		res.push_back('\n');
	} else {
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_COST_WEIGHT, e.GetCost(), e.GetDisplayWeight());
		res.push_back('\n');
		AppendStringInPlace(res, STR_ENGINE_PREVIEW_SPEED_POWER_MAX_TE, PackVelocity(e.GetDisplayMaxSpeed(), e.type), e.GetPower(), e.GetDisplayMaxTractiveEffort());
		res.push_back('\n');
	}

	AppendStringInPlace(res, GetRunningCostString(), e.GetDisplayRunningCost());
	res.push_back('\n');

	AppendStringInPlace(res, ProcessEngineCapacityString(STR_ENGINE_PREVIEW_CAPACITY), GetEngineInfoCapacityStringParameter(e.index));

	return res.to_string();
}

static std::string GetShipEngineInfoString(const Engine &e)
{
	format_buffer res;

	AppendStringInPlace(res, STR_ENGINE_PREVIEW_COST_MAX_SPEED, e.GetCost(), PackVelocity(e.GetDisplayMaxSpeed(), e.type));
	res.push_back('\n');

	AppendStringInPlace(res, GetRunningCostString(), e.GetDisplayRunningCost());
	res.push_back('\n');

	AppendStringInPlace(res, ProcessEngineCapacityString(STR_ENGINE_PREVIEW_CAPACITY), GetEngineInfoCapacityStringParameter(e.index));

	return res.to_string();
}


/**
 * Get a multi-line string with some technical data, describing the engine.
 * @param engine Engine to describe.
 * @return String describing the engine.
 * @post \c DParam array is set up for printing the string.
 */
std::string GetEngineInfoString(EngineID engine)
{
	const Engine &e = *Engine::Get(engine);

	switch (e.type) {
		case VEH_TRAIN:
			return GetTrainEngineInfoString(e);

		case VEH_ROAD:
			return GetRoadVehEngineInfoString(e);

		case VEH_SHIP:
			return GetShipEngineInfoString(e);

		case VEH_AIRCRAFT:
			return GetAircraftEngineInfoString(e);

		default: NOT_REACHED();
	}
}

/**
 * Draw an engine.
 * @param left   Minimum horizontal position to use for drawing the engine
 * @param right  Maximum horizontal position to use for drawing the engine
 * @param preferred_x Horizontal position to use for drawing the engine.
 * @param y      Vertical position to use for drawing the engine.
 * @param engine Engine to draw.
 * @param pal    Palette to use for drawing.
 */
void DrawVehicleEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	const Engine *e = Engine::Get(engine);

	switch (e->type) {
		case VEH_TRAIN:
			DrawTrainEngine(left, right, preferred_x, y, engine, pal, image_type);
			break;

		case VEH_ROAD:
			DrawRoadVehEngine(left, right, preferred_x, y, engine, pal, image_type);
			break;

		case VEH_SHIP:
			DrawShipEngine(left, right, preferred_x, y, engine, pal, image_type);
			break;

		case VEH_AIRCRAFT:
			DrawAircraftEngine(left, right, preferred_x, y, engine, pal, image_type);
			break;

		default: NOT_REACHED();
	}
}

/**
 * Sort all items using quick sort and given 'CompareItems' function
 * @param el list to be sorted
 * @param compare function for evaluation of the quicksort
 */
void EngList_Sort(GUIEngineList &el, EngList_SortTypeFunction compare)
{
	if (el.size() < 2) return;
	std::sort(el.begin(), el.end(), [&](const GUIEngineListItem &a, const GUIEngineListItem &b) {
		return compare(a, b, el.SortParameterData());
	});
}

/**
 * Sort selected range of items (on indices @ <begin, begin+num_items-1>)
 * @param el list to be sorted
 * @param compare function for evaluation of the quicksort
 * @param begin start of sorting
 * @param num_items count of items to be sorted
 */
void EngList_SortPartial(GUIEngineList &el, EngList_SortTypeFunction compare, size_t begin, size_t num_items)
{
	if (num_items < 2) return;
	assert(begin < el.size());
	assert(begin + num_items <= el.size());
	std::sort(el.begin() + begin, el.begin() + begin + num_items, [&](const GUIEngineListItem &a, const GUIEngineListItem &b) {
		return compare(a, b, el.SortParameterData());
	});
}

