/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_gui.cpp GUI for trains. */

#include "stdafx.h"
#include "window_gui.h"
#include "command_func.h"
#include "train.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "zoom_func.h"
#include "core/backup_type.hpp"

#include "table/strings.h"

#include "safeguards.h"

uint16 GetTrainVehicleMaxSpeed(const Train *u, const RailVehicleInfo *rvi_u, const Train *front);

/**
 * Callback for building wagons.
 * @param result The result of the command.
 * @param tile   The tile the command was executed on.
 * @param p1 Additional data for the command (for the #CommandProc)
 * @param p2 Additional data for the command (for the #CommandProc)
 * @param cmd Unused.
 */
void CcBuildWagon(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	/* find a locomotive in the depot. */
	const Vehicle *found = nullptr;
	for (const Train *t : Train::Iterate()) {
		if (t->IsFrontEngine() && t->tile == tile && t->IsStoppedInDepot() && !t->IsVirtual()) {
			if (found != nullptr) return; // must be exactly one.
			found = t;
		}
	}

	/* if we found a loco, */
	if (found != nullptr) {
		found = found->Last();
		/* put the new wagon at the end of the loco. */
		DoCommandP(0, _new_vehicle_id, found->index, CMD_MOVE_RAIL_VEHICLE);
		InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
		InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS, 0);
		InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
	}
}

/**
 * Highlight the position where a rail vehicle is dragged over by drawing a light gray background.
 * @param px        The current x position to draw from.
 * @param max_width The maximum space available to draw.
 * @param y         The vertical centre position to draw from.
 * @param selection Selected vehicle that is dragged.
 * @param chain     Whether a whole chain is dragged.
 * @return The width of the highlight mark.
 */
static int HighlightDragPosition(int px, int max_width, int y, VehicleID selection, bool chain)
{
	bool rtl = _current_text_dir == TD_RTL;

	assert(selection != INVALID_VEHICLE);
	int dragged_width = 0;
	for (Train *t = Train::Get(selection); t != nullptr; t = chain ? t->Next() : (t->HasArticulatedPart() ? t->GetNextArticulatedPart() : nullptr)) {
		dragged_width += t->GetDisplayImageWidth(nullptr);
	}

	int drag_hlight_left = rtl ? std::max(px - dragged_width + 1, 0) : px;
	int drag_hlight_right = rtl ? px : std::min(px + dragged_width, max_width) - 1;
	int drag_hlight_width = std::max(drag_hlight_right - drag_hlight_left + 1, 0);

	if (drag_hlight_width > 0) {
		int height = ScaleSpriteTrad(12);
		int top = y - height / 2;
		Rect r = {drag_hlight_left, top, drag_hlight_right, top + height - 1};
		/* Sprite-scaling is used here as the area is from sprite size */
		GfxFillRect(r.Shrink(ScaleSpriteTrad(1)), _colour_gradient[COLOUR_GREY][7]);
	}

	return drag_hlight_width;
}

/**
 * Draws an image of a whole train
 * @param v         Front vehicle
 * @param r         Rect to draw at
 * @param selection Selected vehicle to draw a frame around
 * @param skip      Number of pixels to skip at the front (for scrolling)
 * @param drag_dest The vehicle another one is dragged over, \c INVALID_VEHICLE if none.
 */
