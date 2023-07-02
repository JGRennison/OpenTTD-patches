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
#include "strings_func.h"
#include "vehicle_func.h"
#include "spritecache.h"
#include "zoom_func.h"
#include "ship.h"

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
	int y = ScaleSpriteTrad(-1) + CenterBounds(r.top, r.bottom, 0);

	seq.Draw(x, y, GetVehiclePalette(v), false);

	if (v->index == selection) {
		x += x_offs;
		y += UnScaleGUI(rect.top);
		Rect hr = {x, y, x + width - 1, y + UnScaleGUI(rect.Height()) - 1};
		DrawFrameRect(hr.Expand(WidgetDimensions::scaled.bevel), COLOUR_WHITE, FR_BORDERONLY);
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

	SetDParam(0, PackEngineNameDParam(v->engine_type, EngineNameContext::VehicleDetails));
	SetDParam(1, v->build_year);
	SetDParam(2, v->value);
	DrawString(r.left, r.right, y, STR_VEHICLE_INFO_BUILT_VALUE);
	y += FONT_HEIGHT_NORMAL;

	Money feeder_share = 0;

	if (v->Next() != nullptr) {
		CargoArray max_cargo{};
		StringID subtype_text[NUM_CARGO];
		char capacity[512];

		memset(subtype_text, 0, sizeof(subtype_text));

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			max_cargo[u->cargo_type] += u->cargo_cap;
			if (u->cargo_cap > 0) {
				StringID text = GetCargoSubtypeText(u);
				if (text != STR_EMPTY) subtype_text[u->cargo_type] = text;
			}
		}

		GetString(capacity, STR_VEHICLE_DETAILS_TRAIN_ARTICULATED_RV_CAPACITY, lastof(capacity));

		bool first = true;
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (max_cargo[i] > 0) {
				char buffer[128];

				SetDParam(0, i);
				SetDParam(1, max_cargo[i]);
				GetString(buffer, STR_JUST_CARGO, lastof(buffer));

				if (!first) strecat(capacity, ", ", lastof(capacity));
				strecat(capacity, buffer, lastof(capacity));

				if (subtype_text[i] != 0) {
					GetString(buffer, subtype_text[i], lastof(buffer));
					strecat(capacity, buffer, lastof(capacity));
				}

				first = false;
			}
		}

		DrawString(r.left, r.right, y, capacity, TC_BLUE);
		y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			if (u->cargo_cap == 0) continue;

			StringID str = STR_VEHICLE_DETAILS_CARGO_EMPTY;
			if (u->cargo.StoredCount() > 0) {
				SetDParam(0, u->cargo_type);
				SetDParam(1, u->cargo.StoredCount());
				SetDParam(2, u->cargo.Source());
				str = STR_VEHICLE_DETAILS_CARGO_FROM;
				feeder_share += u->cargo.FeederShare();
			}
			DrawString(r.left, r.right, y, str);
			y += FONT_HEIGHT_NORMAL;
		}
		y += WidgetDimensions::scaled.vsep_normal;
	} else {
		SetDParam(0, v->cargo_type);
		SetDParam(1, v->cargo_cap);
		SetDParam(4, GetCargoSubtypeText(v));
		DrawString(r.left, r.right, y, STR_VEHICLE_INFO_CAPACITY);
		y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;

		StringID str = STR_VEHICLE_DETAILS_CARGO_EMPTY;
		if (v->cargo.StoredCount() > 0) {
			SetDParam(0, v->cargo_type);
			SetDParam(1, v->cargo.StoredCount());
			SetDParam(2, v->cargo.Source());
			str = STR_VEHICLE_DETAILS_CARGO_FROM;
			feeder_share += v->cargo.FeederShare();
		}
		DrawString(r.left, r.right, y, str);
		y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;
	}

	/* Draw Transfer credits text */
	SetDParam(0, feeder_share);
	DrawString(r.left, r.right, y, STR_VEHICLE_INFO_FEEDER_CARGO_VALUE);
	y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal;

	if (Ship::From(v)->critical_breakdown_count > 0) {
		SetDParam(0, Ship::From(v)->GetDisplayEffectiveMaxSpeed());
		DrawString(r.left, r.right, y, STR_NEED_REPAIR);
	}
}
