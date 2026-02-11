/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ship_gui.cpp GUI for ships. */

#include "stdafx.h"
#include "vehicle_base.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "vehicle_gui.h"
#include "vehicle_gui_base.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "zoom_func.h"
#include "ship.h"
#include "core/format.hpp"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Draws an image of a ship
 * @param v         Front vehicle
 * @param r         Rect to draw at
 * @param selection Selected vehicle to draw a frame around
 */
void DrawShipImage(const Vehicle *v, const Rect &r, VehicleID selection, EngineImageType image_type)
{
	bool rtl = _current_text_dir == TD_RTL;

	VehicleSpriteSeq seq;
	v->GetImage(rtl ? DIR_E : DIR_W, image_type, &seq);

	Rect rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

	int width = UnScaleGUI(rect.Width());
	int x_offs = UnScaleGUI(rect.left);
	int x = rtl ? r.right - width - x_offs : r.left - x_offs;
	/* This magic -1 offset is related to the sprite_y_offsets in build_vehicle_gui.cpp */
	int y = ScaleSpriteTrad(-1) + CentreBounds(r.top, r.bottom, 0);

	seq.Draw(x, y, GetVehiclePalette(v), false);
	if (v->cargo_cap > 0) DrawCargoIconOverlay(x, y, v->cargo_type);

	if (v->index == selection) {
		x += x_offs;
		y += UnScaleGUI(rect.top);
		Rect hr = {x, y, x + width - 1, y + UnScaleGUI(rect.Height()) - 1};
		DrawFrameRect(hr.Expand(WidgetDimensions::scaled.bevel), COLOUR_WHITE, FrameFlag::BorderOnly);
	}
}

/**
 * Draw the details for the given vehicle at the given position
 *
 * @param v     current vehicle
 * @param r     the Rect to draw within
 */
void DrawShipDetails(const Vehicle *v, const Rect &r)
{
	int y = r.top;

	DrawString(r.left, r.right, y, GetString(STR_VEHICLE_INFO_BUILT_VALUE, PackEngineNameDParam(v->engine_type, EngineNameContext::VehicleDetails), v->build_year, v->value));
	y += GetCharacterHeight(FS_NORMAL);

	Money feeder_share = 0;

	if (v->Next() != nullptr) {
		CargoArray max_cargo{};
		StringID subtype_text[NUM_CARGO];

		memset(subtype_text, 0, sizeof(subtype_text));

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			max_cargo[u->cargo_type] += u->cargo_cap;
			if (u->cargo_cap > 0) {
				StringID text = GetCargoSubtypeText(u);
				if (text != STR_EMPTY) subtype_text[u->cargo_type] = text;
			}
		}

		format_buffer capacity;
		AppendStringInPlace(capacity, STR_VEHICLE_DETAILS_TRAIN_ARTICULATED_RV_CAPACITY);

		bool first = true;
		for (CargoType i = 0; i < NUM_CARGO; i++) {
			if (max_cargo[i] > 0) {
				if (!first) capacity.append(", ");
				AppendStringInPlace(capacity, STR_JUST_CARGO, i, max_cargo[i]);

				if (subtype_text[i] != 0) {
					AppendStringInPlace(capacity, subtype_text[i]);
				}

				first = false;
			}
		}

		DrawString(r.left, r.right, y, capacity, TC_BLUE);
		y += GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			if (u->cargo_cap == 0) continue;

			if (u->cargo.StoredCount() > 0) {
				DrawString(r.left, r.right, y, GetString(STR_VEHICLE_DETAILS_CARGO_FROM, u->cargo_type, u->cargo.StoredCount(), u->cargo.GetFirstStation()));
				feeder_share += u->cargo.GetFeederShare();
			} else {
				DrawString(r.left, r.right, y, STR_VEHICLE_DETAILS_CARGO_EMPTY);
			}
			y += GetCharacterHeight(FS_NORMAL);
		}
		y += WidgetDimensions::scaled.vsep_normal;
	} else {
		DrawString(r.left, r.right, y, GetString(STR_VEHICLE_INFO_CAPACITY, v->cargo_type, v->cargo_cap, GetCargoSubtypeText(v)));
		y += GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;

		if (v->cargo.StoredCount() > 0) {
			DrawString(r.left, r.right, y, GetString(STR_VEHICLE_DETAILS_CARGO_FROM, v->cargo_type, v->cargo.StoredCount(), v->cargo.GetFirstStation()));
			feeder_share += v->cargo.GetFeederShare();
		} else {
			DrawString(r.left, r.right, y, STR_VEHICLE_DETAILS_CARGO_EMPTY);
		}
		y += GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;
	}

	/* Draw Transfer credits text */
	DrawString(r.left, r.right, y, GetString(STR_VEHICLE_INFO_FEEDER_CARGO_VALUE, feeder_share));
	y += GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;

	if (Ship::From(v)->critical_breakdown_count > 0) {
		DrawString(r.left, r.right, y, GetString(STR_NEED_REPAIR, Ship::From(v)->GetDisplayEffectiveMaxSpeed()));
	}
}