void DrawTrainImage(const Train *v, const Rect &r, VehicleID selection, EngineImageType image_type, int skip, VehicleID drag_dest)
{
	bool rtl = _current_text_dir == TD_RTL;
	Direction dir = rtl ? DIR_E : DIR_W;

	DrawPixelInfo tmp_dpi;
	/* Position of highlight box */
	int highlight_l = 0;
	int highlight_r = 0;
	int max_width = r.Width();

	if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.Width(), r.Height())) return;

	{
		AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);

		int px = rtl ? max_width + skip : -skip;
		int y = r.Height() / 2;
		bool sel_articulated = false;
		bool dragging = (drag_dest != INVALID_VEHICLE);
		bool drag_at_end_of_train = (drag_dest == v->index); // Head index is used to mark dragging at end of train.
		for (; v != nullptr && (rtl ? px > 0 : px < max_width); v = v->Next()) {
			if (dragging && !drag_at_end_of_train && drag_dest == v->index) {
				/* Highlight the drag-and-drop destination inside the train. */
				int drag_hlight_width = HighlightDragPosition(px, max_width, y, selection, _cursor.vehchain);
				px += rtl ? -drag_hlight_width : drag_hlight_width;
			}

			Point offset;
			int width = Train::From(v)->GetDisplayImageWidth(&offset);

			if (rtl ? px + width > 0 : px - width < max_width) {
				PaletteID pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);
				VehicleSpriteSeq seq;
				v->GetImage(dir, image_type, &seq);
				seq.Draw(px + (rtl ? -offset.x : offset.x), y + offset.y, pal, (v->vehstatus & VS_CRASHED) != 0);
			}

			if (!v->IsArticulatedPart()) sel_articulated = false;

			if (v->index == selection) {
				/* Set the highlight position */
				highlight_l = rtl ? px - width : px;
				highlight_r = rtl ? px - 1 : px + width - 1;
				sel_articulated = true;
			} else if ((_cursor.vehchain && highlight_r != 0) || sel_articulated) {
				if (rtl) {
					highlight_l -= width;
				} else {
					highlight_r += width;
				}
			}

			px += rtl ? -width : width;
		}

		if (dragging && drag_at_end_of_train) {
			/* Highlight the drag-and-drop destination at the end of the train. */
			HighlightDragPosition(px, max_width, y, selection, _cursor.vehchain);
		}
	}

	if (highlight_l != highlight_r) {
		/* Draw the highlight. Now done after drawing all the engines, as
		 * the next engine after the highlight could overlap it. */
		int height = ScaleSpriteTrad(12);
		Rect hr = {highlight_l, 0, highlight_r, height - 1};
		DrawFrameRect(hr.Translate(r.left, CenterBounds(r.top, r.bottom, height)).Expand(WidgetDimensions::scaled.bevel), COLOUR_WHITE, FR_BORDERONLY);
	}
}

/** Helper struct for the cargo details information */
struct CargoSummaryItem {
	CargoID cargo;    ///< The cargo that is carried
	StringID subtype; ///< STR_EMPTY if none
	uint capacity;    ///< Amount that can be carried
	uint amount;      ///< Amount that is carried
	StationID source; ///< One of the source stations

	/** Used by CargoSummary::Find() and similar functions */
	inline bool operator != (const CargoSummaryItem &other) const
	{
		return this->cargo != other.cargo || this->subtype != other.subtype;
	}

	/** Used by std::find() and similar functions */
	inline bool operator == (const CargoSummaryItem &other) const
	{
		return !(this->cargo != other.cargo);
	}
};

static const uint TRAIN_DETAILS_MIN_INDENT  = 32; ///< Minimum indent level in the train details window
static const uint TRAIN_DETAILS_MAX_INDENT  = 72; ///< Maximum indent level in the train details window; wider than this and we start on a new line

/** Container for the cargo summary information. */
typedef std::vector<CargoSummaryItem> CargoSummary;
/** Reused container of cargo details */
static CargoSummary _cargo_summary;

/**
 * Draw the details cargo tab for the given vehicle at the given position
 *
 * @param item  Data to draw
 * @param left  The left most coordinate to draw
 * @param right The right most coordinate to draw
 * @param y     The y coordinate
 */
static void TrainDetailsCargoTab(const CargoSummaryItem *item, int left, int right, int y)
{
	StringID str;
	if (item->amount > 0) {
		SetDParam(0, item->cargo);
		SetDParam(1, item->amount);
		SetDParam(2, item->source);
		SetDParam(3, _settings_game.vehicle.freight_trains);
		str = FreightWagonMult(item->cargo) > 1 ? STR_VEHICLE_DETAILS_CARGO_FROM_MULT : STR_VEHICLE_DETAILS_CARGO_FROM;
	} else {
		str = item->cargo == INVALID_CARGO ? STR_QUANTITY_N_A : STR_VEHICLE_DETAILS_CARGO_EMPTY;
	}

	DrawString(left, right, y, str, TC_LIGHT_BLUE);
}

/**
 * Draw the details info tab for the given vehicle at the given position
 *
 * @param v     current vehicle
 * @param left  The left most coordinate to draw
 * @param right The right most coordinate to draw
 * @param y     The y coordinate
 */
static void TrainDetailsInfoTab(const Train *v, int left, int right, int y, byte line_number)
{
	const RailVehicleInfo *rvi = RailVehInfo(v->engine_type);
	bool show_speed = !UsesWagonOverride(v) && (_settings_game.vehicle.wagon_speed_limits || rvi->railveh_type != RAILVEH_WAGON);
	uint16 speed;

	if (rvi->railveh_type == RAILVEH_WAGON) {
		SetDParam(0, PackEngineNameDParam(v->engine_type, EngineNameContext::VehicleDetails));
		SetDParam(1, v->value);

		if (show_speed && (speed = GetVehicleProperty(v, PROP_TRAIN_SPEED, rvi->max_speed))) {
			SetDParam(2, speed); // StringID++
			DrawString(left, right, y, STR_VEHICLE_DETAILS_TRAIN_WAGON_VALUE_AND_SPEED);
		} else {
			DrawString(left, right, y, STR_VEHICLE_DETAILS_TRAIN_WAGON_VALUE);
		}
	} else {
		switch (line_number) {
			case 0:
				SetDParam(0, PackEngineNameDParam(v->engine_type, EngineNameContext::VehicleDetails));
				SetDParam(1, v->build_year);
				SetDParam(2, v->value);

				if (show_speed && (speed = GetVehicleProperty(v, PROP_TRAIN_SPEED, rvi->max_speed))) {
					SetDParam(3, speed); // StringID++
					DrawString(left, right, y, STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE_AND_SPEED, TC_FROMSTRING, SA_LEFT);
				} else {
					DrawString(left, right, y, STR_VEHICLE_DETAILS_TRAIN_ENGINE_BUILT_AND_VALUE);
				}
				break;

			case 1:
				SetDParam(0, v->reliability * 100 >> 16);
				SetDParam(1, v->breakdowns_since_last_service);
				DrawString(left, right, y, STR_VEHICLE_INFO_RELIABILITY_BREAKDOWNS, TC_FROMSTRING, SA_LEFT);
				break;

			case 2:
				if (v->breakdown_ctr == 1) {
					if (_settings_game.vehicle.improved_breakdowns) {
						SetDParam(0, STR_VEHICLE_STATUS_BROKEN_DOWN_VEL_SHORT);
						SetDParam(1, STR_BREAKDOWN_TYPE_CRITICAL + v->breakdown_type);
						if (v->breakdown_type == BREAKDOWN_LOW_SPEED) {
							SetDParam(2, std::min<int>(v->First()->GetCurrentMaxSpeed(), v->breakdown_severity));
						} else if (v->breakdown_type == BREAKDOWN_LOW_POWER) {
							SetDParam(2, v->breakdown_severity * 100 / 256);
						}
					} else {
						SetDParam(0, STR_VEHICLE_STATUS_BROKEN_DOWN);
					}
				} else {
					if (HasBit(v->flags, VRF_NEED_REPAIR)) {
						SetDParam(0, STR_NEED_REPAIR);
						SetDParam(1, GetTrainVehicleMaxSpeed(v, &(v->GetEngine()->u.rail), v->First()));
					} else {
						SetDParam(0, STR_RUNNING);
					}
				}
				DrawString(left, right, y, STR_CURRENT_STATUS);
				break;

			default:
				NOT_REACHED();
		}
	}
}

/**
 * Draw the details capacity tab for the given vehicle at the given position
 *
 * @param item  Data to draw
 * @param left  The left most coordinate to draw
 * @param right The right most coordinate to draw
 * @param y     The y coordinate
 */
static void TrainDetailsCapacityTab(const CargoSummaryItem *item, int left, int right, int y)
{
	StringID str;
	if (item->cargo != INVALID_CARGO) {
		SetDParam(0, item->cargo);
		SetDParam(1, item->capacity);
		SetDParam(4, item->subtype);
		SetDParam(5, _settings_game.vehicle.freight_trains);
		str = FreightWagonMult(item->cargo) > 1 ? STR_VEHICLE_INFO_CAPACITY_MULT : STR_VEHICLE_INFO_CAPACITY;
	} else {
		/* Draw subtype only */
		SetDParam(0, item->subtype);
		str = STR_VEHICLE_INFO_NO_CAPACITY;
	}
	DrawString(left, right, y, str);
}

/**
 * Collects the cargo transported
 * @param v Vehicle to process
 * @param summary Space for the result
 */
static void GetCargoSummaryOfArticulatedVehicle(const Train *v, CargoSummary *summary)
{
	summary->clear();
	do {
		if (!v->GetEngine()->CanCarryCargo()) continue;

		CargoSummaryItem new_item;
		new_item.cargo = v->cargo_cap > 0 ? v->cargo_type : INVALID_CARGO;
		new_item.subtype = GetCargoSubtypeText(v);
		if (new_item.cargo == INVALID_CARGO && new_item.subtype == STR_EMPTY) continue;

		auto item = std::find(summary->begin(), summary->end(), new_item);
		if (item == summary->end()) {
			summary->emplace_back();
			item = summary->end() - 1;
			item->cargo = new_item.cargo;
			item->subtype = new_item.subtype;
			item->capacity = 0;
			item->amount = 0;
			item->source = INVALID_STATION;
		}

		item->capacity += v->cargo_cap;
		item->amount += v->cargo.StoredCount();
		if (item->source == INVALID_STATION) item->source = v->cargo.Source();
	} while ((v = v->Next()) != nullptr && v->IsArticulatedPart());
}

/**
 * Get the length of an articulated vehicle.
 * @param v the vehicle to get the length of.
 * @return the length in pixels.
 */
static uint GetLengthOfArticulatedVehicle(const Train *v)
{
	uint length = 0;

	do {
		length += v->GetDisplayImageWidth();
	} while ((v = v->Next()) != nullptr && v->IsArticulatedPart());

	return length;
}

/**
 * Determines the number of lines in the train details window
 * @param veh_id Train
 * @param det_tab Selected details tab
 * @return Number of line
 */
int GetTrainDetailsWndVScroll(VehicleID veh_id, TrainDetailsWindowTabs det_tab)
{
	int num = 0;

	if (det_tab == TDW_TAB_TOTALS) { // Total cargo tab
		CargoArray max_cargo{};
		for (const Vehicle *v = Vehicle::Get(veh_id); v != nullptr; v = v->Next()) {
			max_cargo[v->cargo_type] += v->cargo_cap;
		}

		num = max_cargo.GetCount();

		if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
			num += 5; // needs five more because first line is description string and we have the weight and speed info and the feeder share
		} else {
			num += 2; // needs one more because first line is description string and we have the feeder share
		}
	} else {
		for (const Train *v = Train::Get(veh_id); v != nullptr; v = v->GetNextVehicle()) {
			GetCargoSummaryOfArticulatedVehicle(v, &_cargo_summary);
			num += std::max(1u, (unsigned)_cargo_summary.size());

			uint length = GetLengthOfArticulatedVehicle(v);
			if (length > (uint)ScaleSpriteTrad(TRAIN_DETAILS_MAX_INDENT)) num++;
		}
		if (det_tab == 1) num += 2 * Train::Get(veh_id)->tcache.cached_num_engines;
	}

	return num;
}

/**
 * Draw the details for the given vehicle at the given position
 *
 * @param v     current vehicle
 * @param r     the Rect to draw within
 * @param vscroll_pos Position of scrollbar
 * @param vscroll_cap Number of lines currently displayed
 * @param det_tab Selected details tab
 */
void DrawTrainDetails(const Train *v, const Rect &r, int vscroll_pos, uint16 vscroll_cap, TrainDetailsWindowTabs det_tab)
{
	bool rtl = _current_text_dir == TD_RTL;
	int line_height = r.Height();
	int sprite_y_offset = line_height / 2;
	int text_y_offset = (line_height - FONT_HEIGHT_NORMAL) / 2;

	/* draw the first 3 details tabs */
	if (det_tab != TDW_TAB_TOTALS) {
		Direction dir = rtl ? DIR_E : DIR_W;
		int x = rtl ? r.right : r.left;
		byte line_number = 0;
		for (; v != nullptr && vscroll_pos > -vscroll_cap; v = v->GetNextVehicle()) {
			GetCargoSummaryOfArticulatedVehicle(v, &_cargo_summary);

			/* Draw sprites */
			uint dx = 0;
			int px = x;
			const Train *u = v;
			do {
				Point offset;
				int width = u->GetDisplayImageWidth(&offset);
				if (vscroll_pos <= 0 && vscroll_pos > -vscroll_cap && line_number == 0) {
					int pitch = 0;
					const Engine *e = Engine::Get(v->engine_type);
					if (e->GetGRF() != nullptr) {
						pitch = ScaleSpriteTrad(e->GetGRF()->traininfo_vehicle_pitch);
					}
					PaletteID pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);
					VehicleSpriteSeq seq;
					u->GetImage(dir, EIT_IN_DETAILS, &seq);
					seq.Draw(px + (rtl ? -offset.x : offset.x), r.top - line_height * vscroll_pos + sprite_y_offset + pitch, pal, (v->vehstatus & VS_CRASHED) != 0);
				}
				px += rtl ? -width : width;
				dx += width;
				u = u->Next();
			} while (u != nullptr && u->IsArticulatedPart());

			bool separate_sprite_row = (dx > (uint)ScaleSpriteTrad(TRAIN_DETAILS_MAX_INDENT));
			if (separate_sprite_row) {
				vscroll_pos--;
				dx = 0;
			}

			int sprite_width = std::max<int>(dx, ScaleSpriteTrad(TRAIN_DETAILS_MIN_INDENT)) + WidgetDimensions::scaled.hsep_normal;
			Rect dr = r.Indent(sprite_width, rtl);
			uint num_lines = std::max(1u, (unsigned)_cargo_summary.size());
			for (uint i = 0; i < num_lines;) {
				if (vscroll_pos <= 0 && vscroll_pos > -vscroll_cap) {
					int py = r.top - line_height * vscroll_pos + text_y_offset;
					if (i > 0 || separate_sprite_row) {
						if (vscroll_pos != 0) GfxFillRect(r.left, py - WidgetDimensions::scaled.matrix.top - 1, r.right, py - WidgetDimensions::scaled.matrix.top, _colour_gradient[COLOUR_GREY][5]);
					}
					switch (det_tab) {
						case TDW_TAB_CARGO:
							if (i < _cargo_summary.size()) {
								TrainDetailsCargoTab(&_cargo_summary[i], dr.left, dr.right, py);
							} else {
								DrawString(dr.left, dr.right, py, STR_QUANTITY_N_A, TC_LIGHT_BLUE);
							}
							break;

						case TDW_TAB_INFO:
							if (i == 0) TrainDetailsInfoTab(v, dr.left, dr.right, py, line_number);
							break;

						case TDW_TAB_CAPACITY:
							if (i < _cargo_summary.size()) {
								TrainDetailsCapacityTab(&_cargo_summary[i], dr.left, dr.right, py);
							} else {
								SetDParam(0, STR_EMPTY);
								DrawString(dr.left, dr.right, py, STR_VEHICLE_INFO_NO_CAPACITY);
							}
							break;

						default: NOT_REACHED();
					}
				}
				if (det_tab != 1 || line_number >= (Train::From(v)->IsWagon() ? 0 : 2)) {
					line_number = 0;
					i++;
				} else {
					line_number++;
				}
				vscroll_pos--;
			}
		}
	} else {
		int y = r.top;
		CargoArray act_cargo{};
		CargoArray max_cargo{};
		Money feeder_share = 0;
		int empty_weight = 0;
		int loaded_weight = 0;

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			const Train *train = Train::From(u);
			const auto weight_without_cargo = train->GetWeightWithoutCargo();
			act_cargo[u->cargo_type] += u->cargo.StoredCount();
			max_cargo[u->cargo_type] += u->cargo_cap;
			feeder_share             += u->cargo.FeederShare();
			empty_weight             += weight_without_cargo;
			loaded_weight            += weight_without_cargo + train->GetCargoWeight(train->cargo_cap);
		}

		if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
			const int empty_max_speed = GetTrainEstimatedMaxAchievableSpeed(v, empty_weight, v->GetDisplayMaxSpeed());
			const int loaded_max_speed = GetTrainEstimatedMaxAchievableSpeed(v, loaded_weight, v->GetDisplayMaxSpeed());

			if (--vscroll_pos < 0 && vscroll_pos >= -vscroll_cap) {
				SetDParam(0, empty_weight);
				SetDParam(1, loaded_weight);
				DrawString(r.left, r.right, y + text_y_offset, STR_VEHICLE_DETAILS_TRAIN_TOTAL_WEIGHT);
				y += line_height;
			}

			if (--vscroll_pos < 0 && vscroll_pos >= -vscroll_cap) {
				SetDParam(0, empty_max_speed);
				SetDParam(1, loaded_max_speed);
				DrawString(r.left, r.right, y + text_y_offset, STR_VEHICLE_DETAILS_TRAIN_MAX_SPEED);
				y += line_height;
			}

			if (--vscroll_pos < 0 && vscroll_pos >= -vscroll_cap) {
				y += line_height;
			}
		}

		if (--vscroll_pos < 0 && vscroll_pos >= -vscroll_cap) {
			DrawString(r.left, r.right, y + text_y_offset, STR_VEHICLE_DETAILS_TRAIN_TOTAL_CAPACITY_TEXT);
			y += line_height;
		}

		/* Indent the total cargo capacity details */
		Rect ir = r.Indent(WidgetDimensions::scaled.hsep_indent, rtl);
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (max_cargo[i] > 0 && --vscroll_pos < 0 && vscroll_pos >= -vscroll_cap) {
				SetDParam(0, i);            // {CARGO} #1
				SetDParam(1, act_cargo[i]); // {CARGO} #2
				SetDParam(2, i);            // {SHORTCARGO} #1
				SetDParam(3, max_cargo[i]); // {SHORTCARGO} #2
				SetDParam(4, _settings_game.vehicle.freight_trains);
				DrawString(ir.left, ir.right, y + text_y_offset, FreightWagonMult(i) > 1 ? STR_VEHICLE_DETAILS_TRAIN_TOTAL_CAPACITY_MULT : STR_VEHICLE_DETAILS_TRAIN_TOTAL_CAPACITY);
				y += line_height;
			}
		}

		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			act_cargo[u->cargo_type] += u->cargo.StoredCount();
			max_cargo[u->cargo_type] += u->cargo_cap;
			feeder_share             += u->cargo.FeederShare();
		}

		if (--vscroll_pos < 0 && vscroll_pos >= -vscroll_cap) {
			SetDParam(0, feeder_share);
			DrawString(r.left, r.right, y + text_y_offset, STR_VEHICLE_INFO_FEEDER_CARGO_VALUE);
		}
	}
}
